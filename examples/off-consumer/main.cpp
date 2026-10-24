// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace fs = std::filesystem;
constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
constexpr std::uint64_t kRequestLimit = 8 * kPhysicalLimit;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kDescriptorLimit = 4096;
constexpr std::uint64_t kAttributeLimit = 8;
constexpr std::uint64_t kLineLimit = 512;
constexpr std::uint64_t kTokenLimit = 128;
constexpr std::uint64_t kCommentLimit = 256;
constexpr std::string_view kSource =
    "# original indexed polygons\nOFF\n8 3 17\n"
    "-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 5\n"
    "4 0 1 2 3\n4 2 3 0 1\n3 4 5 6\n";
void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
geo::MeshMaterializationLimits MeshLimits() {
  return {kCountLimit,      kCountLimit,      kCountLimit,  kCountLimit,
          kAttributeLimit,  kCountLimit,      kCountLimit,  kCountLimit,
          kDescriptorLimit, kDescriptorLimit, kRequestLimit};
}
ic::OffReadLimits ReadLimits() {
  return {kPhysicalLimit,    kCountLimit,   kCountLimit,
          kCountLimit,       kCountLimit,   kLineLimit,
          kTokenLimit,       kCommentLimit, kRequestLimit,
          3 * kRequestLimit, MeshLimits(),  {kCountLimit, kRequestLimit}};
}
ic::OffWriteLimits WriteLimits() {
  return {kPhysicalLimit,    kCountLimit,
          kCountLimit,       kCountLimit,
          kCountLimit,       kLineLimit,
          kTokenLimit,       kRequestLimit,
          2 * kRequestLimit, {kCountLimit, kRequestLimit}};
}
ic::OffWorkPolicy Work() { return {16, 4, 1, {1, 64, 8, 64}}; }
void Check(const ic::OffDocument& document) {
  constexpr std::array<double, 24> kPositions{-0., 0, 0, 1, 0, 0,  1,   1,
                                              0,   0, 1, 0, 2, 0,  0,   3,
                                              0,   0, 2, 1, 0, 99, -0., 5};
  constexpr std::array<geo::index_t, 4> kOffsets{0, 4, 8, 11};
  constexpr std::array<geo::index_t, 11> kCorners{0, 1, 2, 3, 2, 3,
                                                  0, 1, 4, 5, 6};
  const auto view = document.Mesh().View();
  Require(document.Profile() == ic::OffProfile::kAsciiRecords &&
              document.Inspection().IsStorageValid() &&
              view.attributes.empty() &&
              view.positions.size() == kPositions.size() &&
              std::ranges::equal(view.face_offsets, kOffsets) &&
              std::ranges::equal(view.corner_vertices, kCorners),
          "Installed literal polygons retain shared identities, winding/start "
          "order and unused vertex");
  for (std::size_t i = 0; i < kPositions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) ==
                std::bit_cast<std::uint64_t>(kPositions[i]),
            "Installed positions retain all literal binary64 bits");
}
void WriteNew(const fs::path& path, std::span<const std::byte> bytes) {
  Require(!fs::exists(path), "Example output already exists; no overwrite");
  std::ofstream output(path, std::ios::binary);
  Require(output.is_open(), "New example file opened");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  Require(!output.fail(), "New example file written completely");
}
std::vector<std::byte> ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  Require(input.is_open(), "Actual output reopened");
  const std::string value{std::istreambuf_iterator<char>(input), {}};
  Require(!input.bad(), "Actual output read completely");
  return {Bytes(value).begin(), Bytes(value).end()};
}
class Directory {
 public:
  explicit Directory(std::optional<fs::path> requested)
      : keep_(requested.has_value()) {
    if (requested) {
      path_ = fs::absolute(*requested);
      Require(fs::create_directory(path_), "Choose a new output directory");
    } else {
      constexpr std::size_t kCreationAttempts = 128;
      const auto seed =
          std::chrono::steady_clock::now().time_since_epoch().count();
      for (std::size_t i = 0; i < kCreationAttempts; ++i) {
        path_ = fs::temp_directory_path() /
                ("meshvale-installed-off-" + std::to_string(seed) + "-" +
                 std::to_string(i));
        if (fs::create_directory(path_)) return;
      }
      throw std::runtime_error(
          "Cannot create an owned temporary example directory");
    }
  }
  ~Directory() {
    if (!keep_) {
      std::error_code error;
      fs::remove_all(path_, error);
    }
  }
  const fs::path& Path() const { return path_; }

 private:
  fs::path path_;
  bool keep_;
};

template <class Char>
int Main(int argc, Char** argv) {
  try {
    bool unsupported = false;
    std::optional<fs::path> requested;
    for (int i = 1; i < argc; ++i) {
      const fs::path argument(argv[i]);
      if (argument == fs::path("--unsupported-profile"))
        unsupported = true;
      else if (argument == fs::path("--output") && i + 1 < argc && !requested)
        requested = fs::path(argv[++i]);
      else
        throw std::runtime_error(
            "Usage: consumer [--unsupported-profile] [--output NEW_DIRECTORY]");
    }
    Directory directory(requested);
    const auto source_path = directory.Path() / "original.off";
    WriteNew(source_path, Bytes(kSource));
    const auto original = ReadFile(source_path);
    geo::ExecutionContext context({4, 1, 64 * kPhysicalLimit});
    {
      const auto read = ic::ReadOff(original, ReadLimits(), Work(), context);
      if (unsupported) {
        Require(
            read.status == ic::OffStatus::kGeometryFailure && !read.document &&
                read.diagnostic.geometry_status ==
                    geo::MaterializationStatus::kUnsupportedAllocationProfile,
            "Actual nonempty numerical allocation profile refuses honestly");
      } else {
        Require(read.status == ic::OffStatus::kSuccess && read.document,
                "Installed original OFF read succeeds");
        Check(*read.document);
        Require(
            read.document->DeclaredEdgeCount() == 17 &&
                std::ranges::equal(read.document->OriginalBytes(), original),
            "Original bytes and edge-count provenance retained");
        const ic::OffWriteOptions options{ic::OffTextPolicy::kCanonicalAscii,
                                          ic::OffEdgeCountPolicy::kUnknownZero,
                                          &*read.document};
        const auto write =
            ic::WriteOff(read.document->Mesh(), options, WriteLimits(), Work());
        Require(write.status == ic::OffStatus::kSuccess && write.output,
                "Explicit installed canonical OFF serialization succeeds");
        const auto& facts = write.output->Facts();
        Require(facts.profile == ic::OffProfile::kAsciiRecords &&
                    facts.vertex_count == 8 && facts.face_count == 3 &&
                    facts.corner_count == 11 &&
                    facts.correspondence == ic::OffCorrespondence::kIdentity &&
                    facts.text_policy == ic::OffTextPolicy::kCanonicalAscii &&
                    facts.edge_count_policy ==
                        ic::OffEdgeCountPolicy::kUnknownZero &&
                    facts.emitted_edge_count == 0 &&
                    facts.source_declared_edge_count == 17,
                "Actual codec facts declare supplied-mesh identity and "
                "explicit rewrite/edge policy");
        const auto output_path = directory.Path() / "canonical.off";
        WriteNew(output_path, write.output->Bytes());
        const auto actual = ReadFile(output_path);
        Require(std::ranges::equal(actual, write.output->Bytes()),
                "Physical output equals actual returned bytes");
        const auto reloaded =
            ic::ReadOff(actual, ReadLimits(), Work(), context);
        Require(reloaded.document.has_value(),
                "Physical canonical OFF independently reloads");
        Check(*reloaded.document);
        Require(
            reloaded.document->DeclaredEdgeCount() == 0 &&
                ReadFile(source_path) == original,
            "Canonical unknown edges and unchanged source verified physically");
      }
    }
    {
      // Empty numerical storage remains admitted on every producer profile.
      const auto empty =
          ic::ReadOff(Bytes("OFF\n0 0 0\n"), ReadLimits(), Work(), context);
      Require(empty.document &&
                  empty.document->Mesh().View().positions.empty() &&
                  empty.document->Mesh().View().corner_vertices.empty(),
              "Actual empty OFF admission works on every profile");
      const ic::OffWriteOptions options{ic::OffTextPolicy::kCanonicalAscii,
                                        ic::OffEdgeCountPolicy::kUnknownZero,
                                        nullptr};
      const auto written =
          ic::WriteOff(empty.document->Mesh(), options, WriteLimits(), Work());
      Require(written.output && written.output->Facts().vertex_count == 0 &&
                  written.output->Facts().face_count == 0,
              "Actual empty canonical OFF serialization succeeds");
    }
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "All installed example owners/workers release");
    std::cout << (unsupported ? "Actual nonempty profile refusal and empty OFF "
                                "roundtrip passed\n"
                              : "OFF read, inspect, canonical write and "
                                "physical reload passed; polygons/unused "
                                "vertices retained, no repair performed\n");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
}  // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return Main(argc, argv); }
#else
int main(int argc, char** argv) { return Main(argc, argv); }
#endif
