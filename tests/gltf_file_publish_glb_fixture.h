// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PUBLISH_GLB_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PUBLISH_GLB_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "gltf_file_provider_fixture.h"

// Literal wire/resource inputs plus a real public pipeline. Expected wire is
// encoded independently; neither production GLB writer nor publication runs
// here.
namespace meshvale::interchange::test_glb_publication {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace fs = std::filesystem;
using test_provider::Bytes;
using test_provider::FileLimits;
using test_provider::GeometryWork;
using test_provider::InspectionLimits;
using test_provider::kCountLimit;
using test_provider::kDynamicPath;
using test_provider::kInitialPaths;
using test_provider::kOpaqueBytes;
using test_provider::kOpaquePaths;
using test_provider::kPhysicalBytes;
using test_provider::kRequestBytes;
using test_provider::kStaticBytes;
using test_provider::kStaticPath;
using test_provider::kWorkerCount;
using test_provider::Limits;
using test_provider::ReadBytes;
using test_provider::ReadLimits;
using test_provider::ReadWork;
using test_provider::Require;
using test_provider::TransferLimits;
using test_provider::Work;
using test_provider::Write;
inline constexpr std::string_view kEntry = "scene.gltf";
inline constexpr std::uint32_t kMagic = 0x46546c67;
inline constexpr std::uint32_t kVersion = 2;
inline constexpr std::uint32_t kJsonType = 0x4e4f534a;
inline constexpr std::uint32_t kBinType = 0x004e4942;
inline constexpr std::uint32_t kOpaqueType = 0x12345678;
inline constexpr std::uint32_t kOtherOpaqueType = 0xdecafbad;
inline constexpr std::size_t kAlignmentBytes = 4;
inline constexpr std::size_t kWordBytes = 4;
inline constexpr std::size_t kWordBitsPerByte = 8;
inline constexpr std::uint32_t kByteMask = 0xff;
inline constexpr std::size_t kHeaderBytes = 12;
inline constexpr std::size_t kChunkHeaderBytes = 8;
inline constexpr std::size_t kJsonPayloadOffset =
    kHeaderBytes + kChunkHeaderBytes;
inline constexpr std::size_t kPositionBytes = 36;
inline constexpr std::size_t kColorBytes = 12;
inline constexpr std::size_t kIndexOffset = kPositionBytes + kColorBytes;
inline constexpr std::size_t kTriangleCorners = 3;
inline constexpr std::size_t kSourceIndexBytes = 2;
inline constexpr std::size_t kPngBytes = 8;
inline constexpr std::size_t kBinChunkOrdinal = 1;
inline constexpr std::size_t kFirstOpaqueOrdinal = 2;
inline constexpr std::size_t kChunkCount = 5;
inline constexpr std::size_t kResourceBindings = 4;
inline constexpr std::array<std::byte, 12> kColors{
    std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255},
    std::byte{0},   std::byte{255}, std::byte{0},   std::byte{128},
    std::byte{0},   std::byte{0},   std::byte{255}, std::byte{0}};
inline constexpr std::array<std::byte, 16> kBinary{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
    std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a},
    std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef},
    std::byte{0xca}, std::byte{0xfe}, std::byte{0xba}, std::byte{0xbe}};
inline constexpr std::array<std::uint32_t, 6> kSourceTriangles{0, 1, 2,
                                                               0, 1, 2};
inline constexpr std::array<std::uint32_t, 3> kChangedTriangles{0, 1, 2};
inline constexpr std::string_view kColorSemantic = "COLOR_0";
struct Chunk {
  std::uint32_t type;
  std::vector<std::byte> bytes;
};
inline void Word(std::vector<std::byte>& bytes, std::uint32_t value) {
  for (std::size_t byte = 0; byte < kWordBytes; ++byte)
    bytes.push_back(static_cast<std::byte>(
        (value >> (byte * kWordBitsPerByte)) & kByteMask));
}
inline void SetWord(std::vector<std::byte>& bytes, std::size_t offset,
                    std::uint32_t value) {
  Require(offset + kWordBytes <= bytes.size(), "literal word mutation extent");
  for (std::size_t byte = 0; byte < kWordBytes; ++byte)
    bytes[offset + byte] = static_cast<std::byte>(
        (value >> (byte * kWordBitsPerByte)) & kByteMask);
}
inline std::size_t Padding(std::size_t bytes) {
  return (kAlignmentBytes - bytes % kAlignmentBytes) % kAlignmentBytes;
}
inline std::vector<std::byte> Wire(std::span<const Chunk> chunks) {
  std::vector<std::byte> bytes;
  Word(bytes, kMagic);
  Word(bytes, kVersion);
  Word(bytes, 0);
  for (const auto& chunk : chunks) {
    Require(chunk.bytes.size() % kAlignmentBytes == 0,
            "independently authored aligned chunk");
    Word(bytes, static_cast<std::uint32_t>(chunk.bytes.size()));
    Word(bytes, chunk.type);
    bytes.insert(bytes.end(), chunk.bytes.begin(), chunk.bytes.end());
  }
  SetWord(bytes, kWordBytes * 2, static_cast<std::uint32_t>(bytes.size()));
  return bytes;
}
inline std::vector<std::uint32_t> Triangles(std::size_t faces) {
  std::vector<std::uint32_t> indices;
  for (std::size_t face = 0; face < faces; ++face)
    for (std::uint32_t vertex = 0; vertex < kTriangleCorners; ++vertex)
      indices.push_back(vertex);
  return indices;
}
struct Fixture {
  fs::path root;
  fs::path input;
  test_provider::RootRemoval cleanup{root};
  geo::ExecutionContext context{{kWorkerCount, 1, kRequestBytes}};
  std::vector<std::byte> opaque{kOpaqueBytes, std::byte{0xa7}};
  std::vector<Chunk> chunks;
  std::vector<std::byte> wire;
  std::vector<std::uint32_t> source_triangles;
  std::vector<std::uint32_t> selected_triangles;
  std::optional<ic::GltfFileAsset> source;
  std::optional<ic::GltfTopologyOutputInspection> inspection;
  ic::GltfMaterializationResult materialization;
  explicit Fixture(bool changed = true, std::size_t source_faces = 2,
                   std::size_t selected_faces = 1, std::size_t logical_tail = 0,
                   bool prepare = true, bool entry_alias = false) {
    Require(source_faces > 0 && logical_tail < kAlignmentBytes,
            "literal source configuration");
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-glb-publication-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    Require(fs::create_directory(root), "isolated GLB publication parent");
    input = root / "input";
    Require(fs::create_directory(input), "literal captured root");
    source_triangles = Triangles(source_faces);
    const auto index_bytes = source_triangles.size() * kSourceIndexBytes;
    const auto image_offset = kIndexOffset + index_bytes;
    const auto logical_bytes = image_offset + kPngBytes + logical_tail;
    std::vector<std::byte> bin(test_provider::kBinary.begin(),
                               test_provider::kBinary.begin() + kPositionBytes);
    bin.insert(bin.end(), kColors.begin(), kColors.end());
    for (const auto index : source_triangles) {
      bin.push_back(static_cast<std::byte>(index));
      bin.push_back(std::byte{0});
    }
    bin.insert(bin.end(), kBinary.begin(), kBinary.begin() + kPngBytes);
    for (std::size_t byte = 0; byte < logical_tail; ++byte)
      bin.push_back(static_cast<std::byte>(0xd0 + byte));
    bin.resize(bin.size() + Padding(bin.size()), std::byte{0});
    const auto self_buffer =
        entry_alias ? std::string(",{\"uri\":\"scene.gltf\",\"byteLength\":4}")
                    : std::string{};
    const std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" +
        std::to_string(logical_bytes) +
        R"(},{"uri":"%61.bin","byteLength":8},{"uri":"a.bin","byteLength":8},{"uri":"data:application/octet-stream;base64,AAECAwQFBgc=","byteLength":8})" +
        self_buffer +
        R"(],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":)" +
        std::to_string(index_bytes) + R"(},{"buffer":0,"byteOffset":)" +
        std::to_string(image_offset) +
        R"(,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4","normalized":true},{"bufferView":2,"componentType":5123,"count":)" +
        std::to_string(source_triangles.size()) +
        R"(,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"COLOR_0":1},"indices":2,"mode":4}]}],"images":[{"uri":"a.bin"},{"uri":"data:image/png;base64,iVBORw0KGgo="},{"bufferView":3,"mimeType":"image/png"}],"extras":{"spelling":1.2300e+04}} )" +
        "\t\n";
    std::vector<std::byte> json_chunk(Bytes(json).begin(), Bytes(json).end());
    json_chunk.resize(json_chunk.size() + Padding(json_chunk.size()),
                      std::byte{' '});
    chunks.push_back({kJsonType, std::move(json_chunk)});
    chunks.push_back({kBinType, std::move(bin)});
    chunks.push_back(
        {kOpaqueType, std::vector<std::byte>(kOpaqueBytes, std::byte{0x53})});
    chunks.push_back(
        {kOtherOpaqueType,
         {std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}}});
    chunks.push_back(
        {kOpaqueType,
         {std::byte{0}, std::byte{0x7f}, std::byte{0x80}, std::byte{0xff}}});
    wire = Wire(chunks);
    Write(input / kEntry, wire);
    Write(input / "a.bin", kBinary);
    for (const auto path : kOpaquePaths) Write(input / path, opaque);
    auto captured = ic::ReadGltfFileAsset(
        input / kEntry,
        {{},
         ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
         kOpaquePaths},
        {FileLimits(),
         ReadLimits(),
         {kPhysicalBytes, kCountLimit, kRequestBytes}},
        {Work(true).files, ReadWork(true), Work(true).container}, context);
    Require(captured.asset.has_value(), "actual rich GLB source capture");
    source = *captured.asset;
    if (prepare) Prepare(changed, selected_faces);
  }
  void Prepare(bool changed, std::size_t selected_faces = 1,
               bool require_materialization = true) {
    auto extraction = ic::ExtractGltfPrimitiveCapped(
        source->Asset(), *source->Asset().Primitive(0),
        {ic::GltfTriangleRequest::kRequireTriangles,
         ic::GltfValueRequest::kRawAndNormalized},
        {kCountLimit, kCountLimit, kRequestBytes});
    Require(extraction.extraction &&
                std::ranges::equal(extraction.extraction->TriangleIndices(),
                                   source_triangles),
            "literal source duplicate topology independently extracted");
    materialization = ic::MaterializeGltfPrimitive(
        *extraction.extraction, TransferLimits(), GeometryWork());
    if (!require_materialization && !materialization.primitive) return;
    Require(materialization.primitive.has_value(),
            "actual qualified GLB materialization");
    const auto faces =
        changed ? selected_faces : source_triangles.size() / kTriangleCorners;
    Require(faces > 0 && faces <= source_triangles.size() / kTriangleCorners,
            "literal ordered selection extent");
    selected_triangles = Triangles(faces);
    std::vector<geo::index_t> face_map;
    std::vector<geo::index_t> corner_map;
    for (std::size_t face = 0; face < faces; ++face) {
      face_map.push_back(static_cast<geo::index_t>(face));
      for (std::size_t corner = 0; corner < kTriangleCorners; ++corner)
        corner_map.push_back(
            static_cast<geo::index_t>(face * kTriangleCorners + corner));
    }
    auto selected =
        geo::SelectMeshFaces(materialization.primitive->Mesh(), face_map,
                             TransferLimits().materialization, GeometryWork());
    Require(selected.selection.has_value(), "real GLB selected candidate");
    auto candidate = geo::InspectMaterializedMesh(
        selected.selection->Candidate(), TransferLimits().inspection,
        GeometryWork());
    Require(candidate.inspection && candidate.inspection->IsStorageValid(),
            "actual selected storage inspector");
    const ic::GltfTopologyWriteLimits writer_limits{
        kPhysicalBytes,
        kCountLimit,
        test_provider::kDepthLimit,
        kCountLimit,
        test_provider::kChannelLimit,
        kCountLimit,
        kPhysicalBytes,
        kCountLimit,
        kCountLimit,
        kPhysicalBytes,
        {kPhysicalBytes, kCountLimit, kPhysicalBytes, kPhysicalBytes,
         kRequestBytes}};
    auto written = ic::WriteGltfTopologySelection(
        {*materialization.primitive, *candidate.inspection, face_map,
         corner_map},
        writer_limits,
        {1, test_provider::kPartitionBytes, test_provider::kCheckpointBytes,
         test_provider::kJobLimit});
    Require(written.bundle.has_value(),
            "actual topology output input for publisher");
    std::vector<ic::SuppliedGltfResource> bindings;
    for (const auto& resource : written.bundle->Resources())
      if (resource.origin == ic::GltfResourceOrigin::kCallerBinding)
        bindings.push_back(
            {resource.kind, resource.table_index, resource.bytes});
    auto reloaded =
        ic::ReadGltfAssetCapped(written.bundle->Json(), bindings, ReadLimits(),
                                ReadWork(true), context);
    Require(reloaded.asset.has_value(),
            "actual public memory GLB output reload");
    auto output_extraction = ic::ExtractGltfPrimitiveCapped(
        *reloaded.asset, *reloaded.asset->Primitive(0),
        {ic::GltfTriangleRequest::kRequireTriangles,
         ic::GltfValueRequest::kRawAndNormalized},
        {kCountLimit, kCountLimit, kRequestBytes});
    Require(
        output_extraction.extraction &&
            std::ranges::equal(output_extraction.extraction->TriangleIndices(),
                               selected_triangles),
        "literal selected indices independently decoded before publication");
    auto inspected = ic::InspectGltfTopologyOutput(
        {*materialization.primitive, *candidate.inspection, face_map,
         corner_map, *written.bundle, *reloaded.asset,
         *output_extraction.extraction},
        InspectionLimits(),
        {1, test_provider::kPartitionBytes, test_provider::kCheckpointBytes,
         test_provider::kJobLimit});
    Require(inspected.inspection.has_value(),
            "independent memory output authority");
    inspection = *inspected.inspection;
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  fs::path Destination(std::string_view name = "published-a") const {
    return root / name;
  }
  const fs::path& SourceRoot() const noexcept { return input; }
  std::vector<std::byte> ExpectedWire() const {
    Require(inspection.has_value(), "actual successful input inspection");
    if (!inspection->Output().ChangedBinding()) return wire;
    auto expected_chunks = chunks;
    const auto prefix = inspection->Output().Json();
    expected_chunks[0].bytes.assign(prefix.begin(), prefix.end());
    expected_chunks[0].bytes.resize(prefix.size() + Padding(prefix.size()),
                                    std::byte{' '});
    return Wire(expected_chunks);
  }
};
}  // namespace meshvale::interchange::test_glb_publication
#endif  // MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PUBLISH_GLB_FIXTURE_H_
