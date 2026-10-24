// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

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
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace repair = meshvale::repair;
namespace fs = std::filesystem;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kFileBytes = 1024 * 1024;
constexpr std::uint64_t kRequestBytes = 8 * kFileBytes;
constexpr std::uint64_t kContextBytes = 64 * kFileBytes;
constexpr std::uint64_t kDescriptorBytes = 4096;
constexpr std::uint64_t kLineBytes = 4096;
constexpr std::uint64_t kTokenBytes = 128;
constexpr std::uint64_t kCommentBytes = 1024;
constexpr std::uint64_t kComparisonSteps = 4096;
constexpr geo::MeshMaterializationWorkPolicy kGeometryWork{1, 256, 32, 32};
constexpr ic::OffWorkPolicy kWork{32, 4, 1, kGeometryWork};
constexpr std::string_view kSource =
    "# indexed polygons with one unused vertex\nOFF\n8 3 17\n"
    "-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 5\n"
    "4 0 1 2 3\n4 2 3 0 1\n3 4 5 6\n";
constexpr std::array<double, 24> kPositions{-0.0, 0, 0, 1, 0, 0,  1,    1,
                                            0,    0, 1, 0, 2, 0,  0,    3,
                                            0,    0, 2, 1, 0, 99, -0.0, 5};
constexpr std::array<geo::index_t, 4> kSourceOffsets{0, 4, 8, 11};
constexpr std::array<geo::index_t, 11> kSourceCorners{0, 1, 2, 3, 2, 3,
                                                      0, 1, 4, 5, 6};
constexpr std::array<geo::index_t, 3> kCandidateOffsets{0, 4, 7};
constexpr std::array<geo::index_t, 7> kCandidateCorners{0, 1, 2, 3, 4, 5, 6};
constexpr std::array<geo::index_t, 3> kFaceMap{0, 0, 1};
constexpr std::array<geo::index_t, 11> kCornerMap{0, 1, 2, 3, 2, 3,
                                                  0, 1, 4, 5, 6};
constexpr std::array<geo::index_t, 2> kSourceFaces{0, 2};
constexpr std::array<geo::index_t, 7> kSourceCornerMap{0, 1, 2, 3, 8, 9, 10};
constexpr std::array<repair::DuplicateTarget, 1> kTarget{{{0, 1}}};

void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
geo::MeshMaterializationLimits MeshLimits() {
  return {kCountLimit, kCountLimit,      kCountLimit,  kCountLimit,
          kCountLimit, kCountLimit,      kCountLimit,  kCountLimit,
          kCountLimit, kDescriptorBytes, kRequestBytes};
}
ic::OffReadLimits ReadLimits() {
  return {kFileBytes,        kCountLimit,   kCountLimit,
          kCountLimit,       kCountLimit,   kLineBytes,
          kTokenBytes,       kCommentBytes, kRequestBytes,
          3 * kRequestBytes, MeshLimits(),  {kCountLimit, kRequestBytes}};
}
ic::OffWriteLimits WriteLimits() {
  return {kFileBytes,        kCountLimit,
          kCountLimit,       kCountLimit,
          kCountLimit,       kLineBytes,
          kTokenBytes,       kRequestBytes,
          2 * kRequestBytes, {kCountLimit, kRequestBytes}};
}
repair::MaterializedDuplicateLimits RepairLimits() {
  return {kCountLimit,      kCountLimit,   kDescriptorBytes,
          kComparisonSteps, kRequestBytes, {kCountLimit, kRequestBytes},
          MeshLimits()};
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
template <std::size_t N>
void Equal(std::span<const geo::index_t> actual,
           const std::array<geo::index_t, N>& expected,
           std::string_view message) {
  Require(std::ranges::equal(actual, expected), message);
}
void CheckMesh(const geo::MaterializedMesh& mesh, bool changed) {
  const auto view = mesh.View();
  Require(view.attributes.empty() && view.positions.size() == kPositions.size(),
          "The complete uncolored vertex inventory must survive");
  for (std::size_t i = 0; i < kPositions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) ==
                std::bit_cast<std::uint64_t>(kPositions[i]),
            "Position bits, row order and unused vertices must survive");
  if (changed) {
    Equal(view.face_offsets, kCandidateOffsets,
          "Candidate polygon offsets differ");
    Equal(view.corner_vertices, kCandidateCorners,
          "Candidate polygon loops differ");
  } else {
    Equal(view.face_offsets, kSourceOffsets, "Source polygon offsets differ");
    Equal(view.corner_vertices, kSourceCorners, "Source polygon loops differ");
  }
}
void WriteFile(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary);
  Require(bool(stream), "Cannot create demonstration file");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.flush();
  Require(bool(stream), "Cannot write complete demonstration file");
}
std::vector<std::byte> ReadFile(const fs::path& path) {
  const auto size = fs::file_size(path);
  Require(size <= kFileBytes, "Demonstration file exceeds source limit");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream stream(path, std::ios::binary);
  Require(bool(stream), "Cannot open demonstration file");
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  Require(stream.gcount() == static_cast<std::streamsize>(size) &&
              stream.peek() == std::char_traits<char>::eof(),
          "Demonstration file changed or is incomplete");
  return bytes;
}
class Directory {
 public:
  explicit Directory(const std::optional<fs::path>& requested)
      : path_(requested ? *requested
                        : fs::temp_directory_path() /
                              ("meshvale-off-repair-" +
                               std::to_string(std::chrono::steady_clock::now()
                                                  .time_since_epoch()
                                                  .count()))),
        temporary_(!requested) {
    Require(fs::create_directory(path_), "Output directory must be new");
  }
  Directory(const Directory&) = delete;
  Directory& operator=(const Directory&) = delete;
  ~Directory() {
    if (temporary_) {
      std::error_code ignored;
      fs::remove_all(path_, ignored);
    }
  }
  const fs::path& Path() const { return path_; }

 private:
  fs::path path_;
  bool temporary_;
};
void RejectedEquivalence(std::string_view wire,
                         const geo::ExecutionContext& context) {
  auto read = ic::ReadOff(Bytes(wire), ReadLimits(), kWork, context);
  Require(read.document.has_value(), "Negative equivalence source must import");
  const auto result = repair::RemoveMaterializedDuplicateFaces(
      read.document->Mesh(), kTarget, {}, RepairLimits(), kGeometryWork);
  Require(result.status == repair::MaterializedDuplicateStatus::kRejected &&
              !result.repair &&
              result.diagnostic.code ==
                  repair::MaterializedDuplicateDiagnosticCode::kNotEquivalent,
          "Reversed or different-index loops must not become duplicates");
}
void CheckEmpty(const geo::ExecutionContext& context) {
  {
    constexpr std::string_view kEmptyWire = "OFF\n0 0 0\n";
    constexpr std::array<geo::index_t, 1> kEmptyOffsets{0};
    auto read = ic::ReadOff(Bytes(kEmptyWire), ReadLimits(), kWork, context);
    Require(read.status == ic::OffStatus::kSuccess && read.document &&
                read.document->Inspection().IsStorageValid(),
            "The actual empty numerical profile must remain admissible");
    auto result = repair::RemoveMaterializedDuplicateFaces(
        read.document->Mesh(), {}, {}, RepairLimits(), kGeometryWork);
    Require(
        result.status == repair::MaterializedDuplicateStatus::kSuccess &&
            result.repair &&
            result.repair->Outcome() == repair::CandidateOutcome::unchanged,
        "The existing Repair engine must accept the empty unchanged request");
    const auto& repaired = *result.repair;
    Require(repaired.FaceMap().empty() && repaired.CornerMap().empty() &&
                repaired.SourceFaces().empty() &&
                repaired.SourceCorners().empty(),
            "Empty Repair maps must cover exactly zero faces and corners");
    const ic::OffWriteOptions options{ic::OffTextPolicy::kCanonicalAscii,
                                      ic::OffEdgeCountPolicy::kUnknownZero,
                                      &*read.document};
    auto written =
        ic::WriteOff(repaired.Candidate(), options, WriteLimits(), kWork);
    Require(written.status == ic::OffStatus::kSuccess && written.output &&
                std::ranges::equal(written.output->Bytes(), Bytes(kEmptyWire)),
            "Empty serialization must have the literal canonical wire");
    const auto reloaded =
        ic::ReadOff(written.output->Bytes(), ReadLimits(), kWork, context);
    Require(reloaded.status == ic::OffStatus::kSuccess && reloaded.document &&
                reloaded.document->Inspection().IsStorageValid(),
            "Empty output must independently reload");
    const auto view = reloaded.document->Mesh().View();
    Require(view.positions.empty() && view.corner_vertices.empty() &&
                view.attributes.empty(),
            "Empty reload must contain no fabricated rows or channels");
    Equal(view.face_offsets, kEmptyOffsets,
          "Empty polygon offsets must be [0]");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "Empty codec and Repair owners must fully release");
}
int Run(std::span<const fs::path> args) {
  const bool unsupported =
      args.size() == 1 && args[0] == fs::path("--unsupported-profile");
  std::optional<fs::path> output;
  if (!args.empty() && !unsupported) {
    Require(args.size() == 2 && args[0] == fs::path("--output"),
            "Use --output NEW_DIRECTORY or --unsupported-profile");
    output = args[1];
  }
  geo::ExecutionContext context({2, 1, kContextBytes});
  CheckEmpty(context);
  if (unsupported) {
    const auto result =
        ic::ReadOff(Bytes(kSource), ReadLimits(), kWork, context);
    Require(
        !result.document && result.status == ic::OffStatus::kGeometryFailure &&
            result.diagnostic.geometry_status ==
                geo::MaterializationStatus::kUnsupportedAllocationProfile &&
            context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
        "The actual nonempty numerical profile refusal must remain explicit");
    std::cout << "Actual OFF nonempty numerical-profile refusal and empty "
                 "read/Repair/write/reload verified\n";
    return 0;
  }
  Directory directory(output);
  {
    WriteFile(directory.Path() / "source.off", Bytes(kSource));
    const auto physical = ReadFile(directory.Path() / "source.off");
    auto read = ic::ReadOff(physical, ReadLimits(), kWork, context);
    Require(read.document && read.document->Inspection().IsStorageValid() &&
                read.document->DeclaredEdgeCount() == 17,
            "Actual indexed OFF source must import and retain edge provenance");
    CheckMesh(read.document->Mesh(), false);
    for (bool changed : {false, true}) {
      const auto targets =
          changed ? std::span<const repair::DuplicateTarget>(kTarget)
                  : std::span<const repair::DuplicateTarget>{};
      auto result = repair::RemoveMaterializedDuplicateFaces(
          read.document->Mesh(), targets, {}, RepairLimits(), kGeometryWork);
      Require(
          result.repair && result.repair->Outcome() ==
                               (changed ? repair::CandidateOutcome::accepted
                                        : repair::CandidateOutcome::unchanged),
          "The real charged Repair operation must accept the selected request");
      const auto& repaired = *result.repair;
      CheckMesh(repaired.Candidate(), changed);
      if (changed) {
        Equal(repaired.FaceMap(), kFaceMap, "Complete Repair face map differs");
        Equal(repaired.CornerMap(), kCornerMap,
              "Complete Repair corner map differs");
        Equal(repaired.SourceFaces(), kSourceFaces,
              "Retained source faces differ");
        Equal(repaired.SourceCorners(), kSourceCornerMap,
              "Retained source corners differ");
      } else {
        Require(
            repaired.FaceMap().size() == kSourceOffsets.size() - 1 &&
                repaired.SourceFaces().size() == kSourceOffsets.size() - 1 &&
                repaired.CornerMap().size() == kSourceCorners.size() &&
                repaired.SourceCorners().size() == kSourceCorners.size(),
            "Unchanged maps must cover every face and corner");
        for (std::size_t i = 0; i < repaired.FaceMap().size(); ++i)
          Require(repaired.FaceMap()[i] == i && repaired.SourceFaces()[i] == i,
                  "Unchanged face map must be identity");
        for (std::size_t i = 0; i < repaired.CornerMap().size(); ++i)
          Require(
              repaired.CornerMap()[i] == i && repaired.SourceCorners()[i] == i,
              "Unchanged corner map must be identity");
      }
      const ic::OffWriteOptions options{ic::OffTextPolicy::kCanonicalAscii,
                                        ic::OffEdgeCountPolicy::kUnknownZero,
                                        &*read.document};
      auto written =
          ic::WriteOff(repaired.Candidate(), options, WriteLimits(), kWork);
      Require(
          written.output &&
              written.output->Facts().correspondence ==
                  ic::OffCorrespondence::kIdentity &&
              written.output->Facts().source_declared_edge_count == 17 &&
              written.output->Facts().emitted_edge_count == 0,
          "Serialization identity and edge rewrite facts must stay separate");
      const auto path =
          directory.Path() / (changed ? "changed.off" : "unchanged.off");
      WriteFile(path, written.output->Bytes());
      const auto reloaded_bytes = ReadFile(path);
      Require(std::ranges::equal(reloaded_bytes, written.output->Bytes()),
              "Physical output must equal actual serialization bytes");
      const auto reloaded =
          ic::ReadOff(reloaded_bytes, ReadLimits(), kWork, context);
      Require(reloaded.document && reloaded.document->DeclaredEdgeCount() == 0,
              "Physical output must independently reload with unknown edges");
      CheckMesh(reloaded.document->Mesh(), changed);
    }
    std::string reversed(kSource);
    const auto offset = reversed.find("4 2 3 0 1");
    Require(offset != std::string::npos,
            "Literal negative-control marker missing");
    reversed.replace(offset, std::string_view("4 2 3 0 1").size(), "4 2 1 0 3");
    RejectedEquivalence(reversed, context);
    constexpr std::string_view kDifferentIds =
        "OFF\n12 3 17\n-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 "
        "5\n"
        "-0 0 0\n1 0 0\n1 1 0\n0 1 0\n4 0 1 2 3\n4 10 11 8 9\n3 4 5 6\n";
    RejectedEquivalence(kDifferentIds, context);
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "All codec and Repair owners/workers must release");
  std::cout << "OFF polygons: changed and unchanged Repair, complete maps, "
               "physical reload and negative controls passed\n";
  return 0;
}
template <class Character>
int Main(int argc, Character** argv) {
  try {
    Require(argc >= 1 && argc <= 3, "Too many demonstration arguments");
    std::array<fs::path, 2> arguments;
    for (int i = 1; i < argc; ++i)
      arguments[static_cast<std::size_t>(i - 1)] = fs::path(argv[i]);
    return Run(std::span(arguments).first(static_cast<std::size_t>(argc - 1)));
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
}  // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return Main(argc, argv); }
#else
int main(int argc, char** argv) { return Main(argc, argv); }
#endif
