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

int main() {
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
