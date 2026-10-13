// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PROVIDER_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PROVIDER_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
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
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Authored literal inputs and real public owner setup only. Expected bytes/maps
// do not come from a writer, parser or production preservation helper.
namespace meshvale::interchange::test_provider {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace fs = std::filesystem;
inline std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}
inline constexpr std::uint64_t kPhysicalBytes = 1024 * 1024;
inline constexpr std::uint64_t kRequestBytes = 64 * kPhysicalBytes;
inline constexpr std::uint64_t kCountLimit = 4096;
inline constexpr std::uint64_t kChannelLimit = 64;
inline constexpr std::uint64_t kDepthLimit = 64;
inline constexpr std::uint64_t kPartitionBytes = 4096;
inline constexpr std::uint64_t kCheckpointBytes = 64;
inline constexpr std::uint64_t kJobLimit = 64;
inline constexpr std::size_t kWorkerCount = 4;
inline constexpr std::size_t kOpaqueBytes = 65536;
inline constexpr std::size_t kSourceBinaryBytes = 56;
inline constexpr std::string_view kEntry = "scene.gltf";
inline constexpr std::string_view kStaticPath = "receipts/static.bin";
inline constexpr std::string_view kDynamicPath = "receipts/dynamic.bin";
inline constexpr std::string_view kStaticBytes = "authored static supplement\n";
inline constexpr std::string_view kDynamicBytes =
    "authored dynamic supplement\n";
inline constexpr std::array<std::string_view, 2> kOpaquePaths{"opaque-a.bin",
                                                              "opaque-b.bin"};
inline constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"a.bin","byteLength":48}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"extras":{"spelling":1.2300e+04}})";
inline constexpr std::array<std::byte, kSourceBinaryBytes> kBinary{
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0x80},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0x80}, std::byte{0x3f},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0x80}, std::byte{0x3f},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{1},    std::byte{0},
    std::byte{2},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{1},    std::byte{0},    std::byte{2},    std::byte{0},
    std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef},
    std::byte{0xca}, std::byte{0xfe}, std::byte{0xba}, std::byte{0xbe}};
inline constexpr std::array<std::uint32_t, 6> kSourceTriangles{0, 1, 2,
                                                               0, 1, 2};
inline constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
inline constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
inline constexpr std::array<geo::index_t, 1> kChangedFaces{0};
inline constexpr std::array<geo::index_t, 3> kChangedCorners{0, 1, 2};
inline constexpr std::array<std::uint32_t, 3> kChangedTriangles{0, 1, 2};

inline constexpr std::array<std::string_view, 5> kInitialPaths{
    "a.bin", "opaque-a.bin", "opaque-b.bin", kStaticPath, kEntry};
// Independently authored GLB2 wire: retained JSON references the external
// buffer; the final opaque chunk deliberately establishes retained container
// ownership.
inline std::vector<std::byte> LiteralGlb() {
  constexpr std::uint32_t kMagic = 0x46546c67;
  constexpr std::uint32_t kVersion = 2;
  constexpr std::uint32_t kJsonChunk = 0x4e4f534a;
  constexpr std::uint32_t kOpaqueChunk = 0x12345678;
  constexpr std::size_t kWordBytes = 4;
  constexpr std::size_t kBitsPerByte = 8;
  constexpr std::uint32_t kByteMask = 0xff;
  constexpr std::size_t kHeaderBytes = 12;
  constexpr std::size_t kChunkHeaderBytes = 8;
  constexpr std::array<std::byte, 4> kOpaqueWire{
      std::byte{0x7f}, std::byte{0}, std::byte{0xde}, std::byte{0xad}};
  auto json = std::string(kJson);
  while (json.size() % kWordBytes != 0) json.push_back(' ');
  std::vector<std::byte> bytes;
  const auto word = [&](std::uint32_t value) {
    for (std::size_t i = 0; i < kWordBytes; ++i)
      bytes.push_back(
          static_cast<std::byte>((value >> (i * kBitsPerByte)) & kByteMask));
  };
  word(kMagic);
  word(kVersion);
  word(static_cast<std::uint32_t>(kHeaderBytes + 2 * kChunkHeaderBytes +
                                  json.size() + kOpaqueWire.size()));
  word(static_cast<std::uint32_t>(json.size()));
  word(kJsonChunk);
  bytes.insert(bytes.end(), Bytes(json).begin(), Bytes(json).end());
  word(static_cast<std::uint32_t>(kOpaqueWire.size()));
  word(kOpaqueChunk);
  bytes.insert(bytes.end(), kOpaqueWire.begin(), kOpaqueWire.end());
  return bytes;
}
inline void Require(
    bool condition, std::string_view message,
    std::source_location where = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
inline void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(output.is_open(), "literal fixture output opened");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  Require(!output.fail(), "literal fixture output complete");
}
inline std::vector<std::byte> ReadBytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  Require(input.is_open(), "independent finalized file reopened");
  const std::string text{std::istreambuf_iterator<char>(input), {}};
  return {Bytes(text).begin(), Bytes(text).end()};
}
inline std::uint64_t Threshold(bool serial) {
  return serial ? static_cast<std::uint64_t>(
                      std::numeric_limits<std::ptrdiff_t>::max())
                : 1;
}
inline ic::GltfReadOperationLimits ReadLimits() {
  return {{kPhysicalBytes, kPhysicalBytes, kPhysicalBytes, kCountLimit,
           kDepthLimit, kPhysicalBytes, kCountLimit, kCountLimit, kCountLimit,
           kCountLimit, kCountLimit, kCountLimit, kCountLimit, kRequestBytes},
          kRequestBytes};
}
inline ic::GltfFileLimits FileLimits() {
  return {kCountLimit,    kCountLimit,    kPhysicalBytes, kPhysicalBytes,
          kPhysicalBytes, kPhysicalBytes, kPhysicalBytes, kRequestBytes};
}
inline ic::GltfWorkPolicy ReadWork(bool serial = false) {
  return {Threshold(serial), kPartitionBytes, Threshold(serial),
          kPartitionBytes};
}
inline geo::MeshMaterializationWorkPolicy GeometryWork() {
  return {1, kPartitionBytes, kCheckpointBytes, kJobLimit};
}
inline ic::GltfMaterializationLimits TransferLimits() {
  return {kChannelLimit,
          kRequestBytes,
          {kCountLimit, kCountLimit, kCountLimit, kCountLimit, kChannelLimit,
           kCountLimit, kCountLimit, kCountLimit, kCountLimit, kPhysicalBytes,
           kRequestBytes},
          {kCountLimit, kRequestBytes}};
}
inline ic::GltfTopologyOutputInspectionLimits InspectionLimits() {
  return {kPhysicalBytes, kCountLimit, kDepthLimit,   kCountLimit,
          kPhysicalBytes, kCountLimit, kChannelLimit, kCountLimit,
          kPhysicalBytes, kCountLimit, kCountLimit,   kPhysicalBytes,
          kRequestBytes};
}
inline ic::GltfFilePublicationLimits Limits() {
  return {FileLimits(),
          ReadLimits(),
          {kCountLimit, kCountLimit, kRequestBytes},
          InspectionLimits(),
          {kPhysicalBytes, kCountLimit, kRequestBytes}};
}
inline ic::GltfFilePublicationWorkPolicy Work(bool serial = false) {
  const auto threshold = Threshold(serial);
  return {{kPartitionBytes, threshold, kPartitionBytes, kCheckpointBytes,
           kJobLimit},
          ReadWork(serial),
          {threshold, kPartitionBytes, kCheckpointBytes, kJobLimit},
          {threshold, kPartitionBytes}};
}
struct RootRemoval {
  const fs::path& root;
  ~RootRemoval() noexcept {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};
struct Fixture {
  fs::path root;
  fs::path input;
  RootRemoval cleanup{root};
  geo::ExecutionContext context;
  std::vector<std::byte> opaque{kOpaqueBytes, std::byte{0xa7}};
  std::vector<std::byte> entry_bytes;
  std::optional<ic::GltfFileAsset> source;
  std::optional<ic::GltfTopologyOutputInspection> inspection;
  ic::GltfMaterializationResult materialization;
  explicit Fixture(bool changed = true, bool build_pipeline = true,
                   ic::GltfFileFormat format = ic::GltfFileFormat::kGltfJson,
                   std::uint64_t budget = kRequestBytes)
      : context({kWorkerCount, 1, budget}) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-verified-provider-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    Require(fs::create_directory(root), "isolated publication parent");
    input = root / "input";
    Require(fs::create_directory(input), "isolated source root");
    entry_bytes =
        format == ic::GltfFileFormat::kGlb
            ? LiteralGlb()
            : std::vector<std::byte>(Bytes(kJson).begin(), Bytes(kJson).end());
    Write(input / kEntry, entry_bytes);
    Write(input / "a.bin", kBinary);
    for (const auto path : kOpaquePaths) Write(input / path, opaque);
    auto read = ic::ReadGltfFileAsset(
        input / kEntry,
        {{},
         ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
         kOpaquePaths},
        {FileLimits(),
         ReadLimits(),
         {kPhysicalBytes, kCountLimit, kRequestBytes}},
        {Work(true).files, ReadWork(true), Work(true).container}, context);
    Require(read.asset.has_value(), "real source files admitted");
    source = *read.asset;
    if (build_pipeline) Prepare(changed);
  }
  void Prepare(bool changed = true, bool require_materialization = true) {
    auto extracted = ic::ExtractGltfPrimitiveCapped(
        source->Asset(), *source->Asset().Primitive(0),
        {ic::GltfTriangleRequest::kRequireTriangles,
         ic::GltfValueRequest::kRaw},
        {kCountLimit, kCountLimit, kRequestBytes});
    Require(extracted.extraction &&
                std::ranges::equal(extracted.extraction->TriangleIndices(),
                                   kSourceTriangles),
            "literal source topology before writer invocation");
    materialization = ic::MaterializeGltfPrimitive(
        *extracted.extraction, TransferLimits(), GeometryWork());
    if (!require_materialization && !materialization.primitive) return;
    Require(materialization.primitive.has_value(),
            "actual qualified nonempty materialization");
    const auto faces = changed ? std::span<const geo::index_t>(kChangedFaces)
                               : std::span<const geo::index_t>(kIdentityFaces);
    const auto corners = changed
                             ? std::span<const geo::index_t>(kChangedCorners)
                             : std::span<const geo::index_t>(kIdentityCorners);
    auto selection =
        geo::SelectMeshFaces(materialization.primitive->Mesh(), faces,
                             TransferLimits().materialization, GeometryWork());
    Require(selection.selection.has_value(), "actual selected candidate");
    auto candidate = geo::InspectMaterializedMesh(
        selection.selection->Candidate(), TransferLimits().inspection,
        GeometryWork());
    Require(candidate.inspection && candidate.inspection->IsStorageValid(),
            "actual candidate storage inspection");
    const ic::GltfTopologyWriteLimits writer_limits{
        kPhysicalBytes,
        kCountLimit,
        kDepthLimit,
        kCountLimit,
        kChannelLimit,
        kCountLimit,
        kPhysicalBytes,
        kCountLimit,
        kCountLimit,
        kPhysicalBytes,
        {kPhysicalBytes, kCountLimit, kPhysicalBytes, kPhysicalBytes,
         kRequestBytes}};
    auto written = ic::WriteGltfTopologySelection(
        {*materialization.primitive, *candidate.inspection, faces, corners},
        writer_limits, {1, kPartitionBytes, kCheckpointBytes, kJobLimit});
    Require(written.bundle.has_value(),
            "writer produces publication input only");
    std::vector<ic::SuppliedGltfResource> bindings;
    for (const auto& resource : written.bundle->Resources())
      if (resource.origin == ic::GltfResourceOrigin::kCallerBinding)
        bindings.push_back(
            {resource.kind, resource.table_index, resource.bytes});
    auto reloaded =
        ic::ReadGltfAssetCapped(written.bundle->Json(), bindings, ReadLimits(),
                                ReadWork(true), context);
    Require(reloaded.asset.has_value(), "actual in-memory output reload");
    auto output_extraction = ic::ExtractGltfPrimitiveCapped(
        *reloaded.asset, *reloaded.asset->Primitive(0),
        {ic::GltfTriangleRequest::kRequireTriangles,
         ic::GltfValueRequest::kRaw},
        {kCountLimit, kCountLimit, kRequestBytes});
    Require(output_extraction.extraction &&
                std::ranges::equal(
                    output_extraction.extraction->TriangleIndices(),
                    changed ? std::span<const std::uint32_t>(kChangedTriangles)
                            : std::span<const std::uint32_t>(kSourceTriangles)),
            "literal output topology independently decoded");
    auto verified = ic::InspectGltfTopologyOutput(
        {*materialization.primitive, *candidate.inspection, faces, corners,
         *written.bundle, *reloaded.asset, *output_extraction.extraction},
        InspectionLimits(), {1, kPartitionBytes, kCheckpointBytes, kJobLimit});
    Require(verified.inspection.has_value(),
            "actual independent output inspection");
    inspection = *verified.inspection;
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ~Fixture() noexcept = default;
  const fs::path& SourceRoot() const noexcept { return input; }
  std::string_view EntryPath() const noexcept { return kEntry; }
  fs::path Destination(std::string_view name = "published-a") const {
    return root / name;
  }
};

}  // namespace meshvale::interchange::test_provider
#endif  // MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PROVIDER_FIXTURE_H_
