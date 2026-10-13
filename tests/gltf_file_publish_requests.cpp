// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
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
constexpr std::uint64_t kPhysicalBytes = 1024 * 1024;
constexpr std::uint64_t kRequestBytes = 64 * kPhysicalBytes;
constexpr std::uint64_t kCountLimit = 4096;
constexpr std::uint64_t kChannelLimit = 64;
constexpr std::uint64_t kDepthLimit = 64;
constexpr std::uint64_t kPartitionBytes = 4096;
constexpr std::uint64_t kCheckpointBytes = 64;
constexpr std::uint64_t kJobLimit = 64;
constexpr std::size_t kWorkerCount = 4;
constexpr std::size_t kOpaqueBytes = 65536;
constexpr std::size_t kBlockCapacity = 4096;
constexpr std::size_t kNativeCapacity = 1024;
constexpr std::size_t kSourceBinaryBytes = 56;
constexpr std::chrono::seconds kEntryWait{30};
constexpr std::string_view kEntry = "scene.gltf";
constexpr std::string_view kStaticPath = "receipts/static.bin";
constexpr std::string_view kDynamicPath = "receipts/dynamic.bin";
constexpr std::string_view kStaticBytes = "authored static supplement\n";
constexpr std::string_view kDynamicBytes = "authored dynamic supplement\n";
constexpr std::array<std::string_view, 2> kOpaquePaths{"opaque-a.bin",
                                                       "opaque-b.bin"};
constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"a.bin","byteLength":48}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"extras":{"spelling":1.2300e+04}})";
constexpr std::array<std::byte, kSourceBinaryBytes> kBinary{
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0x80},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0x80}, std::byte{0x3f},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0x80}, std::byte{0x3f},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{1},    std::byte{0},
    std::byte{2},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{1},    std::byte{0},    std::byte{2},    std::byte{0},
    std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef},
    std::byte{0xca}, std::byte{0xfe}, std::byte{0xba}, std::byte{0xbe}};
constexpr std::array<std::uint32_t, 6> kSourceTriangles{0, 1, 2, 0, 1, 2};
constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
constexpr std::array<geo::index_t, 1> kChangedFaces{0};
constexpr std::array<geo::index_t, 3> kChangedCorners{0, 1, 2};
constexpr std::array<std::uint32_t, 3> kChangedTriangles{0, 1, 2};

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
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(output.is_open(), "literal fixture output opened");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  Require(!output.fail(), "literal fixture output complete");
}
std::vector<std::byte> ReadBytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  Require(input.is_open(), "independent finalized file reopened");
  const std::string text{std::istreambuf_iterator<char>(input), {}};
  return {Bytes(text).begin(), Bytes(text).end()};
}
std::uint64_t Threshold(bool serial) {
  return serial ? static_cast<std::uint64_t>(
                      std::numeric_limits<std::ptrdiff_t>::max())
                : 1;
}
ic::GltfReadOperationLimits ReadLimits() {
  return {{kPhysicalBytes, kPhysicalBytes, kPhysicalBytes, kCountLimit,
           kDepthLimit, kPhysicalBytes, kCountLimit, kCountLimit, kCountLimit,
           kCountLimit, kCountLimit, kCountLimit, kCountLimit, kRequestBytes},
          kRequestBytes};
}
ic::GltfFileLimits FileLimits() {
  return {kCountLimit,    kCountLimit,    kPhysicalBytes, kPhysicalBytes,
          kPhysicalBytes, kPhysicalBytes, kPhysicalBytes, kRequestBytes};
}
ic::GltfWorkPolicy ReadWork(bool serial = false) {
  return {Threshold(serial), kPartitionBytes, Threshold(serial),
          kPartitionBytes};
}
geo::MeshMaterializationWorkPolicy GeometryWork() {
  return {1, kPartitionBytes, kCheckpointBytes, kJobLimit};
}
ic::GltfMaterializationLimits TransferLimits() {
  return {kChannelLimit,
          kRequestBytes,
          {kCountLimit, kCountLimit, kCountLimit, kCountLimit, kChannelLimit,
           kCountLimit, kCountLimit, kCountLimit, kCountLimit, kPhysicalBytes,
           kRequestBytes},
          {kCountLimit, kRequestBytes}};
}
ic::GltfTopologyOutputInspectionLimits InspectionLimits() {
  return {kPhysicalBytes, kCountLimit, kDepthLimit,   kCountLimit,
          kPhysicalBytes, kCountLimit, kChannelLimit, kCountLimit,
          kPhysicalBytes, kCountLimit, kCountLimit,   kPhysicalBytes,
          kRequestBytes};
}
ic::GltfFilePublicationLimits Limits() {
  return {FileLimits(),
          ReadLimits(),
          {kCountLimit, kCountLimit, kRequestBytes},
          InspectionLimits(),
          {kPhysicalBytes, kCountLimit, kRequestBytes}};
}
ic::GltfFilePublicationWorkPolicy Work(bool serial = false) {
  const auto threshold = Threshold(serial);
  return {{kPartitionBytes, threshold, kPartitionBytes, kCheckpointBytes,
           kJobLimit},
          ReadWork(serial),
          {threshold, kPartitionBytes, kCheckpointBytes, kJobLimit},
          {threshold, kPartitionBytes}};
}
struct RootRemoval {
  const fs::path& root;
  ~RootRemoval() noexcept {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};
struct Fixture {
  fs::path root;
  RootRemoval cleanup{root};
  geo::ExecutionContext context;
  std::vector<std::byte> opaque{kOpaqueBytes, std::byte{0xa7}};
  std::optional<ic::GltfFileAsset> source;
  std::optional<ic::GltfTopologyOutputInspection> inspection;
  explicit Fixture(bool changed, std::uint64_t budget = kRequestBytes)
      : context({kWorkerCount, 1, budget}) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-publish-requests-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    Require(fs::create_directory(root), "isolated publication parent");
    const auto input = root / "input";
    Require(fs::create_directory(input), "isolated source root");
    Write(input / kEntry, Bytes(kJson));
    Write(input / "a.bin", kBinary);
    for (const auto path : kOpaquePaths) Write(input / path, opaque);
    auto read = ic::ReadGltfFileAsset(
        input / kEntry,
        {{},
         ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
         kOpaquePaths},
        {FileLimits(),
         ReadLimits(),
         {kPhysicalBytes, kCountLimit, kRequestBytes}},
        {Work(true).files, ReadWork(true), Work(true).container}, context);
    Require(read.asset.has_value(), "real source files admitted");
    source = *read.asset;
    auto extracted = ic::ExtractGltfPrimitiveCapped(
        source->Asset(), *source->Asset().Primitive(0),
        {ic::GltfTriangleRequest::kRequireTriangles,
         ic::GltfValueRequest::kRaw},
        {kCountLimit, kCountLimit, kRequestBytes});
    Require(extracted.extraction &&
                std::ranges::equal(extracted.extraction->TriangleIndices(),
                                   kSourceTriangles),
            "literal source topology before writer invocation");
    auto materialized = ic::MaterializeGltfPrimitive(
        *extracted.extraction, TransferLimits(), GeometryWork());
    Require(materialized.primitive.has_value(),
            "actual qualified nonempty materialization");
    const auto faces = changed ? std::span<const geo::index_t>(kChangedFaces)
                               : std::span<const geo::index_t>(kIdentityFaces);
    const auto corners = changed
                             ? std::span<const geo::index_t>(kChangedCorners)
                             : std::span<const geo::index_t>(kIdentityCorners);
    auto selection =
        geo::SelectMeshFaces(materialized.primitive->Mesh(), faces,
                             TransferLimits().materialization, GeometryWork());
    Require(selection.selection.has_value(), "actual selected candidate");
    auto candidate = geo::InspectMaterializedMesh(
        selection.selection->Candidate(), TransferLimits().inspection,
        GeometryWork());
    Require(candidate.inspection && candidate.inspection->IsStorageValid(),
            "actual candidate storage inspection");
    const ic::GltfTopologyWriteLimits writer_limits{
        kPhysicalBytes,
        kCountLimit,
        kDepthLimit,
        kCountLimit,
        kChannelLimit,
        kCountLimit,
        kPhysicalBytes,
        kCountLimit,
        kCountLimit,
        kPhysicalBytes,
        {kPhysicalBytes, kCountLimit, kPhysicalBytes, kPhysicalBytes,
         kRequestBytes}};
    auto written = ic::WriteGltfTopologySelection(
        {*materialized.primitive, *candidate.inspection, faces, corners},
        writer_limits, {1, kPartitionBytes, kCheckpointBytes, kJobLimit});
    Require(written.bundle.has_value(),
            "writer produces publication input only");
    std::vector<ic::SuppliedGltfResource> bindings;
    for (const auto& resource : written.bundle->Resources())
      if (resource.origin == ic::GltfResourceOrigin::kCallerBinding)
        bindings.push_back(
            {resource.kind, resource.table_index, resource.bytes});
    auto reloaded =
        ic::ReadGltfAssetCapped(written.bundle->Json(), bindings, ReadLimits(),
                                ReadWork(true), context);
    Require(reloaded.asset.has_value(), "actual in-memory output reload");
    auto output_extraction = ic::ExtractGltfPrimitiveCapped(
        *reloaded.asset, *reloaded.asset->Primitive(0),
        {ic::GltfTriangleRequest::kRequireTriangles,
         ic::GltfValueRequest::kRaw},
        {kCountLimit, kCountLimit, kRequestBytes});
    Require(output_extraction.extraction &&
                std::ranges::equal(
                    output_extraction.extraction->TriangleIndices(),
                    changed ? std::span<const std::uint32_t>(kChangedTriangles)
                            : std::span<const std::uint32_t>(kSourceTriangles)),
            "literal output topology independently decoded");
    auto verified = ic::InspectGltfTopologyOutput(
        {*materialized.primitive, *candidate.inspection, faces, corners,
         *written.bundle, *reloaded.asset, *output_extraction.extraction},
        InspectionLimits(), {1, kPartitionBytes, kCheckpointBytes, kJobLimit});
    Require(verified.inspection.has_value(),
            "actual independent output inspection");
    inspection = *verified.inspection;
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ~Fixture() noexcept = default;
  fs::path Destination(std::string_view name = "published-a") const {
    return root / name;
  }
};
void Success(const ic::GltfFilePublicationResult& result) {
  Require(
      result.bundle &&
          result.outcome == ic::GltfFilePublicationOutcome::kPublished &&
          result.diagnostic.status == ic::GltfFileStatus::kSuccess,
      "actual published owner expected: status=" +
          std::to_string(static_cast<int>(result.diagnostic.status)) +
          " code=" + std::to_string(static_cast<int>(result.diagnostic.code)) +
          " stage=" +
          std::to_string(static_cast<int>(result.diagnostic.stage)));
}
void Check(const ic::GltfPublishedBundle& bundle, const Fixture& fixture,
           const fs::path& destination, bool dynamic) {
  const std::array<std::string_view, 6> paths{"a.bin",        "opaque-a.bin",
                                              "opaque-b.bin", kDynamicPath,
                                              kStaticPath,    kEntry};
  std::vector<std::string> expected;
  for (const auto path : paths)
    if (dynamic || path != kDynamicPath) expected.emplace_back(path);
  std::sort(expected.begin(), expected.end());
  std::vector<std::string> actual;
  for (const auto& item : fs::recursive_directory_iterator(destination))
    if (!item.is_directory()) {
      Require(item.is_regular_file() && !item.is_symlink(),
              "finalized ordinary inventory");
      actual.push_back(
          item.path().lexically_relative(destination).generic_string());
    }
  std::sort(actual.begin(), actual.end());
  Require(actual == expected && bundle.Files().size() == expected.size(),
          "independent final physical inventory has no index sidecar");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const auto& name = expected[i];
    const auto bytes = name == "a.bin"  ? std::span<const std::byte>(kBinary)
                       : name == kEntry ? fixture.inspection->Output().Json()
                       : name == kStaticPath ? Bytes(kStaticBytes)
                       : name == kDynamicPath
                           ? Bytes(kDynamicBytes)
                           : std::span<const std::byte>(fixture.opaque);
    Require(
        bundle.Files()[i].path == name &&
            std::ranges::equal(bundle.Files()[i].bytes, bytes) &&
            std::ranges::equal(ReadBytes(destination / name), bytes),
        "actual finalized bytes and retained readback match independent input");
  }
  Require(bundle.Format() == ic::GltfFileFormat::kGltfJson &&
              !bundle.Container() && bundle.EntryPath() == kEntry &&
              std::ranges::equal(bundle.EntryBytes(),
                                 fixture.inspection->Output().Json()),
          "exact publication input transferred, without writer correctness "
          "inference");
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
struct Supplements {
  std::array<ic::GltfFileView, 1> initial{{{kStaticPath, Bytes(kStaticBytes)}}};
  bool dynamic = true;
  bool throw_callback = false;
  bool allocation_callback = false;
  bool copy_inspection = false;
  std::size_t verified_calls = 0;
  std::optional<ic::GltfTopologyOutputInspection> retained;
  ic::GltfFilePublicationOptions Options() {
    ic::GltfFilePublicationOptions options;
    options.supplemental_files = initial;
    if (dynamic || throw_callback || allocation_callback) {
      options.on_verified = [this](const ic::GltfFileVerifiedContent& content) {
        ++verified_calls;
        Require(content.format == ic::GltfFileFormat::kGltfJson &&
                    !content.container,
                "callback receives actual JSON verification without container");
        if (allocation_callback) throw std::bad_alloc();
        if (throw_callback) throw HookFailure{};
        if (copy_inspection) retained.emplace(content.inspection);
        const auto bytes = Bytes(kDynamicBytes);
        return std::vector<ic::GltfFileSupplement>{
            {std::string(kDynamicPath), {bytes.begin(), bytes.end()}}};
      };
    }
    return options;
  }
};
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
  for (const bool changed : {false, true}) {
    Fixture fixture(changed);
    const auto foreign = fixture.context.ActiveTrackedPayload();
    for (const bool serial : {true, false}) {
      for (const bool dynamic : {false, true}) {
        Witnesses baseline(fixture.context);
        Supplements baseline_supplements;
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
          Witnesses witness(fixture.context);
          Supplements supplements;
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
                  << (serial ? " serial\n" : " native\n");
      }
    }
  }
}
void FreshCapsAndLifetime() {
  Fixture fixture(true);
  const auto foreign = fixture.context.ActiveTrackedPayload();
  for (const bool serial : {true, false}) {
    Witnesses baseline(fixture.context);
    Supplements initial;
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
      Supplements supplements;
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
  Supplements supplements;
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
  fs::remove_all(fixture.root / "input");
  Check(*retained, fixture, fixture.Destination(), true);
  const auto source_bytes = fixture.source->NewRequestedBytes();
  fixture.source.reset();
  fixture.inspection.reset();
  Require(source_bytes > 0 &&
              std::ranges::equal(retained->Source().Asset().OriginalJson(),
                                 Bytes(kJson)),
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
void NativeFailuresAndStops() {
  Fixture fixture(true);
  const auto foreign = fixture.context.ActiveTrackedPayload();
  {
    Witnesses witness(fixture.context);
    Supplements supplements;
    std::stop_source stop;
    witness.requests.cancel = &stop;
    const auto result =
        Publish(fixture, fixture.Destination(), witness, supplements.Options(),
                false, Limits(), stop.get_token());
    Require(
        !result.bundle &&
            result.diagnostic.status == ic::GltfFileStatus::kCanceled &&
            witness.requests.calls > 0,
        "stop after an actual original allocation releases the admitted block");
    witness.Released(foreign);
    CleanupRemoved(witness);
    NoStaging(fixture);
  }
  enum class Role { kWrite, kImport, kCapture, kVerification };
  for (const auto role :
       {Role::kWrite, Role::kImport, Role::kCapture, Role::kVerification}) {
    Witnesses witness(fixture.context);
    auto& worker = role == Role::kWrite     ? witness.write
                   : role == Role::kImport  ? witness.import
                   : role == Role::kCapture ? witness.capture
                                            : witness.verification;
    worker.fail_at = 2;
    Supplements supplements;
    bool propagated = false;
    try {
      const auto result = Publish(fixture, fixture.Destination(), witness,
                                  supplements.Options());
      Require(!result.bundle,
              "partial native launch returns no published owner");
    } catch (const LaunchFailure&) {
      propagated = true;
    }
    Require(propagated && worker.launched == 1 && worker.entered == 1 &&
                worker.joined == 1,
            "actual first native entry/handle joined after second launch fault "
            "in owning role");
    witness.Released(foreign);
    CleanupRemoved(witness);
    NoStaging(fixture);
  }
  for (const auto role :
       {Role::kWrite, Role::kImport, Role::kCapture, Role::kVerification}) {
    Witnesses witness(fixture.context);
    auto& worker = role == Role::kWrite     ? witness.write
                   : role == Role::kImport  ? witness.import
                   : role == Role::kCapture ? witness.capture
                                            : witness.verification;
    std::stop_source stop;
    worker.cancel = &stop;
    Supplements supplements;
    const auto result =
        Publish(fixture, fixture.Destination(), witness, supplements.Options(),
                false, Limits(), stop.get_token());
    Require(!result.bundle &&
                result.outcome == ic::GltfFilePublicationOutcome::kCanceled &&
                result.diagnostic.status == ic::GltfFileStatus::kCanceled &&
                worker.entered > 0,
            "fresh cancellation from actual native entry in owning byte role");
    witness.Released(foreign);
    CleanupRemoved(witness);
    NoStaging(fixture);
  }
  for (const auto phase : {ic::GltfFilePublicationPhase::kPreflight,
                           ic::GltfFilePublicationPhase::kStaging,
                           ic::GltfFilePublicationPhase::kVerification,
                           ic::GltfFilePublicationPhase::kPublication}) {
    Witnesses witness(fixture.context);
    Supplements supplements;
    auto options = supplements.Options();
    std::stop_source stop;
    options.on_phase = [&](ic::GltfFilePublicationPhase observed) {
      if (observed == phase) stop.request_stop();
    };
    const auto result = Publish(fixture, fixture.Destination(), witness,
                                options, false, Limits(), stop.get_token());
    Require(!result.bundle &&
                result.outcome == ic::GltfFilePublicationOutcome::kCanceled &&
                result.phase == phase,
            "stop from synchronous phase callback precedes its work");
    witness.Released(foreign);
    CleanupRemoved(witness);
    NoStaging(fixture);
  }
}
void DelegatedAllowances() {
  Fixture fixture(true);
  const auto foreign = fixture.context.ActiveTrackedPayload();
  enum class Owner { kReload, kExtraction, kInspection };
  for (const auto owner :
       {Owner::kReload, Owner::kExtraction, Owner::kInspection}) {
    auto limits = Limits();
    if (owner == Owner::kReload) limits.reload.max_new_requested_bytes = 1;
    if (owner == Owner::kExtraction)
      limits.extraction.max_output_requested_bytes = 1;
    if (owner == Owner::kInspection)
      limits.inspection.max_new_requested_bytes = 1;
    Witnesses witness(fixture.context);
    Supplements supplements;
    const auto result = Publish(fixture, fixture.Destination(), witness,
                                supplements.Options(), false, limits);
    const auto expected =
        owner == Owner::kReload ? ic::GltfFileDiagnosticCode::kDelegatedRead
        : owner == Owner::kExtraction
            ? ic::GltfFileDiagnosticCode::kDelegatedExtraction
            : ic::GltfFileDiagnosticCode::kDelegatedInspection;
    const auto expected_status = owner == Owner::kInspection
                                     ? ic::GltfFileStatus::kLimitExceeded
                                     : ic::GltfFileStatus::kUnsupportedProfile;
    const auto expected_stage =
        owner == Owner::kReload       ? ic::GltfFileStage::kAssetRead
        : owner == Owner::kExtraction ? ic::GltfFileStage::kExtraction
                                      : ic::GltfFileStage::kInspection;
    Require(
        !result.bundle && result.diagnostic.status == expected_status &&
            result.diagnostic.stage == expected_stage &&
            result.diagnostic.code == expected &&
            (owner == Owner::kInspection
                 ? result.diagnostic.inspection_status ==
                       ic::GltfTopologyOutputInspectionStatus::kLimitExceeded
                 : result.diagnostic.gltf_diagnostic &&
                       result.diagnostic.gltf_diagnostic->status ==
                           ic::GltfStatus::kUnsupportedProfile &&
                       result.diagnostic.gltf_diagnostic->code ==
                           ic::GltfDiagnosticCode::kLimitExceeded),
        "distinct delegated allowance keeps its actual owner diagnostic: "
        "owner=" +
            std::to_string(static_cast<int>(owner)) + " status=" +
            std::to_string(static_cast<int>(result.diagnostic.status)) +
            " code=" +
            std::to_string(static_cast<int>(result.diagnostic.code)) +
            " gltf-status=" +
            (result.diagnostic.gltf_diagnostic
                 ? std::to_string(static_cast<int>(
                       result.diagnostic.gltf_diagnostic->status))
                 : "none"));
    Require(witness.requests.peak < limits.files.max_new_requested_bytes &&
                supplements.verified_calls == 0,
            "delegated refusal precedes verified callback without relabeling "
            "the file cap");
    witness.Released(foreign);
    CleanupRemoved(witness);
    NoStaging(fixture);
  }
}
void ExpectedCleanupAndRuntimeExceptions() {
  Fixture fixture(true);
  const auto foreign = fixture.context.ActiveTrackedPayload();
  for (const bool prevent_cleanup : {false, true}) {
    Witnesses witness(fixture.context);
    if (prevent_cleanup)
      witness.refuse_cleanup =
          std::make_error_code(std::errc::permission_denied);
    Supplements supplements;
    supplements.throw_callback = true;
    const auto result =
        Publish(fixture, fixture.Destination(), witness, supplements.Options());
    Require(
        !result.bundle &&
            result.outcome == ic::GltfFilePublicationOutcome::kFailed &&
            result.diagnostic.status == ic::GltfFileStatus::kCallbackFailure &&
            result.diagnostic.code ==
                ic::GltfFileDiagnosticCode::kVerifiedCallback,
        "nonallocation verified callback error remains expected primary "
        "failure");
    witness.Released(foreign);
    if (prevent_cleanup) {
      Require(result.cleanup == ic::GltfFileCleanupOutcome::kFailed &&
                  result.cleanup_error == witness.refuse_cleanup &&
                  witness.cleanup == ic::GltfFileCleanupOutcome::kFailed &&
                  fs::is_directory(witness.stage),
              "cleanup refusal retains actual staging root and reports failure "
              "honestly");
      fs::remove_all(witness.stage);
    } else {
      Require(result.cleanup == ic::GltfFileCleanupOutcome::kRemoved,
              "expected failure reports actual root removal");
      CleanupRemoved(witness);
    }
    NoStaging(fixture);
  }
  enum class Failure { kWrite, kInventory, kCommit, kCallbackAllocation };
  for (const auto failure : {Failure::kWrite, Failure::kInventory,
                             Failure::kCommit, Failure::kCallbackAllocation}) {
    Witnesses witness(fixture.context);
    witness.throw_write = failure == Failure::kWrite;
    witness.throw_inventory = failure == Failure::kInventory;
    witness.throw_commit = failure == Failure::kCommit;
    Supplements supplements;
    supplements.allocation_callback = failure == Failure::kCallbackAllocation;
    bool propagated = false;
    try {
      const auto result = Publish(fixture, fixture.Destination(), witness,
                                  supplements.Options());
      Require(!result.bundle, "runtime fault exposes no owner");
    } catch (const HookFailure&) {
      propagated = failure != Failure::kCallbackAllocation;
    } catch (const std::bad_alloc&) {
      propagated = failure == Failure::kCallbackAllocation;
    }
    Require(propagated,
            "actual native-hook/allocation exception identity preserved after "
            "joins");
    witness.Released(foreign);
    CleanupRemoved(witness);
    NoStaging(fixture);
  }
}
void OccupiedWorkers() {
  Fixture fixture(true);
  const auto foreign = fixture.context.ActiveTrackedPayload();
  auto occupied = geo::TryReserveWorkers(fixture.context, kWorkerCount);
  Require(occupied.Count() == kWorkerCount,
          "all authoritative worker slots occupied");
  Witnesses witness(fixture.context);
  Supplements supplements;
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
    Supplements supplements;
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
    Supplements first_supplements;
    Gate gate;
    first.write.gate = &gate;
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
    Require(gate.Wait(),
            "first publisher reached four actual native write entries");
    const auto held = fixture.context.ActiveTrackedPayload();
    Require(
        held > foreign && fixture.context.ActiveWorkers() == kWorkerCount,
        "paused actual writes retain their own requests and all worker slots");
    auto unrelated = geo::TryReservePayload(
        fixture.context,
        tight ? static_cast<std::size_t>(kRequestBytes - held - 1) : 0);
    Require(unrelated.status == geo::ReservationStatus::kAccepted,
            "explicit unrelated common-ledger reservation admitted");
    Witnesses second(fixture.context);
    Supplements second_supplements;
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
}  // namespace

int main() {
  try {
    OriginalRequests();
    FreshCapsAndLifetime();
    NativeFailuresAndStops();
    DelegatedAllowances();
    ExpectedCleanupAndRuntimeExceptions();
    OccupiedWorkers();
    ConcurrentRequests();
    std::cout << "JSON publication original requests, native roles and cleanup "
                 "passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
