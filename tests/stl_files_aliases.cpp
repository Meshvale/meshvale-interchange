// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>
#include <meshvale/interchange/stl_files.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "stl_files_alias_witness.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace a = ic::stl_files_alias_fixture;
namespace f = ic::stl_files_fixture;
namespace fs = std::filesystem;
using f::Require;
using Stats = geo::MaterializationExecutionStats;
using Status = ic::StlFileStatus;
using Phase = ic::StlFilePhase;
using Code = ic::StlFileDiagnosticCode;
using State = ic::StlFileObservationState;

// Independent STL wire and literal triangle-soup dimensions, not producer
// constants. Every imported literal corner has its own vertex identity.
constexpr std::size_t kBinaryHeaderBytes = 80;
constexpr std::size_t kCornersPerFacet = 3;
constexpr std::size_t kFacetCount = f::kFacetBits.size();
constexpr std::size_t kCornerCount = kFacetCount * kCornersPerFacet;
constexpr std::size_t kVertexCount = kCornerCount;
constexpr std::uint64_t kFaceNormalAttributeIndex = 0;

void Joined(const Stats& stats) {
  Require(stats.launched_workers == stats.joined_workers,
          "every actual launched worker joined");
}
void Zero(const Stats& stats) {
  Require(stats.planned_jobs == 0 && stats.launched_workers == 0 &&
              stats.joined_workers == 0 && stats.caller_jobs == 0,
          "genuinely unreached output work stays zero");
}
void Zero(const ic::StlExecution& stats) {
  Zero(stats.codec);
  Zero(stats.materialization);
  Zero(stats.inspection);
}
void Record(std::string_view id, const ic::StlFileResult& result) {
  a::RecordHead(a::record::kResult)
      << id << '\t' << static_cast<int>(result.status) << '\t'
      << static_cast<int>(result.phase) << '\t'
      << static_cast<int>(result.diagnostic.code) << '\n';
  const auto work = [&](std::string_view name, const Stats& stats) {
    Joined(stats);
    a::RecordHead(a::record::kWork)
        << id << '\t' << name << '\t' << stats.planned_jobs << '\t'
        << stats.launched_workers << '\t' << stats.joined_workers << '\t'
        << stats.caller_jobs << '\n';
  };
  const auto& e = result.execution;
  constexpr std::size_t kFileExecutionFamilyCount = 9;
  constexpr std::array<std::string_view, kFileExecutionFamilyCount>
      kFileExecutionNames{"capture",
                          "writing_files",
                          "staged_reread",
                          "byte_verification",
                          "value_verification",
                          "receipt_verification",
                          "inventory",
                          "hashing",
                          "provider_import"};
  const std::array<const Stats*, kFileExecutionFamilyCount> file_execution{
      &e.capture,
      &e.writing_files,
      &e.staged_reread,
      &e.byte_verification,
      &e.value_verification,
      &e.receipt_verification,
      &e.inventory,
      &e.hashing,
      &e.provider_import};
  for (std::size_t i = 0; i != file_execution.size(); ++i)
    work(kFileExecutionNames[i], *file_execution[i]);
  for (const auto* codec : {&e.read, &e.write, &e.fresh_read}) {
    Joined(codec->codec);
    Joined(codec->materialization);
    Joined(codec->inspection);
  }
}
void BytesRecord(std::string_view label, std::span<const std::byte> bytes) {
  a::RecordHead(a::record::kBytes) << a::TextHex(label) << '\t' << bytes.size()
                                   << '\t' << f::Hex(f::Sha256(bytes)) << '\n';
}
std::set<std::string> Names(const fs::path& parent) {
  std::set<std::string> names;
  for (const auto& entry : fs::directory_iterator(parent))
    names.insert(a::Utf8(entry.path().filename()));
  return names;
}
std::array<std::byte, kBinaryHeaderBytes> BoundaryHeader() {
  std::array<std::byte, kBinaryHeaderBytes> header{};
  for (std::size_t i = 0; i != header.size(); ++i)
    header[i] = std::byte(static_cast<unsigned char>(i));
  return header;
}
std::vector<std::byte> Boundary() {
  auto bytes = f::Binary(f::kBoundaryBits);
  const auto header = BoundaryHeader();
  std::ranges::copy(header, bytes.begin());
  return bytes;
}
void Captured(const ic::StlFileCaptureResult& result, const fs::path& input,
              std::span<const std::byte> bytes,
              const geo::ExecutionContext& context) {
  if (!result.captured) f::PrintReadFailure(result, input);
  Require(result.status == Status::kSuccess &&
              result.phase == Phase::kComplete && result.captured &&
              result.captured->EntryPath() == a::Utf8(input.filename()) &&
              std::ranges::equal(result.captured->EntryBytes(), bytes) &&
              result.captured->Sha256() == f::Sha256(bytes) &&
              result.captured->NewRequestedBytes() > 0 &&
              context.ActiveTrackedPayload() > 0 &&
              geo::SharesExecutionBudget(
                  context, result.captured->ShareExecutionContext()),
          "real capture literal bytes/hash/caller basename/shared ledger");
  Require(result.observations.capture.state == State::kSucceeded,
          "actual capture observation succeeded");
  const auto facts =
      std::get<ic::StlFileCaptureFacts>(result.observations.capture.facts);
  Require(facts.entry == a::Utf8(input.filename()) &&
              facts.bytes == bytes.size() && facts.sha256 == f::Sha256(bytes),
          "capture facts have independent literal byte/name/hash oracles");
}
void Asset(const ic::StlFileReadResult& result, const fs::path& input,
           std::span<const std::byte> bytes, std::span<const f::Facet> facets,
           ic::StlEncoding encoding = ic::StlEncoding::kAscii) {
  if (!result.asset) f::PrintReadFailure(result, input);
  Require(
      result.status == Status::kSuccess && result.phase == Phase::kComplete &&
          result.asset && !result.captured &&
          result.asset->EntryPath() == a::Utf8(input.filename()) &&
          std::ranges::equal(result.asset->EntryBytes(), bytes) &&
          result.asset->Sha256() == f::Sha256(bytes) &&
          result.asset->NewRequestedBytes() > 0 &&
          result.observations.capture.state == State::kSucceeded &&
          result.observations.read.state == State::kSucceeded,
      "real read owner/caller basename/full source bytes and reached facts");
  f::CheckDocument(result.asset->Document(), facets);
  const auto& document = result.asset->Document();
  Require(document.Encoding() == encoding &&
              document.AsciiName() == (encoding == ic::StlEncoding::kAscii
                                           ? f::kName
                                           : std::string_view{}),
          "explicit source encoding and literal ASCII metadata");
  if (encoding == ic::StlEncoding::kAscii)
    Require(document.BinaryHeader().empty(), "inactive binary metadata empty");
  else
    Require(std::ranges::equal(document.BinaryHeader(), BoundaryHeader()),
            "literal binary header bytes0..79");
  const auto captured =
      std::get<ic::StlFileCaptureFacts>(result.observations.capture.facts);
  const auto read =
      std::get<ic::StlFileReadFacts>(result.observations.read.facts);
  Require(captured.entry == a::Utf8(input.filename()) &&
              captured.bytes == bytes.size() &&
              captured.sha256 == f::Sha256(bytes) &&
              read.encoding == encoding && read.facets == facets.size() &&
              read.vertices == facets.size() * kCornersPerFacet &&
              read.corners == facets.size() * kCornersPerFacet,
          "all reached capture/read counts have independent literal oracles");
}
ic::StlFileReadResult Read(const fs::path& input,
                           const geo::ExecutionContext& context,
                           a::HookCounters* hooks = nullptr) {
#ifdef MESHVALE_STL_ALIAS_INTERNAL_PROBE
  if (hooks) return a::ReadObserved(input, context, *hooks);
#else
  (void)hooks;
#endif
  return ic::ReadStlFileAsset(input, ic::StlEncoding::kAscii, f::ReadLimits(),
                              f::ReadWork(), context);
}
void IsolatedOverlap(const ic::StlFileRewriteResult& result,
                     const geo::ExecutionContext& context) {
  Require(
      result.status == Status::kInvalidRequest &&
          result.phase == Phase::kPathPlanning &&
          result.diagnostic.code == Code::kSourceDestinationOverlap &&
          !result.output && !result.fresh_document && !result.verified &&
          !result.bundle &&
          result.cleanup == ic::StlFileCleanupOutcome::kNotRequired &&
          result.observations.capture.state == State::kSucceeded &&
          result.observations.read.state == State::kSucceeded &&
          std::ranges::equal(result.source.EntryBytes(), f::Bytes(f::kAscii)),
      "isolated retained logical-domain refusal with original source facts");
  const auto& e = result.execution;
  Require(e.value_verification.planned_jobs > 0,
          "genuine nonempty source value validation precedes path planning");
  Joined(e.value_verification);
  Zero(e.writing_files);
  Zero(e.staged_reread);
  Zero(e.byte_verification);
  Zero(e.receipt_verification);
  Zero(e.inventory);
  Zero(e.provider_import);
  Zero(e.write);
  Zero(e.fresh_read);
  for (const auto* stage :
       {&result.observations.write, &result.observations.staged_reread,
        &result.observations.fresh_read, &result.observations.verification,
        &result.observations.receipt_verification,
        &result.observations.inventory, &result.observations.publication})
    Require(stage->state == State::kNotAttempted && !stage->diagnostic &&
                std::holds_alternative<std::monostate>(stage->facts),
            "unreached output stages do not invent work/facts");
  Require(context.ActiveWorkers() == 0, "actual validation workers quiescent");
}
void Physical(const ic::StlFileRewriteResult& result,
              const fs::path& destination, std::string_view entry, bool cross) {
  f::Published(result);
  Require(result.phase == Phase::kComplete, "true publication completed");
  const auto members = f::Inventory(destination);
  Require(members.size() == 2 && members.contains(std::string(entry)) &&
              members.contains(std::string(f::kReceipt)),
          "independently reopened exact two-member inventory/caller basename");
  const auto& payload = members.at(std::string(entry));
  Require(
      result.bundle->Verified().EntryPath() == entry &&
          result.bundle->Verified().Source().EntryPath() == entry &&
          std::ranges::equal(result.bundle->Verified().EntryBytes(), payload) &&
          std::ranges::equal(result.bundle->ReceiptBytes(),
                             members.at(std::string(f::kReceipt))) &&
          std::ranges::equal(result.source.EntryBytes(), f::Bytes(f::kAscii)),
      "retained verified owners match actual independently reopened members");
  if (cross) {
    const std::array<std::byte, kBinaryHeaderBytes> zeros{};
    f::CheckBinary(payload, f::kFacetBits, zeros);
    Require(result.bundle->Verified().Document().Encoding() ==
                    ic::StlEncoding::kBinary &&
                result.bundle->Verified().Document().AsciiName().empty() &&
                std::ranges::equal(
                    result.bundle->Verified().Document().BinaryHeader(), zeros),
            "explicit cross encoding");
  } else {
    f::CheckAscii(payload, f::kFacetBits, f::kName);
    Require(result.bundle->Verified().Document().AsciiName() == f::kName,
            "ASCII metadata preserved independently");
    Require(result.bundle->Verified().Document().Encoding() ==
                    ic::StlEncoding::kAscii &&
                result.bundle->Verified().Document().BinaryHeader().empty(),
            "explicit ASCII encoding and inactive binary metadata");
  }
  f::CheckDocument(result.bundle->Verified().Document(), f::kFacetBits);
  const auto map = result.output->CornerSourceVertices();
  Require(map.size() == kCornerCount,
          "three literal facets give nine correspondence rows");
  for (std::size_t i = 0; i != map.size(); ++i)
    Require(map[i] == i, "complete source corner map identity");
  const auto facts =
      std::get<ic::StlFileWriteFacts>(result.observations.write.facts);
  Require(facts.bytes == payload.size() && facts.sha256 == f::Sha256(payload) &&
              facts.codec.encoding == (cross ? ic::StlEncoding::kBinary
                                             : ic::StlEncoding::kAscii) &&
              facts.codec.source_encoding == ic::StlEncoding::kAscii &&
              facts.codec.source_vertex_count == kVertexCount &&
              facts.codec.source_face_count == kFacetCount &&
              facts.codec.face_normal_attribute == kFaceNormalAttributeIndex &&
              facts.codec.corner_expansion_acknowledged &&
              facts.codec.metadata_policy ==
                  (cross ? ic::StlMetadataPolicy::kReplacedSourceMetadata
                         : ic::StlMetadataPolicy::kRetainedSourceMetadata),
          "complete write facts match literal geometry/metadata and physical "
          "bytes");
  f::CheckReceipt(*result.bundle,
                  cross ? ic::StlFileMetadataMode::kReplace
                        : ic::StlFileMetadataMode::kRetain,
                  cross ? ic::StlMetadataPolicy::kReplacedSourceMetadata
                        : ic::StlMetadataPolicy::kRetainedSourceMetadata,
                  f::kFacetBits.size());
  BytesRecord(entry, payload);
  BytesRecord(f::kReceipt, members.at(std::string(f::kReceipt)));
}
template <class Function>
void Case(a::Inventory& inventory, std::string_view id,
          const geo::ExecutionContext& context, Function body) {
  inventory.Begin(id);
  const auto before = context.ActiveTrackedPayload();
  body();
  Require(
      context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == before,
      "all actual case owners/leases expire and workers join");
  inventory.Pass(id);
}
std::string Id(std::string_view family, unsigned index) {
  return std::string(family) + "." + std::to_string(index + 1);
}
void Run(a::Inventory& inventory, a::Tree& tree,
         const geo::ExecutionContext& context) {
  f::HashKnownAnswers();
  for (unsigned i = 0; i != 4; ++i) {
    const auto id = Id("N01", i);
    Case(inventory, id, context, [&] {
      a::Source source(tree.grand_long, id, f::Bytes(f::kAscii));
      const auto input = source.Path(i);
      const auto capture =
          ic::CaptureStlFile(input, f::FileLimits(), f::FileWork(), context);
      Record(id, capture);
      Captured(capture, input, f::Bytes(f::kAscii), context);
      const auto decoded =
          ic::DecodeStlFile(*capture.captured, ic::StlEncoding::kAscii,
                            f::CodecReadLimits(), f::CodecWork());
      Record(id, decoded);
      Asset(decoded, input, f::Bytes(f::kAscii), f::kFacetBits);
      Require(std::ranges::equal(f::Read(input), f::Bytes(f::kAscii)) &&
                  a::Observe(input) == source.identity,
              "original physical bytes/identity protected");
    });
  }
  for (unsigned i = 0; i != 2; ++i) {
    const auto id = Id("N02", i);
    Case(inventory, id, context, [&] {
      const auto bytes = Boundary();
      a::Source source(tree.grand_long, id, bytes);
      const auto input = source.Path(i + 2);
      const auto read =
          ic::ReadStlFileAsset(input, ic::StlEncoding::kBinary, f::ReadLimits(),
                               f::ReadWork(), context);
      Record(id, read);
      Asset(read, input, bytes, f::kBoundaryBits, ic::StlEncoding::kBinary);
      const auto header = BoundaryHeader();
      f::CheckBinary(read.asset->EntryBytes(), f::kBoundaryBits, header);
      Require(std::ranges::equal(read.asset->Document().BinaryHeader(), header),
              "literal opaque header bytes0..79 retained");
      Require(f::Read(input) == bytes && a::Observe(input) == source.identity,
              "binary read preserves complete original physical source");
      BytesRecord(a::Utf8(input), bytes);
    });
  }
  for (unsigned i = 0; i != 2; ++i) {
    const auto id = Id("N03", i);
    Case(inventory, id, context, [&] {
      a::Source source(tree.grand_long, id, f::Bytes(f::kAscii));
      const auto input = source.Path(i + 2);
      a::HookCounters hooks;
      std::optional<ic::StlCapturedFile> held;
      {
        const auto shell = context;
#ifdef MESHVALE_STL_ALIAS_INTERNAL_PROBE
        const auto result = a::CaptureObserved(input, shell, hooks);
#else
        const auto result = ic::CaptureStlFile(input, f::FileLimits(), f::FileWork(), shell);
#endif
        Record(id, result);
        Captured(result, input, f::Bytes(f::kAscii), context);
        held = result.captured;
      }
      a::CheckHooks(hooks, id);
      const auto opened = hooks.opened.load(), closed = hooks.closed.load();
      source.Remove(input);
      const auto result = ic::DecodeStlFile(
          *held, ic::StlEncoding::kAscii, f::CodecReadLimits(), f::CodecWork());
      Record(id, result);
      Asset(result, input, f::Bytes(f::kAscii), f::kFacetBits);
      held.reset();
      Require(
          hooks.opened.load() == opened && hooks.closed.load() == closed &&
              !fs::exists(source.long_path) && !fs::exists(source.short_path),
          "immutable retained copies require no source reopen/handle lifetime");
    });
  }
  for (unsigned i = 0; i != 4; ++i) {
    const auto id = Id("N04", i);
    Case(inventory, id, context, [&] {
      a::Source source(tree.grand_long, id, f::Bytes(f::kAscii));
      const auto input = source.Path(i);
      a::HookCounters hooks;
      std::optional<ic::StlFileAsset> held;
      {
        const auto read = Read(input, context, &hooks);
        Asset(read, input, f::Bytes(f::kAscii), f::kFacetBits);
        held = read.asset;
      }
      a::CheckHooks(hooks, id);
      const auto opened = hooks.opened.load(), closed = hooks.closed.load();
      source.Remove(input);
      const auto destination =
          (i < 2 ? source.parent_long : source.parent_short) / "published";
      const bool cross = i == 3;
      auto options =
          f::Retain(cross ? ic::StlEncoding::kBinary : ic::StlEncoding::kAscii);
      const std::array<std::byte, kBinaryHeaderBytes> zeros{};
      if (cross) {
        options.metadata_mode = ic::StlFileMetadataMode::kReplace;
        options.binary_header = zeros;
      }
      const auto result = ic::RewriteStlFile(
          *held, destination, options, f::RewriteLimits(), f::RewriteWork());
      Record(id, result);
      Physical(result, destination, a::Utf8(input.filename()), cross);
      held.reset();
      Require(!fs::exists(source.long_path) && !fs::exists(source.short_path) &&
                  hooks.opened.load() == opened &&
                  hooks.closed.load() == closed,
              "rewrite after actual removal never reopens retained source");
    });
  }
  for (unsigned i = 0; i != 8; ++i) {
    const auto id = Id("N05", i);
    Case(inventory, id, context, [&] {
      a::Source source(tree.grand_long, id, f::Bytes(f::kAscii));
      const auto input = source.Path(i < 4 ? i / 2 : (i - 4) % 2);
      const auto read = Read(input, context);
      Asset(read, input, f::Bytes(f::kAscii), f::kFacetBits);
      source.Remove(input);
      fs::path destination;
      std::optional<a::Identity> replacement;
      if (i < 4) {
        destination =
            source.parent_long / (i % 2 ? source.short_path.filename()
                                        : source.long_path.filename());
      } else if (i < 6) {
        const auto parent =
            source.parent_long / (i == 4 ? source.short_path.filename()
                                         : source.long_path.filename());
        Require(fs::create_directory(parent),
                "new admitted replacement directory");
        a::OrdinaryPrefixes(parent);
        f::Write(parent / "sentinel", f::Bytes(a::kSentinel));
        replacement = a::Observe(parent);
        Require(replacement->file != source.identity.file,
                "replacement parent has separate actual native identity");
        destination = parent / "new-child";
      } else {
        destination = source.long_path / "new-child";
      }
      Require(!fs::exists(destination),
              "new destination absent before isolation check");
      const auto before = Names(source.parent_long);
      const auto result = ic::RewriteStlFile(
          *read.asset, destination, f::Retain(ic::StlEncoding::kAscii),
          f::RewriteLimits(), f::RewriteWork());
      Record(id, result);
      if (i < 6)
        IsolatedOverlap(result, context);
      else
        Require(result.status != Status::kSuccess && !result.bundle,
                "missing-parent control is refusal only, no "
                "alias-classification claim");
      Require(
          Names(source.parent_long) == before && !fs::exists(destination) &&
              std::ranges::equal(result.source.EntryBytes(),
                                 f::Bytes(f::kAscii)),
          "planning refusal preserves all original bytes and creates no stage");
      if (replacement)
        Require(a::Observe(destination.parent_path()) == *replacement &&
                    f::Read(destination.parent_path() / "sentinel") ==
                        std::vector<std::byte>(f::Bytes(a::kSentinel).begin(),
                                               f::Bytes(a::kSentinel).end()),
                "admitted replacement parent/sentinel unchanged by refusal");
    });
  }
  for (unsigned i = 0; i != 2; ++i) {
    const auto id = Id("N06", i);
    Case(inventory, id, context, [&] {
      a::Source source(tree.grand_long, id, f::Bytes(f::kAscii));
      const auto read = Read(source.long_path, context);
      Asset(read, source.long_path, f::Bytes(f::kAscii), f::kFacetBits);
      const auto result = ic::RewriteStlFile(
          *read.asset, i ? source.parent_short : source.parent_long,
          f::Retain(ic::StlEncoding::kAscii), f::RewriteLimits(),
          f::RewriteWork());
      Record(id, result);
      Require(result.status != Status::kSuccess && !result.bundle &&
                  a::Observe(source.long_path) == source.identity &&
                  f::Read(source.long_path) ==
                      std::vector<std::byte>(f::Bytes(f::kAscii).begin(),
                                             f::Bytes(f::kAscii).end()),
              "occupied-ancestor control preserves source; precedence not "
              "invented");
    });
  }
  Case(inventory, "N07.1", context, [&] {
    a::Source source(tree.grand_long, "N07.1", f::Bytes(f::kAscii));
    const auto input = source.Path(2);
    auto limits = f::FileLimits();
    limits.max_native_path_code_units = input.native().size() + 1;
    a::Witness(source.long_path.native().size() + 1 >
                   limits.max_native_path_code_units,
               "MISSED_WITNESS expanded path is not longer than input cap");
    const auto result =
        ic::CaptureStlFile(input, limits, f::FileWork(), context);
    Record("N07.1", result);
    Require(result.status == Status::kLimitExceeded &&
                result.phase == Phase::kPathPlanning &&
                result.diagnostic.code == Code::kCountLimit &&
                !result.captured &&
                result.cleanup == ic::StlFileCleanupOutcome::kNotRequired,
            "independent separating expansion cap/terminator bound");
    Zero(result.execution.capture);
    Require(std::ranges::equal(f::Read(input), f::Bytes(f::kAscii)) &&
                a::Observe(input) == source.identity,
            "expansion-cap refusal preserves source bytes and identity");
  });
  Case(inventory, "N08.1", context, [&] {
    a::Source source(tree.grand_long, "N08.1", f::Bytes(f::kAscii));
    constexpr unsigned kShortParentLongLeafVariant = 2;
    const auto input = source.Path(kShortParentLongLeafVariant);
    const auto before = context.ActiveTrackedPayload();
    // Independent public limits, fixed before any result. The stopped control
    // distinguishes cap-floor admission from planning-memory refusal; it does
    // not claim that a request-owner seed was allocated.
    constexpr std::uint64_t kPlanningRequestCapBytes = 4096;
    constexpr std::uint64_t kPlanningNativeCapacityCodeUnits = 65536;
    auto limits = f::FileLimits();
    limits.max_file_new_requested_bytes = kPlanningRequestCapBytes;
    limits.max_native_path_code_units = kPlanningNativeCapacityCodeUnits;
    const auto work = f::FileWork();
    const auto refusal = [&](const ic::StlFileCaptureResult& result,
                             Status status, Phase phase, Code code,
                             std::string_view message) {
      Record("N08.1", result);
      Require(result.status == status && result.phase == phase &&
                  result.diagnostic.code == code,
              message);
      Require(!result.captured &&
                  result.observations.capture.state != State::kSucceeded &&
                  std::holds_alternative<std::monostate>(
                      result.observations.capture.facts),
              "refused capture has no owner or successful hash facts");
      Require(result.cleanup == ic::StlFileCleanupOutcome::kNotRequired &&
                  !result.cleanup_error,
              "pre-capture guards create no stage requiring cleanup");
      const auto& e = result.execution;
      for (const auto* stats : {&e.capture, &e.writing_files, &e.staged_reread,
                                &e.byte_verification, &e.value_verification,
                                &e.receipt_verification, &e.inventory,
                                &e.hashing, &e.provider_import})
        Zero(*stats);
      for (const auto* codec : {&e.read, &e.write, &e.fresh_read}) Zero(*codec);
    };
    const auto released = [&](std::size_t expected_payload) {
      Require(context.ActiveTrackedPayload() == expected_payload &&
                  context.ActiveWorkers() == 0,
              "actual guard releases charges and workers to its baseline");
      Require(
          std::ranges::equal(f::Read(source.long_path), f::Bytes(f::kAscii)) &&
              a::Observe(source.long_path) == source.identity,
          "guard preserves independent physical source bytes and identity");
    };
    auto held = geo::TryReservePayload(context, f::kContextBytes - before);
    Require(held.status == geo::ReservationStatus::kAccepted,
            "real remaining-ledger lease");
    {
      const auto result =
          ic::CaptureStlFile(input, f::FileLimits(), f::FileWork(), context);
      refusal(result, Status::kBudgetExceeded, Phase::kConfiguration,
              Code::kSharedBudget, "real shared-ledger setup refusal");
    }
    released(f::kContextBytes);
    held.lease.Reset();
    released(before);

    std::stop_source stopped;
    Require(stopped.request_stop(), "genuine pre-stopped admission control");
    {
      const auto result =
          ic::CaptureStlFile(input, limits, work, context, stopped.get_token());
      refusal(result, Status::kCanceled, Phase::kConfiguration,
              Code::kStopRequested, "stopped control admits request-cap floor");
    }
    released(before);
    {
      const auto result = ic::CaptureStlFile(input, limits, work, context);
      refusal(result, Status::kLimitExceeded, Phase::kPathPlanning,
              Code::kRequestBudget, "real planning-memory request-cap refusal");
    }
    released(before);
  });
  Case(inventory, "N09.1", context, [&] {
    a::Source source(tree.grand_long, "N09.1", f::Bytes(f::kAscii));
    const auto read = Read(source.Path(3), context);
    Asset(read, source.Path(3), f::Bytes(f::kAscii), f::kFacetBits);
    struct Provider {
      unsigned calls = 0;
      std::optional<ic::StlVerifiedRewrite> held;
      static std::span<const std::byte> Run(
          const ic::StlFileVerifiedContent& content, void* opaque) {
        auto& self = *static_cast<Provider*>(opaque);
        ++self.calls;
        self.held = content.verified;
        throw std::runtime_error("ordinary fixture provider exception");
      }
    } provider;
    auto options = f::Retain(ic::StlEncoding::kAscii);
    options.report = ic::StlFileReportProvider{&Provider::Run, &provider};
    const auto destination = source.parent_short / "provider-failure";
    const auto before = Names(source.parent_long);
    {
      const auto result =
          ic::RewriteStlFile(*read.asset, destination, options,
                             f::RewriteLimits(), f::RewriteWork());
      Record("N09.1", result);
      Require(result.status == Status::kProviderFailure &&
                  result.phase == Phase::kProvider &&
                  result.diagnostic.code == Code::kProvider &&
                  result.verified && result.output && result.fresh_document &&
                  !result.bundle && provider.calls == 1 && provider.held &&
                  result.cleanup == ic::StlFileCleanupOutcome::kRemoved &&
                  Names(source.parent_long) == before &&
                  !fs::exists(destination) &&
                  a::Observe(source.long_path) == source.identity,
              "actual provider exception retains genuine verified owners and "
              "cleans stage");
    }
    f::CheckDocument(provider.held->Document(), f::kFacetBits);
    f::CheckAscii(provider.held->EntryBytes(), f::kFacetBits, f::kName);
    const auto map = provider.held->Output().CornerSourceVertices();
    Require(map.size() == kCornerCount &&
                std::ranges::equal(provider.held->Source().EntryBytes(),
                                   f::Bytes(f::kAscii)),
            "provider-retained genuine verified owner survives failed result "
            "destruction");
    for (std::size_t i = 0; i != map.size(); ++i)
      Require(map[i] == i,
              "retained output map remains complete after provider failure");
    Require(std::ranges::equal(f::Read(source.long_path), f::Bytes(f::kAscii)),
            "provider failure preserves actual still-existing physical source "
            "bytes");
  });
  for (unsigned i = 0; i != 2; ++i) {
    const auto id = Id("N10", i);
    Case(inventory, id, context, [&] {
      a::Source source(tree.grand_long, id, f::Bytes(f::kAscii));
      const auto input = source.Path(0);
      const auto mixed = tree.grand_long / source.parent_short.filename();
      a::Witness(
          a::Key(tree.grand_long) != a::Key(tree.grand_short) &&
              a::Observe(tree.grand_long) == a::Observe(tree.grand_short) &&
              a::Observe(mixed) == a::Observe(source.parent_long),
          "MISSED_DISTINGUISHING_WITNESS actual grandparent/mixed identities");
      for (const auto& whole :
           {input.parent_path(), a::Spelling(input, false).parent_path(),
            a::Spelling(input, true).parent_path()}) {
        const bool distinct = a::Key(mixed) != a::Key(whole);
        a::RecordHead(a::record::kDistinct)
            << a::TextHex(a::Utf8(mixed)) << '\t' << a::TextHex(a::Utf8(whole))
            << '\t' << distinct << '\n';
        a::Witness(
            distinct,
            "MISSED_DISTINGUISHING_WITNESS mixed equals whole source prefix");
      }
      a::Witness(a::Key(mixed) != a::Key(source.parent_short),
                 "MISSED_DISTINGUISHING_WITNESS mixed duplicates full short");
      const auto read = Read(input, context);
      Asset(read, input, f::Bytes(f::kAscii), f::kFacetBits);
      source.Remove(input);
      Require(fs::remove(source.parent_long),
              "remove checked-closed now-empty source parent");
      const auto destination = i ? mixed : source.parent_short;
      Require(!fs::exists(destination), "remembered ancestor target absent");
      a::OrdinaryPrefixes(destination.parent_path());
      const auto grand = a::Observe(destination.parent_path());
      const auto sentinel = tree.grand_long / (id + " sentinel");
      f::Write(sentinel, f::Bytes(a::kSentinel));
      const auto before = Names(tree.grand_long);
      const auto result = ic::RewriteStlFile(
          *read.asset, destination, f::Retain(ic::StlEncoding::kAscii),
          f::RewriteLimits(), f::RewriteWork());
      Record(id, result);
      IsolatedOverlap(result, context);
      Require(!fs::exists(source.long_path) &&
                  !fs::exists(source.parent_long) && !fs::exists(destination) &&
                  Names(tree.grand_long) == before &&
                  a::Observe(destination.parent_path()) == grand &&
                  f::Read(sentinel) ==
                      std::vector<std::byte>(f::Bytes(a::kSentinel).begin(),
                                             f::Bytes(a::kSentinel).end()),
              "removed-parent ancestor refusal preserves admitted grandparent "
              "and creates nothing");
    });
  }
  for (unsigned i = 0; i != 2; ++i) {
    const auto id = Id("T01", i);
    Case(inventory, id, context, [&] {
      const auto original = tree.ReparseSource();
      const auto input = i ? tree.link / ".." /
                                 original.parent_path().filename() /
                                 original.filename()
                           : tree.link / original.filename();
      const auto result =
          ic::ReadStlFileAsset(input, ic::StlEncoding::kAscii, f::ReadLimits(),
                               f::ReadWork(), context);
      Record(id, result);
      Require(result.status == Status::kUnsupportedProfile &&
                  result.phase == Phase::kPathPlanning &&
                  result.diagnostic.code == Code::kPathUnsupported &&
                  !result.captured && !result.asset &&
                  f::Read(original) ==
                      std::vector<std::byte>(f::Bytes(f::kAscii).begin(),
                                             f::Bytes(f::kAscii).end()),
              "real original reparse traversal refused before parent "
              "folding/capture");
      Zero(result.execution.capture);
    });
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    Require(argc == 2 && std::string_view(argv[1]) == a::kRequiredArgument,
            "explicit required alias gate flag is mandatory");
    a::Inventory inventory;
#ifdef MESHVALE_STL_ALIAS_INTERNAL_PROBE
    constexpr std::string_view kMode = "source", kScope = "actual-filehooks";
    a::RecordHead(a::record::kMode) << kMode << '\t' << kScope << '\n';
#else
    constexpr std::string_view kMode = "public-sdk",
                               kScope = "no-private-hooks";
    a::RecordHead(a::record::kMode) << kMode << '\t' << kScope << '\n';
#endif
    a::Tree tree;
    geo::ExecutionContext context({4, 1, f::kContextBytes});
    Run(inventory, tree, context);
    tree.Close();
    inventory.Complete();
    return 0;
  } catch (const a::WitnessFailure& error) {
    std::cerr << a::kProtocol << '\t' << a::record::kFail << '\t'
              << a::record::kMissingWitness << '\t' << a::TextHex(error.what())
              << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cerr << a::kProtocol << '\t' << a::record::kFail << '\t'
              << a::record::kAssertion << '\t' << a::TextHex(error.what())
              << '\n';
    return 1;
  }
}
