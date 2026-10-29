// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_deformation.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "gltf_asset_state.h"
#include "gltf_workflow_support_internal.h"

namespace meshvale::interchange::gltf_workflow_support_detail {
namespace {
std::uint64_t OriginalTableCount(const Operation& operation,
                                 std::string_view key) {
  const auto& document =
      gltf_detail::Access::State(operation.request.source.SourceAsset())
          .document;
  const auto table = gltf_detail::FindMember(document, 0, key);
  return table ? gltf_detail::ChildCount(document, *table) : 0;
}
template <class Variant>
void CompareValues(BytePlan& plan, const Variant& expected,
                   const Variant& actual, Diagnostic diagnostic) {
  plan.Require(expected.index() == actual.index(), diagnostic);
  if (expected.index() != actual.index()) return;
  std::visit(
      [&](const auto& view) {
        using View = std::remove_cvref_t<decltype(view)>;
        plan.Compare(std::as_bytes(view), std::as_bytes(std::get<View>(actual)),
                     diagnostic, sizeof(typename View::value_type));
      },
      expected);
}
bool SparseEqual(const std::optional<GltfSparseProvenance>& expected,
                 const std::optional<GltfSparseProvenance>& actual) {
  if (expected.has_value() != actual.has_value()) return false;
  return !expected ||
         (expected->indices_buffer_view == actual->indices_buffer_view &&
          expected->indices_byte_offset == actual->indices_byte_offset &&
          expected->indices_component_type == actual->indices_component_type &&
          expected->values_buffer_view == actual->values_buffer_view &&
          expected->values_byte_offset == actual->values_byte_offset);
}
void Channel(BytePlan& plan, const GltfChannelView& expected,
             const GltfChannelView& actual, std::uint64_t ordinal) {
  auto descriptor =
      Location(Stage::kReloadedPrimitive, Code::kChannelDescriptor, ordinal);
  plan.Require(
      expected.set_index == actual.set_index &&
          expected.accessor_index == actual.accessor_index &&
          expected.component_type == actual.component_type &&
          expected.shape == actual.shape &&
          expected.normalized == actual.normalized &&
          expected.element_count == actual.element_count &&
          expected.base_buffer_view == actual.base_buffer_view &&
          expected.base_byte_offset == actual.base_byte_offset &&
          expected.base_byte_stride == actual.base_byte_stride &&
          expected.packed_sparse_step == actual.packed_sparse_step &&
          SparseEqual(expected.sparse_source, actual.sparse_source) &&
          expected.exact_min.has_value() == actual.exact_min.has_value() &&
          expected.exact_max.has_value() == actual.exact_max.has_value(),
      descriptor);
  plan.Compare(expected.semantic, actual.semantic, descriptor);
  plan.Compare(std::as_bytes(expected.sparse_destinations),
               std::as_bytes(actual.sparse_destinations), descriptor,
               sizeof(std::uint64_t));
  if (expected.exact_min && actual.exact_min)
    CompareValues(plan, *expected.exact_min, *actual.exact_min, descriptor);
  if (expected.exact_max && actual.exact_max)
    CompareValues(plan, *expected.exact_max, *actual.exact_max, descriptor);
  auto bits = Location(Stage::kReloadedPrimitive, Code::kChannelBits, ordinal);
  CompareValues(plan, expected.raw, actual.raw, bits);
  if (!expected.normalized_values.empty())
    plan.Compare(std::as_bytes(expected.normalized_values),
                 std::as_bytes(actual.normalized_values), bits, sizeof(double));
}
std::optional<std::size_t> AliasOffset(
    std::span<const GltfResourceView> inventory,
    const GltfResourceView& resource) {
  if (resource.origin != GltfResourceOrigin::kBufferView) return {};
  if (!resource.buffer_index || !resource.buffer_view_index) return {};
  for (const auto& buffer : inventory) {
    if (buffer.kind != GltfResourceKind::kBuffer ||
        buffer.table_index != *resource.buffer_index)
      continue;
    const auto start = reinterpret_cast<std::uintptr_t>(buffer.bytes.data());
    const auto view = reinterpret_cast<std::uintptr_t>(resource.bytes.data());
    if (view < start || view - start > buffer.bytes.size() ||
        resource.bytes.size() > buffer.bytes.size() - (view - start))
      return {};
    return static_cast<std::size_t>(view - start);
  }
  return {};
}
struct EncodedResource {
  const Operation& operation;
  std::span<const std::byte> actual;
  std::uint64_t resource;
  static std::optional<Diagnostic> Check(void* opaque, std::size_t begin,
                                         std::size_t end, bool finalizing) {
    const auto& self = *static_cast<EncodedResource*>(opaque);
    const auto candidate = self.operation.request.candidate.Source().View();
    constexpr std::size_t kBitsPerByte = 8;
    for (auto byte = begin; byte < end; ++byte) {
      if (!finalizing &&
          byte % self.operation.work.cancellation_block_bytes == 0)
        self.operation.allocation.Checkpoint();
      const auto expected = static_cast<std::uint8_t>(
          candidate.corner_vertices[byte / kUint32Bytes] >>
          ((byte % kUint32Bytes) * kBitsPerByte));
      if (std::to_integer<std::uint8_t>(self.actual[byte]) != expected) {
        auto diagnostic =
            Location(Stage::kResources, Code::kResourceBytes, {}, byte);
        diagnostic.resource = self.resource;
        return diagnostic;
      }
    }
    return {};
  }
};
struct SelectedTopology {
  const Operation& operation;
  bool indices;
  static std::optional<Diagnostic> Check(void* opaque, std::size_t begin,
                                         std::size_t end, bool finalizing) {
    const auto& self = *static_cast<SelectedTopology*>(opaque);
    const auto candidate = self.operation.request.candidate.Source().View();
    const auto& reload = self.operation.request.reloaded_extraction;
    const auto first = begin / sizeof(geometry::index_t);
    const auto last = 1 + (end - 1) / sizeof(geometry::index_t);
    for (auto corner = first; corner < last; ++corner) {
      if (!finalizing &&
          corner % std::max<std::size_t>(
                       1, self.operation.work.cancellation_block_bytes /
                              sizeof(geometry::index_t)) ==
              0)
        self.operation.allocation.Checkpoint();
      std::uint32_t actual = reload.TriangleIndices()[corner];
      if (self.indices) {
        const auto index = reload.SourceIndices();
        actual = std::get<std::span<const std::uint32_t>>(index->raw)[corner];
      }
      if (candidate.corner_vertices[corner] != actual)
        return Location(Stage::kReloadedPrimitive, Code::kSelectedTopology, {},
                        corner);
    }
    return {};
  }
};
struct Provenance {
  const Operation& operation;
  InspectionState& owner;
  static std::optional<Diagnostic> Check(void* opaque, std::size_t begin,
                                         std::size_t end, bool finalizing) {
    auto& self = *static_cast<Provenance*>(opaque);
    const auto& request = self.operation.request;
    const auto original = request.source.SourceExtraction().SourceCorners();
    const auto candidate = request.candidate.Source().View();
    const auto first = begin / sizeof(GltfSourceCorner);
    const auto last = 1 + (end - 1) / sizeof(GltfSourceCorner);
    for (auto ordinal = first; ordinal < last; ++ordinal) {
      if (!finalizing &&
          ordinal % std::max<std::size_t>(
                        1, self.operation.work.cancellation_block_bytes /
                               sizeof(GltfSourceCorner)) ==
              0)
        self.operation.allocation.Checkpoint();
      const auto expected = original[request.source_corners[ordinal]];
      const auto bundle = request.output.OriginalCorners()[ordinal];
      const auto reload = request.reloaded_extraction.SourceCorners()[ordinal];
      const auto expected_reload =
          self.operation.changed
              ? GltfSourceCorner{ordinal, candidate.corner_vertices[ordinal]}
              : expected;
      if (bundle.primitive_stream_corner != expected.primitive_stream_corner ||
          bundle.accessor_row != expected.accessor_row ||
          reload.primitive_stream_corner !=
              expected_reload.primitive_stream_corner ||
          reload.accessor_row != expected_reload.accessor_row)
        return Location(Stage::kProvenance, Code::kSourceProvenance, {},
                        ordinal);
      self.owner.original_corners[ordinal] = expected;
    }
    return {};
  }
};
}  // namespace
void VerifyResources(const Operation& operation, Stats& stats) {
  const auto original = operation.request.source.SourceAsset().Resources();
  const auto bundle = operation.request.output.Resources();
  const auto reload = operation.request.reloaded_asset.Resources();
  const auto expected_size = original.size() + (operation.changed ? 1 : 0);
  std::optional<std::size_t> added_reload;
  if (operation.changed) {
    const auto buffer_index = OriginalTableCount(operation, kBuffers);
    for (std::size_t ordinal = 0; ordinal < reload.size(); ++ordinal)
      if (reload[ordinal].kind == GltfResourceKind::kBuffer &&
          reload[ordinal].table_index == buffer_index) {
        added_reload = ordinal;
        break;
      }
  }
  BytePlan plan(operation);
  plan.Require(bundle.size() == expected_size &&
                   reload.size() == expected_size &&
                   (!operation.changed || added_reload.has_value()),
               Location(Stage::kResources, Code::kResourceIdentity));
  for (std::size_t ordinal = 0;
       ordinal < std::min({original.size(), bundle.size(), reload.size()});
       ++ordinal) {
    plan.Checkpoint();
    const auto& expected = original[ordinal];
    auto descriptor = Location(Stage::kResources, Code::kResourceIdentity);
    descriptor.resource = ordinal;
    // A bundle appends its new buffer after all old exposed views. A reader
    // inventories buffers before images. Match identities in original order
    // while skipping that one independently located new buffer on reload.
    const auto reload_ordinal =
        ordinal + (added_reload && ordinal >= *added_reload ? 1 : 0);
    for (const bool reloaded : {false, true}) {
      const auto inventory = reloaded ? reload : bundle;
      const auto actual_ordinal = reloaded ? reload_ordinal : ordinal;
      plan.Require(actual_ordinal < inventory.size(), descriptor);
      if (actual_ordinal >= inventory.size()) continue;
      const auto& actual = inventory[actual_ordinal];
      plan.Require(expected.kind == actual.kind &&
                       expected.table_index == actual.table_index &&
                       expected.origin == actual.origin &&
                       expected.logical_bytes == actual.logical_bytes &&
                       expected.buffer_index == actual.buffer_index &&
                       expected.buffer_view_index == actual.buffer_view_index,
                   descriptor);
      if (expected.origin == GltfResourceOrigin::kBufferView) {
        const auto expected_offset = AliasOffset(original, expected);
        const auto actual_offset = AliasOffset(inventory, actual);
        plan.Require(expected_offset && actual_offset &&
                         expected_offset == actual_offset,
                     descriptor);
      }
      plan.Compare(expected.authored_uri, actual.authored_uri, descriptor);
      auto bits = descriptor;
      bits.code = Code::kResourceBytes;
      plan.Compare(expected.bytes, actual.bytes, bits, 1);
    }
  }
  plan.Run(stats);
  if (!operation.changed) return;
  const auto ordinal = original.size();
  const auto bytes =
      operation.request.candidate.Source().View().corner_vertices.size() *
      kUint32Bytes;
  auto descriptor = Location(Stage::kResources, Code::kResourceIdentity);
  descriptor.resource = ordinal;
  BytePlan appended(operation);
  for (const auto* actual_pointer :
       {&bundle[ordinal], &reload[*added_reload]}) {
    const auto& actual = *actual_pointer;
    const bool buffer_index_matches =
        actual_pointer == &bundle[ordinal]
            ? actual.buffer_index.has_value() &&
                  *actual.buffer_index ==
                      OriginalTableCount(operation, kBuffers)
            : !actual.buffer_index.has_value();
    appended.Require(
        actual.kind == GltfResourceKind::kBuffer &&
            actual.table_index == OriginalTableCount(operation, kBuffers) &&
            actual.origin == GltfResourceOrigin::kDecodedDataUri &&
            actual.logical_bytes == bytes && actual.bytes.size() == bytes &&
            buffer_index_matches && !actual.buffer_view_index,
        descriptor);
  }
  appended.Compare(bundle[ordinal].authored_uri,
                   reload[*added_reload].authored_uri, descriptor);
  appended.Run(stats);
  for (const auto* actual : {&bundle[ordinal], &reload[*added_reload]}) {
    EncodedResource check{operation, actual->bytes, ordinal};
    RunRanges(operation, bytes, EncodedResource::Check, &check, stats);
  }
}
void VerifyReload(const Operation& operation, Stats& stats) {
  const auto& source = operation.request.source.SourceExtraction();
  const auto& actual = operation.request.reloaded_extraction;
  BytePlan plan(operation);
  plan.Require(source.MaterialIndex() == actual.MaterialIndex(),
               Location(Stage::kReloadedPrimitive, Code::kChannelDescriptor));
  const auto expected_channels = source.Channels();
  const auto actual_channels = actual.Channels();
  plan.Require(expected_channels.size() == actual_channels.size(),
               Location(Stage::kReloadedPrimitive, Code::kChannelCount));
  for (std::size_t ordinal = 0;
       ordinal < std::min(expected_channels.size(), actual_channels.size());
       ++ordinal)
    Channel(plan, expected_channels[ordinal], actual_channels[ordinal],
            ordinal);
  const auto expected_indices = source.SourceIndices();
  const auto actual_indices = actual.SourceIndices();
  if (!operation.changed) {
    plan.Require(source.SourceMode() == actual.SourceMode() &&
                     expected_indices.has_value() == actual_indices.has_value(),
                 Location(Stage::kReloadedPrimitive, Code::kSelectedTopology));
    if (expected_indices && actual_indices)
      Channel(plan, *expected_indices, *actual_indices,
              expected_channels.size());
  } else {
    const auto corners =
        operation.request.candidate.Source().View().corner_vertices.size();
    plan.Require(
        actual.SourceMode() == kTrianglesMode && actual_indices.has_value(),
        Location(Stage::kReloadedPrimitive, Code::kSelectedTopology));
    if (actual_indices) {
      const auto& index = *actual_indices;
      plan.Require(
          index.semantic == kIndices && !index.set_index &&
              index.accessor_index ==
                  OriginalTableCount(operation, kAccessors) &&
              index.component_type == GltfComponentType::kUint32 &&
              index.shape == GltfAccessorShape::kScalar && !index.normalized &&
              index.element_count == corners &&
              index.base_buffer_view ==
                  OriginalTableCount(operation, kBufferViews) &&
              index.base_byte_offset == 0 &&
              index.base_byte_stride == kUint32Bytes &&
              index.packed_sparse_step == kUint32Bytes &&
              !index.sparse_source && index.sparse_destinations.empty() &&
              !index.exact_min && !index.exact_max &&
              std::holds_alternative<std::span<const std::uint32_t>>(index.raw),
          Location(Stage::kReloadedPrimitive, Code::kChannelDescriptor,
                   expected_channels.size()));
      if (std::holds_alternative<std::span<const std::uint32_t>>(index.raw))
        plan.Require(
            std::get<std::span<const std::uint32_t>>(index.raw).size() ==
                corners,
            Location(Stage::kReloadedPrimitive, Code::kSelectedTopology));
    }
  }
  const auto corners =
      operation.request.candidate.Source().View().corner_vertices.size();
  plan.Require(actual.TriangleIndices().size() == corners,
               Location(Stage::kReloadedPrimitive, Code::kSelectedTopology));
  plan.Run(stats);
  SelectedTopology topology{operation, false};
  RunRanges(operation,
            gltf_detail::CheckedProduct(corners, sizeof(geometry::index_t)),
            SelectedTopology::Check, &topology, stats);
  if (operation.changed) {
    topology.indices = true;
    RunRanges(operation,
              gltf_detail::CheckedProduct(corners, sizeof(geometry::index_t)),
              SelectedTopology::Check, &topology, stats);
  }
}
void VerifyDeformationReload(const Operation& operation, Stats& stats,
                             const GltfDeformationPolicy& policy,
                             GltfDeformationObservation& observation) {
  const auto* source =
      operation.request.source.SourceExtraction().Deformation();
  const auto* actual = operation.request.reloaded_extraction.Deformation();
  const auto diagnostic =
      Location(Stage::kReloadedPrimitive, Code::kDeformation);
  BytePlan plan(operation);
  auto relationship = [&] {
    operation.allocation.Checkpoint();
    if (observation.counts.relationship_steps ==
        policy.limits.max_relationship_steps)
      Fail(Status::kLimitExceeded, Stage::kReloadedPrimitive,
           Code::kCountLimit);
    ++observation.counts.relationship_steps;
  };
  plan.Require(source && actual, diagnostic);
  if (!source || !actual) {
    plan.Run(stats);
    return;
  }
  const auto expected_revision = source->NormativeRevision();
  const auto actual_revision = actual->NormativeRevision();
  plan.Require(
      source->AdmissionProfile() == actual->AdmissionProfile() &&
          source->ProcessingProfile() == actual->ProcessingProfile() &&
          source->NumericProfile() == actual->NumericProfile() &&
          source->Primitive().MeshIndex() == actual->Primitive().MeshIndex() &&
          source->Primitive().PrimitiveIndex() ==
              actual->Primitive().PrimitiveIndex(),
      diagnostic);
  plan.Compare(expected_revision.commit, actual_revision.commit, diagnostic);
  plan.Compare(expected_revision.blob, actual_revision.blob, diagnostic);
  plan.Compare(source->QuaternionPolicy(), actual->QuaternionPolicy(),
               diagnostic);
  auto rows = [&](auto expected, auto reloaded, auto check) {
    plan.Require(expected.size() == reloaded.size(), diagnostic);
    for (std::size_t i = 0; i < std::min(expected.size(), reloaded.size());
         ++i) {
      relationship();
      check(expected[i], reloaded[i]);
    }
  };
  auto raw = [&](const GltfDeformationRawValues& expected,
                 const GltfDeformationRawValues& reloaded) {
    plan.Require(expected.index() == reloaded.index(), diagnostic);
    if (expected.index() != reloaded.index()) return;
    std::visit(
        [&](const auto& view) {
          using View = std::remove_cvref_t<decltype(view)>;
          const auto& other = std::get<View>(reloaded);
          if constexpr (std::is_same_v<View, GltfFloat32BitWords>)
            plan.Compare(view.little_endian_scalar_words,
                         other.little_endian_scalar_words, diagnostic,
                         sizeof(std::uint32_t));
          else
            plan.Compare(std::as_bytes(view), std::as_bytes(other), diagnostic,
                         sizeof(typename View::value_type));
        },
        expected);
  };
  auto optional_raw = [&](const auto& expected, const auto& reloaded) {
    plan.Require(expected.has_value() == reloaded.has_value(), diagnostic);
    if (expected && reloaded) raw(*expected, *reloaded);
  };
  // The selected index stream has its independent candidate/topology oracle in
  // VerifyReload. All other actual accessor uses retain their original index.
  auto selected_index = [&](const auto& use) {
    return use.semantic == kIndices &&
           use.mesh == source->Primitive().MeshIndex() &&
           use.primitive == source->Primitive().PrimitiveIndex();
  };
  rows(source->AccessorUses(), actual->AccessorUses(),
       [&](const auto& a, const auto& b) {
         const auto expected_accessor =
             operation.changed && selected_index(a)
                 ? OriginalTableCount(operation, kAccessors)
                 : a.accessor;
         plan.Require(expected_accessor == b.accessor && a.role == b.role &&
                          a.mesh == b.mesh && a.primitive == b.primitive &&
                          a.target == b.target && a.set == b.set &&
                          a.skin == b.skin && a.animation == b.animation &&
                          a.sampler == b.sampler && a.channel == b.channel &&
                          a.node == b.node,
                      diagnostic);
         plan.Compare(a.semantic, b.semantic, diagnostic);
       });
  for (const auto& a : source->Accessors()) {
    relationship();
    bool selected_only = operation.changed;
    bool used = false;
    for (const auto& use : source->AccessorUses()) {
      relationship();
      if (use.accessor == a.accessor) {
        used = true;
        if (!selected_index(use)) selected_only = false;
      }
    }
    if (used && selected_only) continue;
    const GltfDeformationAccessorView* b = nullptr;
    for (const auto& candidate : actual->Accessors()) {
      relationship();
      if (candidate.accessor == a.accessor) b = &candidate;
    }
    plan.Require(b != nullptr, diagnostic);
    if (!b) continue;
    plan.Require(a.component_type == b->component_type && a.shape == b->shape &&
                     a.normalized == b->normalized &&
                     a.element_count == b->element_count &&
                     a.base_buffer_view == b->base_buffer_view &&
                     a.base_byte_offset == b->base_byte_offset &&
                     a.base_byte_stride == b->base_byte_stride &&
                     a.packed_sparse_step == b->packed_sparse_step &&
                     SparseEqual(a.sparse_source, b->sparse_source) &&
                     a.derived_state == b->derived_state,
                 diagnostic);
    plan.Compare(std::as_bytes(a.sparse_destinations),
                 std::as_bytes(b->sparse_destinations), diagnostic,
                 sizeof(std::uint64_t));
    optional_raw(a.exact_min, b->exact_min);
    optional_raw(a.exact_max, b->exact_max);
    raw(a.raw, b->raw);
    plan.Compare(std::as_bytes(a.derived_values),
                 std::as_bytes(b->derived_values), diagnostic, sizeof(double));
  }
  // Raw/layout/use identity is a common prerequisite. A failed common check
  // does not manufacture called domain results. Each following domain owns
  // its complete comparisons and survives a later domain's failure.
  plan.Run(stats);
  plan.Reset();
  observation.predicates.influences = false;
  rows(source->InfluenceSets(), actual->InfluenceSets(),
       [&](const auto& a, const auto& b) {
         plan.Require(a.mesh == b.mesh && a.primitive == b.primitive &&
                          a.set == b.set &&
                          a.joints_accessor == b.joints_accessor &&
                          a.weights_accessor == b.weights_accessor,
                      diagnostic);
       });
  plan.Run(stats);
  observation.predicates.influences = true;
  plan.Reset();
  observation.predicates.bindings = false;
  rows(source->Skins(), actual->Skins(), [&](const auto& a, const auto& b) {
    plan.Require(a.skin == b.skin && a.skeleton == b.skeleton &&
                     a.inverse_bind_accessor == b.inverse_bind_accessor,
                 diagnostic);
    plan.Compare(std::as_bytes(a.joints), std::as_bytes(b.joints), diagnostic,
                 sizeof(std::uint64_t));
  });
  rows(source->Users(), actual->Users(), [&](const auto& a, const auto& b) {
    plan.Require(a.node == b.node && a.mesh == b.mesh &&
                     a.primitive == b.primitive && a.skin == b.skin &&
                     a.instantiated_as_skinned == b.instantiated_as_skinned &&
                     a.reachable_from_any_scene == b.reachable_from_any_scene,
                 diagnostic);
    plan.Compare(std::as_bytes(a.ancestor_nodes),
                 std::as_bytes(b.ancestor_nodes), diagnostic,
                 sizeof(std::uint64_t));
    plan.Compare(std::as_bytes(a.joint_ancestor_nodes),
                 std::as_bytes(b.joint_ancestor_nodes), diagnostic,
                 sizeof(std::uint64_t));
  });
  rows(source->Asset().Nodes(), actual->Asset().Nodes(),
       [&](const auto& a, const auto& b) {
         plan.Require(
             a.table_index == b.table_index && a.parent == b.parent &&
                 a.mesh == b.mesh && a.skin == b.skin &&
                 a.transform_kind == b.transform_kind &&
                 a.authored_transform_mask == b.authored_transform_mask &&
                 a.local_transform_bits == b.local_transform_bits,
             diagnostic);
         plan.Compare(a.name, b.name, diagnostic);
         plan.Compare(std::as_bytes(a.children), std::as_bytes(b.children),
                      diagnostic, sizeof(std::uint64_t));
       });
  rows(source->Asset().Scenes(), actual->Asset().Scenes(),
       [&](const auto& a, const auto& b) {
         plan.Require(a.table_index == b.table_index, diagnostic);
         plan.Compare(a.name, b.name, diagnostic);
         plan.Compare(std::as_bytes(a.roots), std::as_bytes(b.roots),
                      diagnostic, sizeof(std::uint64_t));
       });
  plan.Run(stats);
  observation.predicates.bindings = true;
  plan.Reset();
  observation.predicates.morphs = false;
  rows(source->MorphPrimitives(), actual->MorphPrimitives(),
       [&](const auto& a, const auto& b) {
         plan.Require(a.mesh == b.mesh && a.primitive == b.primitive &&
                          a.targets_authored == b.targets_authored &&
                          a.effective_target_count == b.effective_target_count,
                      diagnostic);
         rows(a.channels, b.channels, [&](const auto& x, const auto& y) {
           plan.Require(x.mesh == y.mesh && x.primitive == y.primitive &&
                            x.target == y.target && x.set == y.set &&
                            x.base_accessor == y.base_accessor &&
                            x.delta_accessor == y.delta_accessor,
                        diagnostic);
           plan.Compare(x.semantic, y.semantic, diagnostic);
         });
       });
  rows(source->MorphDefaults(), actual->MorphDefaults(),
       [&](const auto& a, const auto& b) {
         plan.Require(a.owner_kind == b.owner_kind &&
                          a.owner_index == b.owner_index &&
                          a.authored == b.authored,
                      diagnostic);
         rows(a.authored_values, b.authored_values,
              [&](const auto& x, const auto& y) {
                plan.Require(x.kind == y.kind, diagnostic);
                plan.Compare(x.lexeme, y.lexeme, diagnostic);
              });
       });
  rows(source->RowLocalDeclarations(), actual->RowLocalDeclarations(),
       [&](const auto& a, const auto& b) {
         plan.Compare(a.name, b.name, diagnostic);
       });
  plan.Run(stats);
  observation.predicates.morphs = true;
  plan.Reset();
  observation.predicates.affecting_animation = false;
  rows(source->Animations(), actual->Animations(),
       [&](const auto& a, const auto& b) {
         plan.Require(a.animation == b.animation && a.channel == b.channel &&
                          a.sampler == b.sampler && a.node == b.node &&
                          a.path == b.path &&
                          a.interpolation == b.interpolation &&
                          a.input_accessor == b.input_accessor &&
                          a.output_accessor == b.output_accessor &&
                          a.key_count == b.key_count &&
                          a.values_per_key == b.values_per_key &&
                          a.affects_selected_deformation ==
                              b.affects_selected_deformation,
                      diagnostic);
       });
  rows(source->Asset().NodeRotationSources(),
       actual->Asset().NodeRotationSources(),
       [&](const auto& a, const auto& b) {
         plan.Require(a.node == b.node && a.authored == b.authored &&
                          a.projected_components.has_value() ==
                              b.projected_components.has_value(),
                      diagnostic);
         if (a.projected_components && b.projected_components)
           plan.Compare(std::as_bytes(*a.projected_components),
                        std::as_bytes(*b.projected_components), diagnostic,
                        sizeof(std::uint32_t));
         rows(a.authored_components, b.authored_components,
              [&](const auto& x, const auto& y) {
                plan.Require(x.kind == y.kind, diagnostic);
                plan.Compare(x.lexeme, y.lexeme, diagnostic);
              });
       });
  plan.Run(stats);
  observation.predicates.affecting_animation = true;
  plan.Reset();
}
void VerifyProvenance(const Operation& operation, InspectionState& state,
                      Stats& stats) {
  const auto& request = operation.request;
  BytePlan maps(operation);
  maps.Compare(std::as_bytes(request.source_faces),
               std::as_bytes(request.output.SourceFaces()),
               Location(Stage::kProvenance, Code::kSourceProvenance),
               sizeof(geometry::index_t));
  maps.Compare(std::as_bytes(request.source_corners),
               std::as_bytes(request.output.SourceCorners()),
               Location(Stage::kProvenance, Code::kSourceProvenance),
               sizeof(geometry::index_t));
  maps.Require(request.output.OriginalCorners().size() ==
                       request.source_corners.size() &&
                   request.reloaded_extraction.SourceCorners().size() ==
                       request.source_corners.size(),
               Location(Stage::kProvenance, Code::kSourceProvenance));
  maps.Run(stats);
  state.faces.assign(request.source_faces.begin(), request.source_faces.end());
  state.corners.assign(request.source_corners.begin(),
                       request.source_corners.end());
  state.original_corners.resize(request.source_corners.size());
  Provenance check{operation, state};
  RunRanges(operation,
            gltf_detail::CheckedProduct(request.source_corners.size(),
                                        sizeof(GltfSourceCorner)),
            Provenance::Check, &check, stats, sizeof(GltfSourceCorner));
  const auto binding = request.output.ChangedBinding();
  bool valid = !operation.changed && !binding;
  if (operation.changed && binding) {
    const auto indices = request.source.SourceExtraction().SourceIndices();
    valid =
        binding->source_mode ==
            request.source.SourceExtraction().SourceMode() &&
        binding->source_indices_accessor ==
            (indices ? std::optional(indices->accessor_index) : std::nullopt) &&
        binding->output_mode == kTrianglesMode &&
        binding->output_indices_accessor ==
            OriginalTableCount(operation, kAccessors) &&
        binding->appended_buffer == OriginalTableCount(operation, kBuffers) &&
        binding->appended_buffer_view ==
            OriginalTableCount(operation, kBufferViews);
  }
  if (!valid)
    Fail(Status::kOutputMismatch, Stage::kProvenance, Code::kAppendedBinding);
}
}  // namespace meshvale::interchange::gltf_workflow_support_detail
