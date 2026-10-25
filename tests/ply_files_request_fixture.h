// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_PLY_FILES_REQUEST_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_PLY_FILES_REQUEST_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>

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

#include "ply_fixture.h"

namespace meshvale::interchange::ply_files_request_fixture {
namespace geo = geometry;
namespace fs = std::filesystem;
using ply_fixture::Bytes;
using ply_fixture::Require;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kContextBytes = 256 * kMiB;
inline std::vector<std::byte> Source(bool empty = false) {
  return ply_fixture::Wire(PlyEncoding::kAscii, {.empty = empty});
}
inline PlyFileLimits FileLimits() {
  return {kMiB, kMiB, 2 * kMiB, 256, 256, 4096, 64 * kMiB};
}
inline PlyFileReadLimits ReadLimits() {
  return {FileLimits(), ply_fixture::ReadLimits()};
}
inline PlyFilePublicationLimits PublishLimits() {
  return {FileLimits(), {65536, 8 * kMiB}, ply_fixture::ReadLimits()};
}
inline PlyFileWorkPolicy FileWork(bool parallel = false) {
  return {64, parallel ? 1u : kContextBytes, 128, 16, 16};
}
inline PlyFileReadWorkPolicy ReadWork(bool parallel = false) {
  return {FileWork(parallel), ply_fixture::Work()};
}
inline PlyFilePublicationWorkPolicy PublishWork(bool parallel = false) {
  return {FileWork(parallel), ply_fixture::Work().geometry,
          ply_fixture::Work()};
}
class Root {
 public:
  Root() {
    static std::atomic<std::uint64_t> sequence{0};
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
      const auto tick =
          std::chrono::steady_clock::now().time_since_epoch().count();
      auto path = fs::temp_directory_path() /
                  ("meshvale-ply-requests-" + std::to_string(tick) + "-" +
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
  ply_fixture::CheckMesh(mesh, {.changed = changed, .empty = empty});
}
inline PlyOutput Output(const PlyFileAsset& source,
                        const geo::MaterializedMesh& expected) {
  auto result = WritePly(expected, source.Document(),
                         ply_fixture::Options(PlyEncoding::kAscii),
                         ply_fixture::WriteLimits(), ply_fixture::Work());
  Require(result.output.has_value(), "actual schema-bound typed serialization");
  return *result.output;
}
inline void Joined(const PlyFileExecution& execution) {
  for (const auto* stats :
       {&execution.capture, &execution.copying, &execution.byte_verification,
        &execution.mesh_verification, &execution.expected_inspection,
        &execution.ply.codec, &execution.ply.materialization,
        &execution.ply.inspection})
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
inline std::vector<std::byte> LargeSource(bool empty = false) {
  const auto original = Source(empty);
  std::string text(ply_fixture::Text(original));
  constexpr std::string_view kComment =
      "comment independent capture transfer padding\n";
  std::string padding;
  for (std::size_t i = 0; i < 1024; ++i) padding += kComment;
  constexpr std::string_view kHeaderPrefix = "ply\r\nformat ascii 1.0\n";
  Require(text.starts_with(kHeaderPrefix),
          "literal PLY marker and required format line");
  text.insert(kHeaderPrefix.size(), padding);
  const auto bytes = Bytes(text);
  return {bytes.begin(), bytes.end()};
}
}  // namespace meshvale::interchange::ply_files_request_fixture
#endif  // MESHVALE_INTERCHANGE_TESTS_PLY_FILES_REQUEST_FIXTURE_H_
