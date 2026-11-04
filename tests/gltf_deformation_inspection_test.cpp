// SPDX-License-Identifier: Apache-2.0
#include "gltf_deformation_inspection_test.h"

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
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <latch>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_files_test_internal.h"
#include "gltf_operation_workers.h"
#include "gltf_workflow_support_test_internal.h"

namespace {
namespace inspection = meshvale::interchange::gltf_workflow_support_detail;
namespace detail = meshvale::interchange::gltf_detail;
namespace files = meshvale::interchange::gltf_files_detail;
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kRows = 65536;
constexpr std::size_t kNativeWorkers = 4;
constexpr std::uint64_t kPhysicalFiles = 32;
// Independent finite-loop oracles for the literal boundary-minimal document.
// Ordinary admission330 +geometry3 +correspondence16 +lexical297 +resources14
// +reload17 +deformation48 +provenance19 =744. The actual staged helper calls
// geometry3 +correspondence16 +reload17 +deformation48 =84, then verifies32
// physical files (4R each) and inventories32 flat paths (3N+N(N+1)/2) =836.
// Each owning operation has its own limit; nested producers are not summed.
constexpr std::uint64_t kInspectorRelationships = 744;
constexpr std::uint64_t kHelperRelationships = 836;
constexpr std::uint64_t kHelperCoreRelationships = 84;
constexpr std::uint64_t kPhysicalAndResetRelationshipsPerFile = 5;
constexpr std::uint64_t kWorkerCap = 332;
constexpr std::uint64_t kMismatchPrefixCap = 339;
constexpr std::uint64_t kMismatchPrefixRelationships = 340;
constexpr std::size_t kFirstDifferentPositionComponent = 3;
constexpr std::string_view kTemporaryPrefix = "meshvale-inspection-budget-";
constexpr mv::GltfReadOperationLimits kRead{
    {kMiB, kMiB, 4 * kMiB, kRows, 64, kMiB, 1024, 1024, 32, kRows, kRows, kRows,
     kMiB, 4 * kMiB},
    16 * kMiB};
constexpr mv::GltfWorkPolicy kReadWork{64, 64, 64, 64};
constexpr mv::GltfDeformationPolicy kPolicy{
    {kRows, kRows, kRows, kRows, 16, 64, kRows, kRows, kRows, kRows, kRows,
     kMiB, kRows, kRows, 8 * kMiB, 8 * kMiB, 8 * kMiB, 16 * kMiB},
    {64, 64, 64, 64, 64, 64, 64, 16}};
constexpr mv::GltfExtractionLimits kExtraction{kMiB, kMiB, 32 * kMiB};
constexpr mv::GltfMaterializationLimits kMaterialization{
    1024,
    8 * kMiB,
    {kRows, kRows, kRows, kRows, 1024, kMiB, kRows, kRows, kRows, 4 * kMiB,
     16 * kMiB},
    {1024, 4 * kMiB}};
constexpr geo::MeshMaterializationWorkPolicy kTransferWork{1, 64, 64, 64};
constexpr mv::GltfTopologyWriteLimits kWrite{
    kMiB,
    kRows,
    64,
    kRows,
    1024,
    kMiB,
    4 * kMiB,
    kRows,
    kRows,
    kMiB,
    {kMiB, 1024, kMiB, 8 * kMiB, 16 * kMiB}};
constexpr mv::GltfTopologyWriteWorkPolicy kWriteWork{1, 64, 64, 64};
void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace

namespace {
constexpr mv::GltfTopologyOutputInspectionLimits kInspection{
    kMiB, kRows,    64,    1024,  8 * kMiB, kRows,    1024,
    kMiB, 4 * kMiB, kRows, kRows, kMiB,     16 * kMiB};
constexpr mv::GltfTopologyOutputInspectionWorkPolicy kInspectionWork{1, 64, 64,
                                                                     64};
constexpr mv::GltfFileLimits kFileLimits{1024, 1024,  kMiB,  8 * kMiB,
                                         4096, 65536, 65536, 16 * kMiB};
constexpr mv::GltfFileWorkPolicy kFileWork{4096, 1, 4096, 64, 64};
constexpr mv::GltfFileReadLimits kFileRead{
    kFileLimits, kRead, {kMiB, 64, 16 * kMiB}};
constexpr mv::GltfFileReadWorkPolicy kFileReadWork{
    kFileWork, kReadWork, {1, 64}};
constexpr mv::GltfFilePublicationLimits kPublish{
    kFileLimits, kRead, kExtraction, kInspection, {kMiB, 64, 16 * kMiB}};
constexpr mv::GltfFilePublicationWorkPolicy kPublishWork{
    kFileWork, kReadWork, kInspectionWork, {1, 64}};
mv::GltfDeformationPolicy SerialPolicy(std::uint64_t cap) {
  auto policy = kPolicy;
  policy.limits.max_relationship_steps = cap;
  policy.work.max_jobs = 1;
  policy.work.parallel_min_scalars =
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
  policy.work.parallel_min_bytes = policy.work.parallel_min_scalars;
  return policy;
}
struct Witness {
  std::optional<mv::GltfDeformationObservation> facts;
  bool fail = false;
  static std::span<const mv::GltfFileView> Provide(
      const mv::GltfFileVerifiedContent& content, void* state) {
    auto& self = *static_cast<Witness*>(state);
    Require(content.staged_deformation && content.staged_deformation->complete,
            "actual completed staged-helper callback borrow");
    self.facts = *content.staged_deformation;
    if (self.fail) throw std::runtime_error("owned late provider failure");
    return {};
  }
};
struct Entered {
  std::atomic<std::size_t> count{0};
  std::atomic<std::size_t> launched{0}, first_join_launched{0};
  static void Job(std::size_t, void* state) noexcept {
    static_cast<Entered*>(state)->count.fetch_add(1);
  }
  static void Launched(std::size_t, void* state) noexcept {
    static_cast<Entered*>(state)->launched.fetch_add(1);
  }
  static void Joined(std::size_t ordinal, void* state) noexcept {
    auto& self = *static_cast<Entered*>(state);
    if (ordinal == 0) {
      std::size_t empty = 0;
      (void)self.first_join_launched.compare_exchange_strong(
          empty, self.launched.load());
    }
  }
};
enum class WorkCase { kScalarOnly, kByteOnly, kBelowBoth, kMaxJobsOne };
enum class PublicationCase { kComplete, kLimit, kProviderFailure };
constexpr std::size_t kFirstByteRangeJobs = 2;
enum class FaultMode {
  kCap,
  kStop,
  kSecondLaunch,
  kAllocation,
  kMismatch,
  kMismatchCap,
  kMismatchThirdLaunch
};
struct Fault {
  // Jobs2/3 are the trailing ranges which finish before the known mismatch
  // requests stop and releases the held first range.
  static constexpr std::size_t kOtherWorkerBegin = 2;
  static constexpr unsigned kOtherWorkersComplete =
      (1U << (kNativeWorkers - kOtherWorkerBegin)) - 1;
  struct Slot {
    detail::NativeWorkerRecord forwarded{};
    detail::NativeWorkerRecord* actual = nullptr;
    Fault* owner = nullptr;
  };
  FaultMode mode;
  std::stop_source& stop;
  detail::NativeWorkerBackend real = detail::DefaultNativeWorkerBackend();
  detail::AllocationBackend memory = detail::DefaultAllocationBackend();
  std::array<Slot, kNativeWorkers> slots{};
  std::latch other_ranges{1};
  std::latch first_waiting{1};
  std::latch second_waiting{1};
  std::latch release_first{1};
  std::size_t attempted = 0, started = 0, joined = 0;
  std::atomic<std::size_t> entered{0}, allocation_faults{0};
  std::atomic<bool> allocation_armed{false};
  std::atomic<bool> others_released{false}, first_released{false};
  std::atomic<unsigned> finished_others{0};
  void ReleaseOthers() noexcept {
    if (!others_released.exchange(true)) other_ranges.count_down();
  }
  void ReleaseFirst() noexcept {
    if (!first_released.exchange(true)) release_first.count_down();
  }
  Fault(FaultMode value, std::stop_source& source)
      : mode(value), stop(source) {}
  static void Entry(detail::NativeWorkerRecord& record) noexcept {
    auto& slot = *static_cast<Slot*>(record.state);
    auto& self = *slot.owner;
    self.entered.fetch_add(1);
    if (self.mode == FaultMode::kStop) (void)self.stop.request_stop();
    const bool mismatch = self.mode == FaultMode::kMismatch ||
                          self.mode == FaultMode::kMismatchCap ||
                          self.mode == FaultMode::kMismatchThirdLaunch;
    if (mismatch && record.ordinal == 0) {
      self.first_waiting.count_down();
      self.release_first.wait();
    }
    slot.actual->entry(*slot.actual);
    if (mismatch && record.ordinal >= kOtherWorkerBegin) {
      const auto bit = 1U << (record.ordinal - kOtherWorkerBegin);
      if ((self.finished_others.fetch_or(bit) | bit) == kOtherWorkersComplete)
        self.ReleaseOthers();
    }
    if (mismatch && record.ordinal == 1) {
      self.second_waiting.count_down();
      self.other_ranges.wait();
      (void)self.stop.request_stop();
      self.ReleaseFirst();
    }
  }
  static std::uintptr_t Launch(detail::NativeWorkerRecord* actual,
                               void* state) {
    auto& self = *static_cast<Fault*>(state);
    try {
      ++self.attempted;
      if (self.mode == FaultMode::kSecondLaunch && self.attempted == 2)
        throw std::runtime_error("owned inspector second launch fault");
      if (self.mode == FaultMode::kMismatchThirdLaunch && self.attempted == 3) {
        self.first_waiting.wait();
        self.second_waiting.wait();
        throw std::runtime_error("owned inspector mismatch third launch fault");
      }
      Require(self.started < self.slots.size(),
              "first eligible four-job range");
      auto& slot = self.slots[self.started];
      slot.actual = actual;
      slot.owner = &self;
      slot.forwarded = {Entry, &slot, actual->ordinal};
      const auto handle = self.real.launch(&slot.forwarded, self.real.state);
      ++self.started;
      return handle;
    } catch (...) {
      // Every real or injected launch failure releases both possible waits.
      // Atomic one-shot signals cannot double-count with completed callbacks.
      self.ReleaseOthers();
      self.ReleaseFirst();
      throw;
    }
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& self = *static_cast<Fault*>(state);
    self.real.join(handle, self.real.state);
    ++self.joined;
    if (self.mode == FaultMode::kAllocation && self.joined == kNativeWorkers)
      self.allocation_armed = true;
  }
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Fault*>(state);
    if (self.allocation_armed.exchange(false)) {
      ++self.allocation_faults;
      throw std::bad_alloc();
    }
    return self.memory.allocate(bytes, alignment, self.memory.state);
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Fault*>(state);
    self.memory.deallocate(pointer, bytes, alignment, self.memory.state);
  }
};
struct InventoryStop {
  std::stop_source& stop;
  std::size_t visits = 0;
  static void Before(files::TestNativeView, void* state) {
    auto& self = *static_cast<InventoryStop*>(state);
    ++self.visits;
    // The first physical Inventory is the actual helper's bounded walk.
    (void)self.stop.request_stop();
  }
};
}  // namespace

namespace {
class TemporaryRoot {
 public:
  TemporaryRoot()
      : parent_(
            std::filesystem::canonical(std::filesystem::temp_directory_path())),
        path_(
            parent_ /
            (std::string(kTemporaryPrefix) +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(sequence_.fetch_add(1)))) {
    Require(path_.is_absolute() && path_.parent_path() == parent_ &&
                std::filesystem::create_directory(path_),
            "exclusive canonical owned inspection test root");
    owned_ = true;
  }
  ~TemporaryRoot() noexcept {
    try {
      if (!owned_ || path_.parent_path() != parent_) return;
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
    } catch (...) {
      // Best-effort cleanup cannot mask an earlier native control failure.
    }
  }
  TemporaryRoot(const TemporaryRoot&) = delete;
  TemporaryRoot& operator=(const TemporaryRoot&) = delete;
  const std::filesystem::path& Path() const { return path_; }

 private:
  inline static std::atomic<std::uint64_t> sequence_{0};
  const std::filesystem::path parent_;
  const std::filesystem::path path_;
  bool owned_ = false;
};
}  // namespace

void RunDeformationInspectionControls(const std::filesystem::path& directory) {
  TemporaryRoot root;
  const auto& output = root.Path();
  const auto fixture = directory / "boundary-minimal.gltf";
  constexpr auto inspector_t = kInspectorRelationships;
  constexpr auto helper_t = kHelperRelationships;
  constexpr auto physical_files = kPhysicalFiles;
  const auto entry = output / "source.gltf";
  std::filesystem::copy_file(fixture, entry);
  geo::ExecutionContext context({kNativeWorkers, 1, 128 * kMiB});
  {
    constexpr std::array<std::string_view, 31> kOpaque{
        "opaque-00.bin", "opaque-01.bin", "opaque-02.bin", "opaque-03.bin",
        "opaque-04.bin", "opaque-05.bin", "opaque-06.bin", "opaque-07.bin",
        "opaque-08.bin", "opaque-09.bin", "opaque-10.bin", "opaque-11.bin",
        "opaque-12.bin", "opaque-13.bin", "opaque-14.bin", "opaque-15.bin",
        "opaque-16.bin", "opaque-17.bin", "opaque-18.bin", "opaque-19.bin",
        "opaque-20.bin", "opaque-21.bin", "opaque-22.bin", "opaque-23.bin",
        "opaque-24.bin", "opaque-25.bin", "opaque-26.bin", "opaque-27.bin",
        "opaque-28.bin", "opaque-29.bin", "opaque-30.bin"};
    {
      for (const auto path : kOpaque) {
        std::ofstream file(output / path, std::ios::binary);
        file.put('x');
        file.close();
        Require(bool(file), "complete literal onebyte opaque physical source");
      }
    }
    mv::GltfFileReadOptions read_options;
    {
      read_options.extensions =
          mv::GltfExtensionFilePolicy::kExplicitOpaqueInventory;
      read_options.opaque_source_files = kOpaque;
    }
    read_options.read_request = {
        mv::GltfAdmissionProfile::kRetainedCoreDeformationV1, kPolicy};
    auto captured = mv::ReadGltfFileAsset(entry, read_options, kFileRead,
                                          kFileReadWork, context);
    Require(captured.asset.has_value(),
            "actual minimal physical source capture");
    Require(captured.asset->Files().size() == physical_files &&
                captured.asset->Asset().Resources().size() == 1,
            "literal opaque physical files create no additional asset "
            "resource rows");
    auto extraction = mv::ExtractGltfPrimitiveCapped(
        captured.asset->Asset(), *captured.asset->Asset().Primitive(0),
        {mv::GltfTriangleRequest::kRequireTriangles,
         mv::GltfValueRequest::kRawAndNormalized,
         mv::GltfProcessingProfile::kDeformationRetainedTrianglesV1,
         kPolicy,
         {}},
        kExtraction, {});
    Require(extraction.extraction.has_value(), "actual minimal extraction");
    auto materialized = mv::MaterializeGltfPrimitive(
        *extraction.extraction, kPolicy, kMaterialization, kTransferWork, {});
    Require(materialized.primitive.has_value(), "actual canonical source");
    const auto& source = *materialized.primitive;
    constexpr std::array<geo::index_t, 1> kFaces{0};
    constexpr std::array<geo::index_t, 3> kCorners{0, 1, 2};
    auto written = mv::WriteGltfTopologySelection(
        {source, source.Inspection(), kFaces, kCorners}, kPolicy, kWrite,
        kWriteWork, {});
    Require(written.bundle.has_value(), "actual unchanged writer owner");
    auto reload = mv::ReadGltfAssetCapped(written.bundle->Json(), {},
                                          read_options.read_request, kRead,
                                          kReadWork, context);
    Require(reload.asset.has_value(), "actual output reload");
    auto reload_extraction = mv::ExtractGltfPrimitiveCapped(
        *reload.asset, *reload.asset->Primitive(0),
        {mv::GltfTriangleRequest::kRequireTriangles,
         mv::GltfValueRequest::kRawAndNormalized,
         mv::GltfProcessingProfile::kDeformationRetainedTrianglesV1,
         kPolicy,
         {}},
        kExtraction, {});
    Require(reload_extraction.extraction.has_value(),
            "actual output typed reload");
    const mv::GltfTopologyOutputInspectionRequest request{
        source,
        source.Inspection(),
        kFaces,
        kCorners,
        *written.bundle,
        *reload.asset,
        *reload_extraction.extraction};
    auto inspector_policy = SerialPolicy(inspector_t);
    auto verified = mv::InspectGltfTopologyOutput(
        request, inspector_policy, kInspection, kInspectionWork, {});
    Require(verified.deformation.has_value(),
            "own ordinary-inspector observation");
    std::cout << "inspector T=" << inspector_t
              << " actual=" << verified.deformation->counts.relationship_steps
              << " complete=" << verified.deformation->complete << '\n';
    Require(verified.inspection && verified.deformation->complete &&
                verified.deformation->counts.relationship_steps == inspector_t,
            "independent ordinary-inspector T acceptance");
    inspector_policy.limits.max_relationship_steps = inspector_t - 1;
    auto refused = mv::InspectGltfTopologyOutput(
        request, inspector_policy, kInspection, kInspectionWork, {});
    Require(
        !refused.inspection && refused.deformation &&
            !refused.deformation->complete &&
            refused.status ==
                mv::GltfTopologyOutputInspectionStatus::kLimitExceeded &&
            refused.deformation->counts.relationship_steps == inspector_t - 1,
        "independent ordinary-inspector T-1 bounded refusal");
    inspector_policy.limits.max_relationship_steps = 1;
    auto preflight = mv::InspectGltfTopologyOutput(
        request, inspector_policy, kInspection, kInspectionWork, {});
    Require(!preflight.inspection && preflight.deformation &&
                preflight.status ==
                    mv::GltfTopologyOutputInspectionStatus::kLimitExceeded &&
                preflight.deformation->counts.relationship_steps == 1 &&
                preflight.deformation->execution.planned_jobs == 0,
            "own low-cap refusal during real initial admission");
    {
      const auto retained = context.ActiveTrackedPayload();
      for (const auto schedule :
           {WorkCase::kScalarOnly, WorkCase::kByteOnly, WorkCase::kBelowBoth,
            WorkCase::kMaxJobsOne}) {
        const bool parallel = schedule == WorkCase::kScalarOnly ||
                              schedule == WorkCase::kByteOnly;
        auto policy = kPolicy;
        policy.work.max_jobs =
            schedule == WorkCase::kMaxJobsOne ? 1 : kNativeWorkers;
        policy.work.parallel_min_scalars =
            schedule == WorkCase::kScalarOnly ||
                    schedule == WorkCase::kMaxJobsOne
                ? 1
                : kMiB;
        policy.work.parallel_min_bytes =
            schedule == WorkCase::kByteOnly || schedule == WorkCase::kMaxJobsOne
                ? 1
                : kMiB;
        policy.work.partition_scalars =
            schedule == WorkCase::kByteOnly ? kMiB : 1;
        policy.work.partition_bytes = 64;
        Entered entered;
        detail::OperationWorkerObserver observer;
        observer.job_entered = Entered::Job;
        observer.launched = Entered::Launched;
        observer.joined = Entered::Joined;
        observer.state = &entered;
        inspection::InspectionTestOptions testing;
        testing.observer = &observer;
        {
          auto actual = inspection::InspectGltfTopologyOutputForTest(
              request, kInspection, kInspectionWork, {}, testing, &policy);
          Require(actual.inspection && actual.deformation &&
                      actual.deformation->complete,
                  "actual inspector work schedule preserves result");
          const auto& stats = actual.deformation->execution;
          Require(stats.launched_workers == stats.joined_workers &&
                      entered.count == stats.planned_jobs &&
                      (stats.launched_workers > 0) == (parallel),
                  "actual inspector S-only/B-only/caller dispatch and joins");
          Require(entered.first_join_launched ==
                      (schedule == WorkCase::kScalarOnly ? kNativeWorkers
                       : schedule == WorkCase::kByteOnly ? kFirstByteRangeJobs
                                                         : 0),
                  "independent first geometry range grouping4/2/0/0");
          std::cout << "inspector schedule=" << static_cast<unsigned>(schedule)
                    << " planned=" << stats.planned_jobs
                    << " launched=" << stats.launched_workers
                    << " joined=" << stats.joined_workers << '\n';
        }
        Require(context.ActiveTrackedPayload() == retained &&
                    context.ActiveWorkers() == 0,
                "inspector schedule owner and lease cleanup");
        mv::GltfFilePublicationOptions options;
        options.deformation = {kPolicy, kPolicy, policy};
        files::GltfFilePublicationTestOptions file_testing;
        file_testing.verification_execution.observer = &observer;
        {
          auto actual = files::TestPublishGltfTopologyBundle(
              {*captured.asset, *verified.inspection},
              output / ("scheduled-" +
                        std::to_string(static_cast<unsigned>(schedule))),
              options, kPublish, kPublishWork, {}, file_testing);
          Require(
              actual.bundle && actual.deformation_verification &&
                  actual.deformation_verification->complete,
              "actual staged helper schedule preserves physical publication");
          const auto& stats = actual.deformation_verification->execution;
          Require(stats.launched_workers == stats.joined_workers &&
                      (stats.launched_workers > 0) == (parallel),
                  "actual helper own S-only/B-only/caller dispatch and joins");
          std::cout << "helper schedule=" << static_cast<unsigned>(schedule)
                    << " planned=" << stats.planned_jobs
                    << " launched=" << stats.launched_workers
                    << " joined=" << stats.joined_workers << '\n';
        }
        Require(context.ActiveTrackedPayload() == retained &&
                    context.ActiveWorkers() == 0,
                "helper schedule owner and lease cleanup");
      }
      constexpr std::array<double, 9> kDifferentPositions{0, 0, 0, 2, 0,
                                                          0, 0, 1, 0};
      constexpr std::array<std::uint32_t, 3> kIndices{0, 1, 2};
      geo::MeshMaterializationInput differing;
      differing.positions = std::span<const double>(kDifferentPositions);
      differing.topology = geo::TriangleTopologyInput{kIndices};
      const auto changed = geo::MaterializeMesh(
          differing, kMaterialization.materialization, kTransferWork, context);
      Require(changed.mesh.has_value(),
              "real independently changed candidate storage");
      const auto changed_inspection = geo::InspectMaterializedMesh(
          *changed.mesh, kMaterialization.inspection, kTransferWork);
      Require(changed_inspection.inspection.has_value(),
              "real candidate storage validity");
      for (const auto mode :
           {FaultMode::kCap, FaultMode::kStop, FaultMode::kSecondLaunch,
            FaultMode::kAllocation, FaultMode::kMismatch,
            FaultMode::kMismatchCap, FaultMode::kMismatchThirdLaunch}) {
        std::stop_source stop;
        Fault fault(mode, stop);
        auto policy = kPolicy;
        policy.work.parallel_min_scalars = 1;
        policy.work.parallel_min_bytes = kMiB;
        policy.work.partition_scalars = 1;
        policy.work.max_jobs = kNativeWorkers;
        policy.work.max_relationship_block = 1;
        if (mode == FaultMode::kCap)
          policy.limits.max_relationship_steps = kWorkerCap;
        if (mode == FaultMode::kMismatchCap)
          policy.limits.max_relationship_steps = kMismatchPrefixCap;
        const bool mismatch = mode == FaultMode::kMismatch ||
                              mode == FaultMode::kMismatchCap ||
                              mode == FaultMode::kMismatchThirdLaunch;
        const mv::GltfTopologyOutputInspectionRequest selected{
            source,
            mismatch ? *changed_inspection.inspection : source.Inspection(),
            kFaces,
            kCorners,
            *written.bundle,
            *reload.asset,
            *reload_extraction.extraction};
        inspection::InspectionTestOptions testing;
        testing.worker_backend = {Fault::Launch, Fault::Join, &fault};
        testing.allocation_backend = {Fault::Allocate, Fault::Free, &fault};
        const auto before = context.ActiveTrackedPayload();
        bool launch_threw = false, allocation_threw = false;
        try {
          const auto actual = inspection::InspectGltfTopologyOutputForTest(
              selected, kInspection, kInspectionWork, stop.get_token(), testing,
              &policy);
          std::cout << "inspector fault-result=" << static_cast<unsigned>(mode)
                    << " status=" << static_cast<unsigned>(actual.status)
                    << " code=" << static_cast<unsigned>(actual.diagnostic.code)
                    << " R="
                    << (actual.deformation
                            ? actual.deformation->counts.relationship_steps
                            : 0)
                    << '\n';
          Require(!actual.inspection && actual.deformation &&
                      !actual.deformation->complete,
                  "actual failed inspector publishes no successful proof");
          if (mode == FaultMode::kCap || mode == FaultMode::kMismatchCap) {
            Require(actual.status == mv::GltfTopologyOutputInspectionStatus::
                                         kLimitExceeded &&
                        actual.deformation->counts.relationship_steps ==
                            policy.limits.max_relationship_steps,
                    "actual worker or bounded-prefix relationship cap "
                    "remains hard");
          } else if (mode == FaultMode::kStop) {
            Require(actual.status ==
                        mv::GltfTopologyOutputInspectionStatus::kCanceled,
                    "actual entered worker cooperative stop");
          } else {
            Require(mode == FaultMode::kMismatch &&
                        actual.status ==
                            mv::GltfTopologyOutputInspectionStatus::
                                kOutputMismatch &&
                        actual.diagnostic.code ==
                            mv::GltfTopologyOutputInspectionDiagnosticCode::
                                kPositionBits &&
                        actual.diagnostic.element ==
                            kFirstDifferentPositionComponent &&
                        actual.deformation->counts.relationship_steps ==
                            kMismatchPrefixRelationships,
                    "actual known position mismatch survives later stop and "
                    "charged prefix");
          }
        } catch (const std::bad_alloc&) {
          allocation_threw = true;
          Require(
              mode == FaultMode::kAllocation && fault.allocation_faults == 1,
              "owned first allocation after actual first phase joins");
        } catch (const std::runtime_error& error) {
          launch_threw = true;
          const bool second = mode == FaultMode::kSecondLaunch;
          const bool third = mode == FaultMode::kMismatchThirdLaunch;
          Require(
              (second || third) &&
                  std::string_view(error.what()) ==
                      (second
                           ? "owned inspector second launch fault"
                           : "owned inspector mismatch third launch fault") &&
                  fault.attempted == (second ? 2U : 3U) &&
                  fault.started == (second ? 1U : 2U),
              "exact owned native launch exception propagates after waiter "
              "release");
        }
        Require(launch_threw == (mode == FaultMode::kSecondLaunch ||
                                 mode == FaultMode::kMismatchThirdLaunch) &&
                    allocation_threw == (mode == FaultMode::kAllocation) &&
                    fault.started == (mode == FaultMode::kSecondLaunch ? 1U
                                      : mode == FaultMode::kMismatchThirdLaunch
                                          ? 2U
                                          : kNativeWorkers) &&
                    fault.started == fault.joined &&
                    fault.entered == fault.started &&
                    context.ActiveTrackedPayload() == before &&
                    context.ActiveWorkers() == 0,
                "actual inspector fault joins every required target start "
                "and restores owners");
        std::cout << "inspector fault=" << static_cast<unsigned>(mode)
                  << " started=" << fault.started << " joined=" << fault.joined
                  << '\n';
      }
    }
    {
      for (const auto control :
           {PublicationCase::kComplete, PublicationCase::kLimit,
            PublicationCase::kProviderFailure}) {
        mv::GltfFilePublicationOptions options;
        options.deformation = {
            kPolicy, kPolicy,
            SerialPolicy(helper_t -
                         (control == PublicationCase::kLimit ? 1 : 0))};
        Witness witness;
        witness.fail = control == PublicationCase::kProviderFailure;
        options.verified_files_provider = {Witness::Provide, &witness};
        auto result = mv::PublishGltfTopologyBundle(
            {*captured.asset, *verified.inspection},
            output /
                ("published-" + std::to_string(static_cast<unsigned>(control))),
            options, kPublish, kPublishWork, {});
        Require(result.deformation_verification.has_value(),
                "actual staged helper reached");
        const auto& facts = *result.deformation_verification;
        std::cout << "helper control=" << static_cast<unsigned>(control)
                  << " T=" << helper_t
                  << " actual=" << facts.counts.relationship_steps
                  << " complete=" << facts.complete
                  << " published=" << result.bundle.has_value() << '\n';
        if (control == PublicationCase::kLimit) {
          Require(!result.bundle && !facts.complete && !witness.facts &&
                      facts.status == mv::GltfStatus::kUnsupportedProfile &&
                      facts.diagnostic.code ==
                          mv::GltfDiagnosticCode::kLimitExceeded &&
                      facts.counts.relationship_steps == helper_t - 1 &&
                      result.cleanup == mv::GltfFileCleanupOutcome::kRemoved,
                  "helper T-1 typed refusal before provider and real rollback");
        } else {
          Require(
              facts.complete && facts.counts.relationship_steps == helper_t &&
                  witness.facts && witness.facts->complete &&
                  witness.facts->counts.relationship_steps ==
                      facts.counts.relationship_steps &&
                  witness.facts->execution.planned_jobs ==
                      facts.execution.planned_jobs,
              "actual borrowed staged facts equal stable final result facts");
          Require(result.bundle.has_value() ==
                      (control == PublicationCase::kComplete),
                  "normal publication or actual later provider failure");
        }
      }
    }
    {
      mv::GltfFilePublicationOptions options;
      options.deformation = {kPolicy, kPolicy,
                             SerialPolicy(helper_t - physical_files - 1)};
      auto actual = mv::PublishGltfTopologyBundle(
          {*captured.asset, *verified.inspection}, output / "inventory-cap",
          options, kPublish, kPublishWork, {});
      Require(
          !actual.bundle && actual.deformation_verification &&
              !actual.deformation_verification->complete &&
              actual.deformation_verification->status ==
                  mv::GltfStatus::kUnsupportedProfile &&
              actual.deformation_verification->diagnostic.code ==
                  mv::GltfDiagnosticCode::kLimitExceeded &&
              actual.deformation_verification->counts.relationship_steps ==
                  helper_t - physical_files - 1 &&
              actual.cleanup == mv::GltfFileCleanupOutcome::kRemoved,
          "typed hard-cap from actual noexcept Inventory comparator survives "
          "handle cleanup");
    }
    {
      const auto before_stop = context.ActiveTrackedPayload();
      std::stop_source stop;
      InventoryStop hook{stop};
      files::GltfFilePublicationTestOptions testing;
      testing.hooks.before_inventory = InventoryStop::Before;
      testing.hooks.state = &hook;
      mv::GltfFilePublicationOptions options;
      options.deformation = {kPolicy, kPolicy,
                             SerialPolicy(std::max(inspector_t, helper_t))};
      Witness witness;
      options.verified_files_provider = {Witness::Provide, &witness};
      auto actual = files::TestPublishGltfTopologyBundle(
          {*captured.asset, *verified.inspection}, output / "inventory-stop",
          options, kPublish, kPublishWork, stop.get_token(), testing);
      std::cout
          << "inventory-stop visits=" << hook.visits
          << " outer=" << static_cast<unsigned>(actual.diagnostic.status)
          << " helper="
          << (actual.deformation_verification
                  ? std::to_string(static_cast<unsigned>(
                        actual.deformation_verification->status))
                  : "absent")
          << " R="
          << (actual.deformation_verification
                  ? actual.deformation_verification->counts.relationship_steps
                  : 0)
          << " cleanup=" << static_cast<unsigned>(actual.cleanup) << '\n';
      Require(context.ActiveWorkers() == 0 &&
                  context.ActiveTrackedPayload() == before_stop,
              "actual stop released every transient native owner before "
              "assertion");
      Require(
          hook.visits == 1 && !witness.facts && !actual.bundle &&
              actual.deformation_verification &&
              !actual.deformation_verification->complete &&
              actual.deformation_verification->status ==
                  mv::GltfStatus::kCanceled &&
              actual.deformation_verification->diagnostic.code ==
                  mv::GltfDiagnosticCode::kCanceled &&
              actual.deformation_verification->predicates.staged_reload ==
                  false &&
              actual.deformation_verification->counts.relationship_steps ==
                  kHelperCoreRelationships +
                      kPhysicalAndResetRelationshipsPerFile * physical_files &&
              actual.diagnostic.status == mv::GltfFileStatus::kCanceled &&
              actual.cleanup == mv::GltfFileCleanupOutcome::kRemoved,
          "actual native Inventory cooperative stop before walk preserves "
          "outer rollback");
    }
  }
  std::cout << "workers=" << context.ActiveWorkers()
            << " payload=" << context.ActiveTrackedPayload() << '\n';
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
          "all source and native owners released after real calls");
}
