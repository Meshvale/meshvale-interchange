// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>

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
#include <variant>
#include <vector>

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace fs = std::filesystem;
constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
constexpr std::uint64_t kRequestLimit = 8 * kPhysicalLimit;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kDescriptorLimit = 4096;
constexpr std::size_t kHeaderBytes = 80;
constexpr std::size_t kCountOffset = 80;
constexpr std::size_t kRecordOffset = 84;
constexpr std::size_t kRecordBytes = 50;
constexpr std::string_view kName = "installed original";
constexpr std::string_view kAscii =
    "solid installed original\n"
    "facet normal 0 0 -2\nouter loop\n"
    "vertex -0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
    "endloop\nendfacet\nendsolid installed original\n";

void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
ic::StlReadLimits ReadLimits() {
  return {kPhysicalLimit,
          kCountLimit,
          256,
          512,
          128,
          kRequestLimit,
          3 * kRequestLimit,
          {kCountLimit, kCountLimit, kCountLimit, kCountLimit, 8, kCountLimit,
           kCountLimit, kCountLimit, kDescriptorLimit, kDescriptorLimit,
           kRequestLimit},
          {kCountLimit, kRequestLimit}};
}
ic::StlWriteLimits WriteLimits() {
  return {kPhysicalLimit,
          kCountLimit,
          256,
          512,
          128,
          kRequestLimit,
          2 * kRequestLimit,
          {kCountLimit, kRequestLimit}};
}
ic::StlWorkPolicy Work() { return {64, 8, 1, {1, 64, 8, 64}}; }
void PutWord(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) {
  constexpr unsigned kByteBits = 8;
  constexpr std::uint32_t kMask = 0xff;
  for (std::size_t i = 0; i < sizeof(value); ++i)
    bytes[offset + i] = std::byte((value >> (kByteBits * i)) & kMask);
}
std::array<std::byte, kHeaderBytes> Header() {
  std::array<std::byte, kHeaderBytes> header{};
  constexpr std::string_view kPrefix = "solid explicit binary header";
  for (std::size_t i = 0; i < kPrefix.size(); ++i)
    header[i] = std::byte(static_cast<unsigned char>(kPrefix[i]));
  header.back() = std::byte{0xff};
  return header;
}
std::vector<std::byte> Binary() {
  std::vector<std::byte> bytes(kRecordOffset + kRecordBytes);
  const auto header = Header();
  std::ranges::copy(header, bytes.begin());
  PutWord(bytes, kCountOffset, 1);
  constexpr std::array<std::uint32_t, 12> kComponents{
      0, 0, 0xc0000000, 0x80000000, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0};
  for (std::size_t i = 0; i < kComponents.size(); ++i)
    PutWord(bytes, kRecordOffset + i * sizeof(std::uint32_t), kComponents[i]);
  return bytes;
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
void Check(const ic::StlDocument& document) {
  const auto mesh = document.Mesh().View();
  constexpr std::array<geo::index_t, 2> kOffsets{0, 3};
  constexpr std::array<geo::index_t, 3> kCorners{0, 1, 2};
  constexpr std::array<double, 9> kPositions{-0., 0, 0, 1, 0, 0, 0, 1, 0};
  Require(document.Inspection().IsStorageValid() &&
              mesh.positions.size() == 9 &&
              std::ranges::equal(mesh.face_offsets, kOffsets) &&
              std::ranges::equal(mesh.corner_vertices, kCorners),
          "Literal triangle soup survives installed read/reload");
  for (std::size_t i = 0; i < kPositions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(mesh.positions[i]) ==
                std::bit_cast<std::uint64_t>(kPositions[i]),
            "Position value/zero sign preserved exactly");
  Require(mesh.attributes.size() == 1, "One original face-normal channel");
  const auto& normal = mesh.attributes.front();
  Require(normal.domain == geo::AttributeDomain::face &&
              normal.name == "facet_normal" && normal.semantic == "normal" &&
              normal.components == 3 && !normal.set_index && !normal.offsets &&
              !normal.present && normal.metadata.empty(),
          "Raw face-normal descriptor retained");
  const auto values = std::get<std::span<const double>>(normal.values);
  constexpr std::array<double, 3> kNormal{0, 0, -2};
  Require(std::ranges::equal(values, kNormal),
          "Opposed nonunit normal was not regenerated or normalized");
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
                ("meshvale-installed-stl-" + std::to_string(seed) + "-" +
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
      if (argument == fs::path("--unsupported-profile")) {
        unsupported = true;
      } else if (argument == fs::path("--output") && i + 1 < argc &&
                 !requested) {
        requested = fs::path(argv[++i]);
      } else {
        throw std::runtime_error(
            "Usage: consumer [--unsupported-profile] [--output NEW_DIRECTORY]");
      }
    }
    Directory directory(requested);
    geo::ExecutionContext context({4, 1, 64 * kPhysicalLimit});
    for (const auto encoding :
         {ic::StlEncoding::kAscii, ic::StlEncoding::kBinary}) {
      const auto binary = Binary();
      const auto source = encoding == ic::StlEncoding::kAscii
                              ? Bytes(kAscii)
                              : std::span<const std::byte>(binary);
      const std::string source_name =
          encoding == ic::StlEncoding::kAscii ? "ascii" : "binary";
      const auto source_path =
          directory.Path() / ("source-" + source_name + ".stl");
      WriteNew(source_path, source);
      const auto actual_source = ReadFile(source_path);
      auto result =
          ic::ReadStl(actual_source, encoding, ReadLimits(), Work(), context);
      if (unsupported) {
        Require(
            result.status == ic::StlStatus::kGeometryFailure &&
                !result.document &&
                result.diagnostic.geometry_status ==
                    geo::MaterializationStatus::kUnsupportedAllocationProfile,
            "Require a genuine unsupported numerical-profile outcome");
        continue;
      }
      Require(result.status == ic::StlStatus::kSuccess && result.document,
              "Installed STL source read succeeds");
      Check(*result.document);
      Require(std::ranges::equal(result.document->OriginalBytes(), source),
              "Original wire bytes remain available");
      Require(encoding != ic::StlEncoding::kAscii ||
                  result.document->AsciiName() == kName,
              "Original solid name remains available");
      const auto header = Header();
      Require(encoding != ic::StlEncoding::kBinary ||
                  std::ranges::equal(result.document->BinaryHeader(), header),
              "Opaque original binary header remains available");
      for (const auto destination :
           {ic::StlEncoding::kAscii, ic::StlEncoding::kBinary}) {
        ic::StlWriteOptions options;
        options.encoding = destination;
        options.ascii_name =
            destination == ic::StlEncoding::kAscii ? kName : "";
        if (destination == ic::StlEncoding::kBinary)
          options.binary_header = header;
        options.face_normal_attribute = 0;
        options.acknowledge_corner_expansion = true;
        options.source_document = &*result.document;
        auto output = ic::WriteStl(result.document->Mesh(), options,
                                   WriteLimits(), Work());
        Require(output.status == ic::StlStatus::kSuccess && output.output,
                "Explicit installed STL serialization succeeds");
        Require(
            output.output->Facts().source_vertex_count == 3 &&
                output.output->Facts().source_face_count == 1 &&
                output.output->Facts().corner_expansion_acknowledged,
            "Actual expansion counts are not a topology-preservation claim");
        constexpr std::array<geo::index_t, 3> kSourceCorners{0, 1, 2};
        Require(std::ranges::equal(output.output->CornerSourceVertices(),
                                   kSourceCorners),
                "Literal emitted-corner correspondence is complete");
        Require(output.output->Facts().source_encoding == encoding &&
                    output.output->Facts().encoding == destination &&
                    output.output->Facts().metadata_policy ==
                        (destination == encoding
                             ? ic::StlMetadataPolicy::kRetainedSourceMetadata
                             : ic::StlMetadataPolicy::kReplacedSourceMetadata),
                "Actual encoding and metadata conversion policy are explicit");
        if (destination == ic::StlEncoding::kBinary)
          Require(std::ranges::equal(output.output->Bytes(), binary),
                  "Binary output equals independent packed literal bytes");
        const std::string suffix =
            destination == ic::StlEncoding::kAscii ? "ascii" : "binary";
        const auto path =
            directory.Path() / (source_name + "-to-" + suffix + ".stl");
        WriteNew(path, output.output->Bytes());
        const auto actual_output = ReadFile(path);
        Require(std::ranges::equal(actual_output, output.output->Bytes()),
                "Physical output equals the actual serialized bytes");
        auto reopened = ic::ReadStl(actual_output, destination, ReadLimits(),
                                    Work(), context);
        Require(reopened.document.has_value(),
                "Actual installed output reloads");
        Check(*reopened.document);
        Require(ReadFile(source_path) == actual_source,
                "Source file remains unchanged");
      }
    }
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "All installed example owners and workers release");
    std::cout
        << (unsupported
                ? "Actual ASCII/binary numerical-profile refusals verified\n"
                : "ASCII/binary STL read, inspect, write and physical reload "
                  "passed; units unknown, no welding or repair performed\n");
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
