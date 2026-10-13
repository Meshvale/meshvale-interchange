// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../src/gltf_files_test_internal.h"
#include "gltf_file_provider_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::gltf_files_detail;
namespace fixture = ic::test_provider;
namespace fs = std::filesystem;
using fixture::Bytes;
using fixture::Require;
constexpr std::string_view kUnsupportedArgument = "--unsupported-profile";
constexpr std::string_view kBufferPath = "a.bin";
constexpr std::string_view kExtraPath = "unlisted.bin";
constexpr std::string_view kExcludedPath = "../escaped.bin";
constexpr std::string_view kFailureMessage = "literal provider failure";
constexpr std::size_t kTailByte = 48;
constexpr std::size_t kInitialFiles = 5;
constexpr std::size_t kBoundaryPayloadBytes = 65537;
constexpr std::byte kBoundaryPayloadFill{0x6b};
constexpr std::byte kBoundaryPayloadTail{0xd3};
constexpr std::string_view kBoundaryPath =
    "receipts/provider-boundary-borrowed-output-payload.bin";
constexpr std::size_t kInitialPhysicalBytes =
    fixture::kJson.size() + fixture::kBinary.size() +
    fixture::kOpaquePaths.size() * fixture::kOpaqueBytes +
    fixture::kStaticBytes.size();
constexpr std::size_t kInitialPathBytes = [] {
  std::size_t bytes = 0;
  for (const auto path : fixture::kInitialPaths) bytes += path.size();
  return bytes;
}();
constexpr std::array<std::string_view, 6> kBoundaryPaths{
    kBufferPath,   "opaque-a.bin",       "opaque-b.bin",
    kBoundaryPath, fixture::kStaticPath, fixture::kEntry};
constexpr std::array<std::string_view, 6> kFinalPaths{
    kBufferPath,           "opaque-a.bin",       "opaque-b.bin",
    fixture::kDynamicPath, fixture::kStaticPath, fixture::kEntry};

void CheckFiles(std::span<const ic::GltfFileView> files,
                const fixture::Fixture& source, bool dynamic) {
  Require(files.size() == (dynamic ? kFinalPaths.size() : kInitialFiles),
          "literal physical inventory size");
  for (std::size_t i = 0; i < files.size(); ++i) {
    const auto path = dynamic ? kFinalPaths[i] : fixture::kInitialPaths[i];
    Require(files[i].path == path, "literal UTF8 sorted physical paths");
    const auto expected =
        path == fixture::kEntry ? std::span<const std::byte>(source.entry_bytes)
        : path == kBufferPath   ? std::span<const std::byte>(fixture::kBinary)
        : path == fixture::kStaticPath ? Bytes(fixture::kStaticBytes)
        : path == fixture::kDynamicPath
            ? Bytes(fixture::kDynamicBytes)
            : std::span<const std::byte>(source.opaque);
    Require(std::ranges::equal(files[i].bytes, expected),
            "authored full bytes including unused buffer tail");
  }
}
struct Stage {
  fs::path path;
  std::size_t inventories = 0;
  std::atomic<std::size_t> dynamic_write_calls{0};
  bool add_extra = false;
  detail::GltfFilePublicationTestOptions Testing() {
    detail::GltfFilePublicationTestOptions result;
    result.hooks.state = this;
    result.hooks.before_write = [](std::uint64_t file, std::uint64_t,
                                   void* state) {
      if (file >= kInitialFiles)
        ++static_cast<Stage*>(state)->dynamic_write_calls;
    };
    result.hooks.stage_ready = [](detail::TestNativeView path, void* state) {
      static_cast<Stage*>(state)->path = fs::path(path);
    };
    result.hooks.before_inventory = [](detail::TestNativeView path,
                                       void* state) {
      auto& self = *static_cast<Stage*>(state);
      ++self.inventories;
      if (self.add_extra)
        fixture::Write(fs::path(path) / kExtraPath,
                       Bytes(fixture::kDynamicBytes));
    };
    return result;
  }
};
void Failure(const ic::GltfFilePublicationResult& result,
             ic::GltfFileStatus status, ic::GltfFileDiagnosticCode code,
             ic::GltfFileStage stage) {
  Require(!result.bundle && result.diagnostic.status == status &&
              result.diagnostic.code == code &&
              result.diagnostic.stage == stage,
          "exact typed provider failure " +
              std::to_string(static_cast<int>(result.diagnostic.status)) + "/" +
              std::to_string(static_cast<int>(result.diagnostic.code)) + "/" +
              std::to_string(static_cast<int>(result.diagnostic.stage)));
}
void Removed(const fixture::Fixture& source, const Stage& stage,
             const ic::GltfFilePublicationResult& result) {
  Require(!stage.path.empty() && !fs::exists(stage.path) &&
              !fs::exists(source.Destination()) &&
              result.cleanup == ic::GltfFileCleanupOutcome::kRemoved &&
              source.context.ActiveWorkers() == 0,
          "actual staging removed after joined failure");
}
struct Provider {
  explicit Provider(const fixture::Fixture& fixture_source,
                    Stage* fixture_stage = nullptr)
      : source(&fixture_source), stage(fixture_stage) {}
  const fixture::Fixture* source = nullptr;
  Stage* stage = nullptr;
  std::size_t calls = 0;
  bool empty = false;
  bool mutate_tail = false;
  bool ordinary_throw = false;
  bool allocation_throw = false;
  std::optional<ic::GltfTopologyOutputInspection> retained;
  std::array<ic::GltfFileView, 1> views{
      {{fixture::kDynamicPath, Bytes(fixture::kDynamicBytes)}}};
  std::span<const ic::GltfFileView> returned_views = views;
  static std::span<const ic::GltfFileView> Provide(
      const ic::GltfFileVerifiedContent& content, void* state) {
    auto& self = *static_cast<Provider*>(state);
    ++self.calls;
    Require(content.format == ic::GltfFileFormat::kGltfJson &&
                content.entry_path == fixture::kEntry && !content.container,
            "borrowed provider sees actual verified JSON scope");
    CheckFiles(content.files, *self.source, false);
    Require(std::ranges::equal(
                content.inspection.ReloadedExtraction().TriangleIndices(),
                fixture::kSourceTriangles),
            "provider sees independently decoded literal staged topology");
    if (self.stage)
      Require(
          self.stage->inventories == 1,
          "initial physical byte and inventory verification precedes producer");
    self.retained = content.inspection;
    if (self.ordinary_throw)
      throw std::runtime_error(std::string(kFailureMessage));
    if (self.allocation_throw) throw std::bad_alloc();
    if (self.mutate_tail) {
      // Deliberate adversarial test injection, outside allowed caller behavior.
      auto binary = fixture::kBinary;
      binary[kTailByte] ^= std::byte{1};
      fixture::Write(self.stage->path / kBufferPath, binary);
    }
    return self.empty ? std::span<const ic::GltfFileView>{}
                      : self.returned_views;
  }
};
const std::array<ic::GltfFileView, 1> kStaticFiles{
    {{fixture::kStaticPath, Bytes(fixture::kStaticBytes)}}};
const std::array<ic::GltfFileView, 1> kNullStateFiles{
    {{fixture::kDynamicPath, Bytes(fixture::kDynamicBytes)}}};
std::span<const ic::GltfFileView> NullStateProvider(
    const ic::GltfFileVerifiedContent& content, void* state) {
  Require(state == nullptr && content.files.size() == kInitialFiles,
          "active provider accepts null state");
  return kNullStateFiles;
}
void SourceRouting(bool unsupported) {
  for (const auto format :
       {ic::GltfFileFormat::kGltfJson, ic::GltfFileFormat::kGlb}) {
    std::optional<ic::GltfAsset> retained;
    std::optional<geo::ExecutionContext> retained_context;
    {
      fixture::Fixture source(false, false, format);
      const auto copied = source.context;
      const geo::ExecutionContext foreign(
          {fixture::kWorkerCount, 1, fixture::kRequestBytes});
      auto moving = source.context;
      const auto moved = std::move(moving);
      const auto file_bytes = source.source->NewRequestedBytes();
      const auto asset_bytes = source.source->Asset().NewRequestedBytes();
      const auto active = source.context.ActiveTrackedPayload();
      const auto peak = source.context.PeakTrackedPayload();
      Require(source.source->Format() == format &&
                  (source.source->Container() != nullptr) ==
                      (format == ic::GltfFileFormat::kGlb) &&
                  std::ranges::equal(source.source->EntryBytes(),
                                     source.entry_bytes),
              "actual content dispatch and retained full JSON/GLB wire");
      Require(source.source->Asset().SharesExecutionBudget(source.context) &&
                  source.source->Asset().SharesExecutionBudget(copied) &&
                  source.source->Asset().SharesExecutionBudget(moved) &&
                  !source.source->Asset().SharesExecutionBudget(foreign) &&
                  !source.source->Asset().SharesExecutionBudget(moving),
              "file-retained source asset routes canonical ledger query");
      Require(
          file_bytes == source.source->NewRequestedBytes() &&
              asset_bytes == source.source->Asset().NewRequestedBytes() &&
              active == source.context.ActiveTrackedPayload() &&
              peak == source.context.PeakTrackedPayload(),
          "owner-scoped queries leave file and delegate counters unchanged");
      retained = source.source->Asset();
      retained_context = copied;
      if (unsupported) {
        source.Prepare(false, false);
        Require(
            !source.materialization.primitive &&
                source.materialization.status ==
                    ic::GltfMaterializationStatus::kGeometryFailure &&
                source.materialization.diagnostic.geometry_status ==
                    geo::MaterializationStatus::kUnsupportedAllocationProfile,
            "actual nonempty unsupported dependency follows both routing "
            "checks");
      }
    }
    Require(retained->SharesExecutionBudget(*retained_context),
            "asset copy and ledger remain valid after file/root wrappers die");
  }
}
void ProducersAndRetention() {
  std::optional<ic::GltfPublishedBundle> retained;
  {
    fixture::Fixture source(false);
    Stage stage;
    Provider provider{source, &stage};
    auto testing = stage.Testing();
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = kStaticFiles;
    options.verified_files_provider = {Provider::Provide, &provider};
    fs::remove_all(source.SourceRoot());
    auto result = detail::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work(), {}, testing);
    Require(result.bundle && provider.calls == 1 && stage.inventories == 2 &&
                result.outcome == ic::GltfFilePublicationOutcome::kPublished,
            "borrowed files imported once, fully reverified and published");
    CheckFiles(result.bundle->Files(), source, true);
    for (const auto& file : result.bundle->Files())
      Require(
          std::ranges::equal(
              fixture::ReadBytes(source.Destination() / file.path), file.bytes),
          "every finalized physical file independently reopened");
    Require(provider.retained &&
                std::ranges::equal(
                    provider.retained->ReloadedExtraction().TriangleIndices(),
                    fixture::kSourceTriangles),
            "caller may retain a strong verified inspection copy");
    retained = *result.bundle;
  }
  Require(retained->Files().size() == kFinalPaths.size(),
          "published owner survives producer storage and source destruction");
  for (const auto& file : retained->Files())
    if (file.path == fixture::kDynamicPath)
      Require(std::ranges::equal(file.bytes, Bytes(fixture::kDynamicBytes)),
              "published dynamic bytes own imported storage");
  {
    fixture::Fixture source(false);
    ic::GltfFilePublicationOptions options{kStaticFiles, {}, {}};
    options.verified_files_provider = {NullStateProvider, nullptr};
    auto result = ic::PublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work());
    Require(result.bundle.has_value(),
            "null-state producer publishes real borrowed bytes");
    CheckFiles(result.bundle->Files(), source, true);
  }
  {
    fixture::Fixture source(false);
    std::size_t calls = 0;
    ic::GltfFilePublicationOptions options{
        kStaticFiles, {}, [&](const ic::GltfFileVerifiedContent& content) {
          ++calls;
          CheckFiles(content.files, source, false);
          return std::vector<ic::GltfFileSupplement>{
              {std::string(fixture::kDynamicPath),
               {Bytes(fixture::kDynamicBytes).begin(),
                Bytes(fixture::kDynamicBytes).end()}}};
        }};
    options.verified_files_provider.state = &calls;
    auto result = ic::PublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work());
    Require(result.bundle && calls == 1,
            "legacy aggregate and null-function nonnull state remain enabled "
            "legacy route");
    CheckFiles(result.bundle->Files(), source, true);
  }
}
void ConfigurationPrecedence() {
  for (const auto format :
       {ic::GltfFileFormat::kGltfJson, ic::GltfFileFormat::kGlb}) {
    fixture::Fixture source(false, true, format);
    std::size_t phases = 0;
    std::size_t legacy_calls = 0;
    Provider provider{source};
    ic::GltfFilePublicationOptions options{};
    options.on_phase = [&](auto) { ++phases; };
    options.on_verified = [&](const auto&) {
      ++legacy_calls;
      return std::vector<ic::GltfFileSupplement>{};
    };
    options.verified_files_provider = {Provider::Provide, &provider};
    std::stop_source stop;
    stop.request_stop();
    for (const bool canceled : {false, true}) {
      const auto active = source.context.ActiveTrackedPayload();
      auto result = ic::PublishGltfTopologyBundle(
          {*source.source, *source.inspection}, source.Destination(), options,
          fixture::Limits(), fixture::Work(),
          canceled ? stop.get_token() : std::stop_token{});
      Failure(result, ic::GltfFileStatus::kInvalidConfiguration,
              ic::GltfFileDiagnosticCode::kInvalidRequest,
              ic::GltfFileStage::kConfiguration);
      Require(phases == 0 && legacy_calls == 0 && provider.calls == 0 &&
                  result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired &&
                  source.context.ActiveTrackedPayload() == active,
              "mutual exclusion precedes stop, GLB availability and callbacks");
    }
    auto zero = fixture::Limits();
    zero.files.max_files = 0;
    auto result = ic::PublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        zero, fixture::Work(), stop.get_token());
    Failure(result, ic::GltfFileStatus::kInvalidConfiguration,
            ic::GltfFileDiagnosticCode::kRequiredFieldZero,
            ic::GltfFileStage::kConfiguration);
  }
}
void OrderingAndEmptyReverification() {
  for (const bool inventory : {false, true}) {
    fixture::Fixture source(false);
    Stage stage;
    stage.add_extra = inventory;
    Provider provider{source, &stage};
    auto testing = stage.Testing();
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = kStaticFiles;
    options.verified_files_provider = {Provider::Provide, &provider};
    if (!inventory)
      options.on_phase = [&](auto phase) {
        if (phase == ic::GltfFilePublicationPhase::kVerification) {
          auto binary = fixture::kBinary;
          binary[kTailByte] ^= std::byte{1};
          fixture::Write(stage.path / kBufferPath, binary);
        }
      };
    auto result = detail::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work(), {}, testing);
    Failure(result, ic::GltfFileStatus::kPreservationMismatch,
            inventory ? ic::GltfFileDiagnosticCode::kStagedInventory
                      : ic::GltfFileDiagnosticCode::kDelegatedInspection,
            inventory ? ic::GltfFileStage::kInventory
                      : ic::GltfFileStage::kInspection);
    Require(provider.calls == 0,
            "actual failed initial verification prevents producer");
    Removed(source, stage, result);
  }
  for (const bool mutate : {false, true}) {
    fixture::Fixture source(false);
    Stage stage;
    Provider provider{source, &stage};
    provider.empty = true;
    provider.mutate_tail = mutate;
    auto testing = stage.Testing();
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = kStaticFiles;
    options.verified_files_provider = {Provider::Provide, &provider};
    auto result = detail::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work(), {}, testing);
    Require(provider.calls == 1,
            "empty borrowed producer invoked exactly once");
    if (mutate) {
      Failure(result, ic::GltfFileStatus::kPreservationMismatch,
              ic::GltfFileDiagnosticCode::kResourceBytes,
              ic::GltfFileStage::kInventory);
      Require(result.diagnostic.file_index == 0 &&
                  result.diagnostic.byte_offset == kTailByte,
              "empty span still rechecks first full resource tail byte");
      Removed(source, stage, result);
    } else {
      Require(result.bundle && stage.inventories == 2,
              "empty span still performs final physical inventory");
      CheckFiles(result.bundle->Files(), source, false);
    }
  }
}
void AdmissionAndExceptions() {
  for (const auto path :
       {fixture::kEntry, kExcludedPath, fixture::kDynamicPath}) {
    fixture::Fixture source(false);
    Stage stage;
    Provider provider{source, &stage};
    provider.views[0].path = path;
    auto testing = stage.Testing();
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = kStaticFiles;
    options.verified_files_provider = {Provider::Provide, &provider};
    auto limits = fixture::Limits();
    if (path == fixture::kDynamicPath) limits.files.max_files = kInitialFiles;
    auto result = detail::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        limits, fixture::Work(), {}, testing);
    Failure(result,
            path == fixture::kEntry ? ic::GltfFileStatus::kInvalidRequest
            : path == kExcludedPath ? ic::GltfFileStatus::kUnsupportedProfile
                                    : ic::GltfFileStatus::kLimitExceeded,
            path == fixture::kEntry ? ic::GltfFileDiagnosticCode::kPathCollision
            : path == kExcludedPath
                ? ic::GltfFileDiagnosticCode::kPathUnsupported
                : ic::GltfFileDiagnosticCode::kCountLimit,
            ic::GltfFileStage::kPathPlanning);
    Require(provider.calls == 1,
            "borrowed admission failure follows actual producer");
    if (path == fixture::kEntry)
      Require(result.diagnostic.file_index == kInitialFiles &&
                  stage.dynamic_write_calls == 0,
              "entry collision retains literal first dynamic ordinal");
    Removed(source, stage, result);
  }
  for (const bool allocation : {false, true}) {
    fixture::Fixture source(false);
    Stage stage;
    Provider provider{source, &stage};
    provider.ordinary_throw = !allocation;
    provider.allocation_throw = allocation;
    auto testing = stage.Testing();
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = kStaticFiles;
    options.verified_files_provider = {Provider::Provide, &provider};
    bool propagated = false;
    try {
      auto result = detail::TestPublishGltfTopologyBundle(
          {*source.source, *source.inspection}, source.Destination(), options,
          fixture::Limits(), fixture::Work(), {}, testing);
      Require(!allocation, "producer allocation failure propagates");
      Failure(result, ic::GltfFileStatus::kCallbackFailure,
              ic::GltfFileDiagnosticCode::kVerifiedCallback,
              ic::GltfFileStage::kSupplemental);
      Removed(source, stage, result);
    } catch (const std::bad_alloc&) {
      propagated = true;
    }
    Require(
        propagated == allocation && provider.calls == 1 &&
            !stage.path.empty() && !fs::exists(stage.path) &&
            source.context.ActiveWorkers() == 0,
        "producer exception preserves exact type after joined staging cleanup");
  }
}
enum class PhysicalCap { kFileBytes, kTotalBytes, kPathBytes, kTotalPathBytes };
void BorrowedPhysicalBoundaries() {
  const std::vector<std::byte> payload = [] {
    std::vector<std::byte> bytes(kBoundaryPayloadBytes, kBoundaryPayloadFill);
    bytes.back() = kBoundaryPayloadTail;
    return bytes;
  }();
  Require(payload.size() > fixture::kOpaqueBytes &&
              payload.size() > fixture::kJson.size() &&
              payload.size() > fixture::kBinary.size() &&
              payload.size() > fixture::kStaticBytes.size(),
          "authored borrowed payload dominates every initial physical member");
  for (const auto path : fixture::kInitialPaths)
    Require(kBoundaryPath.size() > path.size(),
            "authored borrowed path dominates every initial relative name");
  for (const auto cap :
       {PhysicalCap::kFileBytes, PhysicalCap::kTotalBytes,
        PhysicalCap::kPathBytes, PhysicalCap::kTotalPathBytes}) {
    for (const bool inclusive : {true, false}) {
      fixture::Fixture source(false);
      Stage stage;
      Provider provider{source, &stage};
      provider.views[0] = {kBoundaryPath, payload};
      auto testing = stage.Testing();
      ic::GltfFilePublicationOptions options{};
      options.supplemental_files = kStaticFiles;
      options.verified_files_provider = {Provider::Provide, &provider};
      auto limits = fixture::Limits();
      const std::uint64_t decrement = inclusive ? 0 : 1;
      switch (cap) {
        case PhysicalCap::kFileBytes:
          limits.files.max_file_bytes = kBoundaryPayloadBytes - decrement;
          break;
        case PhysicalCap::kTotalBytes:
          limits.files.max_total_file_bytes =
              kInitialPhysicalBytes + kBoundaryPayloadBytes - decrement;
          break;
        case PhysicalCap::kPathBytes:
          limits.files.max_path_bytes = kBoundaryPath.size() - decrement;
          break;
        case PhysicalCap::kTotalPathBytes:
          limits.files.max_total_path_bytes =
              kInitialPathBytes + kBoundaryPath.size() - decrement;
          break;
      }
      auto result = detail::TestPublishGltfTopologyBundle(
          {*source.source, *source.inspection}, source.Destination(), options,
          limits, fixture::Work(), {}, testing);
      Require(
          provider.calls == 1,
          "initial members fit so exact physical cap reaches borrowed import");
      if (inclusive) {
        Require(result.bundle && stage.inventories == 2 &&
                    result.bundle->Files().size() == kBoundaryPaths.size(),
                "literal inclusive physical cap publishes full borrowed file");
        for (std::size_t i = 0; i < kBoundaryPaths.size(); ++i) {
          const auto& file = result.bundle->Files()[i];
          Require(file.path == kBoundaryPaths[i],
                  "literal boundary output inventory ordering");
          const auto expected =
              file.path == kBoundaryPath ? std::span<const std::byte>(payload)
              : file.path == fixture::kEntry ? Bytes(fixture::kJson)
              : file.path == kBufferPath
                  ? std::span<const std::byte>(fixture::kBinary)
              : file.path == fixture::kStaticPath
                  ? Bytes(fixture::kStaticBytes)
                  : std::span<const std::byte>(source.opaque);
          Require(
              std::ranges::equal(file.bytes, expected) &&
                  std::ranges::equal(
                      fixture::ReadBytes(source.Destination() / file.path),
                      expected),
              "inclusive output owns exact authored bytes and reopened tail");
        }
      } else {
        Failure(result, ic::GltfFileStatus::kLimitExceeded,
                ic::GltfFileDiagnosticCode::kCountLimit,
                ic::GltfFileStage::kPathPlanning);
        const bool total = cap == PhysicalCap::kTotalBytes ||
                           cap == PhysicalCap::kTotalPathBytes;
        Require(
            result.diagnostic.file_index ==
                    (total ? std::optional<std::uint64_t>(kInitialFiles)
                           : std::nullopt) &&
                !result.diagnostic.byte_offset &&
                stage.dynamic_write_calls == 0,
            "exact optional physical-admission ordinal before dynamic write");
        Removed(source, stage, result);
      }
    }
  }
}
void BorrowedCollisionInventory() {
  for (const auto path :
       {kBufferPath, fixture::kStaticPath, fixture::kDynamicPath}) {
    fixture::Fixture source(false);
    Stage stage;
    Provider provider{source, &stage};
    const bool repeated = path == fixture::kDynamicPath;
    const std::array<ic::GltfFileView, 2> returned{
        {{path, Bytes(fixture::kDynamicBytes)},
         {path, Bytes(fixture::kDynamicBytes)}}};
    provider.returned_views = std::span(returned).first(repeated ? 2 : 1);
    auto testing = stage.Testing();
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = kStaticFiles;
    options.verified_files_provider = {Provider::Provide, &provider};
    auto result = detail::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work(), {}, testing);
    Failure(result, ic::GltfFileStatus::kInvalidRequest,
            ic::GltfFileDiagnosticCode::kPathCollision,
            ic::GltfFileStage::kPathPlanning);
    Require(
        provider.calls == 1 &&
            result.diagnostic.file_index ==
                kInitialFiles + (repeated ? 1 : 0) &&
            !result.diagnostic.byte_offset,
        "resource/static collision ordinal five, prior dynamic ordinal six");
    Require(stage.dynamic_write_calls == 0,
            "whole returned collision inventory admitted before first dynamic "
            "write");
    Removed(source, stage, result);
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == kUnsupportedArgument;
    Require(argc == 1 || unsupported, "known provider fixture invocation");
    SourceRouting(unsupported);
    if (!unsupported) {
      ProducersAndRetention();
      ConfigurationPrecedence();
      OrderingAndEmptyReverification();
      AdmissionAndExceptions();
      BorrowedPhysicalBoundaries();
      BorrowedCollisionInventory();
    }
    std::cout << "actual JSON/GLB owner routing and verified provider "
                 "semantics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
