// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>

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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace fs = std::filesystem;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kPhysicalBytes = 1024 * 1024;
constexpr std::uint64_t kRequestBytes = 8 * kPhysicalBytes;
constexpr std::uint64_t kContextBytes = 64 * kPhysicalBytes;
constexpr std::uint64_t kPathUnits = 4096;
constexpr std::uint64_t kNameBytes = 1024;
constexpr std::uint64_t kLineBytes = 4096;
constexpr std::uint64_t kTokenBytes = 128;
constexpr std::uint64_t kCommentBytes = 1024;
constexpr geo::MeshMaterializationWorkPolicy kGeometryWork{1, 256, 32, 32};
constexpr ic::OffWorkPolicy kCodecWork{32, 4, 1, kGeometryWork};
constexpr ic::OffFileWorkPolicy kFileWork{64, 1, 64, 32, 32};
constexpr std::string_view kReceiptName = "receipt.json";
constexpr std::string_view kReceipt =
    "{\"checked\":\"off-entry\",\"version\":1}\n";
constexpr std::string_view kSource =
    "# indexed polygons with an unused signed-zero vertex\nOFF\n8 3 17\n"
    "-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 5\n"
    "4 0 1 2 3\n4 2 3 0 1\n3 4 5 6\n";
constexpr std::string_view kEmpty = "# actual empty capture\nOFF\n0 0 17\n";
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

void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
ic::OffReadLimits ReadLimits() {
  return {kPhysicalBytes,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kLineBytes,
          kTokenBytes,
          kCommentBytes,
          kRequestBytes,
          3 * kRequestBytes,
          {kCountLimit, kCountLimit, kCountLimit, kCountLimit, 1, 1, 1, 1, 1,
           kNameBytes, kRequestBytes},
          {kCountLimit, kRequestBytes}};
}
ic::OffWriteLimits WriteLimits() {
  return {kPhysicalBytes,    kCountLimit,
          kCountLimit,       kCountLimit,
          kCountLimit,       kLineBytes,
          kTokenBytes,       kRequestBytes,
          2 * kRequestBytes, {kCountLimit, kRequestBytes}};
}
ic::OffFileLimits FileLimits() {
  return {kPhysicalBytes, kPhysicalBytes, 2 * kPhysicalBytes, kNameBytes,
          kNameBytes,     kPathUnits,     kRequestBytes};
}
void CheckMesh(const geo::MaterializedMesh& mesh, bool empty) {
  const auto view = mesh.View();
  Require(view.attributes.empty(), "No attributes may appear");
  if (empty) {
    Require(view.positions.empty() && view.corner_vertices.empty() &&
                view.face_offsets.size() == 1 && view.face_offsets[0] == 0,
            "Empty canonical storage must be genuine");
    return;
  }
  Require(view.positions.size() == kPositionBits.size() &&
              std::ranges::equal(view.face_offsets, kOffsets) &&
              std::ranges::equal(view.corner_vertices, kCorners),
          "Complete indexed polygons and unused rows must survive");
  for (std::size_t i = 0; i < kPositionBits.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) == kPositionBits[i],
            "Every source coordinate bit must survive");
}
void WriteFile(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream file(path, std::ios::binary);
  Require(bool(file), "Cannot create demonstration input");
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  file.close();
  Require(bool(file), "Cannot finish demonstration input");
}
std::vector<std::byte> ReadFile(const fs::path& path) {
  const auto size = fs::file_size(path);
  Require(size <= kPhysicalBytes, "Unexpected demonstration output extent");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream file(path, std::ios::binary);
  Require(bool(file), "Cannot reopen physical output");
  file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Require(file.gcount() == static_cast<std::streamsize>(bytes.size()) &&
              file.peek() == std::char_traits<char>::eof(),
          "Actual physical bytes must have the exact expected extent");
  return bytes;
}
class Directory {
 public:
  Directory()
      : path_(fs::canonical(fs::temp_directory_path()) /
              ("meshvale-off-files-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    Require(fs::create_directory(path_), "Demonstration directory must be new");
  }
  Directory(const Directory&) = delete;
  Directory& operator=(const Directory&) = delete;
  ~Directory() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }
  const fs::path& Path() const { return path_; }

 private:
  fs::path path_;
};
struct ReceiptState {
  bool empty;
  std::size_t calls = 0;
};
std::span<const std::byte> Receipt(const ic::OffFileVerifiedContent& content,
                                   void* pointer) {
  auto& state = *static_cast<ReceiptState*>(pointer);
  ++state.calls;
  CheckMesh(content.document.Mesh(), state.empty);
  Require(
      content.document.DeclaredEdgeCount() == 0 &&
          content.entry_bytes.data() == content.document.OriginalBytes().data(),
      "Provider must inspect the actual staged canonical document");
  return Bytes(kReceipt);
}
void Publish(const Directory& directory, bool empty,
             const geo::ExecutionContext& context) {
  const auto entry = fs::path(u8"mesh-\u7f51\u683c.off");
  const auto source = directory.Path() / entry;
  const auto wire = Bytes(empty ? kEmpty : kSource);
  WriteFile(source, wire);
  const ic::OffFileReadLimits reading{FileLimits(), ReadLimits()};
  const ic::OffFileReadWorkPolicy read_work{kFileWork, kCodecWork};
  auto capture = ic::ReadOffFileAsset(source, reading, read_work, context);
  Require(capture.status == ic::OffFileStatus::kSuccess && capture.asset,
          "Actual OFF file capture must succeed");
  auto retained = *capture.asset;
  capture.asset.reset();
  Require(retained.EntryBytes().data() ==
                  retained.Document().OriginalBytes().data() &&
              std::ranges::equal(retained.EntryBytes(), wire),
          "Retained file bytes must alias the one original document");
  CheckMesh(retained.Document().Mesh(), empty);
  Require(fs::remove(source), "Original file must be removable after capture");
  const auto original = std::vector<std::byte>(retained.EntryBytes().begin(),
                                               retained.EntryBytes().end());
  const ic::OffWriteOptions options{ic::OffTextPolicy::kCanonicalAscii,
                                    ic::OffEdgeCountPolicy::kUnknownZero,
                                    &retained.Document()};
  auto written = ic::WriteOff(retained.Document().Mesh(), options,
                              WriteLimits(), kCodecWork);
  Require(written.status == ic::OffStatus::kSuccess && written.output,
          "Real canonical OFF writing must succeed");
  const ic::OffFilePublicationLimits limits{
      FileLimits(), {kCountLimit, kRequestBytes}, ReadLimits()};
  const ic::OffFilePublicationWorkPolicy work{kFileWork, kGeometryWork,
                                              kCodecWork};
  const ic::OffFilePublicationRequest request{
      retained, retained.Document().Mesh(), *written.output};
  for (const bool receipt : {false, true}) {
    ReceiptState state{empty};
    ic::OffFilePublicationOptions publication;
    if (receipt)
      publication.receipt =
          ic::OffFileReceiptProvider{kReceiptName, Receipt, &state};
    const auto destination =
        directory.Path() /
        (empty ? (receipt ? "empty-report" : "empty-entry")
               : (receipt ? "polygons-report" : "polygons-entry"));
    auto result =
        ic::PublishOffBundle(request, destination, publication, limits, work);
    Require(result.outcome == ic::OffFilePublicationOutcome::kPublished &&
                result.bundle,
            "Verified publication must actually commit");
    Require(state.calls == (receipt ? 1u : 0u),
            "Provider invocation count is exact");
    CheckMesh(result.bundle->Document().Mesh(), empty);
    Require(std::ranges::equal(ReadFile(destination / entry),
                               written.output->Bytes()),
            "Independent reopening must match every emitted byte");
    auto reopened =
        ic::ReadOffFileAsset(destination / entry, reading, read_work, context);
    Require(reopened.asset.has_value(),
            "Actual committed OFF file must recapture");
    CheckMesh(reopened.asset->Document().Mesh(), empty);
    std::size_t members = 0;
    for (const auto& member : fs::directory_iterator(destination)) {
      Require(member.is_regular_file(),
              "Bundle contains only regular flat files");
      ++members;
    }
    Require(members == (receipt ? 2u : 1u) &&
                result.bundle->ReceiptBytes().has_value() == receipt,
            "Complete physical inventory and retained receipt must agree");
    if (receipt)
      Require(std::ranges::equal(ReadFile(destination / fs::path(kReceiptName)),
                                 Bytes(kReceipt)) &&
                  std::ranges::equal(*result.bundle->ReceiptBytes(),
                                     Bytes(kReceipt)),
              "Actual committed and retained receipt bytes must match");
    const auto refused =
        ic::PublishOffBundle(request, destination, publication, limits, work);
    Require(refused.outcome == ic::OffFilePublicationOutcome::kFailed &&
                !refused.bundle &&
                refused.diagnostic.status ==
                    ic::OffFileStatus::kDestinationExists &&
                std::ranges::equal(ReadFile(destination / entry),
                                   written.output->Bytes()),
            "Existing destination must refuse without replacing any byte");
  }
  Require(
      std::ranges::equal(retained.EntryBytes(), original) &&
          !fs::exists(source),
      "Publication uses retained source bytes after physical source removal");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "Unknown demonstration argument");
    Directory directory;
    geo::ExecutionContext context({2, 1, kContextBytes});
    Publish(directory, true, context);
    if (unsupported) {
      const auto source = directory.Path() / "unsupported.off";
      WriteFile(source, Bytes(kSource));
      const auto refused =
          ic::ReadOffFileAsset(source, {FileLimits(), ReadLimits()},
                               {kFileWork, kCodecWork}, context);
      Require(
          !refused.asset &&
              refused.diagnostic.off_status ==
                  ic::OffStatus::kGeometryFailure &&
              refused.diagnostic.off_diagnostic &&
              refused.diagnostic.off_diagnostic->geometry_status ==
                  geo::MaterializationStatus::kUnsupportedAllocationProfile,
          "Nonempty capture must retain actual unsupported Geometry refusal");
    } else {
      Publish(directory, false, context);
    }
    Require(
        context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
        "Every owner and worker must release after the actual demonstrations");
    std::cout << "OFF files: actual retained capture, source removal, flat "
                 "publication, "
                 "receipt, physical reload and no-overwrite verified\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
