// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
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
#include <utility>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_files_test_internal.h"
#include "gltf_operation_workers.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::gltf_detail;
namespace files = ic::gltf_files_detail;
namespace fs = std::filesystem;
constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
constexpr std::uint64_t kRequestLimit = 64 * kPhysicalLimit;
constexpr std::size_t kWorkerCount = 4;
constexpr std::size_t kOpaqueBytes = 65536;
constexpr std::size_t kBlockCapacity = 4096;
constexpr std::size_t kWorkerRecordCapacity = 256;
constexpr std::uint64_t kJsonDepth = 64;
constexpr std::uint64_t kPartitionBytes = 4096;
constexpr std::uint64_t kCancelInterval = 64;
constexpr std::uint64_t kJobLimit = 64;
constexpr std::size_t kWordBytes = 4;
constexpr unsigned kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 0xff;
constexpr std::size_t kHeaderBytes = 12;
constexpr std::size_t kChunkHeaderBytes = 8;
constexpr std::uint32_t kLiteralMagic = 0x46546c67;
constexpr std::uint32_t kLiteralVersion = 2;
constexpr std::uint32_t kLiteralJsonType = 0x4e4f534a;
constexpr std::uint32_t kLiteralBinType = 0x004e4942;
constexpr std::uint32_t kLiteralOpaqueType = 0x10203040;
constexpr std::size_t kChunkCount = 3;
constexpr std::size_t kBinChunkIndex = 1;
constexpr std::size_t kOpaqueChunkIndex = 2;
constexpr std::size_t kBinLogicalBytes = 42;
constexpr std::size_t kBinPhysicalBytes = 44;
constexpr std::string_view kEntry = "scene.glb";
constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"extras":{"spelling":1.2300e+04}})";
constexpr std::array<std::byte, kBinPhysicalBytes> kBin{
    std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0x80},
    std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0},
    std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0},
    std::byte{0}, std::byte{0}, std::byte{0x80}, std::byte{0x3f},
    std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0},
    std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0},
    std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0},
    std::byte{0}, std::byte{0}, std::byte{0x80}, std::byte{0x3f},
    std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0},
    std::byte{0}, std::byte{0}, std::byte{1},    std::byte{0},
    std::byte{2}, std::byte{0}, std::byte{0},    std::byte{0}};

void Require(bool condition, std::string_view message,
             std::source_location where = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
void Word(std::vector<std::byte>& wire, std::uint32_t value) {
  for (std::size_t i = 0; i < kWordBytes; ++i)
    wire.push_back(std::byte((value >> (i * kBitsPerByte)) & kByteMask));
}
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(output.is_open(), "literal fixture file opened");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  Require(!output.fail(), "complete literal fixture file written");
}
struct Fixture {
  fs::path root;
  std::string json = std::string(kJson);
  std::vector<std::byte> opaque{kOpaqueBytes, std::byte{0xa7}};
  std::vector<std::byte> wire;
  std::array<std::string_view, 2> inventory{"opaque-a.bin", "opaque-b.bin"};
  Fixture() {
    // The fixture authors wire fields; no production writer/padding helper
    // runs.
    while (json.size() % kWordBytes != 0) json.push_back(' ');
    const auto total = kHeaderBytes + kChunkCount * kChunkHeaderBytes +
                       json.size() + kBin.size() + opaque.size();
    Word(wire, kLiteralMagic);
    Word(wire, kLiteralVersion);
    Word(wire, static_cast<std::uint32_t>(total));
    Word(wire, static_cast<std::uint32_t>(json.size()));
    Word(wire, kLiteralJsonType);
    wire.insert(wire.end(), Bytes(json).begin(), Bytes(json).end());
    Word(wire, static_cast<std::uint32_t>(kBin.size()));
    Word(wire, kLiteralBinType);
    wire.insert(wire.end(), kBin.begin(), kBin.end());
    Word(wire, static_cast<std::uint32_t>(opaque.size()));
    Word(wire, kLiteralOpaqueType);
    wire.insert(wire.end(), opaque.begin(), opaque.end());
    Require(wire.size() == total, "independent literal wire extent");
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-glb-requests-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    Require(fs::create_directory(root), "isolated fixture root");
    Write(root / kEntry, wire);
    for (const auto path : inventory) Write(root / path, opaque);
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ~Fixture() noexcept {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  ic::GltfFileReadOptions Options() const {
    return {
        {}, ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory, inventory};
  }
};
geo::ExecutionContext Context(std::uint64_t budget = kRequestLimit) {
  return geo::ExecutionContext({kWorkerCount, 1, budget});
}
ic::GltfFileReadLimits Limits() {
  return {{kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
           kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kRequestLimit},
          {{kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
            kJsonDepth, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
            kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
            kPhysicalLimit, kRequestLimit},
           kRequestLimit},
          {kPhysicalLimit, kPhysicalLimit, kRequestLimit}};
}
ic::GltfFileReadWorkPolicy Work(bool serial = false) {
  const auto threshold = serial
                             ? static_cast<std::uint64_t>(
                                   std::numeric_limits<std::ptrdiff_t>::max())
                             : 1;
  return {
      {kPartitionBytes, threshold, kPartitionBytes, kCancelInterval, kJobLimit},
      {threshold, kPartitionBytes, threshold, kPartitionBytes},
      {threshold, kPartitionBytes}};
}
void Empty(const geo::ExecutionContext& context) {
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
          "all original requests and worker reservations released");
}
void Success(const ic::GltfFileReadResult& result) {
  Require(
      result.asset.has_value(),
      "owner expected: actual status=" +
          std::to_string(static_cast<int>(result.diagnostic.status)) +
          " code=" + std::to_string(static_cast<int>(result.diagnostic.code)) +
          " stage=" +
          std::to_string(static_cast<int>(result.diagnostic.stage)));
  Require(result.diagnostic.status == ic::GltfFileStatus::kSuccess,
          "successful owner has success diagnostic");
}
void Check(const ic::GltfFileAsset& asset, const Fixture& fixture) {
  const auto* container = asset.Container();
  Require(asset.Format() == ic::GltfFileFormat::kGlb && container &&
              container->ChunkCount() == kChunkCount,
          "real retained GLB container");
  Require(
      std::ranges::equal(asset.EntryBytes(), fixture.wire) &&
          std::ranges::equal(container->Bytes(), fixture.wire) &&
          std::ranges::equal(asset.Asset().OriginalJson(), Bytes(fixture.json)),
      "complete original wire and padded JSON retained independently");
  Require(std::ranges::equal(container->Chunk(kBinChunkIndex).bytes, kBin) &&
              std::ranges::equal(container->Chunk(kOpaqueChunkIndex).bytes,
                                 fixture.opaque),
          "literal BIN padding and opaque chunk bytes retained");
  const auto resources = asset.Asset().Resources();
  Require(resources.size() == 1 &&
              resources[0].logical_bytes == kBinLogicalBytes &&
              std::ranges::equal(resources[0].bytes, kBin),
          "canonical reader owns full BIN binding with logical extent");
  constexpr std::array<std::string_view, 3> kPaths{"opaque-a.bin",
                                                   "opaque-b.bin", "scene.glb"};
  Require(asset.Files().size() == kPaths.size(), "BIN is not a physical file");
  for (std::size_t i = 0; i < kPaths.size(); ++i)
    Require(asset.Files()[i].path == kPaths[i] &&
                std::ranges::equal(asset.Files()[i].bytes,
                                   i == kPaths.size() - 1
                                       ? std::span(fixture.wire)
                                       : std::span(fixture.opaque)),
            "independent sorted physical inventory");
}

struct AllocationFailure {};
struct LaunchFailure {};
struct Requests {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
  };
  geo::ExecutionContext context;
  std::array<Block, kBlockCapacity> blocks{};
  detail::AllocationBackend original = detail::DefaultAllocationBackend();
  std::mutex mutex;
  std::size_t calls = 0;
  std::size_t fail_at = 0;
  std::size_t live = 0;
  std::size_t peak = 0;
  bool incorrect_free = false;
  std::stop_source* cancel = nullptr;
  explicit Requests(const geo::ExecutionContext& ledger) : context(ledger) {}
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Requests*>(state);
    std::lock_guard lock(self.mutex);
    ++self.calls;
    Require(self.context.ActiveTrackedPayload() >= self.live + bytes,
            "original allocation follows shared ledger admission");
    if (self.calls == self.fail_at) throw AllocationFailure{};
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks) {
      if (block.pointer) continue;
      block = {pointer, bytes, alignment};
      self.live += bytes;
      self.peak = std::max(self.peak, self.live);
      if (self.cancel) self.cancel->request_stop();
      return pointer;
    }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::runtime_error("preallocated original observer capacity");
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
  void Check(std::size_t bytes) const {
    Require(!incorrect_free && live == bytes,
            "exact original blocks, alignment and free-before-release order");
  }
};
struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  std::size_t entered = 0;
  bool released = false;
  bool finished = false;
  void Enter() {
    std::unique_lock lock(mutex);
    ++entered;
    changed.notify_all();
    changed.wait(lock, [&] { return released; });
  }
  bool Wait() {
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return entered >= kWorkerCount || finished; });
    return entered >= kWorkerCount;
  }
  void Release() noexcept {
    std::lock_guard lock(mutex);
    released = true;
    changed.notify_all();
  }
  void Finish() noexcept {
    std::lock_guard lock(mutex);
    finished = true;
    changed.notify_all();
  }
};
struct Workers {
  struct Entry {
    detail::NativeWorkerRecord original;
    Workers* owner = nullptr;
  };
  std::array<Entry, kWorkerRecordCapacity> entries{};
  detail::NativeWorkerBackend original = detail::DefaultNativeWorkerBackend();
  std::size_t calls = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::size_t fail_at = 0;
  std::atomic<std::size_t> entered{0};
  std::atomic<std::size_t> exited{0};
  std::stop_source* cancel = nullptr;
  Requests* retained_container = nullptr;
  Gate* gate = nullptr;
  static void Enter(detail::NativeWorkerRecord& record) noexcept {
    auto& entry = *static_cast<Entry*>(record.state);
    auto& self = *entry.owner;
    ++self.entered;
    if (self.cancel &&
        (!self.retained_container || self.retained_container->live > 0))
      self.cancel->request_stop();
    if (self.gate) self.gate->Enter();
    entry.original.entry(entry.original);
    ++self.exited;
  }
  static std::uintptr_t Launch(detail::NativeWorkerRecord* record,
                               void* state) {
    auto& self = *static_cast<Workers*>(state);
    if (++self.calls == self.fail_at) throw LaunchFailure{};
    Require(self.calls <= self.entries.size(), "fixed native record capacity");
    auto& entry = self.entries[self.calls - 1];
    entry = {*record, &self};
    record->entry = Enter;
    record->state = &entry;
    const auto handle = self.original.launch(record, self.original.state);
    ++self.launched;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& self = *static_cast<Workers*>(state);
    self.original.join(handle, self.original.state);
    ++self.joined;
  }
  detail::NativeWorkerBackend Backend() { return {Launch, Join, this}; }
  void Check() const {
    Require(entered == launched && exited == launched && joined == launched,
            "real file native entries/exits and returned handles all joined");
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
    Require(opened == closed, "actual native file handles closed");
  }
};
struct ContainerLaunches {
  std::size_t callbacks = 0;
  std::size_t fail_at = 0;
  std::stop_source* cancel = nullptr;
  static void BeforeLaunch(std::size_t, void* state) {
    auto& self = *static_cast<ContainerLaunches*>(state);
    if (++self.callbacks == self.fail_at) throw LaunchFailure{};
    if (self.cancel) self.cancel->request_stop();
  }
};
files::GltfFileTestOptions Testing(Requests& file_requests,
                                   Requests& container_requests,
                                   Workers& workers, Handles& handles,
                                   ContainerLaunches& container) {
  files::GltfFileTestOptions testing;
  testing.allocation_backend = file_requests.Backend();
  testing.execution.backend = workers.Backend();
  testing.hooks = handles.Hooks();
  testing.container = {container_requests.Backend(),
                       ContainerLaunches::BeforeLaunch, &container};
  return testing;
}
ic::GltfFileReadResult Read(const Fixture& fixture,
                            const geo::ExecutionContext& context,
                            const files::GltfFileTestOptions& testing,
                            bool serial = false,
                            const ic::GltfFileReadLimits& limits = Limits(),
                            std::stop_token stop = {}) {
  return files::TestReadGltfFileAsset(fixture.root / kEntry, fixture.Options(),
                                      limits, Work(serial), context, stop,
                                      testing);
}
void Scopes(const ic::GltfFileAsset& asset, Requests& file_requests,
            Requests& container_requests,
            const geo::ExecutionContext& context) {
  file_requests.Check(asset.NewRequestedBytes());
  const auto reader = asset.Asset().NewRequestedBytes();
  Require(reader && container_requests.live > 0 &&
              context.ActiveTrackedPayload() ==
                  file_requests.live + container_requests.live + *reader,
          "three attributable retained scopes on common ledger");
}
void OriginalFaultsAndLifetime() {
  Fixture fixture;
  for (const bool serial : {true, false}) {
    auto context = Context();
    Requests baseline_file(context);
    Requests baseline_container(context);
    Workers workers;
    Handles handles;
    ContainerLaunches container;
    std::size_t file_calls = 0;
    std::size_t container_calls = 0;
    {
      auto result = Read(fixture, context,
                         Testing(baseline_file, baseline_container, workers,
                                 handles, container),
                         serial);
      Success(result);
      Check(*result.asset, fixture);
      Scopes(*result.asset, baseline_file, baseline_container, context);
      file_calls = baseline_file.calls;
      container_calls = baseline_container.calls;
      workers.Check();
      handles.Check();
      Require((workers.launched > 0) == !serial &&
                  (container.callbacks > 0) == !serial,
              "file actual native work and container prelaunch route selected");
      // Container prelaunch callbacks are not worker entry or join counters.
    }
    baseline_file.Check(0);
    baseline_container.Check(0);
    Empty(context);
    for (const bool container_scope : {false, true}) {
      const auto calls = container_scope ? container_calls : file_calls;
      Require(calls > 0, "both original allocator scopes actually reached");
      for (std::size_t ordinal = 1; ordinal <= calls; ++ordinal) {
        Requests file_requests(context);
        Requests container_requests(context);
        Workers attempt;
        Handles actual;
        ContainerLaunches launches;
        (container_scope ? container_requests : file_requests).fail_at =
            ordinal;
        bool propagated = false;
        try {
          const auto result = Read(fixture, context,
                                   Testing(file_requests, container_requests,
                                           attempt, actual, launches),
                                   serial);
          Require(!result.asset, "fault must not expose retained owner");
        } catch (const AllocationFailure&) {
          propagated = true;
        }
        Require(propagated, "each actual original allocation fault propagates");
        file_requests.Check(0);
        container_requests.Check(0);
        attempt.Check();
        actual.Check();
        Empty(context);
      }
      std::cout << "GLB " << (container_scope ? "container" : "file")
                << " original faults " << calls
                << (serial ? " serial\n" : " native\n");
    }
  }
  auto context = Context();
  Requests file_requests(context);
  Requests container_requests(context);
  Workers workers;
  Handles handles;
  ContainerLaunches container;
  std::optional<ic::GltfFileAsset> retained;
  std::optional<ic::GltfAsset> reader;
  std::uint64_t reader_bytes = 0;
  const ic::GlbContainer* stable = nullptr;
  {
    auto result = Read(fixture, context,
                       Testing(file_requests, container_requests, workers,
                               handles, container));
    Success(result);
    Scopes(*result.asset, file_requests, container_requests, context);
    const auto calls = file_requests.calls + container_requests.calls;
    retained.emplace(*result.asset);
    reader.emplace(retained->Asset());
    stable = retained->Container();
    reader_bytes = *reader->NewRequestedBytes();
    Require(file_requests.calls + container_requests.calls == calls,
            "immutable wrapper copies allocate no original blocks");
  }
  fs::remove_all(fixture.root);
  Require(retained->Container() == stable,
          "container const pointer stable across copies");
  Check(*retained, fixture);
  retained.reset();
  file_requests.Check(0);
  container_requests.Check(0);
  Require(context.ActiveTrackedPayload() == reader_bytes &&
              std::ranges::equal(reader->OriginalJson(), Bytes(fixture.json)) &&
              std::ranges::equal(reader->Resources()[0].bytes, kBin),
          "delegated reader outlives last file/container owner independently");
  reader.reset();
  workers.Check();
  handles.Check();
  Empty(context);
}
void RequestCapsAndOccupiedWorkers() {
  Fixture fixture;
  for (const bool serial : {true, false}) {
    auto context = Context();
    Requests baseline_file(context);
    Requests baseline_container(context);
    Workers baseline_workers;
    Handles baseline_handles;
    ContainerLaunches baseline_launches;
    {
      auto result =
          Read(fixture, context,
               Testing(baseline_file, baseline_container, baseline_workers,
                       baseline_handles, baseline_launches),
               serial);
      Success(result);
      Scopes(*result.asset, baseline_file, baseline_container, context);
    }
    Empty(context);
    auto limits = Limits();
    limits.files.max_new_requested_bytes = baseline_file.peak;
    limits.container.max_requested_bytes = baseline_container.peak;
    enum class Boundary { kExact, kFileMinusOne, kContainerMinusOne };
    for (const auto boundary : {Boundary::kExact, Boundary::kFileMinusOne,
                                Boundary::kContainerMinusOne}) {
      auto bounded = limits;
      if (boundary == Boundary::kFileMinusOne)
        --bounded.files.max_new_requested_bytes;
      if (boundary == Boundary::kContainerMinusOne)
        --bounded.container.max_requested_bytes;
      Requests file_requests(context);
      Requests container_requests(context);
      Workers workers;
      Handles handles;
      ContainerLaunches launches;
      {
        const auto result = Read(fixture, context,
                                 Testing(file_requests, container_requests,
                                         workers, handles, launches),
                                 serial, bounded);
        if (boundary == Boundary::kExact) {
          Success(result);
          Check(*result.asset, fixture);
          Scopes(*result.asset, file_requests, container_requests, context);
        } else {
          Require(!result.asset && result.diagnostic.status ==
                                       ic::GltfFileStatus::kLimitExceeded,
                  "minus-one own request allowance refused");
          if (boundary == Boundary::kContainerMinusOne)
            Require(result.diagnostic.container_status ==
                            ic::GlbStatus::kLimitExceeded &&
                        result.diagnostic.code ==
                            ic::GltfFileDiagnosticCode::kDelegatedContainer,
                    "container allowance reports actual delegated limit");
        }
      }
      file_requests.Check(0);
      container_requests.Check(0);
      workers.Check();
      handles.Check();
      Empty(context);
    }
  }
  auto context = Context();
  auto foreign = geo::TryReserveWorkers(context, kWorkerCount);
  Require(foreign.Count() == kWorkerCount, "all shared worker slots held");
  Requests file_requests(context);
  Requests container_requests(context);
  Workers workers;
  Handles handles;
  ContainerLaunches launches;
  {
    auto result = Read(
        fixture, context,
        Testing(file_requests, container_requests, workers, handles, launches));
    Success(result);
    Check(*result.asset, fixture);
    Scopes(*result.asset, file_requests, container_requests, context);
    Require(workers.launched == 0 && launches.callbacks == 0 &&
                result.execution.capture.caller_jobs > 0,
            "occupied context uses caller work in file and container owners");
  }
  file_requests.Check(0);
  container_requests.Check(0);
  Require(context.ActiveWorkers() == kWorkerCount &&
              context.ActiveTrackedPayload() == 0,
          "caller fallback preserves foreign reservation");
  foreign.Reset();
  Empty(context);
}
void PartialLaunchAndCancellation() {
  Fixture fixture;
  for (const bool container_scope : {false, true}) {
    auto context = Context();
    Requests file_requests(context);
    Requests container_requests(context);
    Workers workers;
    Handles handles;
    ContainerLaunches launches;
    if (container_scope)
      launches.fail_at = 2;
    else
      workers.fail_at = 2;
    bool propagated = false;
    try {
      const auto result = Read(fixture, context,
                               Testing(file_requests, container_requests,
                                       workers, handles, launches));
      Require(!result.asset, "partial launch exposes no owner");
    } catch (const LaunchFailure&) {
      propagated = true;
    }
    Require(propagated,
            "partial file/container native launch fault propagates");
    workers.Check();
    if (!container_scope)
      Require(
          workers.launched == 1 && workers.joined == 1 && workers.entered == 1,
          "actual first file worker entered and joined after second launch "
          "fault");
    else
      Require(
          launches.callbacks == 2,
          "container throws at second prelaunch, not a claimed entry witness");
    handles.Check();
    file_requests.Check(0);
    container_requests.Check(0);
    Empty(context);
  }
  enum class Timing {
    kAlreadyStopped,
    kFileAllocation,
    kFileEntry,
    kContainerAllocation,
    kContainerPrelaunch,
    kDependencyEntry
  };
  for (const auto timing :
       {Timing::kAlreadyStopped, Timing::kFileAllocation, Timing::kFileEntry,
        Timing::kContainerAllocation, Timing::kContainerPrelaunch,
        Timing::kDependencyEntry}) {
    auto context = Context();
    Requests file_requests(context);
    Requests container_requests(context);
    Workers workers;
    Handles handles;
    ContainerLaunches launches;
    std::stop_source stop;
    if (timing == Timing::kAlreadyStopped) stop.request_stop();
    if (timing == Timing::kFileAllocation) file_requests.cancel = &stop;
    if (timing == Timing::kFileEntry) workers.cancel = &stop;
    if (timing == Timing::kContainerAllocation)
      container_requests.cancel = &stop;
    if (timing == Timing::kContainerPrelaunch) launches.cancel = &stop;
    if (timing == Timing::kDependencyEntry) {
      workers.cancel = &stop;
      workers.retained_container = &container_requests;
    }
    const auto result = Read(
        fixture, context,
        Testing(file_requests, container_requests, workers, handles, launches),
        false, Limits(), stop.get_token());
    Require(!result.asset &&
                result.diagnostic.status == ic::GltfFileStatus::kCanceled,
            "fresh actual stop source causes typed cancellation");
    if (timing == Timing::kContainerAllocation ||
        timing == Timing::kContainerPrelaunch)
      Require(result.diagnostic.container_status == ic::GlbStatus::kCanceled &&
                  result.diagnostic.code ==
                      ic::GltfFileDiagnosticCode::kDelegatedContainer,
              "container stop preserves actual delegated diagnosis");
    if (timing == Timing::kFileEntry)
      Require(workers.entered > 0, "stop came from real file entry");
    if (timing == Timing::kDependencyEntry)
      Require(
          workers.entered > kWorkerCount && launches.callbacks > 0 &&
              result.diagnostic.code ==
                  ic::GltfFileDiagnosticCode::kStopRequested,
          "real dependency entry stops after successful container retention");
    workers.Check();
    handles.Check();
    file_requests.Check(0);
    container_requests.Check(0);
    Empty(context);
  }
}

struct JoinedCall {
  Gate& gate;
  std::thread thread;
  explicit JoinedCall(Gate& entry_gate) : gate(entry_gate) {}
  ~JoinedCall() noexcept {
    gate.Release();
    if (thread.joinable()) thread.join();
  }
};
std::uint64_t ConcurrentSharedLedger(const Fixture& fixture,
                                     std::uint64_t budget,
                                     bool expect_budget_failure) {
  auto context = Context(budget);
  Requests first_file(context);
  Requests first_container(context);
  Workers first_workers;
  Handles first_handles;
  ContainerLaunches first_launches;
  Gate gate;
  first_workers.gate = &gate;
  std::optional<ic::GltfFileReadResult> first;
  std::exception_ptr error;
  JoinedCall call(gate);
  call.thread = std::thread([&] {
    try {
      first = Read(fixture, context,
                   Testing(first_file, first_container, first_workers,
                           first_handles, first_launches));
    } catch (...) {
      error = std::current_exception();
    }
    gate.Finish();
  });
  Require(gate.Wait() && context.ActiveWorkers() == kWorkerCount,
          "first real file entries occupy shared native workers");
  const auto foreign = context.ActiveTrackedPayload();
  Require(foreign > 0, "first blocked call holds real original requests");
  Requests second_file(context);
  Requests second_container(context);
  Workers second_workers;
  Handles second_handles;
  ContainerLaunches second_launches;
  {
    auto result = Read(fixture, context,
                       Testing(second_file, second_container, second_workers,
                               second_handles, second_launches));
    if (expect_budget_failure) {
      Require(!result.asset && result.diagnostic.status ==
                                   ic::GltfFileStatus::kBudgetExceeded,
              "tight common ledger refuses second operation, not its fresh "
              "scope cap");
    } else {
      Success(result);
      Check(*result.asset, fixture);
      Require(context.ActiveTrackedPayload() ==
                  foreign + second_file.live + second_container.live +
                      *result.asset->Asset().NewRequestedBytes(),
              "second operation scopes exclude first real foreign requests");
    }
    Require(second_workers.launched == 0 && second_launches.callbacks == 0,
            "concurrent second fresh scopes use caller fallback");
  }
  const auto common_peak = context.PeakTrackedPayload();
  second_file.Check(0);
  second_container.Check(0);
  second_workers.Check();
  second_handles.Check();
  Require(context.ActiveTrackedPayload() == foreign &&
              context.ActiveWorkers() == kWorkerCount,
          "second cleanup preserves first charged requests and occupied slots");
  gate.Release();
  call.thread.join();
  if (error) std::rethrow_exception(error);
  Require(first.has_value(), "first operation returned after gate release");
  Success(*first);
  Check(*first->asset, fixture);
  first->asset.reset();
  first_file.Check(0);
  first_container.Check(0);
  first_workers.Check();
  first_handles.Check();
  Empty(context);
  return common_peak;
}
void ConcurrentAndCommonLedgerCaps() {
  Fixture fixture;
  const auto common_peak =
      ConcurrentSharedLedger(fixture, kRequestLimit, false);
  Require(common_peak > 0 && common_peak < kRequestLimit,
          "deterministic concurrent schedule records actual combined peak");
  const auto exact_peak = ConcurrentSharedLedger(fixture, common_peak, false);
  Require(exact_peak == common_peak,
          "inclusive common ledger admits same concurrent schedule");
  (void)ConcurrentSharedLedger(fixture, common_peak - 1, true);
}
}  // namespace

int main() {
  try {
    OriginalFaultsAndLifetime();
    RequestCapsAndOccupiedWorkers();
    PartialLaunchAndCancellation();
    ConcurrentAndCommonLedgerCaps();
    std::cout
        << "GLB file/container requests, native faults and lifetimes passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
