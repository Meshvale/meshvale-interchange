// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace fs = std::filesystem;
constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
constexpr std::uint64_t kRequestLimit = 64 * kPhysicalLimit;
constexpr std::uint64_t kDepthLimit = 64;
constexpr std::size_t kWorkers = 4;
constexpr std::uint64_t kPartitionBytes = 64;
constexpr std::uint64_t kCheckpointBytes = 16;
constexpr std::uint64_t kMaxJobs = 64;
// Independent GLB 2 wire values, not the production writer's constants.
constexpr std::uint32_t kMagic = 0x46546c67;
constexpr std::uint32_t kVersion = 2;
constexpr std::uint32_t kJsonType = 0x4e4f534a;
constexpr std::uint32_t kBinType = 0x004e4942;
constexpr std::uint32_t kOpaqueType = 0x12345678;
constexpr std::uint32_t kOtherOpaqueType = 0xdecafbad;
constexpr std::size_t kAlignmentBytes = 4;
constexpr std::size_t kWordBytes = 4;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xff;
constexpr std::size_t kContainerHeaderBytes = 12;
constexpr std::size_t kChunkHeaderBytes = 8;
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kLengthOffset = 8;
constexpr std::size_t kPositionBytes = 36;
constexpr std::size_t kIndexBytes = 6;
constexpr std::size_t kPngBytes = 8;
constexpr std::size_t kLogicalBinBytes = 50;
constexpr std::size_t kFullBinBytes = 52;
constexpr std::size_t kExternalBytes = 16;
constexpr std::size_t kExternalLogicalBytes = 8;
constexpr std::size_t kImageViewIndex = 2;
constexpr std::size_t kResourceCount = 6;
constexpr std::size_t kFileCount = 4;
constexpr std::size_t kChunkCount = 4;
constexpr std::size_t kBindingCount = 4;
constexpr std::size_t kFirstOpaqueChunkOrdinal = 2;
constexpr std::size_t kTwoChunkCount = 2;
constexpr std::size_t kExternalOnlyFileCount = 2;
constexpr std::size_t kLastExternalBufferOrdinal = 2;
constexpr std::uint64_t kTriangleMode = 4;
constexpr std::string_view kEntry = "scene.gltf";
constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":50},{"uri":"%61.bin","byteLength":8},{"uri":"a.bin","byteLength":8}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6},{"buffer":0,"byteOffset":42,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"images":[{"uri":"texture.png"},{"uri":"data:image/png;base64,iVBORw0KGgo="},{"bufferView":2,"mimeType":"image/png"}],"extras":{"literal":1.2300e+04}})";
constexpr std::string_view kBufferZero = R"({"byteLength":50})";
constexpr std::array<std::uint32_t, 9> kPositionBits{
    0x80000000, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0};
constexpr std::array<std::byte, kPngBytes> kPng{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
    std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
constexpr std::array<std::uint32_t, 3> kTriangles{0, 1, 2};

void Require(bool condition, std::string_view message,
             std::source_location where = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
void Word(std::vector<std::byte>& bytes, std::uint32_t word) {
  for (unsigned i = 0; i < kWordBytes; ++i)
    bytes.push_back(
        static_cast<std::byte>((word >> (i * kByteBits)) & kByteMask));
}
void SetWord(std::vector<std::byte>& bytes, std::size_t offset,
             std::uint32_t word) {
  Require(offset + kWordBytes <= bytes.size(), "literal word edit extent");
  for (unsigned i = 0; i < kWordBytes; ++i)
    bytes[offset + i] =
        static_cast<std::byte>((word >> (i * kByteBits)) & kByteMask);
}
void Replace(std::string& text, std::string_view before,
             std::string_view after) {
  const auto offset = text.find(before);
  Require(offset != std::string::npos, "literal JSON edit exists");
  text.replace(offset, before.size(), after);
}
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(output.is_open(), "literal file opened");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  Require(!output.fail(), "literal file write completed");
}
struct Chunk {
  std::uint32_t type;
  std::vector<std::byte> bytes;
};
// A fixture-only little-endian encoder. Expected logical coupling and resource
// contents below are independent literals; no production container helper runs.
std::vector<std::byte> Assemble(std::span<const Chunk> chunks) {
  std::vector<std::byte> wire;
  Word(wire, kMagic);
  Word(wire, kVersion);
  Word(wire, 0);
  for (const auto& chunk : chunks) {
    Require(chunk.bytes.size() % kAlignmentBytes == 0,
            "authored aligned chunk payload");
    Word(wire, static_cast<std::uint32_t>(chunk.bytes.size()));
    Word(wire, chunk.type);
    wire.insert(wire.end(), chunk.bytes.begin(), chunk.bytes.end());
  }
  SetWord(wire, kLengthOffset, static_cast<std::uint32_t>(wire.size()));
  return wire;
}
struct Fixture {
  fs::path root;
  std::string json = std::string(kJson) + "\n\t   ";
  std::vector<std::byte> json_chunk;
  std::vector<std::byte> bin;
  std::vector<std::byte> external{kExternalBytes, std::byte{0xa7}};
  std::vector<std::byte> image;
  std::vector<std::byte> opaque{kPngBytes, std::byte{0x5b}};
  std::vector<Chunk> unknown{
      {kOpaqueType,
       {std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}}},
      {kOtherOpaqueType,
       {std::byte{'a'}, std::byte{'.'}, std::byte{'b'}, std::byte{'i'},
        std::byte{'n'}, std::byte{0}, std::byte{0}, std::byte{0}}}};
  std::vector<std::byte> wire;
  bool include_bin = true;
  bool include_opaque_file = true;
  std::array<std::string_view, 1> inventory{"opaque.bin"};
  Fixture() {
    static std::atomic<std::uint64_t> sequence = 0;
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-glb-files-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    Require(fs::create_directory(root), "isolated literal GLB directory");
    for (auto bits : kPositionBits) Word(bin, bits);
    constexpr std::array<std::byte, kIndexBytes> kIndexWire{
        std::byte{0}, std::byte{0}, std::byte{1},
        std::byte{0}, std::byte{2}, std::byte{0}};
    bin.insert(bin.end(), kIndexWire.begin(), kIndexWire.end());
    bin.insert(bin.end(), kPng.begin(), kPng.end());
    bin.resize(kFullBinBytes, std::byte{0});
    image.assign(kPng.begin(), kPng.end());
    constexpr std::array<std::byte, kPngBytes> kImageTail{
        std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
        std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88}};
    image.insert(image.end(), kImageTail.begin(), kImageTail.end());
    Save();
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ~Fixture() noexcept {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  void Build() {
    json_chunk.assign(Bytes(json).begin(), Bytes(json).end());
    while (json_chunk.size() % kAlignmentBytes != 0)
      json_chunk.push_back(std::byte{' '});
    std::vector<Chunk> chunks{{kJsonType, json_chunk}};
    if (include_bin) chunks.push_back({kBinType, bin});
    chunks.insert(chunks.end(), unknown.begin(), unknown.end());
    wire = Assemble(chunks);
  }
  void Save() {
    Build();
    Write(root / kEntry, wire);
    Write(root / "a.bin", external);
    Write(root / "texture.png", image);
    Write(root / "opaque.bin", opaque);
  }
  std::size_t BinOffset() const {
    return kContainerHeaderBytes + kChunkHeaderBytes + json_chunk.size() +
           kChunkHeaderBytes;
  }
  ic::GltfFileReadOptions Options() const {
    if (!include_opaque_file) return {};
    return {
        {}, ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory, inventory};
  }
};
geo::ExecutionContext Context() {
  return geo::ExecutionContext({kWorkers, 1, kRequestLimit});
}
ic::GltfFileReadLimits Limits() {
  return {{kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
           kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kRequestLimit},
          {{kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
            kDepthLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
            kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
            kPhysicalLimit, kRequestLimit},
           kRequestLimit},
          {kPhysicalLimit, kPhysicalLimit, kRequestLimit}};
}
ic::GltfFileReadWorkPolicy Work(bool serial = false) {
  const auto threshold = serial
                             ? static_cast<std::uint64_t>(
                                   std::numeric_limits<std::ptrdiff_t>::max())
                             : 1;
  return {
      {kPartitionBytes, threshold, kPartitionBytes, kCheckpointBytes, kMaxJobs},
      {threshold, kPartitionBytes, threshold, kPartitionBytes},
      {threshold, kPartitionBytes}};
}
ic::GltfFileReadResult Read(const Fixture& fixture,
                            const geo::ExecutionContext& context,
                            const ic::GltfFileReadLimits& limits = Limits(),
                            const ic::GltfFileReadWorkPolicy& work = Work()) {
  return ic::ReadGltfFileAsset(fixture.root / kEntry, fixture.Options(), limits,
                               work, context);
}
void Empty(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "all GLB/file/reader owners and workers released");
}
void Success(const ic::GltfFileReadResult& result) {
  Require(
      result.asset.has_value() &&
          result.diagnostic.status == ic::GltfFileStatus::kSuccess &&
          result.diagnostic.stage == ic::GltfFileStage::kComplete &&
          result.diagnostic.code == ic::GltfFileDiagnosticCode::kNone,
      "GLB capture status=" +
          std::to_string(static_cast<int>(result.diagnostic.status)) +
          " code=" + std::to_string(static_cast<int>(result.diagnostic.code)) +
          " stage=" +
          std::to_string(static_cast<int>(result.diagnostic.stage)));
}
void Failure(const ic::GltfFileReadResult& result, ic::GltfFileStatus status,
             ic::GltfFileStage stage, ic::GltfFileDiagnosticCode code,
             std::source_location where = std::source_location::current()) {
  Require(
      !result.asset && result.diagnostic.status == status &&
          result.diagnostic.stage == stage && result.diagnostic.code == code,
      "typed GLB failure expected=" + std::to_string(static_cast<int>(status)) +
          "/" + std::to_string(static_cast<int>(stage)) + "/" +
          std::to_string(static_cast<int>(code)) + " actual=" +
          std::to_string(static_cast<int>(result.diagnostic.status)) + "/" +
          std::to_string(static_cast<int>(result.diagnostic.stage)) + "/" +
          std::to_string(static_cast<int>(result.diagnostic.code)),
      where);
}
void ContainerFailure(const ic::GltfFileReadResult& result,
                      ic::GltfFileStatus status, ic::GlbStatus delegated,
                      std::uint64_t offset) {
  Failure(result, status, ic::GltfFileStage::kContainer,
          ic::GltfFileDiagnosticCode::kDelegatedContainer);
  Require(result.diagnostic.container_status == delegated &&
              result.diagnostic.container_byte_offset == offset &&
              !result.diagnostic.gltf_diagnostic,
          "actual delegated container outcome and source offset");
}
void BindingFailure(
    const ic::GltfFileReadResult& result, ic::GltfFileStatus status,
    ic::GltfFileDiagnosticCode code, std::optional<std::uint64_t> index,
    std::optional<std::uint64_t> offset = {},
    std::source_location where = std::source_location::current()) {
  Failure(result, status, ic::GltfFileStage::kContainer, code, where);
  Require(result.diagnostic.resource_index == index &&
              result.diagnostic.resource_kind ==
                  (index ? std::optional(ic::GltfResourceKind::kBuffer)
                         : std::nullopt) &&
              result.diagnostic.byte_offset == offset &&
              !result.diagnostic.gltf_diagnostic &&
              !result.diagnostic.container_status,
          "independent BIN binding/padding identity and wire offset");
}
void Check(const ic::GltfFileAsset& asset, const Fixture& fixture) {
  Require(
      asset.Format() == ic::GltfFileFormat::kGlb && asset.Container() &&
          asset.EntryPath() == kEntry &&
          std::ranges::equal(asset.EntryBytes(), fixture.wire) &&
          std::ranges::equal(asset.Container()->Bytes(), fixture.wire) &&
          std::ranges::equal(asset.Asset().OriginalJson(), fixture.json_chunk),
      "complete original entry/container/JSON authored whitespace");
  Require(asset.Container()->ChunkCount() == kChunkCount,
          "JSON/BIN/two distinct ordered opaque chunks");
  Require(
      asset.Container()->Chunk(0).type == kJsonType &&
          std::ranges::equal(asset.Container()->Chunk(0).bytes,
                             fixture.json_chunk) &&
          asset.Container()->Chunk(1).type == kBinType &&
          std::ranges::equal(asset.Container()->Chunk(1).bytes, fixture.bin),
      "full independently authored JSON and BIN padding");
  for (std::size_t i = 0; i < fixture.unknown.size(); ++i)
    Require(
        asset.Container()->Chunk(i + kFirstOpaqueChunkOrdinal).type ==
                fixture.unknown[i].type &&
            std::ranges::equal(
                asset.Container()->Chunk(i + kFirstOpaqueChunkOrdinal).bytes,
                fixture.unknown[i].bytes),
        "unknown types/order/full bytes preserved without path discovery");
  constexpr std::array<std::string_view, kFileCount> kPaths{
      "a.bin", "opaque.bin", "scene.gltf", "texture.png"};
  const std::array<std::span<const std::byte>, kFileCount> contents{
      fixture.external, fixture.opaque, fixture.wire, fixture.image};
  Require(asset.Files().size() == kFileCount, "BIN creates no physical file");
  for (std::size_t i = 0; i < kPaths.size(); ++i)
    Require(asset.Files()[i].path == kPaths[i] &&
                std::ranges::equal(asset.Files()[i].bytes, contents[i]),
            "unique sorted physical files/full tails with shared URI paths");
  const auto resources = asset.Asset().Resources();
  Require(resources.size() == kResourceCount,
          "three buffers/three image roles");
  Require(resources[0].kind == ic::GltfResourceKind::kBuffer &&
              resources[0].table_index == 0 &&
              resources[0].origin == ic::GltfResourceOrigin::kCallerBinding &&
              resources[0].authored_uri.empty() &&
              resources[0].logical_bytes == kLogicalBinBytes &&
              std::ranges::equal(resources[0].bytes, fixture.bin),
          "buffer zero owns full BIN including admitted two zero bytes");
  for (std::size_t i = 1; i <= kLastExternalBufferOrdinal; ++i)
    Require(resources[i].kind == ic::GltfResourceKind::kBuffer &&
                resources[i].table_index == i &&
                resources[i].origin == ic::GltfResourceOrigin::kCallerBinding &&
                resources[i].authored_uri == (i == 1 ? "%61.bin" : "a.bin") &&
                resources[i].logical_bytes == kExternalLogicalBytes &&
                std::ranges::equal(resources[i].bytes, fixture.external),
            "external URI aliases retain independent buffer identities/tails");
  constexpr std::size_t kExternalImageOrdinal = 3;
  constexpr std::size_t kDataImageOrdinal = 4;
  constexpr std::size_t kAliasImageOrdinal = 5;
  Require(
      resources[kExternalImageOrdinal].kind == ic::GltfResourceKind::kImage &&
          resources[kExternalImageOrdinal].table_index == 0 &&
          std::ranges::equal(resources[kExternalImageOrdinal].bytes,
                             fixture.image) &&
          resources[kDataImageOrdinal].table_index == 1 &&
          resources[kDataImageOrdinal].origin ==
              ic::GltfResourceOrigin::kDecodedDataUri &&
          std::ranges::equal(resources[kDataImageOrdinal].bytes, kPng) &&
          resources[kAliasImageOrdinal].table_index == kImageViewIndex &&
          resources[kAliasImageOrdinal].origin ==
              ic::GltfResourceOrigin::kBufferView &&
          resources[kAliasImageOrdinal].buffer_index == 0 &&
          resources[kAliasImageOrdinal].buffer_view_index == kImageViewIndex &&
          resources[kAliasImageOrdinal].bytes.data() ==
              resources[0].bytes.data() + kPositionBytes + kIndexBytes &&
          std::ranges::equal(resources[kAliasImageOrdinal].bytes, kPng),
      "external/inline/BIN image representations and actual buffer alias");
}
void CheckExtraction(const ic::GltfAsset& asset) {
  const auto primitive = asset.Primitive(0);
  Require(primitive.has_value(), "literal BIN-backed primitive");
  const auto result = ic::ExtractGltfPrimitiveCapped(
      asset, *primitive,
      {ic::GltfTriangleRequest::kRequireTriangles, ic::GltfValueRequest::kRaw},
      {kPhysicalLimit, kPhysicalLimit, kRequestLimit});
  Require(result.extraction &&
              result.diagnostic.status == ic::GltfStatus::kAccepted,
          "actual canonical BIN-backed extraction");
  Require(
      result.extraction->SourceMode() == kTriangleMode &&
          std::ranges::equal(result.extraction->TriangleIndices(), kTriangles),
      "implicit TRIANGLES and independently authored UInt16 indices");
  const auto channels = result.extraction->Channels();
  Require(channels.size() == 1 && channels[0].semantic == "POSITION" &&
              channels[0].base_buffer_view == 0,
          "literal POSITION channel provenance");
  const auto values = std::get<std::span<const float>>(channels[0].raw);
  Require(values.size() == kPositionBits.size(),
          "physical nine POSITION scalars");
  for (std::size_t i = 0; i < values.size(); ++i)
    Require(std::bit_cast<std::uint32_t>(values[i]) == kPositionBits[i],
            "binary32 raw bits preserve negative zero");
  const auto corners = result.extraction->SourceCorners();
  Require(corners.size() == kTriangles.size(), "three literal source corners");
  for (std::size_t i = 0; i < corners.size(); ++i)
    Require(
        corners[i].primitive_stream_corner == i && corners[i].accessor_row == i,
        "unchanged literal index/corner correspondence");
}
void CaptureAndLifetime() {
  Fixture fixture;
  auto context = Context();
  std::optional<ic::GltfFileAsset> retained;
  std::optional<ic::GltfAsset> delegated;
  const ic::GlbContainer* container = nullptr;
  {
    const auto temporary_context = context;
    auto result = Read(fixture, temporary_context);
    Success(result);
    Check(*result.asset, fixture);
    CheckExtraction(result.asset->Asset());
    Require(result.execution.capture.launched_workers > 0 &&
                result.execution.capture.launched_workers ==
                    result.execution.capture.joined_workers,
            "actual native file capture jobs join before GLB success");
    const auto before = context.ActiveTrackedPayload();
    retained = *result.asset;
    delegated = result.asset->Asset();
    container = result.asset->Container();
    Require(context.ActiveTrackedPayload() == before &&
                retained->Container() == container &&
                before > retained->NewRequestedBytes() +
                             *delegated->NewRequestedBytes(),
            "allocation-free file copies retain separate GLB ledger scope");
  }
  for (const auto& path :
       {kEntry, std::string_view("a.bin"), std::string_view("texture.png"),
        std::string_view("opaque.bin")})
    Require(fs::remove(fixture.root / path),
            "actual captured source file removed");
  Check(*retained, fixture);
  Require(retained->Container() == container,
          "container stable through file copy");
  CheckExtraction(*delegated);
  retained.reset();
  Require(
      context.ActiveTrackedPayload() == *delegated->NewRequestedBytes() &&
          std::ranges::equal(delegated->OriginalJson(), fixture.json_chunk) &&
          std::ranges::equal(delegated->Resources()[0].bytes, fixture.bin),
      "asset-only survivor releases file/container scopes, retains real BIN");
  CheckExtraction(*delegated);
  delegated.reset();
  Empty(context);
}
void PaddingAndJsonOnly() {
  Fixture fixture;
  auto context = Context();
  fixture.include_opaque_file = false;
  fixture.unknown.clear();
  fixture.bin.assign(kAlignmentBytes, std::byte{0});
  for (std::size_t padding = 0; padding < kAlignmentBytes; ++padding) {
    const auto logical = kAlignmentBytes - padding;
    fixture.json =
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" +
        std::to_string(logical) + "}]}\n\t    ";
    fixture.Save();
    {
      const auto result = Read(fixture, context);
      Success(result);
      Require(
          result.asset->Files().size() == 1 &&
              result.asset->Container()->ChunkCount() == kTwoChunkCount &&
              std::ranges::equal(result.asset->Asset().OriginalJson(),
                                 fixture.json_chunk) &&
              result.asset->Asset().Resources()[0].logical_bytes == logical &&
              std::ranges::equal(result.asset->Asset().Resources()[0].bytes,
                                 fixture.bin),
          "independent zero/one/two/three BIN zeros and full authored JSON");
    }
    Empty(context);
  }
  fixture.include_bin = false;
  fixture.json =
      R"({"asset":{"version":"2.0"},"buffers":[{"uri":"%61.bin","byteLength":8},{"uri":"data:application/octet-stream;base64,AAAAAAAAAAA=","byteLength":8}]})";
  fixture.unknown = {
      {kOpaqueType,
       {std::byte{'B'}, std::byte{'I'}, std::byte{'N'}, std::byte{0}}}};
  fixture.Save();
  {
    const auto result = Read(fixture, context);
    Success(result);
    Require(result.asset->Files().size() == kExternalOnlyFileCount &&
                result.asset->Container()->ChunkCount() == kTwoChunkCount &&
                result.asset->Container()->Chunk(1).type == kOpaqueType &&
                std::ranges::equal(result.asset->Container()->Chunk(1).bytes,
                                   fixture.unknown[0].bytes) &&
                std::ranges::equal(result.asset->Asset().Resources()[0].bytes,
                                   fixture.external) &&
                result.asset->Asset().Resources()[1].origin ==
                    ic::GltfResourceOrigin::kDecodedDataUri &&
                std::ranges::equal(result.asset->Asset().Resources()[1].bytes,
                                   std::array<std::byte, kPngBytes>{}),
            "JSON-only GLB external/inline buffers and opaque non-BIN chunk");
  }
  Empty(context);
  fixture.json =
      R"({"asset":{"version":"2.0"},"extras":{"path":"absent.bin"}})";
  fixture.Save();
  {
    const auto result = Read(fixture, context);
    Success(result);
    Require(
        result.asset->Files().size() == 1 &&
            result.asset->Asset().Resources().empty(),
        "opaque chunk and JSON extras do not invent filesystem dependencies");
  }
  Empty(context);
}
void BindingAndPaddingFailures() {
  Fixture fixture;
  auto context = Context();
  fixture.include_bin = false;
  fixture.Save();
  BindingFailure(Read(fixture, context), ic::GltfFileStatus::kMalformed,
                 ic::GltfFileDiagnosticCode::kContainerBinding, 0);
  Empty(context);
  fixture.include_bin = true;
  Replace(fixture.json, kBufferZero, R"({"byteLength":53})");
  fixture.Save();
  BindingFailure(Read(fixture, context), ic::GltfFileStatus::kMalformed,
                 ic::GltfFileDiagnosticCode::kContainerBinding, 0,
                 fixture.BinOffset() + kFullBinBytes);
  Empty(context);
  // Keep metadata valid independently: the rich image view ends at byte 50.
  // This smaller logical buffer has no accessor/image view to reject earlier.
  fixture.json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":48}]})";
  fixture.Save();
  constexpr std::size_t kFirstExcessSuffix = 51;
  BindingFailure(Read(fixture, context), ic::GltfFileStatus::kMalformed,
                 ic::GltfFileDiagnosticCode::kContainerPadding, 0,
                 fixture.BinOffset() + kFirstExcessSuffix);
  Empty(context);
  fixture.json = std::string(kJson);
  fixture.bin[kLogicalBinBytes] = std::byte{0x91};
  fixture.bin[kLogicalBinBytes + 1] = std::byte{0x92};
  fixture.Save();
  BindingFailure(Read(fixture, context), ic::GltfFileStatus::kMalformed,
                 ic::GltfFileDiagnosticCode::kContainerPadding, 0,
                 fixture.BinOffset() + kLogicalBinBytes);
  Empty(context);
  fixture.bin[kLogicalBinBytes] = std::byte{0};
  fixture.bin[kLogicalBinBytes + 1] = std::byte{0};
  Replace(fixture.json, kBufferZero, R"({"uri":"a.bin","byteLength":50})");
  fixture.Save();
  BindingFailure(Read(fixture, context),
                 ic::GltfFileStatus::kUnsupportedProfile,
                 ic::GltfFileDiagnosticCode::kContainerBinding, 0);
  Empty(context);
  fixture.json = std::string(kJson);
  Replace(fixture.json, R"({"uri":"%61.bin","byteLength":8})",
          R"({"byteLength":8})");
  fixture.Save();
  BindingFailure(Read(fixture, context),
                 ic::GltfFileStatus::kUnsupportedProfile,
                 ic::GltfFileDiagnosticCode::kContainerBinding, 1);
  Empty(context);
  fixture.json = R"({"asset":{"version":"2.0"}})";
  fixture.Save();
  BindingFailure(Read(fixture, context),
                 ic::GltfFileStatus::kUnsupportedProfile,
                 ic::GltfFileDiagnosticCode::kContainerBinding, {});
  Empty(context);
}
void DelegatedFailures() {
  Fixture fixture;
  auto context = Context();
  auto wire = fixture.wire;
  SetWord(wire, kVersionOffset, kVersion + 1);
  Write(fixture.root / kEntry, wire);
  ContainerFailure(Read(fixture, context),
                   ic::GltfFileStatus::kUnsupportedProfile,
                   ic::GlbStatus::kUnsupportedVersion, kVersionOffset);
  Empty(context);
  wire = fixture.wire;
  wire.pop_back();
  Write(fixture.root / kEntry, wire);
  ContainerFailure(Read(fixture, context), ic::GltfFileStatus::kMalformed,
                   ic::GlbStatus::kMalformed, kLengthOffset);
  Empty(context);
  wire = fixture.wire;
  SetWord(wire, kContainerHeaderBytes + kWordBytes, kOpaqueType);
  Write(fixture.root / kEntry, wire);
  ContainerFailure(Read(fixture, context), ic::GltfFileStatus::kMalformed,
                   ic::GlbStatus::kMalformed, kContainerHeaderBytes);
  Empty(context);
  const std::array misplaced{Chunk{kJsonType, fixture.json_chunk},
                             fixture.unknown[0], Chunk{kBinType, fixture.bin}};
  Write(fixture.root / kEntry, Assemble(misplaced));
  ContainerFailure(Read(fixture, context), ic::GltfFileStatus::kMalformed,
                   ic::GlbStatus::kMalformed,
                   kContainerHeaderBytes + kChunkHeaderBytes +
                       fixture.json_chunk.size() + kChunkHeaderBytes +
                       fixture.unknown[0].bytes.size());
  Empty(context);
  fixture.json = R"({"asset":})";
  fixture.Save();
  auto result = Read(fixture, context);
  Failure(result, ic::GltfFileStatus::kMalformed, ic::GltfFileStage::kAssetRead,
          ic::GltfFileDiagnosticCode::kDelegatedRead);
  Require(result.diagnostic.gltf_diagnostic &&
              result.diagnostic.gltf_diagnostic->status ==
                  ic::GltfStatus::kMalformed &&
              result.diagnostic.gltf_diagnostic->code ==
                  ic::GltfDiagnosticCode::kJsonSyntax &&
              !result.diagnostic.container_status,
          "actual invalid JSON after successful wire admission");
  Empty(context);
  fixture.json = std::string(kJson);
  Replace(fixture.json, kBufferZero, R"({"uri":null,"byteLength":50})");
  fixture.include_bin = false;
  fixture.Save();
  result = Read(fixture, context);
  Failure(result, ic::GltfFileStatus::kMalformed, ic::GltfFileStage::kAssetRead,
          ic::GltfFileDiagnosticCode::kDelegatedRead);
  Require(result.diagnostic.gltf_diagnostic &&
              result.diagnostic.gltf_diagnostic->code ==
                  ic::GltfDiagnosticCode::kCoreFieldType,
          "malformed null URI precedes provider missing-BIN diagnosis");
  Empty(context);
}
void InclusiveLimits() {
  Fixture fixture;
  auto context = Context();
  auto limits = Limits();
  limits.files.max_files = kFileCount;
  limits.files.max_resource_bindings = kBindingCount;
  limits.files.max_file_bytes = fixture.wire.size();
  limits.files.max_total_file_bytes =
      fixture.wire.size() + fixture.external.size() + fixture.image.size() +
      fixture.opaque.size();
  constexpr std::uint64_t kLongestPathBytes = 11;
  constexpr std::uint64_t kTotalPathBytes = 36;
  constexpr std::uint64_t kOwnedResourceBytes = 108;
  limits.files.max_path_bytes = kLongestPathBytes;
  limits.files.max_total_path_bytes = kTotalPathBytes;
  limits.container.max_container_bytes = fixture.wire.size();
  limits.container.max_chunks = kChunkCount;
  limits.asset.source.max_json_bytes = fixture.json_chunk.size();
  limits.asset.source.max_resource_bytes = kFullBinBytes;
  limits.asset.source.max_owned_source_resource_bytes =
      fixture.json_chunk.size() + kOwnedResourceBytes;
  {
    const auto result = Read(fixture, context, limits);
    Success(result);
    Check(*result.asset, fixture);
  }
  Empty(context);
  constexpr std::array kFileMembers{&ic::GltfFileLimits::max_files,
                                    &ic::GltfFileLimits::max_resource_bindings,
                                    &ic::GltfFileLimits::max_file_bytes,
                                    &ic::GltfFileLimits::max_total_file_bytes,
                                    &ic::GltfFileLimits::max_path_bytes,
                                    &ic::GltfFileLimits::max_total_path_bytes};
  for (auto member : kFileMembers) {
    auto tight = limits;
    --(tight.files.*member);
    const auto result = Read(fixture, context, tight);
    Require(
        !result.asset &&
            result.diagnostic.status == ic::GltfFileStatus::kLimitExceeded &&
            !result.diagnostic.gltf_diagnostic &&
            !result.diagnostic.container_status,
        "one-below independent physical file/path/BIN-binding inventory");
    Empty(context);
  }
  for (bool chunk_limit : {false, true}) {
    auto tight = limits;
    if (chunk_limit)
      --tight.container.max_chunks;
    else
      --tight.container.max_container_bytes;
    ContainerFailure(Read(fixture, context, tight),
                     ic::GltfFileStatus::kLimitExceeded,
                     ic::GlbStatus::kLimitExceeded, 0);
    Empty(context);
  }
  constexpr std::array kReaderMembers{
      &ic::GltfReadLimits::max_json_bytes,
      &ic::GltfReadLimits::max_resource_bytes,
      &ic::GltfReadLimits::max_owned_source_resource_bytes};
  for (auto member : kReaderMembers) {
    auto tight = limits;
    --(tight.asset.source.*member);
    const auto result = Read(fixture, context, tight);
    Failure(result, ic::GltfFileStatus::kUnsupportedProfile,
            ic::GltfFileStage::kAssetRead,
            ic::GltfFileDiagnosticCode::kDelegatedRead);
    Require(result.diagnostic.gltf_diagnostic &&
                result.diagnostic.gltf_diagnostic->status ==
                    ic::GltfStatus::kUnsupportedProfile &&
                result.diagnostic.gltf_diagnostic->code ==
                    ic::GltfDiagnosticCode::kLimitExceeded,
            "full padded JSON/BIN/tails/decoded resource source counts");
    Empty(context);
  }
  auto tight = limits;
  tight.container.max_requested_bytes = 1;
  ContainerFailure(Read(fixture, context, tight),
                   ic::GltfFileStatus::kLimitExceeded,
                   ic::GlbStatus::kLimitExceeded, 0);
  Empty(context);
  tight = limits;
  tight.asset.max_new_requested_bytes = 1;
  const auto result = Read(fixture, context, tight);
  Failure(result, ic::GltfFileStatus::kUnsupportedProfile,
          ic::GltfFileStage::kAssetRead,
          ic::GltfFileDiagnosticCode::kDelegatedRead);
  Require(result.diagnostic.gltf_diagnostic &&
              result.diagnostic.gltf_diagnostic->code ==
                  ic::GltfDiagnosticCode::kLimitExceeded,
          "reader fresh request cap remains separate from file/container caps");
  Empty(context);
}
void SerialAgreement() {
  Fixture fixture;
  auto context = Context();
  {
    const auto result = Read(fixture, context, Limits(), Work(true));
    Success(result);
    Check(*result.asset, fixture);
    CheckExtraction(result.asset->Asset());
    Require(result.execution.capture.launched_workers == 0 &&
                result.execution.capture.joined_workers == 0,
            "explicit serial route agrees with actual native capture");
  }
  Empty(context);
}
}  // namespace

int main() {
  try {
    CaptureAndLifetime();
    PaddingAndJsonOnly();
    BindingAndPaddingFailures();
    DelegatedFailures();
    InclusiveLimits();
    SerialAgreement();
    std::cout << "independent literal GLB file capture semantics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
