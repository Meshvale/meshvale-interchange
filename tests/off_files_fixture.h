// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_OFF_FILES_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_OFF_FILES_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "off_fixture.h"

namespace meshvale::interchange::off_files_fixture {
namespace geo = geometry;
namespace fs = std::filesystem;
using off_fixture::Bytes;
using off_fixture::Require;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kContextBytes = 256 * kMiB;
constexpr std::string_view kSource =
    "# Original indexed polygons; retain these exact bytes.\nOFF\n8 3 17\n"
    "-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 5\n"
    "4 0 1 2 3\n4 2 3 0 1\n3 4 5 6\n";
constexpr std::string_view kEmpty = "# real empty source\nOFF\n0 0 17\n";
constexpr std::array<std::uint64_t, 24> kPositionBits{0x8000000000000000,
                                                      0,
                                                      0,
                                                      0x3ff0000000000000,
                                                      0,
                                                      0,
                                                      0x3ff0000000000000,
                                                      0x3ff0000000000000,
                                                      0,
                                                      0,
                                                      0x3ff0000000000000,
                                                      0,
                                                      0x4000000000000000,
                                                      0,
                                                      0,
                                                      0x4008000000000000,
                                                      0,
                                                      0,
                                                      0x4000000000000000,
                                                      0x3ff0000000000000,
                                                      0,
                                                      0x4058c00000000000,
                                                      0x8000000000000000,
                                                      0x4014000000000000};
constexpr std::array<geo::index_t, 4> kOffsets{0, 4, 8, 11};
constexpr std::array<geo::index_t, 11> kCorners{0, 1, 2, 3, 2, 3,
                                                0, 1, 4, 5, 6};
constexpr std::array<geo::index_t, 3> kChangedOffsets{0, 4, 7};
constexpr std::array<geo::index_t, 7> kChangedCorners{0, 1, 2, 3, 4, 5, 6};
constexpr std::array<geo::index_t, 1> kEmptyOffsets{0};
inline OffFileLimits FileLimits() {
  return {kMiB, kMiB, 2 * kMiB, 256, 256, 4096, 64 * kMiB};
}
inline OffFileReadLimits ReadLimits() {
  return {FileLimits(), off_fixture::ReadLimits()};
}
inline OffFilePublicationLimits PublishLimits() {
  return {FileLimits(), {65536, 8 * kMiB}, off_fixture::ReadLimits()};
}
inline OffFileWorkPolicy FileWork(bool parallel = false) {
  return {64, parallel ? 1u : kContextBytes, 128, 16, 16};
}
inline OffFileReadWorkPolicy ReadWork(bool parallel = false) {
  return {FileWork(parallel), off_fixture::Work()};
}
inline OffFilePublicationWorkPolicy PublishWork(bool parallel = false) {
  return {FileWork(parallel), off_fixture::Work().geometry,
          off_fixture::Work()};
}
inline std::string Utf8(std::u8string_view text) {
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}
class Root {
 public:
  Root() {
    static std::atomic<std::uint64_t> sequence{0};
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
      const auto tick =
          std::chrono::steady_clock::now().time_since_epoch().count();
      auto path = fs::temp_directory_path() /
                  ("meshvale-off-files-" + std::to_string(tick) + "-" +
                   std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(path, error)) {
        path_ = fs::canonical(path);
        return;
      }
    }
    Require(false, "exclusive owned temporary fixture directory");
  }
  Root(const Root&) = delete;
  Root& operator=(const Root&) = delete;
  ~Root() {
    std::error_code ignored;
    if (!path_.empty()) fs::remove_all(path_, ignored);
  }
  const fs::path& Path() const { return path_; }

 private:
  fs::path path_;
};
inline void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(output.good(), "fixture file opens");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  Require(output.good(), "fixture writes and closes exact bytes");
}
inline void Write(const fs::path& path, std::string_view text) {
  Write(path, Bytes(text));
}
inline std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  Require(input.good(), "independent physical read opens");
  const auto count = input.tellg();
  Require(count >= 0 && count <= static_cast<std::streamoff>(4 * kMiB),
          "bounded fixture physical read");
  std::vector<std::byte> bytes(static_cast<std::size_t>(count));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  Require(input.good() || (bytes.empty() && input.eof()),
          "complete independent physical read");
  return bytes;
}
inline void Check(const geo::MaterializedMesh& mesh, bool changed = false,
                  bool empty = false) {
  const auto view = mesh.View();
  Require(view.positions.size() == (empty ? 0 : kPositionBits.size()) &&
              view.attributes.empty(),
          "literal OFF vertex rows and absent channels");
  for (std::size_t i = 0; i < view.positions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) == kPositionBits[i],
            "all literal position bits including unused signed-zero vertex");
  const std::span<const geo::index_t> offsets =
      empty ? std::span<const geo::index_t>(kEmptyOffsets)
            : (changed ? std::span<const geo::index_t>(kChangedOffsets)
                       : std::span<const geo::index_t>(kOffsets));
  const std::span<const geo::index_t> corners =
      empty ? std::span<const geo::index_t>{}
            : (changed ? std::span<const geo::index_t>(kChangedCorners)
                       : std::span<const geo::index_t>(kCorners));
  Require(std::ranges::equal(view.face_offsets, offsets) &&
              std::ranges::equal(view.corner_vertices, corners),
          "every literal offset/directed corner, polygon order and identity");
}
inline geo::MaterializedMesh Candidate(const OffFileAsset& source) {
  auto context = source.Document().Mesh().ShareExecutionContext();
  auto result = geo::MaterializeMesh(
      {source.Document().Mesh().View().positions,
       geo::PolygonTopologyInput{kChangedOffsets, kChangedCorners},
       {}},
      off_fixture::MeshLimits(), off_fixture::Work().geometry, context);
  Require(result.mesh.has_value(), "actual alternate strong indexed mesh");
  Check(*result.mesh, true);
  return *result.mesh;
}
inline OffOutput Output(const OffFileAsset& source,
                        const geo::MaterializedMesh& expected) {
  auto written = WriteOff(expected, off_fixture::Options(&source.Document()),
                          off_fixture::WriteLimits(), off_fixture::Work());
  Require(written.output.has_value(), "real canonical OFF serialization");
  Require(written.output->Facts().source_declared_edge_count == 17 &&
              written.output->Facts().emitted_edge_count == 0,
          "literal original edge provenance and explicit unknown-zero output");
  return *written.output;
}
inline void Joined(const OffFileExecution& execution) {
  for (const auto* stats :
       {&execution.capture, &execution.copying, &execution.byte_verification,
        &execution.mesh_verification, &execution.expected_inspection,
        &execution.off.codec, &execution.off.materialization,
        &execution.off.inspection})
    Require(stats->launched_workers == stats->joined_workers,
            "every actual phase's launched workers joined before return");
}
inline void Empty(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "final strong-owner release leaves no tracked payload or workers");
}
inline void Inventory(const fs::path& destination, std::string_view entry,
                      std::optional<std::string_view> receipt = {}) {
  std::vector<std::string> names;
  for (const auto& item : fs::directory_iterator(destination)) {
    Require(item.is_regular_file() && !item.is_symlink(),
            "physical published inventory contains only regular flat files");
    const auto name = item.path().filename().u8string();
    names.emplace_back(reinterpret_cast<const char*>(name.data()), name.size());
  }
  std::sort(names.begin(), names.end());
  std::vector<std::string> expected{std::string(entry)};
  if (receipt) expected.emplace_back(*receipt);
  std::sort(expected.begin(), expected.end());
  Require(names == expected, "complete exact physical entry/receipt inventory");
}
inline std::string LargeSource(bool empty = false) {
  std::string text;
  constexpr std::string_view kComment =
      "# independent capture transfer padding\n";
  for (std::size_t i = 0; i < 4096; ++i) text += kComment;
  text += empty ? kEmpty : kSource;
  return text;
}
}  // namespace meshvale::interchange::off_files_fixture
#endif  // MESHVALE_INTERCHANGE_TESTS_OFF_FILES_FIXTURE_H_
