// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>
#include <meshvale/interchange/stl_files.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "stl_files_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::stl_files_fixture;
using f::Require;

void Physical(const ic::StlFileRewriteResult& result,
              const std::filesystem::path& destination,
              std::span<const f::Facet> facets,
              ic::StlFileMetadataMode requested, ic::StlMetadataPolicy actual,
              ic::StlEncoding encoding, std::string_view ascii_name,
              std::span<const std::byte> binary_header,
              const geo::ExecutionContext& context) {
  f::Published(result);
  const auto& owner = *result.bundle;
  const auto& verified = owner.Verified();
  Require(
      verified.Document().Encoding() == encoding &&
          verified.Document().AsciiName() == ascii_name &&
          std::ranges::equal(verified.Document().BinaryHeader(), binary_header),
      "fresh encoding and complete metadata match independent requested "
      "values");
  const auto members = f::Inventory(destination);
  Require(members.size() == 2 &&
              members.contains(std::string(verified.EntryPath())) &&
              members.contains(std::string(f::kReceipt)),
          "exact two-file closed inventory");
  const auto bytes = f::Read(destination / std::string(verified.EntryPath()));
  Require(std::ranges::equal(bytes, verified.EntryBytes()) &&
              std::ranges::equal(bytes, result.output->Bytes()) &&
              verified.Sha256() == f::Sha256(bytes),
          "physical bytes and independent complete hash");
  const auto receipt = f::Read(destination / std::string(f::kReceipt));
  Require(std::ranges::equal(receipt, owner.ReceiptBytes()) &&
              owner.ReceiptPath() == f::kReceipt &&
              owner.ReceiptSha256() == f::Sha256(receipt),
          "physical receipt bytes/hash/name");
  const auto read = ic::ReadStl(bytes, verified.Document().Encoding(),
                                f::CodecReadLimits(), f::CodecWork(), context);
  Require(read.document && read.status == ic::StlStatus::kSuccess,
          "actual independent fresh ReadStl from committed physical bytes");
  f::CheckDocument(*read.document, facets);
  f::CheckDocument(verified.Document(), facets);
  if (read.document->Encoding() == ic::StlEncoding::kBinary)
    f::CheckBinary(bytes, facets, verified.Document().BinaryHeader());
  else
    f::CheckAscii(bytes, facets, ascii_name);
  const auto& facts = result.output->Facts();
  Require(facts.encoding == encoding &&
              facts.source_encoding == result.source.Document().Encoding() &&
              facts.metadata_policy == actual &&
              facts.face_normal_attribute == 0 &&
              facts.corner_expansion_acknowledged &&
              facts.source_vertex_count == facets.size() * 3 &&
              facts.source_face_count == facets.size(),
          "complete genuine writer facts");
  const auto map = result.output->CornerSourceVertices();
  Require(map.size() == facets.size() * 3,
          "complete writer correspondence map extent");
  for (std::size_t i = 0; i != map.size(); ++i)
    Require(map[i] == i, "writer source corner map is complete soup identity");
  const auto verification = std::get<ic::StlFileVerificationFacts>(
      result.observations.verification.facts);
  Require(verification.position_bits && verification.raw_normal_bits &&
              verification.facet_order && verification.corner_order &&
              verification.soup_identity && verification.metadata &&
              verification.exact_bytes,
          "all separately reached verification facts");
  const auto inventory =
      std::get<ic::StlFileInventoryFacts>(result.observations.inventory.facts);
  Require(inventory.entries[0].entry < inventory.entries[1].entry,
          "inventory facts sorted by portable basename");
  for (const auto& member : inventory.entries) {
    const auto& physical = members.at(std::string(member.entry));
    Require(
        member.bytes == physical.size() && member.sha256 == f::Sha256(physical),
        "each inventory fact matches independently reopened bytes");
  }
  f::CheckReceipt(owner, requested, actual, facets.size());
}

void RoundTrip(std::span<const std::byte> bytes, ic::StlEncoding encoding,
               std::span<const f::Facet> facets, bool unsupported) {
  f::TempTree tree;
  const auto input = tree.root / "part.stl";
  f::Write(input, bytes);
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  {
    const auto capture =
        ic::CaptureStlFile(input, f::FileLimits(), f::FileWork(), context);
    if (capture.status != ic::StlFileStatus::kSuccess || !capture.captured)
      f::PrintReadFailure(capture, input);
    Require(capture.status == ic::StlFileStatus::kSuccess && capture.captured,
            "genuine native regular-file capture");
    Require(
        capture.captured->EntryPath() == "part.stl" &&
            std::ranges::equal(capture.captured->EntryBytes(), bytes) &&
            capture.captured->Sha256() == f::Sha256(bytes) &&
            geo::SharesExecutionBudget(
                context, capture.captured->ShareExecutionContext()),
        "complete capture bytes/hash/basename and original execution ledger");
    const auto captured_charge = context.ActiveTrackedPayload();
    const auto decoded = ic::DecodeStlFile(
        *capture.captured, encoding, f::CodecReadLimits(), f::CodecWork());
    if (!facets.empty() && f::Unsupported(decoded, unsupported)) return;
    Require(
        decoded.asset && !decoded.captured &&
            decoded.status == ic::StlFileStatus::kSuccess,
        "successful decode exposes asset and releases result capture payload");
    const auto& source = *decoded.asset;
    f::CheckDocument(source.Document(), facets);
    const auto expected_name =
        encoding == ic::StlEncoding::kAscii
            ? (facets.empty() ? std::string_view{} : f::kName)
            : std::string_view{};
    const auto header = f::Header();
    const auto expected_header = encoding == ic::StlEncoding::kBinary
                                     ? std::span<const std::byte>(header)
                                     : std::span<const std::byte>{};
    Require(source.Document().Encoding() == encoding &&
                source.Document().AsciiName() == expected_name &&
                std::ranges::equal(source.Document().BinaryHeader(),
                                   expected_header),
            "literal source encoding and complete opaque/name metadata");
    Require(source.EntryBytes().data() ==
                    source.Document().OriginalBytes().data() &&
                source.Sha256() == f::Sha256(bytes) &&
                context.ActiveTrackedPayload() > captured_charge &&
                geo::SharesExecutionBudget(
                    context, source.Document().Mesh().ShareExecutionContext()),
            "asset aliases actual codec bytes; explicitly retained capture "
            "overlaps charge");
    const auto source_before = f::Read(input);
    const auto result =
        ic::RewriteStlFile(source, tree.root / "retained", f::Retain(encoding),
                           f::RewriteLimits(), f::RewriteWork());
    Physical(result, tree.root / "retained", facets,
             ic::StlFileMetadataMode::kRetain,
             ic::StlMetadataPolicy::kRetainedSourceMetadata, encoding,
             expected_name, expected_header, context);
    Require(f::Read(input) == source_before,
            "rewrite leaves original source bytes unchanged");
    auto replaced = f::Retain(encoding);
    replaced.metadata_mode = ic::StlFileMetadataMode::kReplace;
    if (encoding == ic::StlEncoding::kAscii)
      replaced.ascii_name = source.Document().AsciiName();
    else
      replaced.binary_header = source.Document().BinaryHeader();
    const auto same =
        ic::RewriteStlFile(source, tree.root / "same-metadata", replaced,
                           f::RewriteLimits(), f::RewriteWork());
    Physical(same, tree.root / "same-metadata", facets,
             ic::StlFileMetadataMode::kReplace,
             ic::StlMetadataPolicy::kRetainedSourceMetadata, encoding,
             expected_name, expected_header, context);
    auto changed = replaced;
    auto changed_header = header;
    changed_header.front() ^= std::byte{0x5a};
    changed.ascii_name =
        encoding == ic::StlEncoding::kAscii ? "replacement" : "";
    changed.binary_header = encoding == ic::StlEncoding::kBinary
                                ? std::span<const std::byte>(changed_header)
                                : std::span<const std::byte>{};
    const auto changed_result =
        ic::RewriteStlFile(source, tree.root / "replaced-metadata", changed,
                           f::RewriteLimits(), f::RewriteWork());
    Physical(changed_result, tree.root / "replaced-metadata", facets,
             ic::StlFileMetadataMode::kReplace,
             ic::StlMetadataPolicy::kReplacedSourceMetadata, encoding,
             changed.ascii_name, changed.binary_header, context);
    replaced.encoding = encoding == ic::StlEncoding::kAscii
                            ? ic::StlEncoding::kBinary
                            : ic::StlEncoding::kAscii;
    replaced.ascii_name =
        replaced.encoding == ic::StlEncoding::kAscii ? "converted" : "";
    replaced.binary_header = replaced.encoding == ic::StlEncoding::kBinary
                                 ? std::span<const std::byte>(header)
                                 : std::span<const std::byte>{};
    const auto cross = ic::RewriteStlFile(source, tree.root / "cross", replaced,
                                          f::RewriteLimits(), f::RewriteWork());
    Physical(cross, tree.root / "cross", facets,
             ic::StlFileMetadataMode::kReplace,
             ic::StlMetadataPolicy::kReplacedSourceMetadata, replaced.encoding,
             replaced.ascii_name, replaced.binary_header, context);
  }
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
          "all joined work and overlapping owner charges released");
}

void RefusedWireAndNarrowing(bool unsupported) {
  f::TempTree tree;
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  for (unsigned variant = 0; variant != 3; ++variant) {
    auto binary = f::Binary(f::kFacetBits);
    if (variant == 0) binary.pop_back();
    if (variant == 1) binary.push_back(std::byte{0});
    if (variant == 2) binary[132] = std::byte{1};
    const auto input = tree.root / "bad.stl";
    f::Write(input, binary);
    const auto read =
        ic::ReadStlFileAsset(input, ic::StlEncoding::kBinary, f::ReadLimits(),
                             f::ReadWork(), context);
    Require(read.status == ic::StlFileStatus::kStlFailure && read.captured &&
                !read.asset &&
                read.observations.capture.state ==
                    ic::StlFileObservationState::kSucceeded &&
                read.observations.read.state ==
                    ic::StlFileObservationState::kFailed,
            "malformed extent/nonzero attr retains genuine capture but no "
            "parse owner");
  }
  if (unsupported) return;
  std::string inexact(f::kAscii);
  const auto offset = inexact.find("vertex 1 0 0");
  Require(offset != std::string::npos, "owned literal mutation marker");
  inexact.replace(offset, 12, "vertex 0.1 0 0");
  const auto input = tree.root / "inexact.stl";
  f::Write(input, f::Bytes(inexact));
  const auto read = ic::ReadStlFileAsset(
      input, ic::StlEncoding::kAscii, f::ReadLimits(), f::ReadWork(), context);
  Require(read.asset.has_value(), "actual binary64 ASCII parse");
  auto options = f::Retain(ic::StlEncoding::kBinary);
  const auto header = f::Header();
  options.metadata_mode = ic::StlFileMetadataMode::kReplace;
  options.binary_header = header;
  const auto result =
      ic::RewriteStlFile(*read.asset, tree.root / "inexact-output", options,
                         f::RewriteLimits(), f::RewriteWork());
  Require(result.status == ic::StlFileStatus::kStlFailure && !result.output &&
              !result.verified && !result.bundle &&
              !std::filesystem::exists(tree.root / "inexact-output") &&
              result.diagnostic.stl_status ==
                  ic::StlStatus::kUnsupportedNumericConversion,
          "exact-binary32 refusal from real writer creates no stage/output");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "known fixture profile argument");
    f::HashKnownAnswers();
    RoundTrip(f::Bytes(f::kAscii), ic::StlEncoding::kAscii, f::kFacetBits,
              unsupported);
    RoundTrip(f::Binary(f::kFacetBits), ic::StlEncoding::kBinary, f::kFacetBits,
              unsupported);
    RoundTrip(f::Binary(f::kBoundaryBits), ic::StlEncoding::kBinary,
              f::kBoundaryBits, unsupported);
    RoundTrip(f::Bytes(f::kEmpty), ic::StlEncoding::kAscii, {}, false);
    RoundTrip(f::Binary({}), ic::StlEncoding::kBinary, {}, false);
    RefusedWireAndNarrowing(unsupported);
    std::cout << "STL file literal/physical semantics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
