// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "fixture_support.h"

namespace gltf_materialization_semantics {
namespace {
constexpr std::uint64_t kExpectedPrimitives = 4;
constexpr std::uint64_t kExpectedInstances = 6;
constexpr std::size_t kExpectedNodes = 3;
constexpr std::size_t kExpectedScenes = 2;
constexpr std::size_t kResourceTrailerBytes = 3;
constexpr std::string_view kResourceUri = "memory.bin";
constexpr std::string_view kExactIntegerLexeme = "9007199254740993";
constexpr std::string_view kNegativeZeroLexeme = "-0.0";
constexpr std::uint64_t kSmallExtractionElements = 4096;
constexpr std::uint64_t kSmallExtractionBytes = 64 * 1024 * 1024;
constexpr std::uint64_t kFanMode = 6;

template <class T>
std::vector<std::byte> CopyBytes(std::span<const T> values) {
  const auto bytes = std::as_bytes(values);
  return {bytes.begin(), bytes.end()};
}
std::vector<std::byte> RawBytes(const mv::GltfRawValues& values) {
  return std::visit([](auto span) { return CopyBytes(span); }, values);
}
std::vector<std::byte> AttributeBytes(const geo::ScalarReadView& values) {
  return std::visit([](auto span) { return CopyBytes(span); }, values);
}
struct ChannelSnapshot {
  std::string semantic;
  std::uint64_t accessor;
  mv::GltfComponentType component_type;
  mv::GltfAccessorShape shape;
  bool normalized;
  std::uint64_t elements;
  std::optional<std::uint64_t> set;
  std::optional<std::uint64_t> base;
  std::uint64_t offset;
  std::uint64_t stride;
  std::uint64_t sparse_step;
  std::optional<mv::GltfSparseProvenance> sparse;
  std::vector<std::uint64_t> destinations;
  std::optional<std::vector<std::byte>> minimum;
  std::optional<std::vector<std::byte>> maximum;
  std::vector<std::byte> raw;
  std::vector<std::byte> derived;
};
std::vector<ChannelSnapshot> Snapshot(
    const mv::GltfPrimitiveExtraction& source) {
  std::vector<ChannelSnapshot> result;
  for (const auto& channel : source.Channels()) {
    result.push_back(
        {std::string(channel.semantic),
         channel.accessor_index,
         channel.component_type,
         channel.shape,
         channel.normalized,
         channel.element_count,
         channel.set_index,
         channel.base_buffer_view,
         channel.base_byte_offset,
         channel.base_byte_stride,
         channel.packed_sparse_step,
         channel.sparse_source,
         {channel.sparse_destinations.begin(),
          channel.sparse_destinations.end()},
         channel.exact_min ? std::optional(RawBytes(*channel.exact_min))
                           : std::nullopt,
         channel.exact_max ? std::optional(RawBytes(*channel.exact_max))
                           : std::nullopt,
         RawBytes(channel.raw),
         CopyBytes(channel.normalized_values)});
  }
  return result;
}
void CheckSnapshot(const mv::GltfPrimitiveExtraction& source,
                   const std::vector<ChannelSnapshot>& expected) {
  Require(source.Channels().size() == expected.size(),
          "Retained channel inventory changed after owner destruction");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const auto& actual = source.Channels()[i];
    const auto& before = expected[i];
    Require(actual.semantic == before.semantic &&
                actual.accessor_index == before.accessor &&
                actual.component_type == before.component_type &&
                actual.shape == before.shape &&
                actual.normalized == before.normalized &&
                actual.element_count == before.elements &&
                actual.set_index == before.set &&
                actual.base_buffer_view == before.base &&
                actual.base_byte_offset == before.offset &&
                actual.base_byte_stride == before.stride &&
                actual.packed_sparse_step == before.sparse_step &&
                actual.sparse_source.has_value() == before.sparse.has_value(),
            "Retained channel provenance changed after owner destruction");
    if (before.sparse) {
      const auto& sparse = *actual.sparse_source;
      Require(
          sparse.indices_buffer_view == before.sparse->indices_buffer_view &&
              sparse.indices_byte_offset ==
                  before.sparse->indices_byte_offset &&
              sparse.indices_component_type ==
                  before.sparse->indices_component_type &&
              sparse.values_buffer_view == before.sparse->values_buffer_view &&
              sparse.values_byte_offset == before.sparse->values_byte_offset,
          "Sparse byte provenance changed after owner destruction");
    }
    RequireBits<std::uint64_t>(actual.sparse_destinations, before.destinations,
                               "Sparse destination lifetime failed");
    Require(RawBytes(actual.raw) == before.raw &&
                CopyBytes(actual.normalized_values) == before.derived &&
                actual.exact_min.has_value() == before.minimum.has_value() &&
                actual.exact_max.has_value() == before.maximum.has_value(),
            "Original encodings, derived values or bounds lost their owner");
    if (before.minimum)
      Require(RawBytes(*actual.exact_min) == *before.minimum,
              "Minimum bound lifetime failed");
    if (before.maximum)
      Require(RawBytes(*actual.exact_max) == *before.maximum,
              "Maximum bound lifetime failed");
  }
}
void CheckAsset(const mv::GltfMaterializedPrimitive& primitive,
                std::string_view json, std::span<const std::byte> resource) {
  const auto& asset = primitive.SourceAsset();
  RequireBits<std::byte>(
      asset.OriginalJson(),
      {reinterpret_cast<const std::byte*>(json.data()), json.size()},
      "Retained JSON bytes changed or reference destroyed input");
  Require(asset.Resources().size() == 1, "Retained resource inventory changed");
  const auto& retained = asset.Resources()[0];
  Require(retained.kind == mv::GltfResourceKind::kBuffer &&
              retained.table_index == 0 &&
              retained.origin == mv::GltfResourceOrigin::kCallerBinding &&
              retained.authored_uri == kResourceUri &&
              retained.logical_bytes == resource.size() - kResourceTrailerBytes,
          "Full resource and authored logical extent must remain distinct");
  RequireBits<std::byte>(
      retained.bytes, resource,
      "Retained full buffer, including trailing bytes, changed");
  Require(asset.PrimitiveCount() == kExpectedPrimitives &&
              asset.InstanceCount() == kExpectedInstances &&
              asset.Nodes().size() == kExpectedNodes &&
              asset.Scenes().size() == kExpectedScenes &&
              primitive.SourcePrimitive().MeshIndex() == 1 &&
              primitive.SourcePrimitive().PrimitiveIndex() == 1 &&
              primitive.SourceExtraction().MaterialIndex() == 0,
          "Retained graph, selected identity or material annotation changed");
  constexpr std::array<std::uint64_t, 2> kFirstRoots{0, 1};
  constexpr std::array<std::uint64_t, 1> kSecondRoots{2};
  RequireBits<std::uint64_t>(asset.Scenes()[0].roots, kFirstRoots,
                             "First scene identity changed");
  RequireBits<std::uint64_t>(asset.Scenes()[1].roots, kSecondRoots,
                             "Second scene identity changed");
  Require(asset.Nodes()[0].mesh == 1 && asset.Nodes()[1].mesh == 1 &&
              asset.Nodes()[2].mesh == 0 &&
              asset.Nodes()[0].local_transform_bits[0] == 0x3f800000U &&
              asset.Nodes()[0].local_transform_bits[1] == 0x40000000U &&
              asset.Nodes()[0].local_transform_bits[2] == 0x40400000U,
          "Shared instances or unapplied local transform metadata changed");
  bool integer_retained = false;
  bool zero_retained = false;
  for (const auto& number : asset.RawNumbers()) {
    integer_retained |= number.lexeme == kExactIntegerLexeme;
    zero_retained |= number.lexeme == kNegativeZeroLexeme;
  }
  Require(integer_retained && zero_retained,
          "Opaque exact-number lexemes must survive canonical transfer");
  const mv::GltfExtractionRequest request{
      mv::GltfTriangleRequest::kRequireTriangles,
      mv::GltfValueRequest::kRawAndNormalized};
  const mv::GltfExtractionLimits limits{kSmallExtractionElements,
                                        kSmallExtractionElements,
                                        kSmallExtractionBytes};
  const auto reextracted = mv::ExtractGltfPrimitive(
      asset, primitive.SourcePrimitive(), request, limits, {});
  Require(reextracted.extraction &&
              reextracted.diagnostic.status == mv::GltfStatus::kAccepted,
          "Retained selected primitive must belong to the retained asset");
  CheckSnapshot(*reextracted.extraction,
                Snapshot(primitive.SourceExtraction()));
}
}  // namespace

void CheckSourceAndMeshLifetimes() {
  std::optional<mv::GltfMaterializedPrimitive> retained;
  std::optional<geo::MaterializedMesh> mesh;
  std::optional<mv::GltfPrimitiveExtraction> extraction;
  std::optional<mv::GltfAsset> asset;
  std::optional<mv::GltfPrimitiveHandle> handle;
  std::vector<ChannelSnapshot> channels;
  std::vector<std::vector<std::byte>> attribute_bytes;
  std::vector<std::byte> position_bytes;
  std::vector<geo::index_t> corners;
  std::string expected_json;
  std::vector<std::byte> expected_resource;
  {
    auto fixture = RawChannelsFixture();
    expected_json = fixture.json;
    expected_resource = fixture.bytes;
    const auto original = Transfer(fixture);
    retained.emplace(original);
    mesh.emplace(original.Mesh());
    extraction.emplace(original.SourceExtraction());
    asset.emplace(original.SourceAsset());
    handle.emplace(original.SourcePrimitive());
    channels = Snapshot(original.SourceExtraction());
    const auto view = original.Mesh().View();
    position_bytes = CopyBytes(view.positions);
    corners.assign(view.corner_vertices.begin(), view.corner_vertices.end());
    for (const auto& attribute : view.attributes)
      attribute_bytes.push_back(AttributeBytes(attribute.values));
    std::fill(fixture.json.begin(), fixture.json.end(), 'x');
    std::fill(fixture.bytes.begin(), fixture.bytes.end(), std::byte{0});
  }
  // Transfer has also destroyed its original context/read/extraction wrappers
  // after stopping both earlier operation tokens. Only independent owners
  // remain.
  CheckAsset(*retained, expected_json, expected_resource);
  CheckSnapshot(retained->SourceExtraction(), channels);
  CheckSnapshot(*extraction, channels);
  auto reassigned = *retained;
  reassigned = Transfer(MakeTopologyFixture(TopologyFixture::kFan));
  Require(reassigned.SourceExtraction().SourceMode() == kFanMode,
          "Independent wrapper reassignment did not change its own value");
  CheckSnapshot(retained->SourceExtraction(), channels);
  retained.reset();
  CheckSnapshot(*extraction, channels);
  extraction.reset();
  asset.reset();
  handle.reset();
  const auto view = mesh->View();
  Require(CopyBytes(view.positions) == position_bytes &&
              view.attributes.size() == attribute_bytes.size(),
          "Copied-out canonical mesh depended on destroyed source owners");
  RequireBits<geo::index_t>(view.corner_vertices, corners,
                            "Copied-out corner storage lifetime failed");
  for (std::size_t i = 0; i < attribute_bytes.size(); ++i)
    Require(AttributeBytes(view.attributes[i].values) == attribute_bytes[i],
            "Copied-out raw attribute lifetime failed");
  const auto inspected = geo::InspectMaterializedMesh(
      *mesh, TransferLimits().inspection, Work(), {});
  Require(inspected.status == geo::MaterializationStatus::kSuccess &&
              inspected.inspection && inspected.inspection->IsStorageValid() &&
              inspected.inspection->Source().View().positions.data() ==
                  view.positions.data(),
          "Fresh inspection must retain the same independent canonical owner");
  const auto selected = geo::SelectMeshFaces(
      *mesh, {}, TransferLimits().materialization, Work(), {});
  Require(
      selected.status == geo::MaterializationStatus::kSuccess &&
          selected.selection,
      "Copied-out mesh must retain its execution context for later operations");
}
}  // namespace gltf_materialization_semantics
