// SPDX-License-Identifier: Apache-2.0
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
    const auto expected_buffer_index =
        actual_pointer == &bundle[ordinal]
            ? std::optional(OriginalTableCount(operation, kBuffers))
            : std::nullopt;
    appended.Require(
        actual.kind == GltfResourceKind::kBuffer &&
            actual.table_index == OriginalTableCount(operation, kBuffers) &&
            actual.origin == GltfResourceOrigin::kDecodedDataUri &&
            actual.logical_bytes == bytes && actual.bytes.size() == bytes &&
            actual.buffer_index == expected_buffer_index &&
            !actual.buffer_view_index,
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
