// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_files.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {
namespace mv = meshvale::interchange;
namespace fs = std::filesystem;
constexpr std::uint64_t kExtent = 65536;
constexpr std::uint64_t kRequests = 1048576;
constexpr std::uint64_t kDepth = 32;
constexpr std::uint64_t kPartition = 4096;
constexpr std::uint64_t kCheckpoint = 64;
constexpr std::uint64_t kJobs = 64;
constexpr std::string_view kEntry = "scene.gltf";
constexpr std::string_view kDocument =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"positions.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
constexpr std::size_t kPhysicalBufferBytes = 40;
constexpr std::size_t kTailStart = 36;
constexpr std::byte kTailByte{0xa7};
constexpr std::uint64_t kPositionComponents = 9;
constexpr std::uint64_t kTriangleCorners = 3;
constexpr std::uint64_t kIndexedComponents =
    kPositionComponents + kTriangleCorners;
// Independent GLB fixture: authored JSON whitespace plus three alignment
// spaces, one logical 42-byte BIN with two zero padding bytes, and an opaque
// chunk.
constexpr std::string_view kGlbEntry = "content.gltf";
constexpr std::string_view kGlbDocument =
    R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[0,0,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})"
    "\n\t   ";
constexpr std::size_t kWireBytes = 476;
constexpr std::size_t kContainerHeaderBytes = 12;
constexpr std::size_t kAuthoredJsonBytes = 389;
constexpr std::size_t kJsonPayloadOffset = 20;
constexpr std::size_t kJsonPayloadBytes = 392;
constexpr std::size_t kBinHeaderOffset = 412;
constexpr std::size_t kBinPayloadOffset = 420;
constexpr std::size_t kBinPayloadBytes = 44;
constexpr std::size_t kIndexOneByteOffset = 38;
constexpr std::size_t kIndexTwoByteOffset = 40;
constexpr std::size_t kOpaqueHeaderOffset = 464;
constexpr std::size_t kOpaquePayloadOffset = 472;
constexpr std::size_t kWireWordBytes = 4;
constexpr unsigned kWireByteBits = 8;
constexpr std::uint32_t kWireByteMask = 0xff;
constexpr std::uint32_t kFixtureMagic = 0x46546c67;
constexpr std::uint32_t kFixtureVersion = 2;
constexpr std::uint32_t kFixtureJsonType = 0x4e4f534a;
constexpr std::uint32_t kFixtureBinType = 0x004e4942;
constexpr std::uint32_t kFixtureOpaqueType = 0x12345678;
constexpr std::size_t kFixtureChunkCount = 3;
constexpr std::array<std::byte, 4> kOpaquePayload{
    std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};
static_assert(kGlbDocument.size() == kAuthoredJsonBytes);

constexpr void PutWord(std::array<std::byte, kWireBytes>& wire,
                       std::size_t offset, std::uint32_t value) {
  for (std::size_t byte = 0; byte < kWireWordBytes; ++byte)
    wire[offset + byte] = static_cast<std::byte>(
        (value >> (byte * kWireByteBits)) & kWireByteMask);
}
constexpr std::array<std::byte, kWireBytes> GlbWire() {
  std::array<std::byte, kWireBytes> wire{};
  PutWord(wire, 0, kFixtureMagic);
  PutWord(wire, kWireWordBytes, kFixtureVersion);
  PutWord(wire, kWireWordBytes * 2, kWireBytes);
  PutWord(wire, kContainerHeaderBytes, kJsonPayloadBytes);
  PutWord(wire, kContainerHeaderBytes + kWireWordBytes, kFixtureJsonType);
  for (std::size_t byte = 0; byte < kJsonPayloadBytes; ++byte)
    wire[kJsonPayloadOffset + byte] = static_cast<std::byte>(
        byte < kGlbDocument.size() ? kGlbDocument[byte] : ' ');
  PutWord(wire, kBinHeaderOffset, kBinPayloadBytes);
  PutWord(wire, kBinHeaderOffset + kWireWordBytes, kFixtureBinType);
  // Nine zero binary32 components, then UInt16 indices 0, 1, 2 and zero
  // padding.
  wire[kBinPayloadOffset + kIndexOneByteOffset] = std::byte{1};
  wire[kBinPayloadOffset + kIndexTwoByteOffset] = std::byte{2};
  PutWord(wire, kOpaqueHeaderOffset,
          static_cast<std::uint32_t>(kOpaquePayload.size()));
  PutWord(wire, kOpaqueHeaderOffset + kWireWordBytes, kFixtureOpaqueType);
  std::copy(kOpaquePayload.begin(), kOpaquePayload.end(),
            wire.begin() + kOpaquePayloadOffset);
  return wire;
}
constexpr auto kGlbWire = GlbWire();

class Fixture {
 public:
  Fixture() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = fs::canonical(fs::temp_directory_path()) /
            ("meshvale-installed-gltf-capture-" + std::to_string(stamp));
    if (!fs::create_directory(root_))
      throw std::runtime_error("fixture exists");
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ~Fixture() noexcept {
    std::error_code ignored;
    fs::remove_all(root_, ignored);
  }
  const fs::path& Root() const noexcept { return root_; }

 private:
  fs::path root_;
};
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  if (!stream) throw std::runtime_error("fixture write failed");
}
mv::GltfFileReadLimits Limits() {
  return {{kExtent, kExtent, kExtent, kExtent, kExtent, kExtent, kExtent,
           kRequests},
          {{kExtent, kExtent, kExtent, kExtent, kDepth, kExtent, kExtent,
            kExtent, kExtent, kExtent, kExtent, kExtent, kExtent, kRequests},
           kRequests},
          {kExtent, kExtent, kRequests}};
}
}  // namespace

int RunJson() {
  Fixture fixture;
  std::array<std::byte, kPhysicalBufferBytes> binary{};
  std::fill(binary.begin() + kTailStart, binary.end(), kTailByte);
  Write(fixture.Root() / kEntry, std::as_bytes(std::span(kDocument)));
  Write(fixture.Root() / "positions.bin", binary);
  std::optional<mv::GltfFileAsset> retained;
  std::optional<mv::GltfAsset> delegated;
  meshvale::geometry::ExecutionContext ledger;
  {
    auto context = ledger;
    auto read = mv::ReadGltfFileAsset(
        fixture.Root() / kEntry, {}, Limits(),
        {{kPartition, kPartition, kPartition, kCheckpoint, kJobs},
         {kPartition, kPartition, kPartition, kPartition},
         {kPartition, kPartition}},
        context);
    if (!read.asset || read.diagnostic.status != mv::GltfFileStatus::kSuccess ||
        read.asset->Format() != mv::GltfFileFormat::kGltfJson ||
        read.asset->Container() || read.asset->EntryPath() != kEntry ||
        !std::ranges::equal(read.asset->EntryBytes(),
                            std::as_bytes(std::span(kDocument))) ||
        read.asset->Files().size() != 2 ||
        read.asset->Files()[0].path != "positions.bin" ||
        !std::ranges::equal(read.asset->Files()[0].bytes, binary) ||
        read.asset->Files()[1].path != kEntry ||
        read.asset->NewRequestedBytes() == 0 ||
        read.asset->NewRequestedBytes() > kRequests)
      return 1;
    retained = *read.asset;
    delegated = read.asset->Asset();
  }
  if (!fs::remove(fixture.Root() / kEntry) ||
      !fs::remove(fixture.Root() / "positions.bin"))
    return 1;
  constexpr std::array<std::uint32_t, 3> kIndices{0, 1, 2};
  auto extraction = mv::ExtractGltfPrimitiveCapped(
      *delegated, *delegated->Primitive(0),
      {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw},
      {kPositionComponents, kTriangleCorners, kRequests});
  if (!extraction.extraction ||
      !std::ranges::equal(extraction.extraction->TriangleIndices(), kIndices) ||
      !std::ranges::equal(retained->Files()[0].bytes, binary))
    return 1;
  retained.reset();
  delegated.reset();
  if (extraction.extraction->NewRequestedBytes() == 0) return 1;
  extraction.extraction.reset();
  return ledger.ActiveTrackedPayload() == 0 && ledger.ActiveWorkers() == 0 ? 0
                                                                           : 1;
}

int RunGlb() {
  Fixture fixture;
  Write(fixture.Root() / kGlbEntry, kGlbWire);
  std::optional<mv::GltfFileAsset> retained;
  std::optional<mv::GltfAsset> delegated;
  meshvale::geometry::ExecutionContext ledger;
  {
    auto context = ledger;
    auto read = mv::ReadGltfFileAsset(
        fixture.Root() / kGlbEntry, {}, Limits(),
        {{kPartition, kPartition, kPartition, kCheckpoint, kJobs},
         {kPartition, kPartition, kPartition, kPartition},
         {kPartition, kPartition}},
        context);
    if (!read.asset || read.diagnostic.status != mv::GltfFileStatus::kSuccess ||
        read.asset->Format() != mv::GltfFileFormat::kGlb ||
        !read.asset->Container() || read.asset->EntryPath() != kGlbEntry ||
        read.asset->Files().size() != 1 ||
        read.asset->Files()[0].path != kGlbEntry ||
        !std::ranges::equal(read.asset->Files()[0].bytes, kGlbWire) ||
        !std::ranges::equal(read.asset->EntryBytes(), kGlbWire) ||
        read.asset->NewRequestedBytes() == 0 ||
        read.asset->NewRequestedBytes() > kRequests)
      return 1;
    const auto& container = *read.asset->Container();
    if (container.ChunkCount() != kFixtureChunkCount ||
        !std::ranges::equal(container.Bytes(), kGlbWire) ||
        container.Chunk(0).type != kFixtureJsonType ||
        container.Chunk(1).type != kFixtureBinType ||
        container.Chunk(2).type != kFixtureOpaqueType ||
        !std::ranges::equal(container.Chunk(0).bytes,
                            std::span(kGlbWire).subspan(kJsonPayloadOffset,
                                                        kJsonPayloadBytes)) ||
        !std::ranges::equal(
            container.Chunk(1).bytes,
            std::span(kGlbWire).subspan(kBinPayloadOffset, kBinPayloadBytes)) ||
        !std::ranges::equal(container.Chunk(2).bytes, kOpaquePayload) ||
        !std::ranges::equal(
            std::as_bytes(std::span(read.asset->Asset().OriginalJson())),
            container.Chunk(0).bytes))
      return 1;
    retained = *read.asset;
    delegated = read.asset->Asset();
  }
  if (!fs::remove(fixture.Root() / kGlbEntry)) return 1;
  constexpr std::array<std::uint32_t, 3> kIndices{0, 1, 2};
  auto extraction = mv::ExtractGltfPrimitiveCapped(
      *delegated, *delegated->Primitive(0),
      {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw},
      {kIndexedComponents, kTriangleCorners, kRequests});
  if (!extraction.extraction ||
      !std::ranges::equal(extraction.extraction->TriangleIndices(), kIndices) ||
      !std::ranges::equal(retained->Container()->Bytes(), kGlbWire))
    return 1;
  retained.reset();
  delegated.reset();
  if (extraction.extraction->NewRequestedBytes() == 0) return 1;
  extraction.extraction.reset();
  return ledger.ActiveTrackedPayload() == 0 && ledger.ActiveWorkers() == 0 ? 0
                                                                           : 1;
}

int main() { return RunJson() == 0 && RunGlb() == 0 ? 0 : 1; }
