// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>

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
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_file_publish_translation_fixture.h"
#include "gltf_files_internal.h"
#include "gltf_files_test_internal.h"

namespace meshvale::interchange::test_translation_publication {
namespace {
namespace detail = gltf_files_detail;
struct Stage {
  fs::path path;
  static void Ready(detail::TestNativeView value, void* opaque) {
    static_cast<Stage*>(opaque)->path = fs::path(value);
  }
  detail::GltfFilePublicationTestOptions Options() {
    detail::GltfFilePublicationTestOptions result{};
    result.hooks.stage_ready = Ready;
    result.hooks.state = this;
    return result;
  }
};
void DelegatedFailures(bool glb) {
  geo::ExecutionContext context({4, 1, kRequestBytes});
  {
    Fixture source(context, glb, false, false);
    const auto before = context.ActiveTrackedPayload();
    for (bool inspection : {false, true}) {
      auto limits = Limits();
      if (inspection)
        limits.inspection.direct_translation.max_total_keys = 1;
      else
        limits.extraction.direct_translation.max_total_keys = 1;
      Stage stage;
      const auto result = detail::TestPublishGltfTopologyBundle(
          source.Request(), source.Destination(), {}, limits, Work(), {},
          stage.Options());
      Require(
          !result.bundle &&
              result.phase == ic::GltfFilePublicationPhase::kVerification &&
              result.diagnostic.status ==
                  (inspection ? ic::GltfFileStatus::kLimitExceeded
                              : ic::GltfFileStatus::kUnsupportedProfile) &&
              result.diagnostic.stage ==
                  (inspection ? ic::GltfFileStage::kInspection
                              : ic::GltfFileStage::kExtraction) &&
              result.diagnostic.code ==
                  (inspection
                       ? ic::GltfFileDiagnosticCode::kDelegatedInspection
                       : ic::GltfFileDiagnosticCode::kDelegatedExtraction) &&
              result.translation_extraction &&
              result.translation_extraction->complete == inspection &&
              (inspection ? result.translation_inspection.has_value()
                          : !result.translation_inspection) &&
              (!result.translation_inspection ||
               !result.translation_inspection->complete),
          "real delegated phase maps and preserves actual partial facts");
      if (inspection)
        Require(
            result.diagnostic.inspection_status ==
                    ic::GltfTopologyOutputInspectionStatus::kLimitExceeded &&
                result.diagnostic.inspection_diagnostic &&
                result.diagnostic.inspection_diagnostic->stage ==
                    ic::GltfTopologyOutputInspectionStage::kReloadedPrimitive &&
                result.diagnostic.inspection_diagnostic->code ==
                    ic::GltfTopologyOutputInspectionDiagnosticCode::kCountLimit,
            "actual staged V logical-limit cause remains nested");
      else
        Require(result.diagnostic.gltf_diagnostic &&
                    result.diagnostic.gltf_diagnostic->status ==
                        ic::GltfStatus::kUnsupportedProfile &&
                    result.diagnostic.gltf_diagnostic->code ==
                        ic::GltfDiagnosticCode::kLimitExceeded,
                "actual staged E logical-limit cause remains nested");
      Require(!stage.path.empty() && !fs::exists(stage.path) &&
                  !fs::exists(source.Destination()) &&
                  result.cleanup == ic::GltfFileCleanupOutcome::kRemoved &&
                  context.ActiveTrackedPayload() == before &&
                  context.ActiveWorkers() == 0,
              "actual failed staged E/V cleanup releases requests after joins");
    }
  }
  Require(context.ActiveTrackedPayload() == 0,
          "delegated failure source owners release");
}
void ConfigurationFirst(bool glb) {
  geo::ExecutionContext context({4, 1, kRequestBytes});
  {
    Fixture source(context, glb, false, false);
    const auto before = context.ActiveTrackedPayload();
    std::stop_source stop;
    stop.request_stop();
    constexpr unsigned kConfigurations = 4;
    for (unsigned item = 0; item < kConfigurations; ++item) {
      auto limits = Limits();
      auto work = Work();
      if (item == 0) limits.extraction.direct_translation.max_node_rows = 0;
      if (item == 1) work.reload.direct_translation.max_jobs = 0;
      if (item == 2) limits.inspection.direct_translation.max_node_rows = 0;
      if (item == 3) work.inspection.direct_translation.partition_keys = 0;
      Stage stage;
      std::size_t callbacks = 0;
      ic::GltfFilePublicationOptions options;
      options.on_phase = [&](auto) { ++callbacks; };
      options.on_verified = [&](const auto&) {
        ++callbacks;
        return std::vector<ic::GltfFileSupplement>{};
      };
      const auto result = detail::TestPublishGltfTopologyBundle(
          source.Request(), source.Destination(), options, limits, work,
          stop.get_token(), stage.Options());
      Require(
          !result.bundle &&
              result.outcome == ic::GltfFilePublicationOutcome::kFailed &&
              result.phase == ic::GltfFilePublicationPhase::kPreflight &&
              result.diagnostic.status ==
                  ic::GltfFileStatus::kInvalidConfiguration &&
              result.diagnostic.stage == ic::GltfFileStage::kConfiguration &&
              result.diagnostic.code ==
                  ic::GltfFileDiagnosticCode::kRequiredFieldZero &&
              !result.translation_extraction &&
              !result.translation_inspection &&
              result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired &&
              callbacks == 0 && stage.path.empty() &&
              !fs::exists(source.Destination()) &&
              context.ActiveTrackedPayload() == before,
          "E/V cap/work configuration precedes stop, callbacks and staging");
    }
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "invalid configuration creates no leaked requests or workers");
}
}  // namespace
void NativeFailures() {
  for (bool glb : {false, true}) {
    DelegatedFailures(glb);
    ConfigurationFirst(glb);
  }
}
}  // namespace meshvale::interchange::test_translation_publication

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::gltf_files_detail;
namespace fs = std::filesystem;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kByteLimit = 1024 * 1024;
constexpr std::uint64_t kRequests = 64 * kByteLimit;
constexpr std::uint64_t kDepthLimit = 128;
constexpr std::uint64_t kChannels = 64;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCheckpointBytes = 64;
constexpr std::uint64_t kMaxJobs = 32;
constexpr std::size_t kWorkers = 4;
constexpr unsigned kByteBits = 8;
constexpr std::size_t kWordBytes = 4;
constexpr std::uint32_t kByteMask = 255;
constexpr std::size_t kLogicalBufferBytes = 68;
constexpr std::size_t kPhysicalBufferBytes = 76;
constexpr std::size_t kColorOffset = 48;
constexpr std::size_t kImageOffset = 60;
constexpr std::size_t kPngBytes = 8;
constexpr std::size_t kTailBytes = 8;
constexpr std::size_t kSourceFiles = 4;
constexpr std::string_view kEntry = "meshes/scene.gltf";
constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"../%61.bin","byteLength":68},{"uri":"../a.bin","byteLength":68}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":12},{"buffer":0,"byteOffset":60,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"},{"bufferView":2,"componentType":5121,"count":3,"type":"VEC4","normalized":true}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"COLOR_0":2},"indices":1}]}],"images":[{"uri":"../texture.png"},{"uri":"data:image/png;base64,iVBORw0KGgo="},{"bufferView":3,"mimeType":"image/png"}],"extras":{"literal":1.2300e+04}})";
constexpr std::array<std::uint32_t, 9> kPositionBits{
    0x80000000, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0};
constexpr std::array<std::uint32_t, 6> kOriginalTriangles{0, 1, 2, 0, 1, 2};
constexpr std::array<std::uint32_t, 3> kSelectedTriangles{0, 1, 2};
constexpr std::array<std::uint32_t, 12> kStripTriangles{0, 1, 2, 2, 1, 0,
                                                        2, 0, 1, 1, 0, 2};
constexpr std::array<std::uint32_t, 12> kFanTriangles{0, 1, 2, 0, 2, 1,
                                                      0, 1, 2, 0, 2, 1};
constexpr std::array<std::uint32_t, 6> kFanAuthoredIndices{0, 1, 2, 1, 2, 1};
constexpr std::array<geo::index_t, 1> kKeptFaces{1};
constexpr std::array<geo::index_t, 3> kKeptCorners{3, 4, 5};
constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
constexpr std::array<geo::index_t, 4> kConvertedFaces{0, 1, 2, 3};
constexpr std::array<geo::index_t, 12> kConvertedCorners{0, 1, 2, 3, 4,  5,
                                                         6, 7, 8, 9, 10, 11};
constexpr std::array<std::uint8_t, 12> kColor{255, 0,   0, 255, 0,   128,
                                              0,   255, 0, 0,   255, 255};
constexpr std::array<std::byte, kPngBytes> kPng{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
    std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
constexpr std::array<std::byte, 12> kSelectedIndexBytes{
    std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}};
constexpr std::string_view kSelectedIndexUri =
    "data:application/octet-stream;base64,AAAAAAEAAAACAAAA";
constexpr std::string_view kStaticPath = "notes/static.txt";
constexpr std::string_view kStaticBytes = "literal static bytes\n";
constexpr std::string_view kDynamicPath = "reports/result.json";
constexpr std::string_view kDynamicBytes = "{\"literal\":17}\n";
constexpr std::string_view kPositionSemantic = "POSITION";
constexpr std::string_view kColorSemantic = "COLOR_0";
constexpr std::string_view kUnsupportedProfileArgument =
    "--unsupported-profile";

void Require(bool value, std::string_view message,
             std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(output.is_open(), "actual fixture file opened");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  Require(!output.fail(), "actual fixture full byte write");
}
std::vector<std::byte> ReadBytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  Require(input.is_open(), "actual finalized file opened");
  const std::vector<char> chars((std::istreambuf_iterator<char>(input)), {});
  Require(!input.bad(), "actual finalized full byte read");
  std::vector<std::byte> bytes(chars.size());
  std::transform(chars.begin(), chars.end(), bytes.begin(),
                 [](char c) { return static_cast<std::byte>(c); });
  return bytes;
}
void Word(std::vector<std::byte>& bytes, std::uint32_t word) {
  for (unsigned i = 0; i < kWordBytes; ++i)
    bytes.push_back(
        static_cast<std::byte>((word >> (i * kByteBits)) & kByteMask));
}
void Replace(std::string& text, std::string_view before,
             std::string_view after) {
  const auto offset = text.find(before);
  Require(offset != std::string::npos, "independent literal edit exists");
  text.replace(offset, before.size(), after);
}
struct Fixture {
  fs::path root;
  std::string json = std::string(kJson) + "\n\t   ";
  std::vector<std::byte> binary;
  std::vector<std::byte> image;
  std::vector<std::byte> opaque{kTailBytes, std::byte{0x5b}};
  std::array<std::string_view, 1> opaque_paths{"opaque.bin"};
  bool strip_or_fan = false;
  bool fan = false;
  Fixture() {
    static std::atomic<unsigned> sequence{0};
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-json-publication-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(sequence.fetch_add(1)));
    fs::create_directories(root / "source");
    for (const auto word : kPositionBits) Word(binary, word);
    for (const auto index : kOriginalTriangles) {
      binary.push_back(static_cast<std::byte>(index));
      binary.push_back(std::byte{0});
    }
    Require(binary.size() == kColorOffset, "literal POSITION/index extent");
    for (const auto color : kColor)
      binary.push_back(static_cast<std::byte>(color));
    Require(binary.size() == kImageOffset, "literal color extent");
    binary.insert(binary.end(), kPng.begin(), kPng.end());
    Require(binary.size() == kLogicalBufferBytes,
            "literal logical buffer extent");
    for (std::size_t i = 0; i < kTailBytes; ++i)
      binary.push_back(static_cast<std::byte>(0xe0 + i));
    Require(binary.size() == kPhysicalBufferBytes,
            "literal physical tail extent");
    image.assign(kPng.begin(), kPng.end());
    image.insert(image.end(), kTailBytes, std::byte{0x7a});
    Save();
  }
  ~Fixture() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  fs::path SourceRoot() const { return root / "source"; }
  fs::path Destination(std::string_view name = "published") const {
    return root / name;
  }
  void Save() {
    Write(SourceRoot() / kEntry, Bytes(json));
    Write(SourceRoot() / "a.bin", binary);
    Write(SourceRoot() / "texture.png", image);
    Write(SourceRoot() / "opaque.bin", opaque);
  }
  ic::GltfFileReadOptions Options() const {
    return {SourceRoot(), ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
            opaque_paths};
  }
  void ConvertedMode(bool use_fan) {
    strip_or_fan = true;
    fan = use_fan;
    Replace(
        json, "\"indices\":1}",
        use_fan ? "\"indices\":1,\"mode\":6}" : "\"indices\":1,\"mode\":5}");
    if (use_fan) {
      constexpr std::size_t kIndexOffset = 36;
      constexpr std::size_t kIndexWidth = 2;
      for (std::size_t i = 0; i < kFanAuthoredIndices.size(); ++i)
        binary[kIndexOffset + i * kIndexWidth] =
            static_cast<std::byte>(kFanAuthoredIndices[i]);
    }
    Save();
  }
};
geo::ExecutionContext Context() {
  return geo::ExecutionContext({kWorkers, 1, kRequests});
}
ic::GltfReadOperationLimits ReadLimits() {
  return {{kByteLimit, kByteLimit, kByteLimit, kCountLimit, kDepthLimit,
           kByteLimit, kByteLimit, kByteLimit, kByteLimit, kCountLimit,
           kCountLimit, kCountLimit, kCountLimit, kByteLimit},
          kRequests};
}
ic::GltfFileLimits FileLimits() {
  return {kCountLimit, kCountLimit, kByteLimit, kByteLimit,
          kByteLimit,  kByteLimit,  kByteLimit, kRequests};
}
ic::GltfWorkPolicy ReadWork() {
  return {1, kPartitionBytes, 1, kPartitionBytes};
}
ic::GltfExtractionLimits ExtractionLimits() {
  return {kCountLimit, kCountLimit, kRequests};
}
ic::GltfMaterializationLimits TransferLimits() {
  return {kChannels,
          kByteLimit,
          {kCountLimit, kCountLimit, kCountLimit, kCountLimit, kChannels,
           kCountLimit, kCountLimit, kCountLimit, kCountLimit, kByteLimit,
           kRequests},
          {kCountLimit, kRequests}};
}
geo::MeshMaterializationWorkPolicy GeometryWork() {
  return {1, kPartitionBytes, kCheckpointBytes, kMaxJobs};
}
ic::GltfTopologyOutputInspectionLimits InspectionLimits() {
  return {kByteLimit,  kCountLimit, kDepthLimit, kCountLimit, kByteLimit,
          kCountLimit, kChannels,   kCountLimit, kByteLimit,  kCountLimit,
          kCountLimit, kByteLimit,  kRequests};
}
ic::GltfFilePublicationLimits Limits() {
  return {FileLimits(),
          ReadLimits(),
          ExtractionLimits(),
          InspectionLimits(),
          {kByteLimit, kCountLimit, kRequests}};
}
ic::GltfFilePublicationWorkPolicy Work(bool serial = false) {
  const auto threshold = serial
                             ? static_cast<std::uint64_t>(
                                   std::numeric_limits<std::ptrdiff_t>::max())
                             : 1;
  return {{kCheckpointBytes, threshold, kPartitionBytes, kCheckpointBytes,
           kMaxJobs},
          {threshold, kPartitionBytes, threshold, kPartitionBytes},
          {threshold, kPartitionBytes, kCheckpointBytes, kMaxJobs},
          {threshold, kPartitionBytes}};
}
ic::GltfFileAsset Capture(const Fixture& fixture,
                          const geo::ExecutionContext& context) {
  const auto result = ic::ReadGltfFileAsset(
      fixture.SourceRoot() / kEntry, fixture.Options(),
      {FileLimits(), ReadLimits(), {kByteLimit, kCountLimit, kRequests}},
      {{kCheckpointBytes, 1, kPartitionBytes, kCheckpointBytes, kMaxJobs},
       ReadWork(),
       {1, kPartitionBytes}},
      context);
  Require(result.asset.has_value(), "actual independent file source");
  return *result.asset;
}
void NativeHandleLifetimeAndRegularResources() {
  constexpr std::uint64_t kObservedFileOrdinal = 17;
  struct Observer {
    std::size_t before_open = 0;
    std::size_t opened = 0;
    std::size_t closed = 0;
    std::uint64_t opened_ordinal = 0;
    std::uint64_t closed_ordinal = 0;
    detail::GltfFileHooks* input_table = nullptr;
    bool mutate_on_open = false;
    static void BeforeOpen(std::uint64_t, void* state) noexcept {
      auto& observer = *static_cast<Observer*>(state);
      ++observer.before_open;
      observer.input_table->opened = nullptr;
      observer.input_table->closed = nullptr;
    }
    static void Opened(std::uint64_t ordinal, void* state) noexcept {
      auto& observer = *static_cast<Observer*>(state);
      ++observer.opened;
      observer.opened_ordinal = ordinal;
      if (observer.mutate_on_open) observer.input_table->closed = nullptr;
    }
    static void Closed(std::uint64_t ordinal, void* state) noexcept {
      auto& observer = *static_cast<Observer*>(state);
      ++observer.closed;
      observer.closed_ordinal = ordinal;
    }
    detail::GltfFileHooks Hooks() {
      return {nullptr, nullptr, Opened, Closed, this};
    }
  } observer;
  Fixture fixture;
  const auto path = fixture.SourceRoot() / "a.bin";
  {
    auto hooks = observer.Hooks();
    auto handle = detail::OpenFile(path.native(), kObservedFileOrdinal, hooks);
    Require(observer.opened == 1 && observer.closed == 0 &&
                observer.opened_ordinal == kObservedFileOrdinal,
            "actual native file opened with independent observer state");
    hooks.closed = nullptr;
    handle.Close();
    Require(
        observer.closed == 1 && observer.closed_ordinal == kObservedFileOrdinal,
        "open handle owns original hook table after live input mutation");
  }
  const auto open_from_local_table = [&] {
    const auto hooks = observer.Hooks();
    return detail::OpenFile(path.native(), kObservedFileOrdinal, hooks);
  };
  struct HandleFailure final : std::exception {};
  bool propagated = false;
  try {
    auto handle = open_from_local_table();
    Require(detail::Metadata(handle).size == kPhysicalBufferBytes,
            "escaped native handle preserves literal full regular-file size");
    throw HandleFailure{};
  } catch (const HandleFailure&) {
    propagated = true;
  }
  Require(
      propagated && observer.opened == 2 && observer.closed == 2 &&
          observer.closed_ordinal == kObservedFileOrdinal,
      "escaped local hook table closes actual handle during exception unwind");
  for (const bool mutate_on_open : {false, true}) {
    const auto opened = observer.opened;
    const auto closed = observer.closed;
    auto hooks = observer.Hooks();
    observer.input_table = &hooks;
    observer.mutate_on_open = mutate_on_open;
    if (!mutate_on_open) hooks.before_open = Observer::BeforeOpen;
    auto handle = detail::OpenFile(path.native(), kObservedFileOrdinal, hooks);
    handle.Close();
    Require(observer.opened == opened + 1 && observer.closed == closed + 1,
            "before-open and opened callbacks retain original dispatch table");
    observer.input_table = nullptr;
  }
  Require(observer.before_open == 1,
          "actual before-open callback mutates only borrowed input table");
  observer.mutate_on_open = false;
  {
    const auto opened = observer.opened;
    const auto closed = observer.closed;
    const auto directory = fixture.SourceRoot();
    const auto hooks = observer.Hooks();
    bool rejected_regular_role = false;
    try {
      auto handle = detail::OpenFile(directory.native(), kObservedFileOrdinal,
                                     hooks, true);
      (void)detail::Metadata(handle);
    } catch (const detail::Failure& failure) {
      rejected_regular_role =
          failure.diagnostic.status ==
              ic::GltfFileStatus::kUnsupportedProfile &&
          failure.diagnostic.code ==
              ic::GltfFileDiagnosticCode::kResourceNotRegularFile;
    }
    Require(rejected_regular_role && observer.opened == opened + 1 &&
                observer.closed == closed + 1,
            "ordinary-resource metadata rejects a real directory handle");
  }
  Require(fs::remove(path) && fs::create_directory(path),
          "literal buffer URI now resolves to an actual directory");
  auto context = Context();
  const auto rejected = ic::ReadGltfFileAsset(
      fixture.SourceRoot() / kEntry, fixture.Options(),
      {FileLimits(), ReadLimits(), {kByteLimit, kCountLimit, kRequests}},
      {{kCheckpointBytes, 1, kPartitionBytes, kCheckpointBytes, kMaxJobs},
       ReadWork(),
       {1, kPartitionBytes}},
      context);
#ifdef _WIN32
  constexpr auto kDirectoryStatus = ic::GltfFileStatus::kIoFailure;
  constexpr auto kDirectoryCode = ic::GltfFileDiagnosticCode::kResourceMissing;
  constexpr auto kDirectoryStage = ic::GltfFileStage::kCapture;
#else
  constexpr auto kDirectoryStatus = ic::GltfFileStatus::kUnsupportedProfile;
  constexpr auto kDirectoryCode =
      ic::GltfFileDiagnosticCode::kResourceNotRegularFile;
  constexpr auto kDirectoryStage = ic::GltfFileStage::kPathPlanning;
#endif
  Require(!rejected.asset && rejected.diagnostic.status == kDirectoryStatus &&
              rejected.diagnostic.code == kDirectoryCode &&
              rejected.diagnostic.stage == kDirectoryStage &&
              context.ActiveTrackedPayload() == 0,
          "native first failure refuses directory buffer without owner or "
          "retained charge");
#ifdef _WIN32
  Require(static_cast<bool>(rejected.diagnostic.system_error),
          "actual Windows directory-open failure retains its system error");
#endif
}
ic::GltfPrimitiveExtraction Extract(const ic::GltfAsset& asset,
                                    bool convert = false) {
  const auto result = ic::ExtractGltfPrimitiveCapped(
      asset, *asset.Primitive(0),
      {convert ? ic::GltfTriangleRequest::kConvertStripOrFan
               : ic::GltfTriangleRequest::kRequireTriangles,
       ic::GltfValueRequest::kRawAndNormalized},
      ExtractionLimits());
  Require(result.extraction.has_value(),
          "actual normalized primitive extraction");
  return *result.extraction;
}
void CheckChannels(const ic::GltfPrimitiveExtraction& extraction) {
  const auto channels = extraction.Channels();
  Require(channels.size() == 2, "literal POSITION/COLOR inventory");
  const auto positions = std::get<std::span<const float>>(channels[0].raw);
  Require(channels[0].semantic == kPositionSemantic &&
              positions.size() == kPositionBits.size(),
          "literal POSITION shape");
  for (std::size_t i = 0; i < positions.size(); ++i)
    Require(std::bit_cast<std::uint32_t>(positions[i]) == kPositionBits[i],
            "literal raw signed-zero POSITION bits");
  const auto colors = std::get<std::span<const std::uint8_t>>(channels[1].raw);
  Require(
      channels[1].semantic == kColorSemantic && channels[1].set_index == 0 &&
          channels[1].normalized && std::ranges::equal(colors, kColor) &&
          channels[1].normalized_values.size() == kColor.size(),
      "literal normalized COLOR descriptor/raw inventory semantic=" +
          std::string(channels[1].semantic) +
          " normalized=" + std::to_string(channels[1].normalized) + " set=" +
          (channels[1].set_index ? std::to_string(*channels[1].set_index)
                                 : "absent") +
          " values=" + std::to_string(channels[1].normalized_values.size()));
  constexpr double kColorMaximum = 255;
  for (std::size_t i = 0; i < kColor.size(); ++i)
    Require(channels[1].normalized_values[i] == kColor[i] / kColorMaximum,
            "independent complete normalized color values");
}
struct Workflow {
  geo::ExecutionContext context = Context();
  ic::GltfFileAsset file;
  std::optional<ic::GltfMaterializedPrimitive> source;
  std::optional<geo::MeshInspection> candidate;
  std::optional<ic::GltfTopologyWriteBundle> output;
  std::optional<ic::GltfTopologyOutputInspection> inspection;
  bool changed;
  bool convert;
  std::span<const std::uint32_t> expected_topology;
  Workflow(Fixture& fixture, bool changed)
      : file(Capture(fixture, context)),
        changed(changed),
        convert(fixture.strip_or_fan),
        expected_topology(
            changed
                ? std::span<const std::uint32_t>(kSelectedTriangles)
                : (fixture.strip_or_fan
                       ? (fixture.fan
                              ? std::span<const std::uint32_t>(kFanTriangles)
                              : std::span<const std::uint32_t>(kStripTriangles))
                       : std::span<const std::uint32_t>(kOriginalTriangles))) {
    Require(!convert || !changed, "converted fixture covers actual no-op");
    const auto extraction = Extract(file.Asset(), convert);
    Require(std::ranges::equal(
                extraction.TriangleIndices(),
                convert ? expected_topology
                        : std::span<const std::uint32_t>(kOriginalTriangles)),
            "independent literal directed source triangles");
    CheckChannels(extraction);
    const auto transfer = ic::MaterializeGltfPrimitive(
        extraction, TransferLimits(), GeometryWork());
    Require(transfer.primitive.has_value(),
            "actual supported numerical materialization");
    source = *transfer.primitive;
    const auto faces =
        changed ? std::span<const geo::index_t>(kKeptFaces)
                : (convert ? std::span<const geo::index_t>(kConvertedFaces)
                           : std::span<const geo::index_t>(kIdentityFaces));
    const auto corners =
        changed ? std::span<const geo::index_t>(kKeptCorners)
                : (convert ? std::span<const geo::index_t>(kConvertedCorners)
                           : std::span<const geo::index_t>(kIdentityCorners));
    if (changed) {
      const auto selection = geo::SelectMeshFaces(
          source->Mesh(), faces, TransferLimits().materialization,
          GeometryWork());
      Require(selection.selection.has_value(),
              "actual Geometry face selection");
      const auto checked = geo::InspectMaterializedMesh(
          selection.selection->Candidate(), TransferLimits().inspection,
          GeometryWork());
      Require(checked.inspection.has_value(), "actual candidate inspection");
      candidate = *checked.inspection;
    } else
      candidate = source->Inspection();
    const ic::GltfTopologyWriteLimits write_limits{
        kByteLimit,
        kCountLimit,
        kDepthLimit,
        kCountLimit,
        kChannels,
        kCountLimit,
        kByteLimit,
        kCountLimit,
        kCountLimit,
        kByteLimit,
        {kByteLimit, kChannels, kByteLimit, kByteLimit, kRequests}};
    const auto written = ic::WriteGltfTopologySelection(
        {*source, *candidate, faces, corners}, write_limits,
        {1, kPartitionBytes, kCheckpointBytes, kMaxJobs});
    Require(written.bundle.has_value(), "writer supplies immutable input only");
    output = *written.bundle;
    if (changed) {
      Require(output->ChangedBinding().has_value(), "actual changed binding");
      const auto& resource = output->Resources().back();
      Require(
          resource.authored_uri == kSelectedIndexUri &&
              std::ranges::equal(resource.bytes, kSelectedIndexBytes),
          "independent twelve little-endian UINT32 index bytes/URI actual=" +
              std::string(resource.authored_uri) +
              " bytes=" + std::to_string(resource.bytes.size()));
    } else
      Require(std::ranges::equal(output->Json(), Bytes(fixture.json)),
              "literal no-op preserves complete authored JSON");
    std::vector<ic::SuppliedGltfResource> bindings;
    for (const auto& resource : output->Resources())
      if (resource.origin == ic::GltfResourceOrigin::kCallerBinding)
        bindings.push_back(
            {resource.kind, resource.table_index, resource.bytes});
    const auto read = ic::ReadGltfAssetCapped(
        output->Json(), bindings, ReadLimits(), ReadWork(), context);
    Require(read.asset.has_value(), "actual independent memory reload");
    const auto reloaded = Extract(*read.asset, convert);
    CheckChannels(reloaded);
    const auto checked = ic::InspectGltfTopologyOutput(
        {*source, *candidate, faces, corners, *output, *read.asset, reloaded},
        InspectionLimits(), {1, kPartitionBytes, kCheckpointBytes, kMaxJobs});
    Require(
        checked.inspection.has_value() &&
            checked.status == ic::GltfTopologyOutputInspectionStatus::kSuccess,
        "all actual independent memory output sections");
    inspection = *checked.inspection;
  }
  ic::GltfFilePublicationRequest Request() const { return {file, *inspection}; }
};
void Failure(const ic::GltfFilePublicationResult& result,
             ic::GltfFileStatus status, ic::GltfFileDiagnosticCode code,
             std::source_location where = std::source_location::current()) {
  Require(
      !result.bundle &&
          result.outcome != ic::GltfFilePublicationOutcome::kPublished &&
          result.diagnostic.status == status && result.diagnostic.code == code,
      "typed publication rejection status=" +
          std::to_string(static_cast<int>(result.diagnostic.status)) +
          " code=" + std::to_string(static_cast<int>(result.diagnostic.code)) +
          " inspection=" +
          (result.diagnostic.inspection_status
               ? std::to_string(
                     static_cast<int>(*result.diagnostic.inspection_status))
               : "absent") +
          "/" +
          (result.diagnostic.inspection_diagnostic
               ? std::to_string(static_cast<int>(
                     result.diagnostic.inspection_diagnostic->code))
               : "absent") +
          " expected=" + std::to_string(static_cast<int>(status)) + "/" +
          std::to_string(static_cast<int>(code)),
      where);
}
void Success(const ic::GltfFilePublicationResult& result) {
  Require(
      result.bundle &&
          result.outcome == ic::GltfFilePublicationOutcome::kPublished &&
          result.phase == ic::GltfFilePublicationPhase::kPublication &&
          result.diagnostic.status == ic::GltfFileStatus::kSuccess &&
          result.diagnostic.stage == ic::GltfFileStage::kComplete &&
          result.diagnostic.code == ic::GltfFileDiagnosticCode::kNone &&
          result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired &&
          !result.translation_extraction && !result.translation_inspection,
      "actual no-replace published result status=" +
          std::to_string(static_cast<int>(result.diagnostic.status)) +
          " code=" + std::to_string(static_cast<int>(result.diagnostic.code)));
}
void CheckFiles(std::span<const ic::GltfFileView> files, const Fixture& fixture,
                std::span<const std::byte> entry, bool with_static,
                bool with_dynamic) {
  std::vector<ic::GltfFileView> expected{{"a.bin", fixture.binary},
                                         {kEntry, entry},
                                         {"opaque.bin", fixture.opaque},
                                         {"texture.png", fixture.image}};
  if (with_static) expected.push_back({kStaticPath, Bytes(kStaticBytes)});
  if (with_dynamic) expected.push_back({kDynamicPath, Bytes(kDynamicBytes)});
  std::ranges::sort(expected, {}, &ic::GltfFileView::path);
  Require(files.size() == expected.size(),
          "independent complete physical inventory count");
  for (std::size_t i = 0; i < files.size(); ++i)
    Require(files[i].path == expected[i].path &&
                std::ranges::equal(files[i].bytes, expected[i].bytes),
            "independent sorted full physical bytes/tails/supplements");
}
void Reopen(const fs::path& destination, const Fixture& fixture,
            Workflow& workflow, bool with_static = false,
            bool with_dynamic = false) {
  std::vector<std::string_view> opaque{"opaque.bin"};
  if (with_static) opaque.push_back(kStaticPath);
  if (with_dynamic) opaque.push_back(kDynamicPath);
  const auto reopened = ic::ReadGltfFileAsset(
      destination / kEntry,
      {destination, ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
       opaque},
      {FileLimits(), ReadLimits(), {kByteLimit, kCountLimit, kRequests}},
      {{kCheckpointBytes, 1, kPartitionBytes, kCheckpointBytes, kMaxJobs},
       ReadWork(),
       {1, kPartitionBytes}},
      workflow.context);
  Require(reopened.asset.has_value(), "actual finalized public reopen");
  CheckFiles(reopened.asset->Files(), fixture, workflow.output->Json(),
             with_static, with_dynamic);
  const auto extraction = Extract(reopened.asset->Asset(), workflow.convert);
  CheckChannels(extraction);
  Require(std::ranges::equal(extraction.TriangleIndices(),
                             workflow.expected_topology),
          "actual finalized directed topology against literal indices");
  Require(ReadBytes(destination / kEntry) ==
              std::vector<std::byte>(workflow.output->Json().begin(),
                                     workflow.output->Json().end()),
          "exact provided immutable entry output committed without serializer");
}
void ChangedAndNoop() {
  for (const bool changed : {false, true}) {
    Fixture fixture;
    Workflow workflow(fixture, changed);
    fs::remove_all(fixture.SourceRoot());
    const auto before = workflow.context.ActiveTrackedPayload();
    {
      const auto result = ic::PublishGltfTopologyBundle(
          workflow.Request(), fixture.Destination(), {}, Limits(), Work());
      Success(result);
      Require(result.bundle->Format() == ic::GltfFileFormat::kGltfJson &&
                  !result.bundle->Container() &&
                  result.bundle->EntryPath() == kEntry,
              "same JSON format and relative entry");
      CheckFiles(result.bundle->Files(), fixture, workflow.output->Json(),
                 false, false);
      const auto live = workflow.context.ActiveTrackedPayload();
      auto copy = *result.bundle;
      Require(
          workflow.context.ActiveTrackedPayload() == live &&
              std::ranges::equal(copy.EntryBytes(), workflow.output->Json()),
          "returned immutable bundle copy adds no requests");
      Reopen(fixture.Destination(), fixture, workflow);
    }
    Require(workflow.context.ActiveTrackedPayload() == before &&
                workflow.context.ActiveWorkers() == 0,
            "published/readback owners release their fresh scopes");
  }
}
void UnchangedStripAndFan() {
  for (const bool fan : {false, true}) {
    Fixture fixture;
    fixture.ConvertedMode(fan);
    Workflow workflow(fixture, false);
    fs::remove_all(fixture.SourceRoot());
    const auto result = ic::PublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), {}, Limits(), Work());
    Success(result);
    CheckFiles(result.bundle->Files(), fixture, Bytes(fixture.json), false,
               false);
    Reopen(fixture.Destination(), fixture, workflow);
  }
}
void EntryAsUnusedResource() {
  for (const bool changed : {false, true}) {
    Fixture fixture;
    Replace(fixture.json, "{\"uri\":\"../a.bin\",\"byteLength\":68}]",
            "{\"uri\":\"../a.bin\",\"byteLength\":68},"
            "{\"uri\":\"scene.gltf\",\"byteLength\":1}]");
    fixture.Save();
    Workflow workflow(fixture, changed);
    fs::remove_all(fixture.SourceRoot());
    const auto result = ic::PublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), {}, Limits(), Work());
    if (changed) {
      Failure(result, ic::GltfFileStatus::kPreservationMismatch,
              ic::GltfFileDiagnosticCode::kDelegatedInspection);
      Require(result.diagnostic.inspection_status ==
                      ic::GltfTopologyOutputInspectionStatus::kOutputMismatch &&
                  !fs::exists(fixture.Destination()),
              "actual staged entry self-resource differs from retained "
              "original bytes");
    } else {
      Success(result);
      Require(
          result.bundle->Files().size() == kSourceFiles,
          "self-resource aliases entry without creating fifth physical file");
      Reopen(fixture.Destination(), fixture, workflow);
    }
  }
}
void SupplementsAndRetainedCallback() {
  Fixture fixture;
  Workflow workflow(fixture, true);
  fs::remove_all(fixture.SourceRoot());
  const std::array<ic::GltfFileView, 1> static_files{
      {{kStaticPath, Bytes(kStaticBytes)}}};
  std::optional<ic::GltfTopologyOutputInspection> retained;
  std::vector<ic::GltfFilePublicationPhase> phases;
  ic::GltfFilePublicationOptions options;
  options.supplemental_files = static_files;
  options.on_phase = [&](auto phase) { phases.push_back(phase); };
  options.on_verified = [&](const ic::GltfFileVerifiedContent& content) {
    Require(content.format == ic::GltfFileFormat::kGltfJson &&
                !content.container && content.entry_path == kEntry,
            "actual verified JSON content callback");
    CheckFiles(content.files, fixture, workflow.output->Json(), true, false);
    retained = content.inspection;
    Require(std::ranges::equal(
                content.inspection.ReloadedExtraction().TriangleIndices(),
                kSelectedTriangles),
            "callback actual staged inspection");
    return std::vector<ic::GltfFileSupplement>{
        {std::string(kDynamicPath),
         std::vector<std::byte>(Bytes(kDynamicBytes).begin(),
                                Bytes(kDynamicBytes).end())}};
  };
  const auto result = ic::PublishGltfTopologyBundle(
      workflow.Request(), fixture.Destination(), options, Limits(), Work());
  Success(result);
  constexpr std::array kPhases{ic::GltfFilePublicationPhase::kPreflight,
                               ic::GltfFilePublicationPhase::kStaging,
                               ic::GltfFilePublicationPhase::kVerification,
                               ic::GltfFilePublicationPhase::kPublication};
  Require(std::ranges::equal(phases, kPhases),
          "synchronous named phases once before work");
  CheckFiles(result.bundle->Files(), fixture, workflow.output->Json(), true,
             true);
  Reopen(fixture.Destination(), fixture, workflow, true, true);
  Require(retained.has_value(), "actual callback inspection copied by caller");
}
ic::GltfPublishedBundle PublishFromDestroyedInputs() {
  Fixture fixture;
  Workflow workflow(fixture, true);
  fs::remove_all(fixture.SourceRoot());
  const auto result = ic::PublishGltfTopologyBundle(
      workflow.Request(), fixture.Destination(), {}, Limits(), Work());
  Success(result);
  return *result.bundle;
}
void RetainedPublishedOwner() {
  auto owner = PublishFromDestroyedInputs();
  auto copy = owner;
  Require(copy.Source().Files().size() == kSourceFiles &&
              copy.Files().size() == kSourceFiles &&
              copy.EntryPath() == kEntry &&
              std::ranges::equal(
                  copy.Inspection().ReloadedExtraction().TriangleIndices(),
                  kSelectedTriangles),
          "published owners retain actual staged/source storage after every "
          "wrapper/file dies");
  CheckChannels(copy.Inspection().ReloadedExtraction());
  const auto buffer =
      std::ranges::find(copy.Files(), "a.bin", &ic::GltfFileView::path);
  Require(buffer != copy.Files().end() &&
              buffer->bytes.size() == kPhysicalBufferBytes &&
              buffer->bytes[kLogicalBufferBytes] == std::byte{0xe0} &&
              buffer->bytes.back() == std::byte{0xe7},
          "complete physical tails remain after caller/root destruction");
}
void RejectedPreflight() {
  Fixture fixture;
  Workflow workflow(fixture, true);
  const auto before = workflow.context.ActiveTrackedPayload();
  constexpr std::array kInvalidPaths{"../outside.txt",
                                     "/absolute.txt",
                                     "drive:relative",
                                     "a\\b.txt",
                                     "a//b.txt",
                                     "a/./b.txt",
                                     "a/../b.txt",
                                     "a/",
                                     ""};
  for (const auto path : kInvalidPaths) {
    const std::array<ic::GltfFileView, 1> supplements{
        {{path, Bytes(kStaticBytes)}}};
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = supplements;
    const auto result = ic::PublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), options, Limits(), Work());
    Failure(result, ic::GltfFileStatus::kUnsupportedProfile,
            ic::GltfFileDiagnosticCode::kPathUnsupported);
    Require(result.phase == ic::GltfFilePublicationPhase::kPreflight &&
                result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired &&
                !fs::exists(fixture.Destination()),
            "invalid supplemental path creates no staging/destination");
  }
  constexpr std::array kCollisionPaths{kEntry, std::string_view("a.bin"),
                                       std::string_view("a.bin/nested")};
  for (const auto path : kCollisionPaths) {
    const std::array<ic::GltfFileView, 1> supplements{
        {{path, Bytes(kStaticBytes)}}};
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = supplements;
    Failure(
        ic::PublishGltfTopologyBundle(workflow.Request(), fixture.Destination(),
                                      options, Limits(), Work()),
        ic::GltfFileStatus::kInvalidRequest,
        ic::GltfFileDiagnosticCode::kPathCollision);
  }
  Fixture foreign;
  const auto foreign_source = Capture(foreign, workflow.context);
  const auto identity = ic::PublishGltfTopologyBundle(
      {foreign_source, *workflow.inspection}, fixture.Destination(), {},
      Limits(), Work());
  Failure(identity, ic::GltfFileStatus::kSourceContractFailure,
          ic::GltfFileDiagnosticCode::kSourceIdentity);
  auto other_context = Context();
  const auto other_ledger_source = Capture(foreign, other_context);
  Failure(ic::PublishGltfTopologyBundle(
              {other_ledger_source, *workflow.inspection},
              fixture.Destination(), {}, Limits(), Work()),
          ic::GltfFileStatus::kForeignBudget,
          ic::GltfFileDiagnosticCode::kForeignBudget);
  const std::array<ic::GltfFileView, 1> colliding{
      {{"a.bin", Bytes(kStaticBytes)}}};
  ic::GltfFilePublicationOptions colliding_options{};
  colliding_options.supplemental_files = colliding;
  Failure(ic::PublishGltfTopologyBundle(
              {other_ledger_source, *workflow.inspection},
              fixture.Destination(), colliding_options, Limits(), Work()),
          ic::GltfFileStatus::kInvalidRequest,
          ic::GltfFileDiagnosticCode::kPathCollision);
  auto wrong_limits = Limits();
  wrong_limits.files.max_files = 0;
  Failure(
      ic::PublishGltfTopologyBundle(workflow.Request(), fixture.Destination(),
                                    {}, wrong_limits, Work()),
      ic::GltfFileStatus::kInvalidConfiguration,
      ic::GltfFileDiagnosticCode::kRequiredFieldZero);
  Require(workflow.context.ActiveWorkers() == 0 &&
              workflow.context.ActiveTrackedPayload() >= before,
          "preflight leaves retained inputs available without workers");
}
struct StageCapture {
  fs::path path;
  static void Ready(detail::TestNativeView path, void* opaque) {
    static_cast<StageCapture*>(opaque)->path = fs::path(path);
  }
  detail::GltfFilePublicationTestOptions Testing() {
    detail::GltfFilePublicationTestOptions options{};
    options.hooks.stage_ready = Ready;
    options.hooks.state = this;
    return options;
  }
};
void CheckRemoved(const ic::GltfFilePublicationResult& result,
                  const StageCapture& stage, const fs::path& destination) {
  Require(!stage.path.empty() && !fs::exists(stage.path) &&
              !fs::exists(destination) &&
              result.cleanup == ic::GltfFileCleanupOutcome::kRemoved,
          "actual staged root removed without published destination");
}
void StagedByteChanges() {
  constexpr std::size_t kEarlierByte = 2;
  constexpr std::size_t kLaterByte = 5;
  constexpr std::size_t kTailMismatch = kLogicalBufferBytes + kEarlierByte;
  for (const bool after_callback : {false, true}) {
    Fixture fixture;
    Workflow workflow(fixture, true);
    StageCapture stage;
    auto test = stage.Testing();
    const auto mutate = [&] {
      Require(!stage.path.empty(), "actual exclusive stage observed");
      auto changed = fixture.binary;
      changed[after_callback ? kEarlierByte : kTailMismatch] ^= std::byte{1};
      if (after_callback) changed[kLaterByte] ^= std::byte{1};
      Write(stage.path / "a.bin", changed);
      auto opaque = fixture.opaque;
      opaque.front() ^= std::byte{1};
      Write(stage.path / "opaque.bin", opaque);
    };
    ic::GltfFilePublicationOptions options{};
    if (after_callback)
      options.on_verified = [&](const auto&) {
        mutate();
        return std::vector<ic::GltfFileSupplement>{};
      };
    else
      options.on_phase = [&](auto phase) {
        if (phase == ic::GltfFilePublicationPhase::kVerification) mutate();
      };
    const auto result = detail::TestPublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), options, Limits(), Work(),
        {}, test);
    Failure(result, ic::GltfFileStatus::kPreservationMismatch,
            after_callback ? ic::GltfFileDiagnosticCode::kResourceBytes
                           : ic::GltfFileDiagnosticCode::kDelegatedInspection);
    if (after_callback)
      Require(result.diagnostic.file_index == 0 &&
                  result.diagnostic.byte_offset == kEarlierByte,
              "UTF8 first path and literal first unequal byte win");
    else
      Require(result.diagnostic.inspection_status ==
                  ic::GltfTopologyOutputInspectionStatus::kOutputMismatch,
              "actual full resource tail reaches independent inspection");
    CheckRemoved(result, stage, fixture.Destination());
  }
}
void CallbackFailuresAndStop() {
  constexpr std::array kPhases{ic::GltfFilePublicationPhase::kPreflight,
                               ic::GltfFilePublicationPhase::kStaging,
                               ic::GltfFilePublicationPhase::kVerification,
                               ic::GltfFilePublicationPhase::kPublication};
  for (const auto phase : kPhases) {
    Fixture fixture;
    Workflow workflow(fixture, true);
    StageCapture stage;
    auto testing = stage.Testing();
    std::stop_source stop;
    ic::GltfFilePublicationOptions options{};
    options.on_phase = [&](auto actual) {
      if (actual == phase) stop.request_stop();
    };
    const auto result = detail::TestPublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), options, Limits(), Work(),
        stop.get_token(), testing);
    Failure(result, ic::GltfFileStatus::kCanceled,
            ic::GltfFileDiagnosticCode::kStopRequested);
    Require(result.outcome == ic::GltfFilePublicationOutcome::kCanceled &&
                result.phase == phase && !fs::exists(fixture.Destination()),
            "fresh stop in each actual public phase prevents publication");
    if (stage.path.empty())
      Require(result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired,
              "no staged root before early stop");
    else
      CheckRemoved(result, stage, fixture.Destination());
  }
  for (const bool verified : {false, true}) {
    Fixture fixture;
    Workflow workflow(fixture, true);
    StageCapture stage;
    auto testing = stage.Testing();
    ic::GltfFilePublicationOptions options{};
    if (verified)
      options.on_verified =
          [](const auto&) -> std::vector<ic::GltfFileSupplement> {
        throw std::runtime_error("literal callback failure");
      };
    else
      options.on_phase = [](auto phase) {
        if (phase == ic::GltfFilePublicationPhase::kVerification)
          throw std::runtime_error("literal progress failure");
      };
    const auto result = detail::TestPublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), options, Limits(), Work(),
        {}, testing);
    Failure(result, ic::GltfFileStatus::kCallbackFailure,
            verified ? ic::GltfFileDiagnosticCode::kVerifiedCallback
                     : ic::GltfFileDiagnosticCode::kProgressCallback);
    CheckRemoved(result, stage, fixture.Destination());
  }
}
void DynamicRejectionAndAllocationException() {
  constexpr std::array kDynamicCollisions{
      kEntry, std::string_view("a.bin"), std::string_view("A.BIN"), kStaticPath,
      std::string_view("a.bin/subfile")};
  for (const auto path : kDynamicCollisions) {
    Fixture fixture;
    Workflow workflow(fixture, true);
    StageCapture stage;
    auto testing = stage.Testing();
    const std::array<ic::GltfFileView, 1> static_files{
        {{kStaticPath, Bytes(kStaticBytes)}}};
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = static_files;
    options.on_verified = [&](const auto& content) {
      CheckFiles(content.files, fixture, workflow.output->Json(), true, false);
      return std::vector<ic::GltfFileSupplement>{
          {std::string(path), std::vector<std::byte>{std::byte{0x17}}}};
    };
    const auto result = detail::TestPublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), options, Limits(), Work(),
        {}, testing);
    Failure(result, ic::GltfFileStatus::kInvalidRequest,
            ic::GltfFileDiagnosticCode::kPathCollision);
    CheckRemoved(result, stage, fixture.Destination());
  }
  Fixture fixture;
  Workflow workflow(fixture, true);
  StageCapture stage;
  auto testing = stage.Testing();
  const auto before = workflow.context.ActiveTrackedPayload();
  ic::GltfFilePublicationOptions options{};
  options.on_verified = [](const auto&) -> std::vector<ic::GltfFileSupplement> {
    throw std::bad_alloc();
  };
  bool propagated = false;
  try {
    (void)detail::TestPublishGltfTopologyBundle(workflow.Request(),
                                                fixture.Destination(), options,
                                                Limits(), Work(), {}, testing);
  } catch (const std::bad_alloc&) {
    propagated = true;
  }
  Require(
      propagated && !stage.path.empty() && !fs::exists(stage.path) &&
          !fs::exists(fixture.Destination()) &&
          workflow.context.ActiveWorkers() == 0 &&
          workflow.context.ActiveTrackedPayload() == before,
      "actual callback allocation exception propagates after joined cleanup");
}
void DestinationRacesAndInventory() {
  for (const bool racing : {false, true}) {
    Fixture fixture;
    Workflow workflow(fixture, true);
    StageCapture stage;
    auto testing = stage.Testing();
    if (racing) {
      testing.hooks.before_commit = [](detail::TestNativeView, void* opaque) {
        const auto& target = *static_cast<const fs::path*>(opaque);
        fs::create_directory(target);
      };
      // The stage callback and commit callback require the same per-call state.
      struct Race {
        StageCapture stage;
        fs::path target;
      } race{{}, fixture.Destination()};
      testing.hooks.state = &race;
      testing.hooks.stage_ready = [](detail::TestNativeView path,
                                     void* opaque) {
        static_cast<Race*>(opaque)->stage.path = fs::path(path);
      };
      testing.hooks.before_commit = [](detail::TestNativeView, void* opaque) {
        fs::create_directory(static_cast<Race*>(opaque)->target);
      };
      const auto result = detail::TestPublishGltfTopologyBundle(
          workflow.Request(), fixture.Destination(), {}, Limits(), Work(), {},
          testing);
      Failure(result, ic::GltfFileStatus::kDestinationExists,
              ic::GltfFileDiagnosticCode::kDestinationExists);
      Require(fs::is_empty(fixture.Destination()) &&
                  !fs::exists(race.stage.path) &&
                  result.cleanup == ic::GltfFileCleanupOutcome::kRemoved,
              "actual racing empty directory survives native no-replace");
    } else {
      fs::create_directory(fixture.Destination());
      const auto result = ic::PublishGltfTopologyBundle(
          workflow.Request(), fixture.Destination(), {}, Limits(), Work());
      Failure(result, ic::GltfFileStatus::kDestinationExists,
              ic::GltfFileDiagnosticCode::kDestinationExists);
      Require(fs::is_empty(fixture.Destination()) &&
                  result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired,
              "existing empty directory remains untouched");
    }
  }
  Fixture fixture;
  Workflow workflow(fixture, true);
  StageCapture stage;
  auto testing = stage.Testing();
  testing.hooks.before_inventory = [](detail::TestNativeView path, void*) {
    Write(fs::path(path) / "extra.txt", Bytes(kStaticBytes));
  };
  const auto result = detail::TestPublishGltfTopologyBundle(
      workflow.Request(), fixture.Destination(), {}, Limits(), Work(), {},
      testing);
  Failure(result, ic::GltfFileStatus::kPreservationMismatch,
          ic::GltfFileDiagnosticCode::kStagedInventory);
  CheckRemoved(result, stage, fixture.Destination());
}
void FileAndDanglingDestination() {
  {
    Fixture fixture;
    Workflow workflow(fixture, true);
    Write(fixture.Destination(), Bytes(kStaticBytes));
    const auto result = ic::PublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), {}, Limits(), Work());
    Failure(result, ic::GltfFileStatus::kDestinationExists,
            ic::GltfFileDiagnosticCode::kDestinationExists);
    Require(std::ranges::equal(ReadBytes(fixture.Destination()),
                               Bytes(kStaticBytes)) &&
                result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired,
            "existing ordinary file survives without overwrite");
  }
  for (const bool racing : {false, true}) {
    Fixture fixture;
    Workflow workflow(fixture, true);
    const auto absent_target = fixture.root / "missing-target";
    const auto probe = fixture.root / "link-probe";
    std::error_code error;
    fs::create_symlink(absent_target, probe, error);
    if (error) {
#ifdef _WIN32
      constexpr int kWindowsLinkPrivilegeError = 1314;
      Require(error == std::errc::permission_denied ||
                  error.value() == kWindowsLinkPrivilegeError,
              "Windows dangling-link fixture requires link privilege");
      std::cout
          << "Windows dangling-link injection unavailable without privilege\n";
      continue;
#else
      Require(false, "POSIX dangling-link fixture creation");
#endif
    }
    fs::remove(probe);
    struct State {
      fs::path stage;
      fs::path target;
      fs::path absent;
    } state{{}, fixture.Destination(), absent_target};
    detail::GltfFilePublicationTestOptions testing{};
    testing.hooks.state = &state;
    testing.hooks.stage_ready = [](detail::TestNativeView path, void* opaque) {
      static_cast<State*>(opaque)->stage = fs::path(path);
    };
    if (racing)
      testing.hooks.before_commit = [](detail::TestNativeView, void* opaque) {
        const auto& state = *static_cast<State*>(opaque);
        fs::create_symlink(state.absent, state.target);
      };
    else
      fs::create_symlink(absent_target, fixture.Destination());
    const auto result = detail::TestPublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), {}, Limits(), Work(), {},
        testing);
    Failure(result, ic::GltfFileStatus::kDestinationExists,
            ic::GltfFileDiagnosticCode::kDestinationExists);
    Require(fs::is_symlink(fs::symlink_status(fixture.Destination())) &&
                fs::read_symlink(fixture.Destination()) == absent_target &&
                !fs::exists(absent_target) &&
                result.cleanup ==
                    (racing ? ic::GltfFileCleanupOutcome::kRemoved
                            : ic::GltfFileCleanupOutcome::kNotRequired) &&
                (state.stage.empty() || !fs::exists(state.stage)),
            "actual dangling target survives initial/native racing no-replace");
  }
}
void LinkAndDirectoryInventory() {
  for (const bool link : {false, true}) {
    Fixture fixture;
    Workflow workflow(fixture, true);
    StageCapture stage;
    auto testing = stage.Testing();
    bool available = true;
    ic::GltfFilePublicationOptions options{};
    options.on_phase = [&](auto phase) {
      if (phase != ic::GltfFilePublicationPhase::kVerification) return;
      if (link) {
        fs::remove(stage.path / "opaque.bin");
        std::error_code error;
        fs::create_symlink(stage.path / "a.bin", stage.path / "opaque.bin",
                           error);
        if (error) {
#ifdef _WIN32
          constexpr int kWindowsLinkPrivilegeError = 1314;
          Require(error == std::errc::permission_denied ||
                      error.value() == kWindowsLinkPrivilegeError,
                  "Windows link creation either succeeds or explicitly lacks "
                  "privilege");
          available = false;
          Write(stage.path / "opaque.bin", fixture.opaque);
#else
          Require(false, "POSIX actual symlink fixture creation");
#endif
        }
      } else {
        fs::create_directory(stage.path / "unplanned-empty");
      }
    };
    const auto result = detail::TestPublishGltfTopologyBundle(
        workflow.Request(), fixture.Destination(), options, Limits(), Work(),
        {}, testing);
    if (!available) {
      Success(result);
      std::cout
          << "Windows symbolic-link injection unavailable without privilege\n";
      continue;
    }
    if (link)
      Failure(result, ic::GltfFileStatus::kUnsupportedProfile,
              ic::GltfFileDiagnosticCode::kPathUnsupported);
    else
      Failure(result, ic::GltfFileStatus::kPreservationMismatch,
              ic::GltfFileDiagnosticCode::kStagedInventory);
    CheckRemoved(result, stage, fixture.Destination());
  }
}
void MissingDestinationParent() {
  Fixture fixture;
  Workflow workflow(fixture, true);
  StageCapture stage;
  const auto testing = stage.Testing();
  const auto destination = fixture.root / "absent-parent" / "published";
  const auto result = detail::TestPublishGltfTopologyBundle(
      workflow.Request(), destination, {}, Limits(), Work(), {}, testing);
  Failure(result, ic::GltfFileStatus::kIoFailure,
          ic::GltfFileDiagnosticCode::kDestinationParent);
  Require(
      result.phase == ic::GltfFilePublicationPhase::kPreflight &&
          result.diagnostic.stage == ic::GltfFileStage::kPathPlanning &&
          result.diagnostic.system_error && *result.diagnostic.system_error &&
          stage.path.empty() &&
          result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired &&
          !fs::exists(destination.parent_path()),
      "valid source/inspection reaches actual absent parent without staging");
}
void UnsupportedDependency() {
  NativeHandleLifetimeAndRegularResources();
  Fixture fixture;
  auto context = Context();
  const auto file = Capture(fixture, context);
  const auto extraction = Extract(file.Asset());
  CheckChannels(extraction);
  const auto result = ic::MaterializeGltfPrimitive(extraction, TransferLimits(),
                                                   GeometryWork());
  Require(
      !result.primitive &&
          result.status == ic::GltfMaterializationStatus::kGeometryFailure &&
          result.diagnostic.geometry_status ==
              geo::MaterializationStatus::kUnsupportedAllocationProfile,
      "real unsupported dependency does not fabricate publication inspection");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == kUnsupportedProfileArgument) {
      UnsupportedDependency();
      std::cout
          << "actual unsupported numerical publication dependency retained\n";
      return 0;
    }
    Require(argc == 1, "known publication fixture invocation");
    ic::test_translation_publication::Positive(false);
    ic::test_translation_publication::NativeFailures();
    NativeHandleLifetimeAndRegularResources();
    ChangedAndNoop();
    UnchangedStripAndFan();
    EntryAsUnusedResource();
    SupplementsAndRetainedCallback();
    RetainedPublishedOwner();
    RejectedPreflight();
    StagedByteChanges();
    CallbackFailuresAndStop();
    DynamicRejectionAndAllocationException();
    DestinationRacesAndInventory();
    FileAndDanglingDestination();
    LinkAndDirectoryInventory();
    MissingDestinationParent();
    std::cout << "independent JSON publication semantics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
