// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/surface_files.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <jsoncons/json.hpp>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "surface_files_fixture.h"

namespace {
namespace mv = meshvale::interchange;
namespace geom = meshvale::geometry;
namespace fixture = mv::surface_fixture;
namespace test = mv::surface_files_fixture;
namespace fs = std::filesystem;
using test::Require;
using Json = jsoncons::json;

// Independent test-owned SHA-256, including its own standard-vector checks.
// No production hash routine or reported digest supplies an expected value.
mv::SurfaceDigest Sha256(std::span<const std::byte> input) {
  constexpr std::array<std::uint32_t, 64> kSha256RoundConstants{
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                 0xa54ff53a, 0x510e527f, 0x9b05688c,
                                 0x1f83d9ab, 0x5be0cd19};
  std::vector<std::byte> padded(input.begin(), input.end());
  padded.push_back(std::byte{0x80});
  while (padded.size() % 64 != 56) padded.push_back(std::byte{0});
  const auto bits = static_cast<std::uint64_t>(input.size()) * 8;
  for (unsigned i = 8; i != 0; --i)
    padded.push_back(static_cast<std::byte>((bits >> ((i - 1) * 8)) & 255));
  for (std::size_t block = 0; block < padded.size(); block += 64) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i)
      for (std::size_t j = 0; j < 4; ++j)
        w[i] = (w[i] << 8) |
               std::to_integer<std::uint32_t>(padded[block + 4 * i + j]);
    for (std::size_t i = 16; i < w.size(); ++i) {
      const auto s0 =
          std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const auto s1 =
          std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6],
         z = h[7];
    for (std::size_t i = 0; i < w.size(); ++i) {
      const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto choose = (e & f) ^ (~e & g);
      const auto t1 = z + s1 + choose + kSha256RoundConstants[i] + w[i];
      const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      z = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + s0 + majority;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += z;
  }
  mv::SurfaceDigest result{};
  for (std::size_t i = 0; i < h.size(); ++i)
    for (std::size_t j = 0; j < 4; ++j)
      result[4 * i + j] = static_cast<std::byte>((h[i] >> ((3 - j) * 8)) & 255);
  return result;
}
std::string Hex(std::span<const std::byte> bytes) {
  constexpr std::string_view kHexDigits = "0123456789abcdef";
  std::string result;
  for (auto b : bytes) {
    const auto n = std::to_integer<unsigned>(b);
    result.push_back(kHexDigits[n >> 4]);
    result.push_back(kHexDigits[n & 15]);
  }
  return result;
}
void HashVectors() {
  Require(
      Hex(Sha256({})) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "independent SHA-256 empty standard vector");
  constexpr std::string_view kAbcMessage = "abc";
  Require(
      Hex(Sha256(
          std::as_bytes(std::span{kAbcMessage.data(), kAbcMessage.size()}))) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "independent SHA-256 abc standard vector");
  constexpr std::string_view kTwoBlockMessage =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  Require(
      Hex(Sha256(std::as_bytes(
          std::span{kTwoBlockMessage.data(), kTwoBlockMessage.size()}))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
      "independent SHA-256 two-block standard vector");
}
void Keys(const Json& object,
          std::initializer_list<std::string_view> expected) {
  Require(object.is_object() && object.size() == expected.size(),
          "closed receipt object");
  for (auto key : expected)
    Require(object.contains(key), "required receipt key");
}
std::uint64_t U64(const Json& value) {
  Require(value.is_string(), "receipt u64 is a string");
  const auto text = value.as<std::string>();
  std::uint64_t number = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), number);
  Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
              std::to_string(number) == text,
          "canonical exact receipt u64");
  return number;
}
std::string_view Encoding(mv::SurfaceEncoding encoding) {
  switch (encoding) {
    case mv::SurfaceEncoding::kGltfJson:
      return "gltf-json";
    case mv::SurfaceEncoding::kGlb:
      return "glb";
    case mv::SurfaceEncoding::kStlAscii:
      return "stl-ascii";
    case mv::SurfaceEncoding::kStlBinary:
      return "stl-binary";
    default:
      throw std::runtime_error("unknown test encoding");
  }
}
std::string_view Kind(mv::SurfaceMemberKind kind) {
  switch (kind) {
    case mv::SurfaceMemberKind::kGltf:
      return "gltf";
    case mv::SurfaceMemberKind::kGlb:
      return "glb";
    case mv::SurfaceMemberKind::kStl:
      return "stl";
    case mv::SurfaceMemberKind::kGeometryBuffer:
      return "geometry-buffer";
    case mv::SurfaceMemberKind::kImage:
      return "image";
    case mv::SurfaceMemberKind::kCapsuleJson:
      return "capsule-json";
    case mv::SurfaceMemberKind::kCapsuleBinary:
      return "capsule-binary";
  }
  throw std::runtime_error("unknown test member kind");
}
void Receipt(const mv::SurfaceOutputInspection& input,
             const mv::SurfacePublishedBundle& owner,
             const fs::path& destination,
             const std::optional<std::string>& report) {
  const auto receipt = owner.Receipt();
  const auto raw = test::Read(destination / receipt.path);
  Require(
      !raw.empty() && !(raw.size() >= 3 && raw[0] == std::byte{0xef} &&
                        raw[1] == std::byte{0xbb} && raw[2] == std::byte{0xbf}),
      "BOM-free JSON receipt bytes");
  Require(
      std::ranges::equal(raw, receipt.bytes) && Sha256(raw) == receipt.sha256,
      "receipt view matches independent disk bytes and SHA-256");
  const std::string text(reinterpret_cast<const char*>(raw.data()), raw.size());
  const auto json = Json::parse(text);
  Keys(json, {"schema", "phase", "encoding", "representation", "entry_path",
              "payload_members", "observations", "caller_report"});
  Require(
      json.at("schema").as<std::string>() == "meshvale.surface-publication/1" &&
          json.at("phase").as<std::string>() == "staged-payload-verified",
      "literal neutral precommit receipt identity");
  const auto& expected = input.Expected();
  Require(
      json.at("encoding").as<std::string>() == Encoding(expected.Encoding()) &&
          json.at("representation").as<std::string>() ==
              (expected.Representation() ==
                       mv::SurfaceRepresentation::kStrictBare
                   ? "strict-bare"
                   : "annotated-bundle-v1"),
      "literal receipt format");
  std::map<std::string, mv::SurfaceMemberView> payload;
  std::uint64_t total = 0;
  for (const auto& member : input.CapturedMembers()) {
    payload.emplace(std::string(member.path), member);
    total += member.bytes.size();
  }
  const auto disk = test::Inventory(destination);
  Require(disk.size() == payload.size() + 1 &&
              disk.contains(std::string(receipt.path)),
          "closed physical payload plus exactly one receipt");
  const auto& members = json.at("payload_members");
  Require(members.is_array() && members.size() == payload.size(),
          "closed receipt payload list");
  std::size_t i = 0;
  std::string entry;
  for (const auto& [path, member] : payload) {
    const auto& row = members.at(i++);
    Keys(row, {"kind", "path", "byte_length", "sha256"});
    Require(
        row.at("path").as<std::string>() == path &&
            row.at("kind").as<std::string>() == Kind(member.kind) &&
            U64(row.at("byte_length")) == member.bytes.size() &&
            row.at("sha256").as<std::string>() == Hex(Sha256(disk.at(path))) &&
            std::ranges::equal(disk.at(path), member.bytes),
        "sorted actual kind/path/length/digest and byte-identical physical "
        "payload");
    if (member.kind == mv::SurfaceMemberKind::kGltf ||
        member.kind == mv::SurfaceMemberKind::kGlb ||
        member.kind == mv::SurfaceMemberKind::kStl) {
      Require(entry.empty(), "one actual entry member");
      entry = path;
    }
  }
  Require(json.at("entry_path").as<std::string>() == entry,
          "actual entry path");
  const auto& observations = json.at("observations");
  Keys(observations, {"payload_write", "payload_reread", "fresh_inspection"});
  const auto& write = observations.at("payload_write");
  Keys(write, {"status", "bytes_written", "closed_members"});
  Require(write.at("status").as<std::string>() == "complete" &&
              U64(write.at("bytes_written")) == total &&
              U64(write.at("closed_members")) == payload.size(),
          "receipt actual completed payload writes only");
  const auto& reread = observations.at("payload_reread");
  Keys(reread, {"status", "bytes_read", "members"});
  Require(reread.at("status").as<std::string>() == "complete" &&
              U64(reread.at("bytes_read")) == total &&
              U64(reread.at("members")) == payload.size(),
          "receipt actual completed payload rereads only");
  const auto& inspected = observations.at("fresh_inspection");
  Keys(inspected, {"status", "members"});
  Require(inspected.at("status").as<std::string>() == "success" &&
              U64(inspected.at("members")) == payload.size(),
          "receipt actual fresh inspection");
  const auto& caller = json.at("caller_report");
  if (!report)
    Require(caller.is_null(), "disabled provider is null, not empty report");
  else {
    Keys(caller, {"encoding", "byte_length", "sha256", "text"});
    Require(caller.at("encoding").as<std::string>() == "utf-8" &&
                U64(caller.at("byte_length")) == report->size() &&
                caller.at("text").as<std::string>() == *report &&
                caller.at("sha256").as<std::string>() ==
                    Hex(Sha256(std::as_bytes(
                        std::span{report->data(), report->size()}))),
            "uninterpreted UTF-8 report, embedded NUL roundtrip and "
            "original-byte hash");
  }
}
void PhaseFacts(const mv::SurfaceFilePublicationResult& result,
                std::uint64_t payload_bytes, std::uint64_t member_count,
                bool provider) {
  const auto& f = result.facts;
  Require(f.payload_write && f.payload_inventory && f.payload_reread &&
              f.inspection && f.receipt_write && f.receipt_reread &&
              f.final_inventory && f.final_payload_verification &&
              f.final_receipt_verification,
          "success records every actually reached physical phase");
  Require(
      f.payload_write->bytes_written == payload_bytes &&
          f.payload_write->completed_files == member_count &&
          f.payload_write->closed_files >= f.payload_write->completed_files &&
          f.payload_reread->bytes_read == payload_bytes &&
          f.final_payload_verification->bytes_compared == payload_bytes &&
          f.inspection->status == mv::SurfaceStatus::kSuccess &&
          f.inspection->facts,
      "actual byte counters and independent fresh V facts");
  Require(f.provider.has_value() == provider &&
              f.report_import.has_value() == provider,
          "disabled provider/import phases remain absent");
  for (const auto* io :
       {&*f.payload_write, &*f.payload_reread, &*f.receipt_write,
        &*f.receipt_reread, &*f.final_payload_verification,
        &*f.final_receipt_verification})
    Require(
        io->outcome == mv::SurfaceFilePhaseOutcome::kCompleted &&
            io->execution.launched_workers == io->execution.joined_workers &&
            io->execution.completed_jobs == io->execution.planned_jobs,
        "completed physical phases report actual joined jobs");
}
struct ReportState {
  std::string text;
  const fs::path* destination = nullptr;
  std::size_t calls = 0;
};
std::string CallerReportText() {
  std::string report = "quoted";
  report.push_back('"');
  report.push_back('\\');
  report += "line";
  report.push_back('\n');
  report.push_back('\0');
  report += "UTF-8 \xc3\xa9";
  constexpr std::array<unsigned char, 22> kExpectedReportBytes{
      0x71, 0x75, 0x6f, 0x74, 0x65, 0x64, 0x22, 0x5c, 0x6c, 0x69, 0x6e,
      0x65, 0x0a, 0x00, 0x55, 0x54, 0x46, 0x2d, 0x38, 0x20, 0xc3, 0xa9};
  Require(
      std::ranges::equal(std::as_bytes(std::span{report.data(), report.size()}),
                         std::as_bytes(std::span{kExpectedReportBytes})),
      "independent quote/backslash/newline/NUL/UTF-8 report precondition");
  return report;
}
std::span<const std::byte> Report(const mv::SurfaceFileVerifiedContent& content,
                                  void* opaque) {
  auto& state = *static_cast<ReportState*>(opaque);
  Require(!fs::exists(*state.destination),
          "provider runs before native destination commit");
  Require(!content.entry_path.empty() &&
              !content.inspection.CapturedMembers().empty(),
          "provider receives fresh physical inspection, not synthetic flags");
  ++state.calls;
  return std::as_bytes(std::span{state.text.data(), state.text.size()});
}
void Example(mv::SurfaceEncoding encoding,
             mv::SurfaceRepresentation representation, test::Shape shape,
             const geom::ExecutionContext& context, fixture::Variant variant,
             fixture::MetadataRoute route,
             std::optional<std::string> report = std::nullopt) {
  test::TempTree tree;
  const auto destination = tree.root / "published";
  const auto proof =
      test::MakeProof(encoding, representation, shape, context, variant, route);
  if (!proof.inspection)
    return;  // An asserted actual upstream profile/normal refusal.
  const auto& input = *proof.inspection;
  std::uint64_t bytes = 0;
  for (const auto& member : input.CapturedMembers())
    bytes += member.bytes.size();
  ReportState state{report.value_or(""), &destination, 0};
  mv::SurfaceFilePublicationOptions options;
  options.receipt_path = "receipts/publication.json";
  if (report) options.receipt = {Report, &state};
  auto result = mv::PublishSurfaceBundle(input, destination, options,
                                         test::FileLimits(encoding),
                                         test::FileWork(encoding));
  test::Published(result);
  Require(state.calls == (report ? 1u : 0u),
          "provider called exactly once only when enabled");
  Receipt(input, *result.bundle, destination, report);
  PhaseFacts(result, bytes, input.CapturedMembers().size(), report.has_value());
  const auto& fresh = result.bundle->Inspection();
  Require(
      fresh.CapturedMembers().size() == input.CapturedMembers().size() &&
          geom::SharesExecutionBudget(
              fresh.Expected().Original().Source().ShareExecutionContext(),
              input.Expected().Original().Source().ShareExecutionContext()) &&
          geom::SharesExecutionBudget(
              fresh.Expected().Triangles().ShareExecutionContext(),
              input.Expected().Triangles().ShareExecutionContext()),
      "fresh inspection retains genuine source/triangle budget identities");
  const auto numeric = fresh.Facts().PositionNumeric();
  Require(numeric && numeric->complete &&
              numeric->kind == mv::SurfaceNumericSummary::Kind::kDecodedWire &&
              numeric->examined_values == (proof.empty                   ? 0u
                                           : shape == test::Shape::kRich ? 33u
                                                                         : 9u),
          "literal actually decoded coordinate coverage");
  if (!proof.empty && shape == test::Shape::kRich) {
    const auto& actual = fresh.Expected();
    Require(actual.Original().Source().View().attributes.size() == 19 &&
                actual.WireTriangleSources().size() == 6,
            "rich original attributes and six triangles retained");
    std::array<std::uint64_t, 3> face_counts{};
    for (auto face : actual.WireTriangleFaces()) {
      Require(face < face_counts.size(), "literal original face identity");
      ++face_counts[static_cast<std::size_t>(face)];
    }
    Require(face_counts == std::array<std::uint64_t, 3>{1, 2, 3},
            "literal rich first/second/third polygon triangle multiplicities");
    if (!test::Stl(encoding)) {
      Require(actual.UvMapping().size() == 3 &&
                  actual.Primitives().size() == 3 &&
                  actual.MaterialFactors().size() == 3 &&
                  fresh.Facts().UvNumeric().size() == 3,
              "rich UV/material grouping and actual numeric witnesses survive "
              "physical route");
      for (std::size_t i = 0; i < 3; ++i)
        Require(actual.UvMapping()[i].source_attribute == 15 + i &&
                    actual.UvMapping()[i].source_set ==
                        std::array<std::uint32_t, 3>{0, 2, 5}[i] &&
                    actual.UvMapping()[i].wire_set == i,
                "independent sparse public UV identities");
    }
  }
  // Retain aliases after the result and its facts are gone. Removing the
  // physical tree cannot invalidate the module's charged immutable captured
  // owners.
  auto retained = *result.bundle;
  const auto held_receipt = retained.Receipt();
  const auto saved_receipt = std::vector<std::byte>(held_receipt.bytes.begin(),
                                                    held_receipt.bytes.end());
  result = {};
  fs::remove_all(destination);
  Require(std::ranges::equal(retained.Receipt().bytes, saved_receipt) &&
              retained.Inspection().CapturedMembers().size() ==
                  input.CapturedMembers().size() &&
              retained.NewRequestedBytes() != 0 &&
              retained.Inspection().NewRequestedBytes() != 0,
          "published aliases retain independent charged P and V lifetimes");
}
}  // namespace

int main() {
  try {
    HashVectors();
    geom::ExecutionContext context({4, 1, 1024 * test::kMiB});
    for (auto encoding : test::kEncodings) {
      for (auto representation :
           {mv::SurfaceRepresentation::kStrictBare,
            mv::SurfaceRepresentation::kAnnotatedBundleV1}) {
        Example(encoding, representation, test::Shape::kTriangle, context,
                fixture::Variant::kOriginal, fixture::MetadataRoute::kRetained);
        Example(encoding, representation, test::Shape::kEmpty, context,
                fixture::Variant::kOriginal, fixture::MetadataRoute::kRetained);
      }
      for (auto variant :
           {fixture::Variant::kOriginal, fixture::Variant::kTranslated})
        for (auto route : {fixture::MetadataRoute::kRetained,
                           fixture::MetadataRoute::kLegacy})
          Example(encoding, mv::SurfaceRepresentation::kAnnotatedBundleV1,
                  test::Shape::kRich, context, variant, route,
                  CallerReportText());
      const auto strict_rich =
          test::MakeProof(encoding, mv::SurfaceRepresentation::kStrictBare,
                          test::Shape::kRich, context);
      if (!strict_rich.empty)
        Require(!strict_rich.inspection,
                "strict rich refusal provides no publisher proof");
    }
    Example(mv::SurfaceEncoding::kGltfJson,
            mv::SurfaceRepresentation::kStrictBare, test::Shape::kTriangle,
            context, fixture::Variant::kOriginal,
            fixture::MetadataRoute::kRetained, std::string{});
    Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
            "all physical publication/input owners and workers released");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
