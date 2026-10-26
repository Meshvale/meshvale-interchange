// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_GLTF_TRANSLATION_OUTPUT_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_GLTF_TRANSLATION_OUTPUT_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Original product-owned supplied-byte fixture, shared by native controls and
// the public installed consumer. It invokes only public SDK interfaces.
namespace gltf_translation_output_fixture {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
inline constexpr std::size_t kMiB = 1024 * 1024;
inline constexpr std::size_t kFloatBytes = 4;
inline constexpr std::size_t kByteBits = 8;
inline constexpr std::uint32_t kByteMask = 0xff;
inline constexpr std::uint32_t kOne = 0x3f800000;
inline constexpr std::uint32_t kTwo = 0x40000000;
inline constexpr std::uint32_t kUnusedTail = 0xdecafbad;
inline constexpr std::size_t kLogicalBytes = 152;
inline constexpr std::size_t kTranslationOffset = 128;
inline constexpr std::size_t kTranslationSecondY = 144;
inline constexpr std::uint64_t kCount = 65536;
inline constexpr std::uint64_t kChannels = 64;
inline constexpr std::uint64_t kDepth = 128;
inline constexpr std::uint64_t kPartitionBytes = 256;
inline constexpr std::uint64_t kCancelBytes = 64;
inline constexpr std::uint64_t kJobs = 4;
inline constexpr std::array<geo::index_t, 1> kChangedFaces{1};
inline constexpr std::array<geo::index_t, 3> kChangedCorners{3, 4, 5};
inline constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
inline constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
inline constexpr std::array<std::uint32_t, 39> kWords{
    0,    0, 0, kOne, 0, 0,    0, kOne, 0, 0,    0,    0, kOne,
    0,    0, 0, kOne, 0, 0,    0, kOne, 0, 0,    kOne, 0, 0,
    kOne, 0, 0, kOne, 0, kOne, 0, 0,    0, kTwo, 0,    0, kUnusedTail};
inline constexpr std::string_view kPrefix =
    R"({"asset":{"version":"2.0"},"extras":{"exact":9007199254740993},"buffers":[{"uri":"moving.bin","byteLength":152}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":72,"target":34962},{"buffer":0,"byteOffset":72,"byteLength":48,"target":34962},{"buffer":0,"byteOffset":120,"byteLength":8},{"buffer":0,"byteOffset":128,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":6,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":6,"type":"VEC2"},{"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":3,"componentType":5126,"count":2,"type":"VEC3"}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"roughnessFactor":0.5}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]}],"nodes":[{"children":[1,2]},{"mesh":0},{"mesh":0,"translation":[0,1,0]}],"scenes":[{"nodes":[0]}],"animations":[{"samplers":[{"input":2,"output":3,"interpolation":")";
inline constexpr std::string_view kSuffix =
    R"("}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}},{"sampler":0,"target":{"node":2,"path":"translation"}}]}]})";
inline void Require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
inline std::vector<std::byte> Bytes() {
  std::vector<std::byte> result(kWords.size() * kFloatBytes);
  for (std::size_t word = 0; word < kWords.size(); ++word)
    for (std::size_t byte = 0; byte < kFloatBytes; ++byte)
      result[word * kFloatBytes + byte] =
          std::byte((kWords[word] >> (byte * kByteBits)) & kByteMask);
  return result;
}
inline std::string Json(bool step = false) {
  return std::string(kPrefix) + (step ? "STEP" : "LINEAR") +
         std::string(kSuffix);
}
inline mv::GltfDirectTranslationLimits TranslationLimits() {
  return {65536,  65536,   4096,     4096,     65536,
          262144, 1048576, 2 * kMiB, 4 * kMiB, 8 * kMiB};
}
inline mv::GltfDirectTranslationWorkPolicy TranslationWork(
    bool serial = false) {
  return {serial ? std::uint64_t(std::numeric_limits<std::int64_t>::max()) : 1,
          1, 1, 4};
}
inline mv::GltfReadLimits ReadLimits() {
  return {kMiB, 8 * kMiB, 16 * kMiB, kCount, kChannels, kMiB,   256,
          256,  32,       kCount,    kCount, kCount,    262144, 8 * kMiB};
}
inline mv::GltfWorkPolicy ReadWork() {
  return {kMiB, kMiB, kMiB, 16384, TranslationWork()};
}
inline geo::MeshMaterializationLimits GeometryLimits() {
  return {kCount, kCount, kCount, kCount, kChannels, kCount,
          kCount, kCount, kCount, kMiB,   16 * kMiB};
}
inline geo::MeshMaterializationWorkPolicy GeometryWork() {
  return {1, kPartitionBytes, kCancelBytes, kJobs};
}
inline mv::GltfMaterializationLimits TransferLimits() {
  return {kChannels, 8 * kMiB, GeometryLimits(), {kCount, 8 * kMiB}};
}
inline mv::GltfAsset Capture(
    std::string_view json, std::span<const mv::SuppliedGltfResource> resources,
    const geo::ExecutionContext& context) {
  auto result = mv::ReadGltfAssetCapped(
      std::as_bytes(std::span(json.data(), json.size())), resources,
      {ReadLimits(), 16 * kMiB}, ReadWork(), context);
  Require(result.asset.has_value(), "original moving data reader admission");
  return *result.asset;
}
inline mv::GltfPrimitiveExtraction Extract(const mv::GltfAsset& asset) {
  auto result = mv::ExtractGltfPrimitiveCapped(
      asset, *asset.Primitive(0),
      {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw,
       mv::GltfProcessingProfile::kDirectTranslationTrianglesV1},
      {kMiB, kMiB, 40 * kMiB, TranslationLimits()});
  Require(result.extraction && result.direct_translation &&
              result.direct_translation->complete,
          "real moving extraction/proof");
  return *result.extraction;
}
inline mv::GltfMaterializedPrimitive Source(
    const geo::ExecutionContext& context, bool step = false) {
  auto bytes = Bytes();
  const std::array<mv::SuppliedGltfResource, 1> resources{
      {{mv::GltfResourceKind::kBuffer, 0, bytes}}};
  auto asset = Capture(Json(step), resources, context);
  auto extracted = Extract(asset);
  auto result =
      mv::MaterializeGltfPrimitive(extracted, TransferLimits(), GeometryWork());
  Require(result.primitive.has_value(), "real moving canonical transfer");
  return *result.primitive;
}
inline geo::MeshInspection Candidate(
    const mv::GltfMaterializedPrimitive& source, bool identity = false) {
  const auto faces = identity ? std::span<const geo::index_t>(kIdentityFaces)
                              : std::span<const geo::index_t>(kChangedFaces);
  auto selected = geo::SelectMeshFaces(source.Mesh(), faces, GeometryLimits(),
                                       GeometryWork());
  Require(selected.selection.has_value(),
          "actual public ordered face selection");
  auto inspected =
      geo::InspectMaterializedMesh(selected.selection->Candidate(),
                                   TransferLimits().inspection, GeometryWork());
  Require(inspected.inspection && inspected.inspection->IsStorageValid(),
          "actual candidate inspection");
  return *inspected.inspection;
}
inline std::span<const geo::index_t> Faces(bool identity) {
  return identity ? std::span<const geo::index_t>(kIdentityFaces)
                  : std::span<const geo::index_t>(kChangedFaces);
}
inline std::span<const geo::index_t> Corners(bool identity) {
  return identity ? std::span<const geo::index_t>(kIdentityCorners)
                  : std::span<const geo::index_t>(kChangedCorners);
}
inline mv::GltfTopologyWriteLimits WriteLimits() {
  return {kMiB,
          kCount,
          kDepth,
          kCount,
          kChannels,
          kCount,
          kMiB,
          kCount,
          kCount,
          kMiB,
          {kMiB, kChannels, 8 * kMiB, 16 * kMiB, 72 * kMiB},
          TranslationLimits()};
}
inline mv::GltfTopologyWriteWorkPolicy WriteWork(bool serial = false) {
  return {1, kPartitionBytes, kCancelBytes, kJobs, TranslationWork(serial)};
}
inline mv::GltfTopologyOutputInspectionLimits InspectionLimits() {
  return {kMiB,      kCount,
          kDepth,    kChannels,
          16 * kMiB, kCount,
          kChannels, kCount,
          kMiB,      kCount,
          kCount,    kMiB,
          40 * kMiB, TranslationLimits()};
}
inline mv::GltfTopologyOutputInspectionWorkPolicy InspectionWork(
    bool serial = false) {
  return {1, kPartitionBytes, kCancelBytes, kJobs, TranslationWork(serial)};
}
inline mv::GltfAsset Reload(const mv::GltfTopologyWriteBundle& bundle,
                            const geo::ExecutionContext& context,
                            std::span<const std::byte> replacement = {}) {
  std::vector<mv::SuppliedGltfResource> resources;
  for (const auto& resource : bundle.Resources())
    if (resource.origin == mv::GltfResourceOrigin::kCallerBinding)
      resources.push_back({resource.kind, resource.table_index,
                           replacement.empty() ? resource.bytes : replacement});
  const auto json =
      std::string_view(reinterpret_cast<const char*>(bundle.Json().data()),
                       bundle.Json().size());
  return Capture(json, resources, context);
}
inline void CheckLiteral(const mv::GltfTopologyWriteBundle& bundle,
                         bool identity, bool step) {
  Require(std::ranges::equal(bundle.SourceFaces(), Faces(identity)) &&
              std::ranges::equal(bundle.SourceCorners(), Corners(identity)),
          "literal ordered maps");
  Require(bundle.Source().Mesh().View().positions.size() == 18 &&
              bundle.Source().Mesh().View().attributes.size() == 1 &&
              bundle.Source().SourceExtraction().MaterialIndex() == 0,
          "all vertex rows/UV/material retained");
  Require(std::ranges::equal(bundle.Resources()[0].bytes, Bytes()) &&
              bundle.Resources()[0].logical_bytes == kLogicalBytes,
          "source keys and unused tail exact");
  const auto proof = bundle.Source().SourceExtraction().DirectTranslation();
  Require(proof &&
              std::ranges::equal(proof->mesh_users,
                                 std::array<std::uint64_t, 2>{1, 2}) &&
              std::ranges::equal(proof->node_membership,
                                 std::array<std::uint8_t, 3>{2, 1, 1}) &&
              proof->tracks.size() == 2 &&
              proof->tracks[0].sampler_index == 0 &&
              proof->tracks[1].sampler_index == 0 &&
              proof->tracks[0].key_count == 2,
          "literal shared users/ancestor/channel/sampler/key inventory");
  if (identity) {
    const auto json = Json(step);
    Require(std::ranges::equal(bundle.Json(), std::as_bytes(std::span(json))),
            "identity JSON exact");
  } else
    Require(bundle.ChangedBinding() &&
                bundle.ChangedBinding()->output_indices_accessor == 4 &&
                bundle.ChangedBinding()->appended_buffer == 1 &&
                bundle.ChangedBinding()->appended_buffer_view == 4,
            "append-only binding literals");
}
}  // namespace gltf_translation_output_fixture
#endif  // MESHVALE_INTERCHANGE_TESTS_GLTF_TRANSLATION_OUTPUT_FIXTURE_H_
