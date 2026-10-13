// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "gltf_files_test_internal.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::gltf_detail;
namespace files = ic::gltf_files_detail;
namespace fs = std::filesystem;
constexpr std::uint64_t kPhysical = 1024 * 1024;
constexpr std::uint64_t kRequest = 64 * kPhysical;
constexpr std::size_t kWorkers = 4;
constexpr std::size_t kOpaqueBytes = 65536;
constexpr std::uint64_t kDepth = 64;
constexpr std::uint64_t kPartition = 4096;
constexpr std::uint64_t kCheckpoint = 64;
constexpr std::uint64_t kJobs = 64;
constexpr std::size_t kHexDigits = 2;
constexpr unsigned kHexShift = 4;
constexpr unsigned kHexLetter = 10;
constexpr std::size_t kBlocks = 4096;
constexpr std::size_t kNativeRecords = 256;
constexpr std::string_view kEntry = "scene.gltf";
constexpr std::string_view kBinaryHex =
    "000000800000000000000000"
    "0000803f0000000000000000"
    "000000000000803f00000000"
    "00000100020000000100020089504e470d0a1a0adeadbeefcafebabe";
constexpr std::string_view kImageHex = "89504e470d0a1a0a1122334455667788";
constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"%61.bin","byteLength":56},{"uri":"a.bin","byteLength":56}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"images":[{"uri":"texture.png"},{"uri":"data:image/png;base64,iVBORw0KGgo="},{"bufferView":2,"mimeType":"image/png"}],"extras":{"literal":1.2300e+04}})";

void Require(bool condition, std::string_view message,
             std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()) + ": " +
                             std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
unsigned Hex(char value) {
  if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
  Require(value >= 'a' && value <= 'f', "authored hex digit");
  return static_cast<unsigned>(value - 'a') + kHexLetter;
}
std::vector<std::byte> Decode(std::string_view hex) {
  Require(hex.size() % kHexDigits == 0, "authored hex extent");
  std::vector<std::byte> result(hex.size() / kHexDigits);
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = static_cast<std::byte>((Hex(hex[i * kHexDigits]) << kHexShift) |
                                       Hex(hex[i * kHexDigits + 1]));
  return result;
}
void Replace(std::string& text, std::string_view before,
             std::string_view after) {
  const auto offset = text.find(before);
  Require(offset != std::string::npos, "literal replacement location");
  text.replace(offset, before.size(), after);
}
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  Require(stream.is_open(), "fixture file opened");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(!stream.fail(), "fixture exact file write");
}
struct Fixture {
  fs::path root;
  std::string json = std::string(kJson);
  std::vector<std::byte> binary = Decode(kBinaryHex);
  std::vector<std::byte> image = Decode(kImageHex);
  std::vector<std::byte> opaque{kOpaqueBytes, std::byte{0xa7}};
  std::array<std::string_view, 2> inventory{"opaque.bin", "opaque.bin"};
  Fixture() {
    static std::atomic<std::uint64_t> sequence = 0;
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-gltf-files-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    Require(fs::create_directory(root), "isolated temporary fixture directory");
    Save();
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ~Fixture() noexcept {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  void Save() const {
    Write(root / kEntry, Bytes(json));
    Write(root / "a.bin", binary);
    Write(root / "texture.png", image);
    Write(root / "opaque.bin", opaque);
  }
  ic::GltfFileReadOptions Options() const {
    return {
        {}, ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory, inventory};
  }
};
geo::ExecutionContext Context(std::size_t workers = kWorkers) {
  return geo::ExecutionContext({workers, 1, kRequest});
}
ic::GltfFileReadLimits Limits() {
  return {{kPhysical, kPhysical, kPhysical, kPhysical, kPhysical, kPhysical,
           kPhysical, kRequest},
          {{kPhysical, kPhysical, kPhysical, kPhysical, kDepth, kPhysical,
            kPhysical, kPhysical, kPhysical, kPhysical, kPhysical, kPhysical,
            kPhysical, kRequest},
           kRequest},
          {kPhysical, kPhysical, kRequest}};
}
ic::GltfFileReadWorkPolicy Work(bool serial = false) {
  const auto threshold = serial
                             ? static_cast<std::uint64_t>(
                                   std::numeric_limits<std::ptrdiff_t>::max())
                             : 1;
  return {{kPartition, threshold, kPartition, kCheckpoint, kJobs},
          {threshold, kPartition, threshold, kPartition},
          {threshold, kPartition}};
}
ic::GltfFileReadResult Read(const Fixture& fixture,
                            const geo::ExecutionContext& context,
                            const ic::GltfFileReadLimits& limits = Limits(),
                            const ic::GltfFileReadWorkPolicy& work = Work(),
                            const files::GltfFileTestOptions& test = {},
                            std::stop_token stop = {}) {
  return files::TestReadGltfFileAsset(fixture.root / kEntry, fixture.Options(),
                                      limits, work, context, stop, test);
}
void Success(const ic::GltfFileReadResult& result) {
  Require(
      result.asset.has_value(),
      "capture owner: actual status=" +
          std::to_string(static_cast<int>(result.diagnostic.status)) +
          " code=" + std::to_string(static_cast<int>(result.diagnostic.code)) +
          " stage=" +
          std::to_string(static_cast<int>(result.diagnostic.stage)));
  Require(result.diagnostic.status == ic::GltfFileStatus::kSuccess &&
              result.diagnostic.stage == ic::GltfFileStage::kComplete &&
              result.diagnostic.code == ic::GltfFileDiagnosticCode::kNone,
          "successful terminal diagnostic");
}
void Failure(const ic::GltfFileReadResult& result,
             ic::GltfFileDiagnosticCode code) {
  Require(!result.asset &&
              result.diagnostic.status != ic::GltfFileStatus::kSuccess &&
              result.diagnostic.code == code,
          "typed failure expected=" + std::to_string(static_cast<int>(code)) +
              " actual=" +
              std::to_string(static_cast<int>(result.diagnostic.code)));
}
void Empty(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "all failed/destroyed owners and actual workers released");
}
void Check(const ic::GltfFileAsset& captured, const Fixture& fixture) {
  Require(captured.Format() == ic::GltfFileFormat::kGltfJson &&
              captured.Container() == nullptr &&
              captured.EntryPath() == kEntry &&
              captured.ExtensionFilesPolicy() ==
                  ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
          "literal JSON capture identity and caller inventory policy");
  Require(std::ranges::equal(captured.EntryBytes(), Bytes(fixture.json)) &&
              std::ranges::equal(captured.Asset().OriginalJson(),
                                 Bytes(fixture.json)),
          "all authored JSON bytes and numeric spelling");
  constexpr std::array<std::string_view, 4> kPaths{"a.bin", "opaque.bin",
                                                   "scene.gltf", "texture.png"};
  const std::array<std::span<const std::byte>, 4> expected{
      fixture.binary, fixture.opaque, Bytes(fixture.json), fixture.image};
  Require(captured.Files().size() == kPaths.size(), "unique sorted inventory");
  for (std::size_t i = 0; i < kPaths.size(); ++i)
    Require(captured.Files()[i].path == kPaths[i] &&
                std::ranges::equal(captured.Files()[i].bytes, expected[i]),
            "independent physical path and complete bytes including tails");
  const auto resources = captured.Asset().Resources();
  Require(resources.size() == 5, "two buffers and all three image roles");
  for (std::size_t i = 0; i < 2; ++i)
    Require(resources[i].kind == ic::GltfResourceKind::kBuffer &&
                resources[i].table_index == i &&
                resources[i].origin == ic::GltfResourceOrigin::kCallerBinding &&
                resources[i].authored_uri == (i == 0 ? "%61.bin" : "a.bin") &&
                resources[i].logical_bytes == 56 &&
                std::ranges::equal(resources[i].bytes, fixture.binary),
            "shared file supplies independent authored buffer identities");
  Require(resources[2].kind == ic::GltfResourceKind::kImage &&
              resources[2].table_index == 0 &&
              std::ranges::equal(resources[2].bytes, fixture.image),
          "unselected external image complete physical tail");
  Require(resources[3].table_index == 1 &&
              resources[3].origin == ic::GltfResourceOrigin::kDecodedDataUri &&
              std::ranges::equal(resources[3].bytes,
                                 std::span(fixture.binary).subspan(48, 8)),
          "independently authored embedded PNG remains embedded");
  Require(resources[4].table_index == 2 &&
              resources[4].origin == ic::GltfResourceOrigin::kBufferView &&
              resources[4].buffer_index == 0 &&
              resources[4].buffer_view_index == 2 &&
              resources[4].bytes.data() == resources[0].bytes.data() + 48 &&
              resources[4].bytes.size() == 8,
          "buffer-view image retains exact buffer alias");
}
void CaptureAndLifetime() {
  Fixture fixture;
  auto monitor = Context();
  std::optional<ic::GltfFileAsset> retained;
  std::optional<ic::GltfAsset> delegated;
  {
    const auto temporary_context = monitor;
    auto result = Read(fixture, temporary_context);
    Success(result);
    Check(*result.asset, fixture);
    Require(result.execution.capture.launched_workers > 0 &&
                result.execution.capture.launched_workers ==
                    result.execution.capture.joined_workers,
            "substantial opaque bytes use actual joined native capture");
    const auto before = monitor.ActiveTrackedPayload();
    retained = *result.asset;
    delegated = result.asset->Asset();
    Require(before == retained->NewRequestedBytes() +
                          *delegated->NewRequestedBytes() &&
                monitor.ActiveTrackedPayload() == before,
            "copies share separate file and delegated live request owners");
  }
  Require(fs::remove(fixture.root / kEntry) &&
              fs::remove(fixture.root / "a.bin") &&
              fs::remove(fixture.root / "texture.png") &&
              fs::remove(fixture.root / "opaque.bin"),
          "destroy actual source files after synchronous return");
  Check(*retained, fixture);
  retained.reset();
  Require(
      monitor.ActiveTrackedPayload() == *delegated->NewRequestedBytes() &&
          std::ranges::equal(delegated->OriginalJson(), Bytes(fixture.json)),
      "copied delegated owner survives source file owner destruction");
  delegated.reset();
  Empty(monitor);
}
void ContentDetection() {
  Fixture fixture;
  auto context = Context();
  constexpr std::array<std::byte, 4> kGlbMagic{std::byte{'g'}, std::byte{'l'},
                                               std::byte{'T'}, std::byte{'F'}};
  Write(fixture.root / "container.gltf", kGlbMagic);
  const auto unavailable =
      ic::ReadGltfFileAsset(fixture.root / "container.gltf", fixture.Options(),
                            Limits(), Work(), context);
  Failure(unavailable, ic::GltfFileDiagnosticCode::kContainerBinding);
  Require(unavailable.diagnostic.status ==
                  ic::GltfFileStatus::kUnsupportedProfile &&
              unavailable.diagnostic.stage == ic::GltfFileStage::kContainer,
          "literal GLB magic selects unavailable container route without JSON "
          "fallback");
  Empty(context);
  Write(fixture.root / "json.glb", Bytes(fixture.json));
  {
    const auto captured =
        ic::ReadGltfFileAsset(fixture.root / "json.glb", fixture.Options(),
                              Limits(), Work(), context);
    Success(captured);
    Require(captured.asset->Format() == ic::GltfFileFormat::kGltfJson &&
                captured.asset->Container() == nullptr &&
                captured.asset->EntryPath() == "json.glb" &&
                std::ranges::equal(captured.asset->EntryBytes(),
                                   Bytes(fixture.json)) &&
                std::ranges::equal(captured.asset->Asset().OriginalJson(),
                                   Bytes(fixture.json)),
            "JSON content is admitted despite a GLB suffix");
    constexpr std::array<std::string_view, 4> kPaths{
        "a.bin", "json.glb", "opaque.bin", "texture.png"};
    const std::array<std::span<const std::byte>, 4> expected{
        fixture.binary, Bytes(fixture.json), fixture.opaque, fixture.image};
    Require(captured.asset->Files().size() == kPaths.size(),
            "content-selected JSON unique file inventory");
    for (std::size_t i = 0; i < kPaths.size(); ++i)
      Require(
          captured.asset->Files()[i].path == kPaths[i] &&
              std::ranges::equal(captured.asset->Files()[i].bytes, expected[i]),
          "content detection preserves each exact physical file");
  }
  Empty(context);
}
void InclusivePhysicalLimits() {
  Fixture fixture;
  auto context = Context();
  auto limits = Limits();
  limits.files.max_files = 4;
  limits.files.max_resource_bindings = 3;
  limits.files.max_file_bytes = kOpaqueBytes;
  limits.files.max_total_file_bytes =
      fixture.json.size() + 64 + 16 + kOpaqueBytes;
  limits.files.max_path_bytes = 11;
  limits.files.max_total_path_bytes = 36;
  {
    auto result = Read(fixture, context, limits);
    Success(result);
    Check(*result.asset, fixture);
  }
  Empty(context);
  constexpr std::array kMembers{&ic::GltfFileLimits::max_files,
                                &ic::GltfFileLimits::max_resource_bindings,
                                &ic::GltfFileLimits::max_file_bytes,
                                &ic::GltfFileLimits::max_total_file_bytes,
                                &ic::GltfFileLimits::max_path_bytes,
                                &ic::GltfFileLimits::max_total_path_bytes};
  for (auto member : kMembers) {
    auto tight = limits;
    --(tight.files.*member);
    const auto result = Read(fixture, context, tight);
    Require(!result.asset &&
                result.diagnostic.status == ic::GltfFileStatus::kLimitExceeded,
            "one below independently counted inclusive physical cap");
    Empty(context);
  }
  auto tight = Limits();
  tight.files.max_new_requested_bytes = 1;
  Failure(Read(fixture, context, tight),
          ic::GltfFileDiagnosticCode::kFileRequestLimit);
  Empty(context);
}
void LiteralPathFailures() {
  Fixture fixture;
  auto context = Context();
  struct Case {
    std::string_view uri;
    ic::GltfFileDiagnosticCode code;
  };
  constexpr std::array kCases{
      Case{"%", ic::GltfFileDiagnosticCode::kUriMalformed},
      Case{"%0x.bin", ic::GltfFileDiagnosticCode::kUriMalformed},
      Case{"a%2f.bin", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"a%5C.bin", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"https://example.invalid/a.bin",
           ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"//host/a.bin", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"a.bin?q=1", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"a.bin#fragment", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"/a.bin", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"Q:a.bin", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"a%00.bin", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"a%1f.bin", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"../a.bin", ic::GltfFileDiagnosticCode::kUriUnsupported},
      Case{"CON.bin", ic::GltfFileDiagnosticCode::kPathUnsupported},
      Case{"a.bin.", ic::GltfFileDiagnosticCode::kPathUnsupported},
      Case{"a.bin ", ic::GltfFileDiagnosticCode::kPathUnsupported}};
  for (const auto& test : kCases) {
    fixture.json = std::string(kJson);
    Replace(fixture.json, "%61.bin", test.uri);
    Write(fixture.root / kEntry, Bytes(fixture.json));
    Failure(Read(fixture, context), test.code);
    Empty(context);
  }
  fixture.json = std::string(kJson);
  Replace(fixture.json, "%61.bin", "A.bin");
  Write(fixture.root / "A.bin", fixture.binary);
  Write(fixture.root / kEntry, Bytes(fixture.json));
  Failure(Read(fixture, context), ic::GltfFileDiagnosticCode::kPathCollision);
  Empty(context);
  fixture.json = std::string(kJson);
  Replace(fixture.json, "\"uri\":\"a.bin\"", "\"uri\":\"a.bin/nested\"");
  Write(fixture.root / kEntry, Bytes(fixture.json));
  Failure(Read(fixture, context), ic::GltfFileDiagnosticCode::kPathCollision);
  Empty(context);
}
void OpaqueAndPositivePaths() {
  Fixture fixture;
  auto context = Context();
  // Caller declaration repetitions do not consume unique physical-file slots.
  constexpr std::array<std::string_view, 5> kRepeatedInventory{
      "opaque.bin", "opaque.bin", "opaque.bin", "opaque.bin", "opaque.bin"};
  {
    auto options = fixture.Options();
    options.opaque_source_files = kRepeatedInventory;
    auto limits = Limits();
    limits.files.max_files = 4;
    const auto result = ic::ReadGltfFileAsset(fixture.root / kEntry, options,
                                              limits, Work(), context);
    Success(result);
    Check(*result.asset, fixture);
  }
  Empty(context);
  fixture.json.insert(fixture.json.size() - 1,
                      R"(,"extensions":{"X_CAPTURE":{"literal":7.000}})");
  Write(fixture.root / kEntry, Bytes(fixture.json));
  auto options = fixture.Options();
  options.extensions = ic::GltfExtensionFilePolicy::kRejectOpaqueExtensions;
  options.opaque_source_files = {};
  Failure(ic::ReadGltfFileAsset(fixture.root / kEntry, options, Limits(),
                                Work(), context),
          ic::GltfFileDiagnosticCode::kOpaqueInventoryRequired);
  Empty(context);
  {
    const auto result = Read(fixture, context);
    Success(result);
    Check(*result.asset, fixture);
  }
  Empty(context);
  fixture.json = std::string(kJson);
  Replace(fixture.json, "%61.bin", "sub/../%61.bin");
  Write(fixture.root / kEntry, Bytes(fixture.json));
  {
    const auto result = Read(fixture, context);
    Success(result);
    Require(result.asset->Files().size() == 4 &&
                result.asset->Asset().Resources()[0].authored_uri ==
                    "sub/../%61.bin",
            "normalize shared path while preserving original URI spelling");
  }
  Empty(context);
  fixture.json = std::string(kJson);
  Replace(fixture.json, "%61.bin", "%2561.bin");
  Write(fixture.root / "%61.bin", fixture.binary);
  Write(fixture.root / kEntry, Bytes(fixture.json));
  {
    const auto result = Read(fixture, context);
    Success(result);
    Require(
        result.asset->Files().size() == 5 &&
            result.asset->Files()[0].path == "%61.bin" &&
            result.asset->Asset().Resources()[0].authored_uri == "%2561.bin",
        "percent escape decoding happens exactly once");
  }
  Empty(context);
}

struct Requests {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
  };
  std::array<Block, kBlocks> blocks{};
  detail::AllocationBackend original = detail::DefaultAllocationBackend();
  const geo::ExecutionContext& context;
  std::mutex mutex;
  std::size_t calls = 0;
  std::size_t fail_at = 0;
  std::size_t live = 0;
  std::size_t peak = 0;
  bool incorrect_free = false;
  explicit Requests(const geo::ExecutionContext& ledger) : context(ledger) {}
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Requests*>(state);
    std::lock_guard lock(self.mutex);
    ++self.calls;
    Require(self.context.ActiveTrackedPayload() >= self.live + bytes,
            "file original allocation preceded ledger admission");
    if (self.calls == self.fail_at) throw std::bad_alloc();
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks) {
      if (block.pointer) continue;
      block = {pointer, bytes, alignment};
      self.live += bytes;
      self.peak = std::max(self.peak, self.live);
      return pointer;
    }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::runtime_error("file allocation observer capacity exhausted");
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Requests*>(state);
    std::lock_guard lock(self.mutex);
    for (auto& block : self.blocks) {
      if (block.pointer != pointer) continue;
      if (block.bytes != bytes || block.alignment != alignment ||
          self.context.ActiveTrackedPayload() < self.live)
        self.incorrect_free = true;
      self.original.deallocate(pointer, bytes, alignment, self.original.state);
      self.live -= bytes;
      block = {};
      return;
    }
    self.incorrect_free = true;
  }
  detail::AllocationBackend Backend() { return {Allocate, Free, this}; }
  void Check(std::size_t expected) const {
    Require(!incorrect_free && live == expected,
            "exact original file blocks/alignment/live counter/free ordering");
  }
};
struct Handles {
  std::atomic<std::size_t> opened{0};
  std::atomic<std::size_t> closed{0};
  static void Opened(std::uint64_t, void* state) noexcept {
    ++static_cast<Handles*>(state)->opened;
  }
  static void Closed(std::uint64_t, void* state) noexcept {
    ++static_cast<Handles*>(state)->closed;
  }
  files::GltfFileHooks Hooks() {
    return {nullptr, nullptr, Opened, Closed, this};
  }
  void Check() const {
    Require(opened == closed, "every actual observed file handle closed");
  }
};
struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  std::size_t entered = 0;
  bool release = false;
  bool finished = false;
  void Enter() {
    std::unique_lock lock(mutex);
    ++entered;
    changed.notify_all();
    changed.wait(lock, [&] { return release; });
  }
  bool Wait() {
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return entered >= kWorkers || finished; });
    return entered >= kWorkers;
  }
  void Release() {
    std::lock_guard lock(mutex);
    release = true;
    changed.notify_all();
  }
  void Finish() {
    std::lock_guard lock(mutex);
    finished = true;
    changed.notify_all();
  }
};
struct LaunchFailure {};
struct Workers {
  struct Entry {
    detail::NativeWorkerRecord original;
    Workers* owner = nullptr;
  };
  std::array<Entry, kNativeRecords> entries{};
  detail::NativeWorkerBackend original = detail::DefaultNativeWorkerBackend();
  std::size_t calls = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::size_t fail_at = 0;
  std::atomic<std::size_t> entered{0};
  std::atomic<std::size_t> active{0};
  std::stop_source* cancel = nullptr;
  Gate* gate = nullptr;
  static void Enter(detail::NativeWorkerRecord& record) noexcept {
    auto& entry = *static_cast<Entry*>(record.state);
    ++entry.owner->entered;
    if (entry.owner->cancel) entry.owner->cancel->request_stop();
    if (entry.owner->gate) entry.owner->gate->Enter();
    entry.original.entry(entry.original);
  }
  static std::uintptr_t Launch(detail::NativeWorkerRecord* record,
                               void* state) {
    auto& self = *static_cast<Workers*>(state);
    ++self.calls;
    if (self.calls == self.fail_at) throw LaunchFailure{};
    Require(self.calls <= self.entries.size(), "file native observer capacity");
    auto& entry = self.entries[self.calls - 1];
    entry = {*record, &self};
    record->entry = Enter;
    record->state = &entry;
    const auto handle = self.original.launch(record, self.original.state);
    ++self.launched;
    ++self.active;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& self = *static_cast<Workers*>(state);
    self.original.join(handle, self.original.state);
    --self.active;
    ++self.joined;
  }
  detail::NativeWorkerBackend Backend() { return {Launch, Join, this}; }
  void Check(bool native) const {
    Require(active == 0 && launched == joined && entered == launched &&
                (launched > 0) == native,
            "actual native entry/handle/join observations agree");
  }
};
files::GltfFileTestOptions Testing(Requests& requests, Workers& workers,
                                   Handles& handles) {
  return {requests.Backend(), {workers.Backend(), nullptr}, handles.Hooks()};
}
void RequestFaults() {
  Fixture fixture;
  for (const bool serial : {true, false}) {
    auto context = Context();
    Requests baseline(context);
    Workers workers;
    Handles handles;
    std::size_t calls = 0;
    {
      auto result = Read(fixture, context, Limits(), Work(serial),
                         Testing(baseline, workers, handles));
      Success(result);
      Check(*result.asset, fixture);
      baseline.Check(result.asset->NewRequestedBytes());
      Require(context.ActiveTrackedPayload() ==
                  baseline.live + *result.asset->Asset().NewRequestedBytes(),
              "file original live requests exclude delegated asset scope");
      calls = baseline.calls;
      workers.Check(!serial);
      handles.Check();
    }
    baseline.Check(0);
    Empty(context);
    for (std::size_t fail = 1; fail <= calls; ++fail) {
      Requests requests(context);
      Workers attempt;
      Handles actual;
      requests.fail_at = fail;
      bool propagated = false;
      try {
        const auto result = Read(fixture, context, Limits(), Work(serial),
                                 Testing(requests, attempt, actual));
        Require(!result.asset, "allocation fault produced an owner");
      } catch (const std::bad_alloc&) {
        propagated = true;
      }
      Require(propagated, "actual original allocation fault propagates");
      requests.Check(0);
      actual.Check();
      Require(attempt.active == 0 && attempt.launched == attempt.joined,
              "allocation failure joins every started native handle");
      Empty(context);
    }
    std::cout << "original file allocation faults " << calls
              << (serial ? " serial\n" : " native\n");
  }
}
void WorkerAgreementAndLaunchFailure() {
  Fixture fixture;
  auto context = Context();
  for (const bool saturated : {false, true}) {
    auto lease = saturated ? geo::TryReserveWorkers(context, kWorkers)
                           : geo::WorkerLease{};
    Requests requests(context);
    Workers workers;
    Handles handles;
    {
      auto result = Read(fixture, context, Limits(), Work(),
                         Testing(requests, workers, handles));
      Success(result);
      Check(*result.asset, fixture);
      workers.Check(!saturated);
      handles.Check();
      Require(result.execution.capture.launched_workers == workers.launched &&
                  result.execution.capture.joined_workers == workers.joined &&
                  (!saturated || result.execution.capture.caller_jobs > 0),
              "actual file worker stats and saturated caller fallback");
    }
    requests.Check(0);
    lease.Reset();
    Empty(context);
  }
  Requests requests(context);
  Workers workers;
  Handles handles;
  workers.fail_at = 2;
  bool propagated = false;
  try {
    const auto result = Read(fixture, context, Limits(), Work(),
                             Testing(requests, workers, handles));
    Require(!result.asset, "partial native launch returned owner");
  } catch (const LaunchFailure&) {
    propagated = true;
  }
  Require(propagated && workers.launched == 1 && workers.joined == 1,
          "second actual launch failure propagates after first handle joins");
  workers.Check(true);
  handles.Check();
  requests.Check(0);
  Empty(context);
}
void CancellationAndOccupiedConcurrency() {
  Fixture fixture;
  auto context = Context();
  std::stop_source stop;
  stop.request_stop();
  Failure(Read(fixture, context, Limits(), Work(), {}, stop.get_token()),
          ic::GltfFileDiagnosticCode::kStopRequested);
  Empty(context);
  stop = std::stop_source{};
  Requests requests(context);
  Workers canceled;
  Handles handles;
  canceled.cancel = &stop;
  const auto stopped =
      Read(fixture, context, Limits(), Work(),
           Testing(requests, canceled, handles), stop.get_token());
  Failure(stopped, ic::GltfFileDiagnosticCode::kStopRequested);
  Require(stopped.diagnostic.status == ic::GltfFileStatus::kCanceled,
          "fresh token cancellation from real native entry");
  canceled.Check(true);
  handles.Check();
  requests.Check(0);
  Empty(context);

  Requests first_requests(context);
  Workers first_workers;
  Handles first_handles;
  Gate gate;
  first_workers.gate = &gate;
  std::optional<ic::GltfFileReadResult> first;
  std::exception_ptr error;
  std::thread first_call([&] {
    try {
      first = Read(fixture, context, Limits(), Work(),
                   Testing(first_requests, first_workers, first_handles));
    } catch (...) {
      error = std::current_exception();
    }
    gate.Finish();
  });
  const bool occupied = gate.Wait();
  std::optional<ic::GltfFileReadResult> second;
  try {
    Require(occupied && context.ActiveWorkers() == kWorkers,
            "first real file capture occupies all worker slots");
    const auto foreign = context.ActiveTrackedPayload();
    Require(foreign > 0, "concurrent file call retains real charged requests");
    Requests second_requests(context);
    Workers second_workers;
    Handles second_handles;
    second = Read(fixture, context, Limits(), Work(),
                  Testing(second_requests, second_workers, second_handles));
    Success(*second);
    Check(*second->asset, fixture);
    second_workers.Check(false);
    second_handles.Check();
    second_requests.Check(second->asset->NewRequestedBytes());
    second->asset.reset();
    second_requests.Check(0);
    Require(context.ActiveTrackedPayload() == foreign &&
                context.ActiveWorkers() == kWorkers,
            "second call frees only its requests and preserves first workers");
  } catch (...) {
    gate.Release();
    first_call.join();
    throw;
  }
  gate.Release();
  first_call.join();
  if (error) std::rethrow_exception(error);
  Require(first.has_value(), "first concurrent call returned");
  Success(*first);
  Check(*first->asset, fixture);
  first_workers.Check(true);
  first_handles.Check();
  first->asset.reset();
  first_requests.Check(0);
  Empty(context);
}

struct HookFailure {};
struct Mutate : Handles {
  fs::path entry;
  std::stop_source* cancel = nullptr;
  bool truncate = false;
  bool grow = false;
  bool throw_before_open = false;
  bool throw_before_read = false;
  std::atomic<bool> fired{false};
  static void BeforeOpen(std::uint64_t, void* state) {
    auto& self = *static_cast<Mutate*>(state);
    if (self.throw_before_open) throw HookFailure{};
  }
  static void BeforeRead(std::uint64_t, std::uint64_t, void* state) {
    auto& self = *static_cast<Mutate*>(state);
    if (self.throw_before_read) throw HookFailure{};
    if (self.fired.exchange(true)) return;
    if (self.cancel) self.cancel->request_stop();
    if (self.truncate) fs::resize_file(self.entry, 1);
    if (self.grow) {
      std::ofstream stream(self.entry, std::ios::binary | std::ios::app);
      stream << ' ';
      stream.close();
      Require(!stream.fail(), "actual file growth during read hook");
    }
  }
  static void Opened(std::uint64_t, void* state) noexcept {
    ++static_cast<Mutate*>(state)->opened;
  }
  static void Closed(std::uint64_t, void* state) noexcept {
    ++static_cast<Mutate*>(state)->closed;
  }
  files::GltfFileHooks Hooks() {
    return {BeforeOpen, BeforeRead, Opened, Closed, this};
  }
};
void FileMutationAndHookExceptions() {
  Fixture fixture;
  auto context = Context();
  for (const bool truncate : {true, false}) {
    fixture.Save();
    Mutate mutation;
    mutation.entry = fixture.root / kEntry;
    mutation.truncate = truncate;
    mutation.grow = !truncate;
    files::GltfFileTestOptions options;
    options.hooks = mutation.Hooks();
    Failure(Read(fixture, context, Limits(), Work(true), options),
            ic::GltfFileDiagnosticCode::kFileChanged);
    mutation.Check();
    Require(mutation.fired, "actual entry mutation executed after handle open");
    Empty(context);
  }
  fixture.Save();
  for (const bool before_open : {true, false}) {
    Mutate failure;
    failure.throw_before_open = before_open;
    failure.throw_before_read = !before_open;
    Requests requests(context);
    files::GltfFileTestOptions options;
    options.allocation_backend = requests.Backend();
    options.hooks = failure.Hooks();
    bool propagated = false;
    try {
      const auto result = Read(fixture, context, Limits(), Work(true), options);
      Require(!result.asset, "throwing actual file hook returned owner");
    } catch (const HookFailure&) {
      propagated = true;
    }
    Require(propagated, "actual file hook runtime exception propagates");
    failure.Check();
    requests.Check(0);
    Empty(context);
  }
  std::stop_source stop;
  Mutate mutation;
  mutation.cancel = &stop;
  files::GltfFileTestOptions options;
  options.hooks = mutation.Hooks();
  Failure(
      Read(fixture, context, Limits(), Work(true), options, stop.get_token()),
      ic::GltfFileDiagnosticCode::kStopRequested);
  mutation.Check();
  Empty(context);
}
void RootsAndNativeAliases() {
  Fixture fixture;
  auto context = Context();
  Fixture other;
  auto options = fixture.Options();
  options.resource_root = other.root;
  Failure(ic::ReadGltfFileAsset(fixture.root / kEntry, options, Limits(),
                                Work(), context),
          ic::GltfFileDiagnosticCode::kPathOutsideRoot);
  Empty(context);
  Require(fs::create_directory(fixture.root / "nested"),
          "nested entry directory");
  auto nested_json = fixture.json;
  Replace(nested_json, "%61.bin", "../%61.bin");
  Replace(nested_json, "\"a.bin\"", "\"../a.bin\"");
  Replace(nested_json, "texture.png", "../texture.png");
  Write(fixture.root / "nested" / kEntry, Bytes(nested_json));
  options = fixture.Options();
  options.resource_root = fixture.root;
  {
    const auto result = ic::ReadGltfFileAsset(
        fixture.root / "nested" / kEntry, options, Limits(), Work(), context);
    Success(result);
    Require(
        result.asset->EntryPath() == "nested/scene.gltf" &&
            result.asset->Files().size() == 4 &&
            result.asset->Files()[0].path == "a.bin",
        "explicit enclosing root admits sibling resources from nested entry");
  }
  Empty(context);
  std::error_code error;
  fs::create_symlink(fixture.root / "a.bin", fixture.root / "linked.bin",
                     error);
  if (error) {
    std::cout << "native file symlink fixture unavailable: " << error.message()
              << '\n';
  } else {
    fixture.json = std::string(kJson);
    Replace(fixture.json, "%61.bin", "linked.bin");
    Write(fixture.root / kEntry, Bytes(fixture.json));
    const auto result = Read(fixture, context);
    Require(!result.asset &&
                result.diagnostic.status != ic::GltfFileStatus::kSuccess,
            "even in-root symbolic resource alias rejected");
    Empty(context);
  }
  fixture.json = std::string(kJson);
  fixture.Save();
  fs::create_hard_link(fixture.root / "a.bin", fixture.root / "other.bin",
                       error);
  Require(!error, "actual distinct-name hard-link fixture");
  Replace(fixture.json, "%61.bin", "other.bin");
  Write(fixture.root / kEntry, Bytes(fixture.json));
  {
    const auto result = Read(fixture, context);
    Success(result);
    Require(
        result.asset->Files().size() == 5 &&
            result.asset->Files()[2].path == "other.bin",
        "different logical hard-link paths remain distinct inventory entries");
  }
  Empty(context);
}
}  // namespace

int main() {
  try {
    CaptureAndLifetime();
    ContentDetection();
    InclusivePhysicalLimits();
    LiteralPathFailures();
    OpaqueAndPositivePaths();
    RequestFaults();
    WorkerAgreementAndLaunchFailure();
    CancellationAndOccupiedConcurrency();
    FileMutationAndHookExceptions();
    RootsAndNativeAliases();
    std::cout << "independent JSON file capture semantics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
