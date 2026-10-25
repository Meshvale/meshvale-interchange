// SPDX-License-Identifier: Apache-2.0
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
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "ply_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::ply_fixture;
namespace fs = std::filesystem;
using f::Require;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::string_view kReceiptName = "verified.json";
constexpr std::string_view kReceipt =
    "{\"fixture\":\"typed-ply\",\"verified\":true}\n";

class Root {
 public:
  Root() {
    static std::atomic<std::uint64_t> sequence{0};
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
      const auto tick =
          std::chrono::steady_clock::now().time_since_epoch().count();
      const auto candidate = fs::temp_directory_path() /
                             ("meshvale-ply-files-" + std::to_string(tick) +
                              "-" + std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(candidate, error)) {
        path_ = fs::canonical(candidate);
        return;
      }
    }
    Require(false, "exclusive owned fixture root");
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

std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  Require(input.good(), "physical fixture opens");
  const auto size = input.tellg();
  Require(size >= 0 && size <= static_cast<std::streamoff>(4 * kMiB),
          "physical fixture size is bounded");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  Require(input.good() || (bytes.empty() && input.eof()),
          "physical fixture reads completely");
  return bytes;
}
std::vector<std::byte> ReadFixture(ic::PlyEncoding encoding) {
  const auto directory = fs::path(MESHVALE_PLY_FILES_TEST_DATA_DIR);
  if (encoding == ic::PlyEncoding::kAscii) return Read(directory / "ascii.ply");
  Require(encoding == ic::PlyEncoding::kBinaryLittleEndian ||
              encoding == ic::PlyEncoding::kBinaryBigEndian,
          "known binary fixture encoding");
  const auto word = std::string(f::EncodingWord(encoding));
  const auto text = Read(directory / (word + ".ply.hex"));
  Require(text.size() <= 2 * kMiB, "bounded fixture hex source");
  std::vector<std::byte> bytes;
  bytes.reserve(text.size() / 2);
  std::optional<unsigned char> high;
  constexpr unsigned char kDigitFirst = '0', kDigitLast = '9';
  constexpr unsigned char kLetterFirst = 'a', kLetterLast = 'f';
  constexpr unsigned char kLetterOffset = 10, kNibbleRadix = 16;
  for (const auto value : text) {
    const auto character = std::to_integer<unsigned char>(value);
    if (character == ' ' || character == '\t' || character == '\r' ||
        character == '\n')
      continue;
    Require((character >= kDigitFirst && character <= kDigitLast) ||
                (character >= kLetterFirst && character <= kLetterLast),
            "fixture contains only lowercase hexadecimal and ASCII spacing");
    const auto digit = static_cast<unsigned char>(
        character <= kDigitLast ? character - kDigitFirst
                                : character - kLetterFirst + kLetterOffset);
    if (high) {
      bytes.push_back(
          std::byte{static_cast<unsigned char>(*high * kNibbleRadix + digit)});
      high.reset();
    } else {
      high = digit;
    }
  }
  Require(!high, "fixture hexadecimal has complete byte pairs");
  return bytes;
}
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(output.good(), "owned fixture output opens");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  Require(!output.fail(), "owned fixture output closes after complete write");
}
ic::PlyFileLimits Files() {
  return {kMiB, kMiB, 2 * kMiB, 256, 256, 4096, 64 * kMiB};
}
ic::PlyFileWorkPolicy FileWork() {
  // Deliberately odd byte sizes exercise typed partition alignment.
  return {31, 1, 13, 7, 16};
}
ic::PlyFileReadLimits ReadLimits() { return {Files(), f::ReadLimits()}; }
ic::PlyFileReadWorkPolicy ReadWork() { return {FileWork(), f::Work()}; }
ic::PlyFilePublicationLimits PublishLimits() {
  return {Files(), {65536, 8 * kMiB}, f::ReadLimits()};
}
ic::PlyFilePublicationWorkPolicy PublishWork() {
  return {FileWork(), f::Work().geometry, f::Work()};
}
void Joined(const ic::PlyFileExecution& execution) {
  for (const auto* stats :
       {&execution.capture, &execution.copying, &execution.byte_verification,
        &execution.mesh_verification, &execution.expected_inspection,
        &execution.ply.codec, &execution.ply.materialization,
        &execution.ply.inspection})
    Require(stats->launched_workers == stats->joined_workers,
            "every actual worker joins before public return");
}
void Inventory(const fs::path& destination, std::string_view entry) {
  std::vector<std::string> names;
  for (const auto& member : fs::directory_iterator(destination)) {
    Require(member.is_regular_file() && !member.is_symlink(),
            "published members are flat regular files");
    names.push_back(member.path().filename().string());
  }
  std::sort(names.begin(), names.end());
  std::vector<std::string> expected{std::string(entry),
                                    std::string(kReceiptName)};
  std::sort(expected.begin(), expected.end());
  Require(names == expected, "complete physical entry/receipt inventory");
}
struct Provider {
  f::Shape shape;
  ic::PlyEncoding encoding;
  unsigned calls = 0;
  static std::span<const std::byte> Provide(
      const ic::PlyFileVerifiedContent& content, void* state) {
    auto& self = *static_cast<Provider*>(state);
    ++self.calls;
    f::Check(content.document, self.shape);
    Require(content.document.Encoding() == self.encoding,
            "provider sees actual requested staged encoding");
    Require(std::ranges::equal(content.entry_bytes,
                               content.document.OriginalBytes()),
            "provider entry bytes alias actual checked document bytes");
    return f::Bytes(kReceipt);
  }
};

void Chains() {
  Root root;
  std::size_t completed = 0;
  for (const auto source_encoding : f::kEncodings) {
    for (const bool changed : {false, true}) {
      geo::ExecutionContext context(geo::ExecutionOptions{4, 1, 256 * kMiB});
      {
        const auto word = std::string(f::EncodingWord(source_encoding));
        const auto input_name = word + ".ply";
        const auto input = root.Path() / input_name;
        const auto original = ReadFixture(source_encoding);
        Write(input, original);
        auto captured =
            ic::ReadPlyFileAsset(input, ReadLimits(), ReadWork(), context);
        Require(captured.asset.has_value(),
                "actual typed file capture succeeds");
        Joined(captured.execution);
        auto asset = *captured.asset;
        Require(std::ranges::equal(asset.EntryBytes(), original),
                "all exact original mixed-line-ending/binary bytes retained");
        Require(fs::remove(input), "physical source removed after capture");
        geo::MaterializedMesh expected = asset.Document().Mesh();
        std::optional<geo::MaterializedFaceSelection> selection;
        if (changed) {
          const auto result = geo::SelectMeshFaces(
              expected, f::kSourceFaces, f::MeshLimits(), f::Work().geometry);
          Require(result.selection.has_value(),
                  "explicit Geometry selection, no Repair dependency");
          selection = *result.selection;
          expected = selection->Candidate();
          Require(
              std::ranges::equal(selection->SourceFaces(), f::kSourceFaces) &&
                  std::ranges::equal(selection->SourceCorners(),
                                     f::kSourceCorners),
              "actual Geometry inverse rows match independent literal "
              "selection");
        }
        const f::Shape shape{false, changed};
        f::CheckMesh(expected, shape);
        for (const auto output_encoding : f::kEncodings) {
          const auto written = ic::WritePly(expected, asset.Document(),
                                            f::Options(output_encoding),
                                            f::WriteLimits(), f::Work());
          Require(written.output.has_value(),
                  "real schema-bound canonical output");
          f::CheckOutput(*written.output, source_encoding, output_encoding, {},
                         shape);
          Provider provider{shape, output_encoding};
          const auto destination =
              root.Path() / (word + (changed ? "-changed-" : "-unchanged-") +
                             std::string(f::EncodingWord(output_encoding)));
          const ic::PlyFilePublicationOptions options{
              ic::PlyFileReceiptProvider{kReceiptName, Provider::Provide,
                                         &provider}};
          const auto published = ic::PublishPlyBundle(
              {asset, expected, *written.output}, destination, options,
              PublishLimits(), PublishWork());
          Require(
              published.outcome == ic::PlyFilePublicationOutcome::kPublished &&
                  published.bundle.has_value() && provider.calls == 1,
              "verified typed/schema publication commits once after provider");
          Joined(published.execution);
          Inventory(destination, input_name);
          const auto physical = Read(destination / input_name);
          Require(
              std::ranges::equal(physical, written.output->Bytes()),
              "physical entry equals genuine supplied output byte for byte");
          const auto receipt = Read(destination / std::string(kReceiptName));
          Require(std::ranges::equal(receipt, f::Bytes(kReceipt)),
                  "physical receipt equals actual provider bytes");
          const auto reopened =
              ic::ReadPly(physical, f::ReadLimits(), f::Work(), context);
          Require(reopened.document.has_value(),
                  "independent physical reload succeeds");
          f::Check(*reopened.document, shape);
          Require(reopened.document->Encoding() == output_encoding,
                  "physical encoding matches explicit request");
          ++completed;
        }
      }
      Require(
          context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "all tracked owner payload and workers released");
    }
  }
  Require(completed == 18, "all eighteen physical encoding/selection chains");
}
enum class Mutation {
  kScalarInt,
  kDoubleSignedZero,
  kDoubleSubnormal,
  kListValue,
  kListBoundaries,
  kUnusedPositionSignedZero
};
geo::MaterializedMesh WrongMesh(const ic::PlyDocument& document,
                                const geo::ExecutionContext& context,
                                Mutation mutation) {
  const auto view = document.Mesh().View();
  std::vector<double> positions(view.positions.begin(), view.positions.end());
  auto integers = f::kI8;
  auto samples = f::kSamples;
  auto offsets = f::kSampleOffsets;
  std::array<double, 8> doubles;
  for (std::size_t i = 0; i < doubles.size(); ++i)
    doubles[i] = std::bit_cast<double>(f::kDoubleWords[i]);
  if (mutation == Mutation::kScalarInt) integers[0] = -127;
  if (mutation == Mutation::kDoubleSignedZero) doubles[1] = 0.;
  if (mutation == Mutation::kDoubleSubnormal) doubles[2] = 0.;
  if (mutation == Mutation::kListValue) samples.back() = 32766;
  if (mutation == Mutation::kListBoundaries) offsets[1] = 1;
  constexpr std::size_t kUnusedVertexY = 22;
  if (mutation == Mutation::kUnusedPositionSignedZero)
    positions[kUnusedVertexY] = 0.;
  std::vector<geo::AttributeMaterializationInput> attributes;
  for (const auto& attribute : view.attributes) {
    geo::AttributeMaterializationInput input;
    input.domain = attribute.domain;
    input.name = attribute.name;
    input.semantic = attribute.semantic;
    input.set_index = attribute.set_index;
    input.components = attribute.components;
    input.offsets = attribute.offsets;
    input.present = attribute.present;
    input.metadata = attribute.metadata;
    std::visit([&](auto values) { input.values = values; }, attribute.values);
    if (attribute.name == "sample_i8")
      input.values = std::span<const std::int32_t>(integers);
    if (attribute.name == "sample_f64")
      input.values = std::span<const double>(doubles);
    if (attribute.name == "samples") {
      input.values = std::span<const std::int32_t>(samples);
      input.offsets = std::span<const geo::index_t>(offsets);
    }
    attributes.push_back(input);
  }
  const auto result = geo::MaterializeMesh(
      {std::span<const double>(positions),
       geo::PolygonTopologyInput{view.face_offsets, view.corner_vertices},
       attributes},
      f::MeshLimits(), f::Work().geometry, context);
  Require(result.mesh.has_value(), "genuine valid alternate strong mesh");
  return *result.mesh;
}
ic::PlyDocument ReadAlternate(std::span<const std::byte> bytes,
                              const geo::ExecutionContext& context) {
  const auto result = ic::ReadPly(bytes, f::ReadLimits(), f::Work(), context);
  Require(result.document.has_value(), "genuine alternate typed schema source");
  return *result.document;
}
void RefuseOutput(const Root& root, std::string_view name,
                  const ic::PlyFileAsset& source,
                  const geo::MaterializedMesh& expected,
                  const ic::PlyOutput& output) {
  const auto destination = root.Path() / std::string(name);
  Provider provider{{}, ic::PlyEncoding::kAscii};
  const ic::PlyFilePublicationOptions options{
      ic::PlyFileReceiptProvider{kReceiptName, Provider::Provide, &provider}};
  const auto result =
      ic::PublishPlyBundle({source, expected, output}, destination, options,
                           PublishLimits(), PublishWork());
  Require(
      result.outcome == ic::PlyFilePublicationOutcome::kFailed &&
          !result.bundle &&
          result.diagnostic.status ==
              ic::PlyFileStatus::kPreservationMismatch &&
          provider.calls == 0 && !fs::exists(destination),
      "genuine valid but incompatible output refuses before provider/commit");
  Require(result.cleanup != ic::PlyFileCleanupOutcome::kFailed,
          "owned staged mismatch cleans without replacing primary diagnostic");
  Joined(result.execution);
}
void GenuineMismatches() {
  Root root;
  geo::ExecutionContext context({4, 1, 256 * kMiB});
  const auto original =
      Read(fs::path(MESHVALE_PLY_FILES_TEST_DATA_DIR) / "ascii.ply");
  const auto path = root.Path() / "source.ply";
  Write(path, original);
  const auto captured =
      ic::ReadPlyFileAsset(path, ReadLimits(), ReadWork(), context);
  Require(captured.asset.has_value(),
          "actual source for incompatible-output controls");
  const auto& asset = *captured.asset;
  const auto& expected = asset.Document().Mesh();
  const std::array mutations{
      Mutation::kScalarInt,       Mutation::kDoubleSignedZero,
      Mutation::kDoubleSubnormal, Mutation::kListValue,
      Mutation::kListBoundaries,  Mutation::kUnusedPositionSignedZero};
  std::size_t controls = 0;
  for (const auto mutation : mutations) {
    const auto wrong = WrongMesh(asset.Document(), context, mutation);
    const auto written = ic::WritePly(wrong, asset.Document(),
                                      f::Options(ic::PlyEncoding::kAscii),
                                      f::WriteLimits(), f::Work());
    Require(written.output.has_value(),
            "wrong values still produce genuine valid writer output");
    RefuseOutput(root, "wrong-value-" + std::to_string(controls), asset,
                 expected, *written.output);
    ++controls;
  }
  const std::string ascii(f::Text(original));
  {
    const auto alternate_wire = f::Replace(ascii, "property char sample_i8\n",
                                           "property int sample_i8\n");
    const auto alternate = ReadAlternate(f::Bytes(alternate_wire), context);
    f::CheckMesh(alternate.Mesh());
    const auto written =
        ic::WritePly(expected, alternate, f::Options(ic::PlyEncoding::kAscii),
                     f::WriteLimits(), f::Work());
    Require(written.output.has_value(),
            "different char/int wire schema, same canonical int32");
    RefuseOutput(root, "wrong-wire-width", asset, expected, *written.output);
    ++controls;
  }
  {
    auto wire = f::Replace(ascii, "property char sample_i8\n",
                           "property list uchar char sample_i8\n");
    auto offset =
        wire.find("end_header\n") + std::string_view("end_header\n").size();
    Require(offset < wire.size(), "literal alternate body boundary");
    for (unsigned vertex = 0; vertex < 8; ++vertex) {
      wire.insert(offset, "1 ");
      offset = wire.find('\n', offset) + 1;
    }
    const auto alternate = ReadAlternate(f::Bytes(wire), context);
    const auto written = ic::WritePly(alternate.Mesh(), alternate,
                                      f::Options(ic::PlyEncoding::kAscii),
                                      f::WriteLimits(), f::Work());
    Require(written.output.has_value(),
            "same flat values but genuine dense/list binding difference");
    RefuseOutput(root, "wrong-dense-list", asset, expected, *written.output);
    ++controls;
  }
  {
    const auto selected = geo::SelectMeshFaces(
        expected, f::kSourceFaces, f::MeshLimits(), f::Work().geometry);
    Require(selected.selection.has_value(), "explicit changed expected mesh");
    const auto alternate = ReadAlternate(
        f::Wire(ic::PlyEncoding::kAscii, f::Shape{false, true}), context);
    const auto written = ic::WritePly(
        selected.selection->Candidate(), alternate,
        f::Options(ic::PlyEncoding::kAscii), f::WriteLimits(), f::Work());
    Require(written.output.has_value() &&
                written.output->Facts().source_face_count == 2,
            "valid current output with genuine wrong original face provenance");
    RefuseOutput(root, "wrong-original-faces", asset,
                 selected.selection->Candidate(), *written.output);
    ++controls;
  }
  {
    auto wire = f::Replace(ascii, "element vertex 8\n", "element vertex 7\n");
    auto offset =
        wire.find("end_header\n") + std::string_view("end_header\n").size();
    for (unsigned vertex = 0; vertex < 7; ++vertex)
      offset = wire.find('\n', offset) + 1;
    const auto end = wire.find('\n', offset);
    Require(end != std::string::npos, "literal unused vertex row exists");
    wire.erase(offset, end + 1 - offset);
    const auto alternate = ReadAlternate(f::Bytes(wire), context);
    const auto written =
        ic::WritePly(expected, alternate, f::Options(ic::PlyEncoding::kAscii),
                     f::WriteLimits(), f::Work());
    Require(written.output.has_value() &&
                written.output->Facts().source_vertex_count == 7,
            "valid eight-vertex output with wrong original seven-vertex "
            "provenance");
    RefuseOutput(root, "wrong-original-vertices", asset, expected,
                 *written.output);
    ++controls;
  }
  {
    const auto alternate =
        ReadAlternate(f::Wire(ic::PlyEncoding::kBinaryBigEndian), context);
    const auto written =
        ic::WritePly(expected, alternate, f::Options(ic::PlyEncoding::kAscii),
                     f::WriteLimits(), f::Work());
    Require(written.output.has_value(),
            "valid output with different source encoding provenance");
    RefuseOutput(root, "wrong-source-encoding", asset, expected,
                 *written.output);
    ++controls;
  }
  Require(controls == 11 && std::ranges::equal(Read(path), original),
          "eleven genuine mismatch controls preserve original physical source");
}

void ProfileControls(bool unsupported) {
  Root root;
  geo::ExecutionContext context(geo::ExecutionOptions{4, 1, 256 * kMiB});
  for (const auto encoding : f::kEncodings) {
    for (const bool point_only : {false, true}) {
      const auto input = root.Path() / "nonempty.ply";
      const f::Shape shape{false, false, false, point_only};
      Write(input, f::Wire(encoding, shape));
      const auto result =
          ic::ReadPlyFileAsset(input, ReadLimits(), ReadWork(), context);
      Joined(result.execution);
      if (unsupported) {
        Require(
            result.status == ic::PlyFileStatus::kPlyFailure && !result.asset &&
                result.diagnostic.ply_status ==
                    ic::PlyStatus::kGeometryFailure &&
                result.diagnostic.ply_diagnostic &&
                result.diagnostic.ply_diagnostic->geometry_status ==
                    geo::MaterializationStatus::kUnsupportedAllocationProfile,
            "actual nonempty/point-only unsupported profile is a refusal");
      } else {
        Require(result.asset.has_value(),
                "qualified nonempty/point-only capture");
        f::Check(result.asset->Document(), shape);
      }
    }
    {
      const f::Shape empty{false, false, true};
      const auto input = root.Path() / "empty.ply";
      Write(input, f::Wire(encoding, empty));
      const auto result =
          ic::ReadPlyFileAsset(input, ReadLimits(), ReadWork(), context);
      Require(result.asset.has_value(),
              "genuine empty file capture is not skipped");
      Joined(result.execution);
      f::Check(result.asset->Document(), empty);
      for (const auto destination_encoding : f::kEncodings) {
        const auto written = ic::WritePly(
            result.asset->Document().Mesh(), result.asset->Document(),
            f::Options(destination_encoding), f::WriteLimits(), f::Work());
        Require(written.output.has_value(), "genuine empty canonical writer");
        f::CheckOutput(*written.output, encoding, destination_encoding, empty,
                       empty);
        const auto destination =
            root.Path() / (std::string(f::EncodingWord(encoding)) + "-empty-" +
                           std::string(f::EncodingWord(destination_encoding)));
        const auto published = ic::PublishPlyBundle(
            {*result.asset, result.asset->Document().Mesh(), *written.output},
            destination, {}, PublishLimits(), PublishWork());
        Require(
            published.outcome == ic::PlyFilePublicationOutcome::kPublished &&
                published.bundle.has_value(),
            "actual empty publication/reload works on the common profile");
        Joined(published.execution);
        f::Check(published.bundle->Document(), empty);
        Require(std::ranges::equal(Read(destination / "empty.ply"),
                                   written.output->Bytes()),
                "empty physical output equals actual encoded bytes");
      }
    }
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "profile controls release every owner and worker");
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "known selected-profile argument only");
    ProfileControls(unsupported);
    if (!unsupported) {
      Chains();
      GenuineMismatches();
    }
    std::cout
        << "PLY file actual profile controls and typed/schema chains passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
