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
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
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

#include "glb_detail.h"
#include "gltf_allocation.h"
#include "gltf_file_publish_glb_fixture.h"
#include "gltf_files_test_internal.h"
#include "gltf_operation_workers.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::gltf_detail;
namespace files = ic::gltf_files_detail;
namespace fixture = ic::test_glb_publication;
namespace fs = std::filesystem;
using fixture::Bytes;
using fixture::FileLimits;
using fixture::Fixture;
using fixture::kBinary;
using fixture::kChangedTriangles;
using fixture::kCountLimit;
using fixture::kDynamicPath;
using fixture::kEntry;
using fixture::kInitialPaths;
using fixture::kOpaqueBytes;
using fixture::kPhysicalBytes;
using fixture::kRequestBytes;
using fixture::kSourceTriangles;
using fixture::kStaticBytes;
using fixture::kStaticPath;
using fixture::kWorkerCount;
using fixture::Limits;
using fixture::ReadBytes;
using fixture::ReadLimits;
using fixture::ReadWork;
using fixture::Require;
using fixture::Work;
struct AllocationFailure {};
struct LaunchFailure {};
struct Requests {
  static constexpr std::size_t kBlockCapacity = 4096;
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
  std::size_t post_failure_calls = 0;
  bool failed = false;
  bool frozen = false;
  bool incorrect_free = false;
  std::stop_source* cancel = nullptr;
  explicit Requests(const geo::ExecutionContext& ledger) : context(ledger) {}
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Requests*>(state);
    std::lock_guard lock(self.mutex);
    ++self.calls;
    Require(self.context.ActiveTrackedPayload() >= self.live + bytes,
            "original allocation follows shared ledger admission");
    if (self.frozen)
      throw std::runtime_error("original allocation after commit barrier");
    if (self.failed) {
      ++self.post_failure_calls;
      throw AllocationFailure{};
    }
    if (self.calls == self.fail_at) {
      self.failed = true;
      throw AllocationFailure{};
    }
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
    constexpr std::chrono::seconds kEntryWait{30};
    std::unique_lock lock(mutex);
    changed.wait_for(lock, kEntryWait,
                     [&] { return entered >= kWorkerCount || finished; });
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
  static constexpr std::size_t kNativeCapacity = 1024;
  struct Entry {
    detail::NativeWorkerRecord original;
    Workers* owner = nullptr;
  };
  std::array<Entry, kNativeCapacity> entries{};
  detail::NativeWorkerBackend original = detail::DefaultNativeWorkerBackend();
  std::size_t calls = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::size_t fail_at = 0;
  std::atomic<std::size_t> entered{0};
  std::atomic<std::size_t> exited{0};
  std::atomic<std::size_t> jobs{0};
  detail::OperationWorkerObserver observer{nullptr, nullptr, JobEntered, this};
  std::stop_source* cancel = nullptr;
  Gate* gate = nullptr;
  static void JobEntered(std::size_t, void* state) noexcept {
    ++static_cast<Workers*>(state)->jobs;
  }
  static void Enter(detail::NativeWorkerRecord& record) noexcept {
    auto& entry = *static_cast<Entry*>(record.state);
    auto& self = *entry.owner;
    ++self.entered;
    if (self.cancel) self.cancel->request_stop();
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
  detail::NativeExecutionOptions Execution() { return {Backend(), &observer}; }
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
struct HookFailure {};
struct Witnesses {
  Requests requests;
  Workers write;
  Workers import;
  Workers capture;
  Workers verification;
  Handles handles;
  fs::path stage;
  std::size_t stage_calls = 0;
  std::atomic<std::size_t> write_blocks{0};
  std::size_t inventories = 0;
  std::size_t commit_calls = 0;
  std::size_t cleanup_calls = 0;
  ic::GltfFileCleanupOutcome cleanup = ic::GltfFileCleanupOutcome::kNotRequired;
  std::error_code cleanup_observed_error;
  std::error_code refuse_cleanup;
  bool throw_write = false;
  bool throw_inventory = false;
  bool throw_commit = false;
  explicit Witnesses(const geo::ExecutionContext& context)
      : requests(context) {}
  static void StageReady(files::TestNativeView path, void* state) {
    auto& self = *static_cast<Witnesses*>(state);
    self.stage = fs::path(path);
    ++self.stage_calls;
    Require(fs::is_directory(self.stage),
            "actual exclusive staging root exists");
  }
  static void BeforeWrite(std::uint64_t, std::uint64_t, void* state) {
    auto& self = *static_cast<Witnesses*>(state);
    ++self.write_blocks;
    if (self.throw_write) throw HookFailure{};
  }
  static void BeforeInventory(files::TestNativeView, void* state) {
    auto& self = *static_cast<Witnesses*>(state);
    ++self.inventories;
    if (self.throw_inventory) throw HookFailure{};
  }
  static void BeforeCommit(files::TestNativeView, void* state) {
    auto& self = *static_cast<Witnesses*>(state);
    ++self.commit_calls;
    self.CheckNative();
    self.handles.Check();
    self.requests.frozen = true;
    if (self.throw_commit) throw HookFailure{};
  }
  static std::error_code CleanupError(files::TestNativeView,
                                      void* state) noexcept {
    return static_cast<Witnesses*>(state)->refuse_cleanup;
  }
  static void CleanupFinished(ic::GltfFileCleanupOutcome outcome,
                              std::error_code error, void* state) noexcept {
    auto& self = *static_cast<Witnesses*>(state);
    ++self.cleanup_calls;
    self.cleanup = outcome;
    self.cleanup_observed_error = error;
  }
  files::GltfFilePublicationTestOptions Testing() {
    files::GltfFilePublicationTestOptions testing;
    testing.allocation_backend = requests.Backend();
    testing.write_execution = write.Execution();
    testing.import_execution = import.Execution();
    testing.capture_execution = capture.Execution();
    testing.verification_execution = verification.Execution();
    testing.file_hooks = handles.Hooks();
    testing.hooks = {BeforeWrite,  StageReady,   BeforeInventory,
                     BeforeCommit, CleanupError, CleanupFinished,
                     this};
    return testing;
  }
  void CheckNative() const {
    write.Check();
    import.Check();
    capture.Check();
    verification.Check();
  }
  void Released(std::uint64_t foreign) const {
    requests.Check(0);
    CheckNative();
    handles.Check();
    Require(requests.post_failure_calls == 0 &&
                requests.context.ActiveTrackedPayload() == foreign &&
                requests.context.ActiveWorkers() == 0,
            "own failure cleanup allocates nothing and preserves all retained "
            "inputs");
  }
};
enum class ProviderFailure { kNone, kOrdinary, kAllocation };
struct Provider {
  std::array<ic::GltfFileView, 1> initial{{{kStaticPath, Bytes(kStaticBytes)}}};
  std::vector<std::byte> payload{kOpaqueBytes, std::byte{0xd3}};
  std::array<ic::GltfFileView, 1> dynamic_files{{{kDynamicPath, payload}}};
  bool dynamic = true;
  bool copy_inspection = false;
  std::optional<ic::GltfTopologyOutputInspection> retained;
  std::size_t verified_calls = 0;
  ProviderFailure failure = ProviderFailure::kNone;
  Workers* arm = nullptr;
  bool fail_launch = false;
  std::stop_source* cancel = nullptr;
  Gate* gate = nullptr;
  Requests* freeze = nullptr;
  std::size_t armed_calls = 0;
  std::size_t armed_entries = 0;
  static std::span<const ic::GltfFileView> Provide(
      const ic::GltfFileVerifiedContent& content, void* state) {
    auto& self = *static_cast<Provider*>(state);
    ++self.verified_calls;
    Require(content.format == ic::GltfFileFormat::kGlb && content.container &&
                content.files.size() == kInitialPaths.size(),
            "provider observes actual GLB verification and initial inventory");
    for (std::size_t i = 0; i < kInitialPaths.size(); ++i)
      Require(content.files[i].path == kInitialPaths[i],
              "literal sorted initial inventory excludes dynamic receipt");
    if (self.copy_inspection) self.retained.emplace(content.inspection);
    if (self.arm) {
      self.armed_calls = self.arm->calls;
      self.armed_entries = self.arm->entered;
      if (self.fail_launch) self.arm->fail_at = self.arm->calls + 2;
      self.arm->cancel = self.cancel;
      self.arm->gate = self.gate;
    }
    if (self.failure != ProviderFailure::kNone) {
      if (self.freeze) self.freeze->frozen = true;
      if (self.failure == ProviderFailure::kAllocation) throw std::bad_alloc();
      throw HookFailure{};
    }
    return self.dynamic ? std::span<const ic::GltfFileView>(self.dynamic_files)
                        : std::span<const ic::GltfFileView>{};
  }
  ic::GltfFilePublicationOptions Options() {
    ic::GltfFilePublicationOptions options;
    options.supplemental_files = initial;
    options.verified_files_provider = {Provide, this};
    return options;
  }
};
void Success(const ic::GltfFilePublicationResult& result) {
  Require(result.bundle &&
              result.outcome == ic::GltfFilePublicationOutcome::kPublished &&
              result.diagnostic.status == ic::GltfFileStatus::kSuccess,
          "actual native publication succeeded");
}
void Check(const ic::GltfPublishedBundle& bundle, const Fixture& source,
           const fs::path& root, bool dynamic) {
  constexpr std::array<std::string_view, 6> kFinalPaths{
      "a.bin",      "opaque-a.bin", "opaque-b.bin",
      kDynamicPath, kStaticPath,    kEntry};
  Require(bundle.Files().size() ==
              (dynamic ? kFinalPaths.size() : kInitialPaths.size()),
          "literal final physical file count");
  std::size_t ordinal = 0;
  for (const auto name : kFinalPaths) {
    if (!dynamic && name == kDynamicPath) continue;
    const auto& actual = bundle.Files()[ordinal++];
    Require(
        actual.path == name && ReadBytes(root / name) ==
                                   std::vector<std::byte>(actual.bytes.begin(),
                                                          actual.bytes.end()),
        "committed literal inventory independently reopened");
    if (name == "a.bin")
      Require(std::ranges::equal(actual.bytes, kBinary),
              "complete literal buffer tail");
    else if (name == kStaticPath)
      Require(std::ranges::equal(actual.bytes, Bytes(kStaticBytes)),
              "literal static bytes");
    else if (name == kDynamicPath)
      Require(actual.bytes.size() == kOpaqueBytes &&
                  std::ranges::all_of(
                      actual.bytes,
                      [](std::byte b) { return b == std::byte{0xd3}; }),
              "literal complete borrowed dynamic bytes");
    else if (name != kEntry)
      Require(std::ranges::equal(actual.bytes, source.opaque),
              "literal complete opaque bytes");
    else if (!source.inspection->Output().ChangedBinding())
      Require(std::ranges::equal(actual.bytes, source.wire),
              "unchanged literal GLB wire and authored whitespace");
  }
  auto read = ic::ReadGltfFileAsset(
      root / kEntry,
      {{},
       ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
       dynamic
           ? std::span<const std::string_view>(kFinalPaths).subspan(1, 4)
           : std::span<const std::string_view>(kInitialPaths).subspan(1, 3)},
      {FileLimits(),
       ReadLimits(),
       {kPhysicalBytes, kCountLimit, kRequestBytes}},
      {Work(true).files, ReadWork(true), Work(true).container}, source.context);
  Require(read.asset.has_value(), "actual finalized entry read again");
  auto extracted = ic::ExtractGltfPrimitiveCapped(
      read.asset->Asset(), *read.asset->Asset().Primitive(0),
      {ic::GltfTriangleRequest::kRequireTriangles, ic::GltfValueRequest::kRaw},
      {kCountLimit, kCountLimit, kRequestBytes});
  Require(extracted.extraction &&
              std::ranges::equal(
                  extracted.extraction->TriangleIndices(),
                  source.inspection->Output().ChangedBinding()
                      ? std::span<const std::uint32_t>(kChangedTriangles)
                      : std::span<const std::uint32_t>(kSourceTriangles)),
          "literal selected topology independently re-extracted");
}
ic::GltfFilePublicationResult Publish(
    const Fixture& fixture, const fs::path& destination, Witnesses& witnesses,
    const ic::GltfFilePublicationOptions& options, bool serial = false,
    const ic::GltfFilePublicationLimits& limits = Limits(),
    std::stop_token stop = {}) {
  return files::TestPublishGltfTopologyBundle(
      {*fixture.source, *fixture.inspection}, destination, options, limits,
      Work(serial), stop, witnesses.Testing());
}
void Stats(const ic::GltfFilePublicationResult& result,
           const Witnesses& witness) {
  Require(
      result.execution.capture.launched_workers == witness.capture.launched &&
          result.execution.capture.joined_workers == witness.capture.joined &&
          result.execution.capture.planned_jobs ==
              witness.capture.jobs + result.execution.capture.caller_jobs,
      "public capture counts describe only actual capture native/caller jobs");
  Require(result.execution.byte_verification.launched_workers ==
                  witness.verification.launched &&
              result.execution.byte_verification.joined_workers ==
                  witness.verification.joined &&
              result.execution.byte_verification.planned_jobs ==
                  witness.verification.jobs +
                      result.execution.byte_verification.caller_jobs,
          "public verification counts exclude staging write and all delegated "
          "work");
}
void NoStaging(const Fixture& fixture) {
  for (const auto& item : fs::directory_iterator(fixture.root))
    Require(item.path().filename() == "input",
            "actual fixture parent has no published or abandoned staging "
            "directory");
}
void CleanupRemoved(const Witnesses& witness) {
  if (witness.stage_calls > 0)
    Require(witness.cleanup_calls == 1 &&
                witness.cleanup == ic::GltfFileCleanupOutcome::kRemoved &&
                !witness.cleanup_observed_error && !fs::exists(witness.stage),
            "actual owned staging root removed after attempted rollback");
}
void OriginalRequests() {
  // Sparse caller-boundary output never enters an injected request or cleanup.
  constexpr std::size_t kFaultProgressStride = 128;
  for (const bool changed : {false, true}) {
    Fixture fixture(changed);
    const auto foreign = fixture.context.ActiveTrackedPayload();
    for (const bool serial : {true, false}) {
      for (const bool dynamic : {false, true}) {
        const auto sweep_start = std::chrono::steady_clock::now();
        std::cout << "publication original sweep start"
                  << (changed ? " changed" : " unchanged")
                  << (dynamic ? " dynamic" : " static")
                  << (serial ? " serial" : " native") << std::endl;
        Witnesses baseline(fixture.context);
        Provider baseline_supplements;
        baseline_supplements.dynamic = dynamic;
        std::size_t calls = 0;
        {
          auto result = Publish(fixture, fixture.Destination(), baseline,
                                baseline_supplements.Options(), serial);
          Success(result);
          Check(*result.bundle, fixture, fixture.Destination(), dynamic);
          baseline.requests.Check(result.bundle->NewRequestedBytes());
          Stats(result, baseline);
          Require(baseline.commit_calls == 1 && baseline.cleanup_calls == 0 &&
                      baseline.requests.frozen,
                  "real native commit follows frozen owner and all joins, "
                  "without cleanup callback");
          calls = baseline.requests.calls;
          Require(calls > 0 && (baseline.write.launched > 0) == !serial &&
                      (baseline.import.launched > 0) == !serial &&
                      (baseline.capture.launched > 0) == !serial &&
                      (baseline.verification.launched > 0) == !serial,
                  "baseline original requests and all three actual native "
                  "roles reached");
        }
        baseline.Released(foreign);
        fs::remove_all(fixture.Destination());
        for (std::size_t ordinal = 1; ordinal <= calls; ++ordinal) {
          if (ordinal == 1 || ordinal == calls ||
              ordinal % kFaultProgressStride == 0)
            std::cout << "publication original sweep ordinal " << ordinal << '/'
                      << calls << (changed ? " changed" : " unchanged")
                      << (dynamic ? " dynamic" : " static")
                      << (serial ? " serial" : " native") << " elapsed_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - sweep_start)
                             .count()
                      << std::endl;
          Witnesses witness(fixture.context);
          Provider supplements;
          supplements.dynamic = dynamic;
          witness.requests.fail_at = ordinal;
          bool propagated = false;
          try {
            const auto result = Publish(fixture, fixture.Destination(), witness,
                                        supplements.Options(), serial);
            Require(!result.bundle,
                    "original fault exposes no published owner: ordinal=" +
                        std::to_string(ordinal) +
                        " baseline=" + std::to_string(calls) +
                        " actual=" + std::to_string(witness.requests.calls) +
                        " failed=" + std::to_string(witness.requests.failed) +
                        " baseline-stage-units=" +
                        std::to_string(baseline.stage.native().size()) +
                        " actual-stage-units=" +
                        std::to_string(witness.stage.native().size()));
          } catch (const AllocationFailure&) {
            propagated = true;
          }
          Require(propagated,
                  "every original file request fault propagates unchanged");
          witness.Released(foreign);
          CleanupRemoved(witness);
          NoStaging(fixture);
        }
        std::cout << "publication original faults " << calls
                  << (changed ? " changed" : " unchanged")
                  << (dynamic ? " dynamic" : " static")
                  << (serial ? " serial" : " native") << " complete elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - sweep_start)
                         .count()
                  << std::endl;
      }
    }
  }
}
void FreshCapsAndLifetime() {
  Fixture fixture(true);
  const auto foreign = fixture.context.ActiveTrackedPayload();
  for (const bool serial : {true, false}) {
    Witnesses baseline(fixture.context);
    Provider initial;
    {
      auto result = Publish(fixture, fixture.Destination(), baseline,
                            initial.Options(), serial);
      Success(result);
    }
    baseline.Released(foreign);
    fs::remove_all(fixture.Destination());
    const auto peak = baseline.requests.peak;
    for (const bool reduce : {false, true}) {
      auto limits = Limits();
      limits.files.max_new_requested_bytes = peak - (reduce ? 1 : 0);
      Witnesses witness(fixture.context);
      Provider supplements;
      {
        auto result = Publish(fixture, fixture.Destination(), witness,
                              supplements.Options(), serial, limits);
        if (reduce) {
          Require(!result.bundle &&
                      result.diagnostic.status ==
                          ic::GltfFileStatus::kLimitExceeded &&
                      result.diagnostic.code ==
                          ic::GltfFileDiagnosticCode::kFileRequestLimit,
                  "minus-one fresh file cap refuses independently of retained "
                  "input charges");
          CleanupRemoved(witness);
          NoStaging(fixture);
        } else {
          Success(result);
          Check(*result.bundle, fixture, fixture.Destination(), true);
          witness.requests.Check(result.bundle->NewRequestedBytes());
          Require(witness.requests.peak == peak,
                  "inclusive fresh file allowance admits identical own request "
                  "peak");
        }
      }
      witness.Released(foreign);
      if (!reduce) fs::remove_all(fixture.Destination());
    }
  }
  Witnesses witness(fixture.context);
  Provider supplements;
  supplements.copy_inspection = true;
  std::optional<ic::GltfPublishedBundle> retained;
  {
    auto result =
        Publish(fixture, fixture.Destination(), witness, supplements.Options());
    Success(result);
    const auto original_calls = witness.requests.calls;
    retained.emplace(*result.bundle);
    Require(witness.requests.calls == original_calls && supplements.retained,
            "public bundle and actual callback inspection copies add no file "
            "requests");
  }
  supplements.dynamic_files = {};
  supplements.payload.clear();
  supplements.payload.shrink_to_fit();
  fs::remove_all(fixture.root / "input");
  Check(*retained, fixture, fixture.Destination(), true);
  const auto source_bytes = fixture.source->NewRequestedBytes();
  fixture.source.reset();
  fixture.inspection.reset();
  fixture.materialization.primitive.reset();
  Require(source_bytes > 0 &&
              std::ranges::equal(retained->Source().Asset().OriginalJson(),
                                 fixture.chunks[0].bytes),
          "source owner preserved after source files disappear");
  retained.reset();
  witness.requests.Check(0);
  witness.CheckNative();
  witness.handles.Check();
  Require(supplements.retained &&
              supplements.retained->ReloadedAsset().NewRequestedBytes() &&
              fixture.context.ActiveTrackedPayload() > 0,
          "actual verified reload/inspection can outlive last published file "
          "owner");
  supplements.retained.reset();
  witness.Released(0);
}
enum class Role { kWrite, kImport, kCapture, kVerification };
Workers& Worker(Witnesses& witness, Role role) {
  if (role == Role::kWrite) return witness.write;
  if (role == Role::kImport) return witness.import;
  if (role == Role::kCapture) return witness.capture;
  return witness.verification;
}
void BorrowedNativeFailuresAndStops() {
  Fixture source;
  const auto foreign = source.context.ActiveTrackedPayload();
  for (const auto role :
       {Role::kWrite, Role::kImport, Role::kCapture, Role::kVerification}) {
    for (const bool cancel : {false, true}) {
      Witnesses witness(source.context);
      Provider provider;
      auto& worker = Worker(witness, role);
      provider.arm = &worker;
      provider.fail_launch = !cancel;
      std::stop_source stop;
      if (cancel) provider.cancel = &stop;
      bool propagated = false;
      try {
        const auto result =
            Publish(source, source.Destination(), witness, provider.Options(),
                    false, Limits(), stop.get_token());
        Require(
            cancel && !result.bundle &&
                result.outcome == ic::GltfFilePublicationOutcome::kCanceled &&
                result.diagnostic.status == ic::GltfFileStatus::kCanceled,
            "stop from real native entry after provider precedes commit");
      } catch (const LaunchFailure&) {
        propagated = true;
      }
      Require(provider.verified_calls == 1 &&
                  worker.entered > provider.armed_entries &&
                  (cancel ||
                   (propagated && worker.calls == provider.armed_calls + 2)),
              "borrowed producer storage remains until actual entries/partial "
              "joins");
      witness.Released(foreign);
      CleanupRemoved(witness);
      NoStaging(source);
    }
  }
}
void ProducerExceptionsAndCleanup() {
  Fixture source;
  const auto foreign = source.context.ActiveTrackedPayload();
  for (const auto failure :
       {ProviderFailure::kOrdinary, ProviderFailure::kAllocation}) {
    for (const bool refuse : {false, true}) {
      Witnesses witness(source.context);
      Provider provider;
      provider.failure = failure;
      provider.freeze = &witness.requests;
      if (refuse)
        witness.refuse_cleanup =
            std::make_error_code(std::errc::permission_denied);
      bool allocation = false;
      try {
        const auto result =
            Publish(source, source.Destination(), witness, provider.Options());
        Require(
            failure == ProviderFailure::kOrdinary && !result.bundle &&
                result.outcome == ic::GltfFilePublicationOutcome::kFailed &&
                result.diagnostic.status ==
                    ic::GltfFileStatus::kCallbackFailure &&
                result.diagnostic.stage == ic::GltfFileStage::kSupplemental &&
                result.diagnostic.code ==
                    ic::GltfFileDiagnosticCode::kVerifiedCallback &&
                result.cleanup == (refuse
                                       ? ic::GltfFileCleanupOutcome::kFailed
                                       : ic::GltfFileCleanupOutcome::kRemoved),
            "ordinary producer exception has exact primary and actual cleanup");
      } catch (const std::bad_alloc&) {
        allocation = true;
      }
      Require(
          allocation == (failure == ProviderFailure::kAllocation) &&
              provider.verified_calls == 1 && witness.commit_calls == 0,
          "producer allocation exception propagates without fabricated result");
      witness.Released(foreign);
      Require(fs::exists(witness.stage) == refuse &&
                  witness.cleanup ==
                      (refuse ? ic::GltfFileCleanupOutcome::kFailed
                              : ic::GltfFileCleanupOutcome::kRemoved),
              "preallocated cleanup independently checked on filesystem");
      if (refuse) fs::remove_all(witness.stage);
      NoStaging(source);
    }
  }
}
void OccupiedWorkers() {
  Fixture fixture(true);
  const auto foreign = fixture.context.ActiveTrackedPayload();
  auto occupied = geo::TryReserveWorkers(fixture.context, kWorkerCount);
  Require(occupied.Count() == kWorkerCount,
          "all authoritative worker slots occupied");
  Witnesses witness(fixture.context);
  Provider supplements;
  {
    auto result =
        Publish(fixture, fixture.Destination(), witness, supplements.Options());
    Success(result);
    Check(*result.bundle, fixture, fixture.Destination(), true);
    Stats(result, witness);
    Require(witness.write.launched == 0 && witness.import.launched == 0 &&
                witness.capture.launched == 0 &&
                witness.verification.launched == 0 &&
                witness.write_blocks > 0 &&
                result.execution.capture.caller_jobs > 0 &&
                result.execution.byte_verification.caller_jobs > 0,
            "substantial occupied work uses actual caller paths without "
            "fabricated native counts");
  }
  witness.requests.Check(0);
  Require(fixture.context.ActiveWorkers() == kWorkerCount &&
              fixture.context.ActiveTrackedPayload() == foreign,
          "caller publication preserves unrelated reservations and retained "
          "inputs");
  occupied.Reset();
  witness.Released(foreign);
}
struct ReleaseAndJoin {
  Gate& gate;
  std::thread& thread;
  ~ReleaseAndJoin() noexcept {
    gate.Release();
    if (thread.joinable()) thread.join();
  }
};
void ConcurrentRequests() {
  Fixture fixture(true);
  const auto foreign = fixture.context.ActiveTrackedPayload();
  std::uint64_t cap = 0;
  for (const bool serial : {true, false}) {
    Witnesses baseline(fixture.context);
    Provider supplements;
    {
      auto result = Publish(fixture, fixture.Destination(), baseline,
                            supplements.Options(), serial);
      Success(result);
      cap = std::max(cap, static_cast<std::uint64_t>(baseline.requests.peak));
    }
    baseline.Released(foreign);
    fs::remove_all(fixture.Destination());
  }
  for (const bool tight : {false, true}) {
    auto limits = Limits();
    limits.files.max_new_requested_bytes = cap;
    Witnesses first(fixture.context);
    Provider first_supplements;
    Gate gate;
    first_supplements.arm = &first.import;
    first_supplements.gate = &gate;
    std::optional<ic::GltfFilePublicationResult> first_result;
    std::exception_ptr failure;
    std::thread thread([&] {
      try {
        first_result.emplace(Publish(fixture, fixture.Destination(), first,
                                     first_supplements.Options(), false,
                                     limits));
      } catch (...) {
        failure = std::current_exception();
      }
      gate.Finish();
    });
    ReleaseAndJoin join{gate, thread};
    Require(
        gate.Wait(),
        "first publisher reached four actual borrowed import native entries");
    const auto held = fixture.context.ActiveTrackedPayload();
    Require(held > foreign && fixture.context.ActiveWorkers() == kWorkerCount,
            "paused actual borrowed imports retain their own requests and all "
            "worker slots");
    auto unrelated = geo::TryReservePayload(
        fixture.context,
        tight ? static_cast<std::size_t>(kRequestBytes - held - 1) : 0);
    Require(unrelated.status == geo::ReservationStatus::kAccepted,
            "explicit unrelated common-ledger reservation admitted");
    Witnesses second(fixture.context);
    Provider second_supplements;
    {
      auto result = Publish(fixture, fixture.Destination("published-b"), second,
                            second_supplements.Options(), false, limits);
      if (tight) {
        Require(!result.bundle &&
                    result.diagnostic.status ==
                        ic::GltfFileStatus::kBudgetExceeded &&
                    result.diagnostic.code ==
                        ic::GltfFileDiagnosticCode::kRequestBudget,
                "fresh file allowance does not bypass an actually occupied "
                "common ledger");
        CleanupRemoved(second);
      } else {
        Success(result);
        Check(*result.bundle, fixture, fixture.Destination("published-b"),
              true);
        Stats(result, second);
        Require(second.write.launched == 0 && second.import.launched == 0 &&
                    second.capture.launched == 0 &&
                    second.verification.launched == 0 &&
                    second.requests.peak <= cap,
                "concurrent second invocation owns a fresh cap and actual "
                "occupied caller paths");
      }
    }
    second.requests.Check(0);
    second.CheckNative();
    second.handles.Check();
    Require(second.requests.post_failure_calls == 0 &&
                fixture.context.ActiveTrackedPayload() ==
                    held + unrelated.lease.Bytes() &&
                fixture.context.ActiveWorkers() == kWorkerCount,
            "second completion preserves first native ownership and unrelated "
            "payload");
    unrelated.lease.Reset();
    gate.Release();
    thread.join();
    if (failure) std::rethrow_exception(failure);
    Require(first_result.has_value(),
            "first native invocation returned after joins");
    Success(*first_result);
    Stats(*first_result, first);
    Check(*first_result->bundle, fixture, fixture.Destination(), true);
    Require(first.requests.peak <= cap,
            "first invocation retains its own inclusive fresh cap");
    first_result.reset();
    first.Released(foreign);
    second.Released(foreign);
    fs::remove_all(fixture.Destination());
    fs::remove_all(fixture.Destination("published-b"));
    NoStaging(fixture);
  }
}
void RunPhase(std::string_view name, void (*phase)()) {
  const auto start = std::chrono::steady_clock::now();
  std::cout << "provider request phase start " << name << std::endl;
  phase();
  std::cout << "provider request phase complete " << name << " elapsed_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start)
                   .count()
            << std::endl;
}
// The assembler and staged reader have distinct fresh counters. Backend state
// remains alive until the final retained container copy releases its storage.
struct Containers {
  static constexpr std::size_t kMinimumParallelWorkers = 2;
  Requests assembly;
  Requests read;
  bool reading = false;
  std::size_t assembly_launches = 0;
  std::size_t read_launches = 0;
  std::size_t fail_launch = 0;
  bool target_reading = false;
  std::size_t cancel_launch = 0;
  std::stop_source* cancel = nullptr;
  explicit Containers(const geo::ExecutionContext& context)
      : assembly(context), read(context) {}
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Containers*>(state);
    return Requests::Allocate(bytes, alignment,
                              self.reading ? &self.read : &self.assembly);
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Containers*>(state);
    for (auto* requests : {&self.assembly, &self.read}) {
      bool found = false;
      {
        std::lock_guard lock(requests->mutex);
        for (const auto& block : requests->blocks)
          if (block.pointer == pointer) found = true;
      }
      if (found) {
        Requests::Free(pointer, bytes, alignment, requests);
        return;
      }
    }
    self.read.incorrect_free = true;
  }
  static void BeforeLaunch(std::size_t, void* state) {
    auto& self = *static_cast<Containers*>(state);
    auto& calls = self.reading ? self.read_launches : self.assembly_launches;
    ++calls;
    Require(self.read.context.ActiveWorkers() >= kMinimumParallelWorkers,
            "container launch hook follows actual shared native reservation");
    if (self.reading == self.target_reading && calls == self.fail_launch)
      throw LaunchFailure{};
    if (self.reading == self.target_reading && calls == self.cancel_launch &&
        self.cancel)
      self.cancel->request_stop();
  }
  void Configure(files::GltfFilePublicationTestOptions& testing,
                 ic::GltfFilePublicationOptions& options) {
    testing.container = {{Allocate, Free, this}, BeforeLaunch, this};
    options.on_phase = [&](auto phase) {
      if (phase == ic::GltfFilePublicationPhase::kVerification) reading = true;
    };
  }
  void Released() const {
    assembly.Check(0);
    read.Check(0);
    Require(assembly.post_failure_calls == 0 && read.post_failure_calls == 0,
            "container failure cleanup requests no additional storage");
  }
};
void ContainerOriginalRequests() {
  for (const bool changed : {false, true}) {
    Fixture source(changed);
    const auto foreign = source.context.ActiveTrackedPayload();
    for (const bool serial : {true, false}) {
      // Caller-owned recorders are excluded from module request accounting.
      // Heap ownership avoids a megabyte-scale ASan caller frame without
      // shrinking fixed observer capacity or changing any injected ordinal.
      const auto baseline_owner = std::make_unique<Containers>(source.context);
      auto& baseline = *baseline_owner;
      Witnesses files_witness(source.context);
      auto testing = files_witness.Testing();
      ic::GltfFilePublicationOptions options;
      baseline.Configure(testing, options);
      {
        auto result = files::TestPublishGltfTopologyBundle(
            {*source.source, *source.inspection}, source.Destination(), options,
            Limits(), Work(serial), {}, testing);
        Success(result);
        baseline.assembly.Check(0);
        Require(
            (baseline.assembly.calls > 0) == changed &&
                baseline.read.calls > 0 && baseline.read.live > 0 &&
                (baseline.read_launches > 0) == !serial &&
                (baseline.assembly_launches > 0) == (changed && !serial),
            "actual changed-only assembly and retained staged-reader scopes");
        auto copy = *result.bundle;
        result.bundle.reset();
        Require(copy.Container() && baseline.read.live > 0,
                "copied published container retains exact backend requests");
      }
      baseline.Released();
      files_witness.Released(foreign);
      fs::remove_all(source.Destination());
      for (const bool reading : {false, true}) {
        const auto calls =
            reading ? baseline.read.calls : baseline.assembly.calls;
        std::cout << "GLB original scope start "
                  << (reading ? "read" : "assembly") << " calls=" << calls
                  << " changed=" << changed << " serial=" << serial
                  << std::endl;
        for (std::size_t ordinal = 1; ordinal <= calls; ++ordinal) {
          std::cout << "GLB original ordinal " << ordinal << '/' << calls
                    << (reading ? " read" : " assembly") << std::endl;
          const auto container_owner =
              std::make_unique<Containers>(source.context);
          auto& container = *container_owner;
          (reading ? container.read : container.assembly).fail_at = ordinal;
          Witnesses witness(source.context);
          auto fault_testing = witness.Testing();
          ic::GltfFilePublicationOptions fault_options;
          container.Configure(fault_testing, fault_options);
          bool propagated = false;
          try {
            const auto result = files::TestPublishGltfTopologyBundle(
                {*source.source, *source.inspection}, source.Destination(),
                fault_options, Limits(), Work(serial), {}, fault_testing);
            Require(!result.bundle,
                    "container original fault exposes no owner");
          } catch (const AllocationFailure&) {
            propagated = true;
          }
          Require(propagated,
                  "every actual container original fault propagates");
          container.Released();
          witness.Released(foreign);
          CleanupRemoved(witness);
          NoStaging(source);
        }
        std::cout << "GLB original scope complete " << calls << std::endl;
      }
      const auto peak = std::max(baseline.assembly.peak, baseline.read.peak);
      for (const bool reduce : {false, true}) {
        auto limits = Limits();
        limits.container.max_requested_bytes = peak - (reduce ? 1 : 0);
        const auto container_owner =
            std::make_unique<Containers>(source.context);
        auto& container = *container_owner;
        Witnesses witness(source.context);
        auto cap_testing = witness.Testing();
        ic::GltfFilePublicationOptions cap_options;
        container.Configure(cap_testing, cap_options);
        {
          auto result = files::TestPublishGltfTopologyBundle(
              {*source.source, *source.inspection}, source.Destination(),
              cap_options, limits, Work(serial), {}, cap_testing);
          if (reduce) {
            Require(!result.bundle &&
                        result.diagnostic.code ==
                            ic::GltfFileDiagnosticCode::kDelegatedContainer &&
                        result.diagnostic.container_status ==
                            ic::GlbStatus::kLimitExceeded,
                    "minus-one fresh container allowance retains delegated "
                    "refusal");
            CleanupRemoved(witness);
          } else {
            Success(result);
          }
        }
        container.Released();
        witness.Released(foreign);
        if (!reduce) fs::remove_all(source.Destination());
        NoStaging(source);
      }
    }
  }
}
void PhysicalGlbCaps() {
  enum class Cap {
    kWire,
    kChunks,
    kFiles,
    kBindings,
    kFileBytes,
    kTotalBytes,
    kPath,
    kTotalPaths
  };
  for (const bool changed : {false, true}) {
    Fixture source(changed);
    const auto foreign = source.context.ActiveTrackedPayload();
    const auto wire = source.ExpectedWire();
    constexpr std::size_t kPhysicalOpaqueFiles = 2;
    const auto total =
        wire.size() + kBinary.size() + kPhysicalOpaqueFiles * kOpaqueBytes;
    std::size_t total_paths = 0;
    std::size_t longest = 0;
    for (const auto& file : source.source->Files()) {
      total_paths += file.path.size();
      longest = std::max(longest, file.path.size());
    }
    for (const auto cap :
         {Cap::kWire, Cap::kChunks, Cap::kFiles, Cap::kBindings,
          Cap::kFileBytes, Cap::kTotalBytes, Cap::kPath, Cap::kTotalPaths}) {
      for (const bool reduce : {false, true}) {
        auto limits = Limits();
        const auto minus = reduce ? 1 : 0;
        switch (cap) {
          case Cap::kWire:
            limits.container.max_container_bytes = wire.size() - minus;
            break;
          case Cap::kChunks:
            limits.container.max_chunks = fixture::kChunkCount - minus;
            break;
          case Cap::kFiles:
            limits.files.max_files = source.source->Files().size() - minus;
            break;
          case Cap::kBindings:
            limits.files.max_resource_bindings =
                fixture::kResourceBindings - minus;
            break;
          case Cap::kFileBytes:
            limits.files.max_file_bytes = wire.size() - minus;
            break;
          case Cap::kTotalBytes:
            limits.files.max_total_file_bytes = total - minus;
            break;
          case Cap::kPath:
            limits.files.max_path_bytes = longest - minus;
            break;
          case Cap::kTotalPaths:
            limits.files.max_total_path_bytes = total_paths - minus;
            break;
        }
        Witnesses witness(source.context);
        {
          const auto result =
              Publish(source, source.Destination(), witness, {}, true, limits);
          if (reduce)
            Require(!result.bundle &&
                        result.diagnostic.status ==
                            ic::GltfFileStatus::kLimitExceeded &&
                        result.diagnostic.code ==
                            ic::GltfFileDiagnosticCode::kCountLimit &&
                        result.cleanup ==
                            ic::GltfFileCleanupOutcome::kNotRequired &&
                        witness.stage_calls == 0,
                    "minus-one literal physical GLB cap fails before staging");
          else
            Success(result);
        }
        witness.Released(foreign);
        if (!reduce) fs::remove_all(source.Destination());
        NoStaging(source);
      }
    }
  }
}
void ContainerLaunchesAndOccupied() {
  Fixture source(true);
  const auto foreign = source.context.ActiveTrackedPayload();
  constexpr std::size_t kSecondLaunch = 2;
  for (const bool reading : {false, true}) {
    for (const bool cancel : {false, true}) {
      Containers container(source.context);
      container.target_reading = reading;
      std::stop_source stop;
      if (cancel) {
        container.cancel = &stop;
        container.cancel_launch = kSecondLaunch;
      } else {
        container.fail_launch = kSecondLaunch;
      }
      Witnesses witness(source.context);
      auto testing = witness.Testing();
      ic::GltfFilePublicationOptions options;
      container.Configure(testing, options);
      bool propagated = false;
      try {
        const auto result = files::TestPublishGltfTopologyBundle(
            {*source.source, *source.inspection}, source.Destination(), options,
            Limits(), Work(false), stop.get_token(), testing);
        Require(
            cancel && !result.bundle &&
                result.outcome == ic::GltfFilePublicationOutcome::kCanceled &&
                result.diagnostic.code ==
                    ic::GltfFileDiagnosticCode::kDelegatedContainer &&
                result.diagnostic.container_status == ic::GlbStatus::kCanceled,
            "actual partial container launch cancellation retains delegated "
            "status");
      } catch (const LaunchFailure&) {
        propagated = true;
      }
      Require(
          propagated == !cancel &&
              (reading ? container.read_launches
                       : container.assembly_launches) >= kSecondLaunch,
          "second launch fault is reached after an actual first native launch");
      container.Released();
      witness.Released(foreign);
      CleanupRemoved(witness);
      Require(
          (witness.stage_calls > 0) == reading,
          "assembly failures precede staging; staged-read failures roll back");
      NoStaging(source);
    }
  }
  auto occupied = geo::TryReserveWorkers(source.context, kWorkerCount);
  Require(occupied.Count() == kWorkerCount,
          "actual shared worker slots occupied");
  Containers container(source.context);
  Witnesses witness(source.context);
  auto testing = witness.Testing();
  ic::GltfFilePublicationOptions options;
  container.Configure(testing, options);
  {
    const auto result = files::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        Limits(), Work(false), {}, testing);
    Success(result);
    Require(container.assembly_launches == 0 && container.read_launches == 0 &&
                std::ranges::equal(result.bundle->EntryBytes(),
                                   source.ExpectedWire()),
            "both container scopes use caller fallback with identical literal "
            "wire");
  }
  container.Released();
  occupied.Reset();
  witness.Released(foreign);
}
enum class DelegateScope { kReload, kExtraction, kInspection };
std::string_view DelegateName(DelegateScope scope) {
  switch (scope) {
    case DelegateScope::kReload:
      return "reload";
    case DelegateScope::kExtraction:
      return "extraction";
    case DelegateScope::kInspection:
      return "inspection";
  }
  throw std::runtime_error("known nested cap scope");
}
void DelegateLimit(const ic::GltfFileDiagnostic& diagnostic,
                   DelegateScope scope, bool requested,
                   const ic::GltfDiagnostic* owning_extraction = nullptr) {
  constexpr int kMissingDiagnostic = -1;
  const auto actual =
      " outer=" + std::to_string(static_cast<int>(diagnostic.status)) + "/" +
      std::to_string(static_cast<int>(diagnostic.stage)) + "/" +
      std::to_string(static_cast<int>(diagnostic.code)) + " canonical=" +
      std::to_string(diagnostic.gltf_diagnostic
                         ? static_cast<int>(diagnostic.gltf_diagnostic->status)
                         : kMissingDiagnostic) +
      "/" +
      std::to_string(diagnostic.gltf_diagnostic
                         ? static_cast<int>(diagnostic.gltf_diagnostic->code)
                         : kMissingDiagnostic) +
      " inspection=" +
      std::to_string(diagnostic.inspection_status
                         ? static_cast<int>(*diagnostic.inspection_status)
                         : kMissingDiagnostic) +
      "/" +
      std::to_string(
          diagnostic.inspection_diagnostic
              ? static_cast<int>(diagnostic.inspection_diagnostic->code)
              : kMissingDiagnostic);
  if (scope == DelegateScope::kInspection) {
    Require(
        diagnostic.status == ic::GltfFileStatus::kLimitExceeded &&
            diagnostic.stage == ic::GltfFileStage::kInspection &&
            diagnostic.code ==
                ic::GltfFileDiagnosticCode::kDelegatedInspection &&
            diagnostic.inspection_status ==
                ic::GltfTopologyOutputInspectionStatus::kLimitExceeded &&
            diagnostic.inspection_diagnostic &&
            diagnostic.inspection_diagnostic->code ==
                (requested ? ic::GltfTopologyOutputInspectionDiagnosticCode::
                                 kInspectionRequestLimit
                           : ic::GltfTopologyOutputInspectionDiagnosticCode::
                                 kCountLimit),
        "actual nested inspection ceiling retains its typed first refusal" +
            actual);
  } else {
    Require(
        diagnostic.status == ic::GltfFileStatus::kUnsupportedProfile &&
            diagnostic.stage == (scope == DelegateScope::kReload
                                     ? ic::GltfFileStage::kAssetRead
                                     : ic::GltfFileStage::kExtraction) &&
            diagnostic.code ==
                (scope == DelegateScope::kReload
                     ? ic::GltfFileDiagnosticCode::kDelegatedRead
                     : ic::GltfFileDiagnosticCode::kDelegatedExtraction) &&
            diagnostic.gltf_diagnostic &&
            diagnostic.gltf_diagnostic->status ==
                ic::GltfStatus::kUnsupportedProfile &&
            diagnostic.gltf_diagnostic->code ==
                (owning_extraction ? owning_extraction->code
                                   : ic::GltfDiagnosticCode::kLimitExceeded) &&
            (!owning_extraction ||
             (diagnostic.gltf_diagnostic->rule_id ==
                  owning_extraction->rule_id &&
              diagnostic.gltf_diagnostic->resource_kind ==
                  owning_extraction->resource_kind &&
              diagnostic.gltf_diagnostic->resource_index ==
                  owning_extraction->resource_index)),
        "actual nested canonical ceiling retains delegated status and code" +
            actual);
  }
}
std::uint64_t& DelegateAllowance(ic::GltfFilePublicationLimits& limits,
                                 DelegateScope scope) {
  switch (scope) {
    case DelegateScope::kReload:
      return limits.reload.max_new_requested_bytes;
    case DelegateScope::kExtraction:
      return limits.extraction.max_output_requested_bytes;
    case DelegateScope::kInspection:
      return limits.inspection.max_new_requested_bytes;
  }
  throw std::runtime_error("known nested allowance scope");
}
bool DelegateProbe(const Fixture& source, DelegateScope scope, bool serial,
                   const ic::GltfFilePublicationLimits& limits,
                   bool requested) {
  const auto foreign = source.context.ActiveTrackedPayload();
  Witnesses witness(source.context);
  bool accepted = false;
  {
    const auto result =
        Publish(source, source.Destination(), witness, {}, serial, limits);
    accepted = result.bundle.has_value();
    if (accepted) {
      Success(result);
      Require(result.bundle->Format() == ic::GltfFileFormat::kGlb &&
                  result.bundle->Container() &&
                  std::ranges::equal(result.bundle->EntryBytes(),
                                     source.ExpectedWire()),
              "nested inclusive cap admits actual independently framed GLB "
              "publication");
    } else {
      std::optional<ic::GltfDiagnostic> owning_extraction;
      if (scope == DelegateScope::kExtraction && requested) {
        // Distinct owning refusal sites may classify a size ceiling as either
        // LimitExceeded or SizeOverflow. The wrapper must retain the real
        // public extraction's first diagnostic, rather than normalize it.
        const auto& asset = source.inspection->ReloadedAsset();
        const auto direct = ic::ExtractGltfPrimitiveCapped(
            asset, *asset.Primitive(0),
            {ic::GltfTriangleRequest::kRequireTriangles,
             ic::GltfValueRequest::kRawAndNormalized},
            limits.extraction);
        Require(!direct.extraction && direct.diagnostic.status ==
                                          ic::GltfStatus::kUnsupportedProfile,
                "independent owning public extraction refuses the same fresh "
                "allowance");
        owning_extraction = direct.diagnostic;
      }
      DelegateLimit(result.diagnostic, scope, requested,
                    owning_extraction ? &*owning_extraction : nullptr);
      Require(
          result.cleanup == ic::GltfFileCleanupOutcome::kRemoved &&
              witness.stage_calls == 1 && witness.commit_calls == 0,
          "nested delegate refusal removes staging before native publication");
      CleanupRemoved(witness);
    }
  }
  witness.Released(foreign);
  if (accepted) fs::remove_all(source.Destination());
  NoStaging(source);
  return accepted;
}
void DelegatedGlbCaps() {
  constexpr std::array<DelegateScope, 3> kScopes{DelegateScope::kReload,
                                                 DelegateScope::kExtraction,
                                                 DelegateScope::kInspection};
  for (const bool changed : {false, true}) {
    Fixture source(changed);
    for (const bool serial : {true, false}) {
      for (const auto scope : kScopes) {
        const auto start = std::chrono::steady_clock::now();
        std::cout << "GLB nested cap start " << DelegateName(scope)
                  << " changed=" << changed << " serial=" << serial
                  << std::endl;
        auto limits = Limits();
        std::uint64_t lower = 1;
        std::uint64_t upper = DelegateAllowance(limits, scope);
        Require(DelegateProbe(source, scope, serial, limits, true),
                "declared ample nested allowance is actually accepted");
        // Public results locate the first successful per-operation allowance.
        // No writer layout, allocation-size formula or private delegate hook is
        // used as the expected request oracle. Both adjacent limits are rerun.
        while (lower < upper) {
          const auto middle = lower + (upper - lower) / 2;
          DelegateAllowance(limits, scope) = middle;
          if (DelegateProbe(source, scope, serial, limits, true))
            upper = middle;
          else
            lower = middle + 1;
        }
        Require(
            upper > 1,
            "real delegated GLB operation has a nontrivial fresh allowance");
        DelegateAllowance(limits, scope) = upper;
        Require(DelegateProbe(source, scope, serial, limits, true),
                "inclusive fresh nested allowance is accepted");
        DelegateAllowance(limits, scope) = upper - 1;
        Require(!DelegateProbe(source, scope, serial, limits, true),
                "minus-one fresh nested allowance refuses independently of "
                "retained owners");
        for (const bool reduce : {false, true}) {
          limits = Limits();
          const auto minus = reduce ? 1 : 0;
          if (scope == DelegateScope::kReload)
            limits.reload.source.max_json_bytes =
                source.inspection->Output().Json().size() - minus;
          else if (scope == DelegateScope::kExtraction)
            limits.extraction.max_triangle_corners =
                source.selected_triangles.size() - minus;
          else
            limits.inspection.max_candidate_corners =
                source.selected_triangles.size() - minus;
          Require(
              DelegateProbe(source, scope, serial, limits, false) == !reduce,
              "inclusive/minus-one literal prefix or candidate corner "
              "inventory");
        }
        std::cout << "GLB nested cap complete " << DelegateName(scope)
                  << " changed=" << changed << " serial=" << serial
                  << " inclusive_requested_bytes=" << upper << " elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count()
                  << std::endl;
      }
    }
  }
}
}  // namespace
int main() {
  try {
    RunPhase("ContainerOriginalRequests", ContainerOriginalRequests);
    RunPhase("PhysicalGlbCaps", PhysicalGlbCaps);
    RunPhase("ContainerLaunchesAndOccupied", ContainerLaunchesAndOccupied);
    RunPhase("DelegatedGlbCaps", DelegatedGlbCaps);
    RunPhase("OriginalRequests", OriginalRequests);
    RunPhase("FreshCapsAndLifetime", FreshCapsAndLifetime);
    RunPhase("BorrowedNativeFailuresAndStops", BorrowedNativeFailuresAndStops);
    RunPhase("ProducerExceptionsAndCleanup", ProducerExceptionsAndCleanup);
    RunPhase("OccupiedWorkers", OccupiedWorkers);
    RunPhase("ConcurrentRequests", ConcurrentRequests);
    std::cout << "borrowed provider original requests, native roles and "
                 "retained cleanup passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "unexpected propagated test exception\n";
    return 1;
  }
}
