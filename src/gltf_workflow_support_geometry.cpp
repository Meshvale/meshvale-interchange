// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <variant>

#include "gltf_asset_state.h"
#include "gltf_workflow_support_internal.h"

namespace meshvale::interchange::gltf_workflow_support_detail {
namespace {
constexpr std::size_t kPositionComponents = 3;
template <class Variant>
std::size_t Scalars(const Variant& values) {
  return std::visit([](const auto& view) { return view.size(); }, values);
}
void AdmitMesh(geometry::MeshReadView mesh,
               const GltfTopologyOutputInspectionLimits& limits) {
  Count(mesh.positions.size() / kPositionComponents, limits.max_vertices);
  Count(Add(mesh.attributes.size(), 1), limits.max_channels);
  auto scalars = mesh.positions.size();
  std::size_t descriptors = 0;
  for (const auto& attribute : mesh.attributes) {
    scalars = Add(scalars, Scalars(attribute.values));
    if (attribute.offsets) scalars = Add(scalars, attribute.offsets->size());
    if (attribute.present) scalars = Add(scalars, attribute.present->size());
    descriptors =
        Add(descriptors, Add(attribute.name.size(), attribute.semantic.size()));
    for (const auto& metadata : attribute.metadata)
      descriptors =
          Add(descriptors, Add(metadata.key.size(), metadata.value.size()));
    Count(scalars, limits.max_attribute_scalars);
    Count(descriptors, limits.max_descriptor_bytes);
  }
  Count(scalars, limits.max_attribute_scalars);
}
void AdmitExtraction(const GltfPrimitiveExtraction& extraction,
                     const GltfTopologyOutputInspectionLimits& limits) {
  Count(extraction.Channels().size(), limits.max_channels);
  std::size_t scalars = 0;
  std::size_t descriptors = 0;
  for (const auto& channel : extraction.Channels()) {
    Count(channel.element_count, limits.max_vertices);
    scalars = Add(scalars, Scalars(channel.raw));
    scalars = Add(scalars, channel.normalized_values.size());
    scalars = Add(scalars, channel.sparse_destinations.size());
    if (channel.exact_min) scalars = Add(scalars, Scalars(*channel.exact_min));
    if (channel.exact_max) scalars = Add(scalars, Scalars(*channel.exact_max));
    descriptors = Add(descriptors, channel.semantic.size());
    Count(scalars, limits.max_attribute_scalars);
    Count(descriptors, limits.max_descriptor_bytes);
  }
}
void AdmitResources(std::span<const GltfResourceView> resources,
                    const GltfTopologyOutputInspectionLimits& limits) {
  Count(resources.size(), limits.max_resources);
  std::size_t bytes = 0;
  std::size_t descriptors = 0;
  for (const auto& resource : resources) {
    bytes = Add(bytes, resource.bytes.size());
    descriptors = Add(descriptors, resource.authored_uri.size());
    Count(bytes, limits.max_resource_bytes);
    Count(descriptors, limits.max_descriptor_bytes);
  }
}
void AdmitDocument(const GltfAsset& asset,
                   const GltfTopologyOutputInspectionLimits& limits) {
  Count(asset.OriginalJson().size(), limits.max_json_bytes);
  const auto& document = gltf_detail::Access::State(asset).document;
  Count(document.NodeCount(), limits.max_json_nodes);
  for (std::size_t node = 0; node < document.NodeCount(); ++node) {
    std::size_t depth = 1;
    for (auto parent = document.Parent(node); parent;
         parent = document.Parent(*parent)) {
      ++depth;
      Count(depth, limits.max_json_depth);
    }
  }
}
void AdmitBundleCounts(std::span<const std::byte> bytes,
                       const GltfTopologyOutputInspectionLimits& limits,
                       const GltfTopologyOutputInspectionWorkPolicy& work,
                       std::stop_token stop) {
  // An allocation-free escape-aware count pass admits the bundle's own
  // extents before identity checks and indexed growth. Syntax/structure
  // acceptance remains the later lexical phase's responsibility.
  const auto text = std::string_view(
      reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::size_t nodes = 0;
  std::size_t depth = 0;
  const auto whitespace = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  };
  const auto tick = [&](std::size_t position) {
    if (position % work.cancellation_block_bytes == 0 && stop.stop_requested())
      throw gltf_detail::AdmissionFailure(
          gltf_detail::AdmissionCode::kCanceled);
  };
  auto admit = [&](std::size_t node_depth) {
    nodes = Add(nodes, 1);
    Count(nodes, limits.max_json_nodes);
    Count(node_depth, limits.max_json_depth);
  };
  for (std::size_t position = 0; position < text.size();) {
    tick(position);
    const auto c = text[position++];
    if (whitespace(c) || c == ',' || c == ':') continue;
    if (c == '{' || c == '[') {
      depth = Add(depth, 1);
      admit(depth);
    } else if (c == '}' || c == ']') {
      if (depth) --depth;
    } else if (c == '"') {
      bool escaped = false;
      while (position < text.size()) {
        tick(position);
        const auto character = text[position++];
        if (escaped)
          escaped = false;
        else if (character == '\\')
          escaped = true;
        else if (character == '"')
          break;
      }
      auto next = position;
      while (next < text.size() && whitespace(text[next])) {
        tick(next);
        ++next;
      }
      if (next == text.size() || text[next] != ':') admit(Add(depth, 1));
    } else {
      admit(Add(depth, 1));
      while (position < text.size() && !whitespace(text[position]) &&
             text[position] != ',' && text[position] != '}' &&
             text[position] != ']') {
        tick(position);
        ++position;
      }
    }
  }
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
struct Correspondence {
  const Operation& operation;
  enum class Pass { kFaces, kCorners, kShapes, kLoops } pass;
  static std::optional<Diagnostic> Check(void* opaque, std::size_t begin,
                                         std::size_t end, bool finalizing) {
    const auto& self = *static_cast<Correspondence*>(opaque);
    const auto& operation = self.operation;
    const auto& request = operation.request;
    const auto candidate = request.candidate.Source().View();
    const auto source = request.source.SourceExtraction().TriangleIndices();
    const auto start = begin / sizeof(geometry::index_t);
    const auto stop = 1 + (end - 1) / sizeof(geometry::index_t);
    auto candidate_face = static_cast<std::size_t>(
        std::upper_bound(candidate.face_offsets.begin(),
                         candidate.face_offsets.end(), start) -
        candidate.face_offsets.begin() - 1);
    for (auto ordinal = start; ordinal < stop; ++ordinal) {
      if (!finalizing &&
          ordinal % std::max<std::size_t>(
                        1, operation.work.cancellation_block_bytes /
                               sizeof(geometry::index_t)) ==
              0)
        operation.allocation.Checkpoint();
      switch (self.pass) {
        case Pass::kFaces:
          if (ordinal && request.source_faces[ordinal] <=
                             request.source_faces[ordinal - 1])
            return Location(Stage::kCorrespondence, Code::kFaceMapOrder, {},
                            ordinal);
          if (request.source_faces[ordinal] >= source.size() / kTriangleCorners)
            return Location(Stage::kCorrespondence, Code::kFaceMapRange, {},
                            ordinal);
          break;
        case Pass::kCorners:
          while (ordinal >= candidate.face_offsets[candidate_face + 1])
            ++candidate_face;
          if (request.source_corners[ordinal] !=
              request.source_faces[candidate_face] * kTriangleCorners +
                  ordinal - candidate.face_offsets[candidate_face])
            return Location(Stage::kCorrespondence, Code::kCornerMapValue, {},
                            ordinal);
          break;
        case Pass::kShapes:
          if (candidate.face_offsets[ordinal + 1] -
                  candidate.face_offsets[ordinal] !=
              kTriangleCorners)
            return Location(Stage::kCorrespondence, Code::kTriangleShape, {},
                            ordinal);
          break;
        case Pass::kLoops:
          if (candidate.corner_vertices[ordinal] !=
              source[request.source_corners[ordinal]])
            return Location(Stage::kCorrespondence, Code::kTriangleLoop, {},
                            ordinal);
          break;
      }
    }
    return {};
  }
};
}  // namespace
void AdmitInputs(const GltfTopologyOutputInspectionRequest& request,
                 const GltfTopologyOutputInspectionLimits& limits,
                 const GltfTopologyOutputInspectionWorkPolicy& work,
                 std::stop_token stop) {
  AdmitDocument(request.source.SourceAsset(), limits);
  Count(request.output.Json().size(), limits.max_json_bytes);
  AdmitBundleCounts(request.output.Json(), limits, work, stop);
  AdmitDocument(request.reloaded_asset, limits);
  AdmitResources(request.source.SourceAsset().Resources(), limits);
  AdmitResources(request.output.Resources(), limits);
  AdmitResources(request.reloaded_asset.Resources(), limits);
  AdmitMesh(request.source.Mesh().View(), limits);
  AdmitMesh(request.candidate.Source().View(), limits);
  AdmitExtraction(request.source.SourceExtraction(), limits);
  AdmitExtraction(request.reloaded_extraction, limits);
  const auto mesh = request.candidate.Source().View();
  Count(mesh.face_offsets.empty() ? 0 : mesh.face_offsets.size() - 1,
        limits.max_candidate_faces);
  Count(mesh.corner_vertices.size(), limits.max_candidate_corners);
  Count(request.source_faces.size(), limits.max_candidate_faces);
  Count(request.source_corners.size(), limits.max_candidate_corners);
  if (mesh.corner_vertices.size() > limits.max_index_bytes / kUint32Bytes)
    Fail(Status::kLimitExceeded, Stage::kAdmission, Code::kCountLimit);
}
void VerifyGeometry(const Operation& operation, Stats& stats) {
  const auto original = operation.request.source.Mesh().View();
  const auto candidate = operation.request.candidate.Source().View();
  BytePlan plan(operation);
  plan.Compare(
      std::as_bytes(original.positions), std::as_bytes(candidate.positions),
      Location(Stage::kSourceCandidate, Code::kPositionBits), sizeof(double));
  plan.Require(original.attributes.size() == candidate.attributes.size(),
               Location(Stage::kSourceCandidate, Code::kChannelCount));
  for (std::size_t ordinal = 0; ordinal < std::min(original.attributes.size(),
                                                   candidate.attributes.size());
       ++ordinal) {
    plan.Checkpoint();
    const auto& expected = original.attributes[ordinal];
    const auto& actual = candidate.attributes[ordinal];
    auto descriptor =
        Location(Stage::kSourceCandidate, Code::kAttributeDescriptor, ordinal);
    plan.Require(
        expected.domain == actual.domain &&
            expected.set_index == actual.set_index &&
            expected.components == actual.components &&
            expected.values.index() == actual.values.index() &&
            expected.offsets.has_value() == actual.offsets.has_value() &&
            expected.present.has_value() == actual.present.has_value() &&
            expected.metadata.size() == actual.metadata.size(),
        descriptor);
    plan.Compare(expected.name, actual.name, descriptor);
    plan.Compare(expected.semantic, actual.semantic, descriptor);
    if (expected.offsets && actual.offsets)
      plan.Compare(std::as_bytes(*expected.offsets),
                   std::as_bytes(*actual.offsets), descriptor,
                   sizeof(geometry::index_t));
    if (expected.present && actual.present)
      plan.Compare(std::as_bytes(*expected.present),
                   std::as_bytes(*actual.present), descriptor, 1);
    for (std::size_t entry = 0;
         entry < std::min(expected.metadata.size(), actual.metadata.size());
         ++entry) {
      descriptor.element = entry;
      plan.Compare(expected.metadata[entry].key, actual.metadata[entry].key,
                   descriptor);
      plan.Compare(expected.metadata[entry].value, actual.metadata[entry].value,
                   descriptor);
    }
    CompareValues(
        plan, expected.values, actual.values,
        Location(Stage::kSourceCandidate, Code::kAttributeBits, ordinal));
  }
  plan.Run(stats);
}
void VerifyCorrespondence(Operation& operation, Stats& stats) {
  const auto& request = operation.request;
  const auto candidate = request.candidate.Source().View();
  const auto faces = candidate.face_offsets.size() - 1;
  if (request.source_faces.size() != faces)
    Fail(Status::kInvalidCorrespondence, Stage::kCorrespondence,
         Code::kFaceMapShape);
  if (request.source_corners.size() != candidate.corner_vertices.size())
    Fail(Status::kInvalidCorrespondence, Stage::kCorrespondence,
         Code::kCornerMapShape);
  Correspondence state{operation, Correspondence::Pass::kFaces};
  auto run = [&](Correspondence::Pass pass, std::size_t count) {
    state.pass = pass;
    RunRanges(operation,
              gltf_detail::CheckedProduct(count, sizeof(geometry::index_t)),
              Correspondence::Check, &state, stats);
  };
  run(Correspondence::Pass::kFaces, faces);
  run(Correspondence::Pass::kCorners, request.source_corners.size());
  run(Correspondence::Pass::kShapes, faces);
  run(Correspondence::Pass::kLoops, request.source_corners.size());
  operation.changed =
      faces != request.source.SourceExtraction().TriangleIndices().size() /
                   kTriangleCorners;
  if (operation.changed && faces == 0)
    Fail(Status::kUnsupportedSelection, Stage::kCorrespondence,
         Code::kEmptyChangedSelection);
}
}  // namespace meshvale::interchange::gltf_workflow_support_detail
