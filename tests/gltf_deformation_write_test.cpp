// SPDX-License-Identifier: Apache-2.0
#include "gltf_deformation_write_test.h"

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <latch>
#include <new>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include "gltf_topology_write_internal.h"

namespace {
namespace writer = meshvale::interchange::gltf_topology_write_detail;
namespace detail = meshvale::interchange::gltf_detail;
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kRows = 65536;
constexpr std::uint64_t kUnusedNodes = 32;
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
enum class Mode {
  kCap,
  kStop,
  kSecondLaunch,
  kAllocation,
  kMismatch,
  kMismatchCap,
  kMismatchSecondLaunch
};
struct Fault {
  struct Slot {
    detail::NativeWorkerRecord forwarded{};
    detail::NativeWorkerRecord* actual = nullptr;
    Fault* owner = nullptr;
  };
  Mode mode;
  std::stop_source& stop;
  writer::NativeWorkerBackend real = writer::DefaultNativeWorkerBackend();
  detail::AllocationBackend memory = detail::DefaultAllocationBackend();
  std::array<Slot, 4> slots{};
  std::latch mismatch_done{1};
  std::size_t attempted = 0, started = 0, joined = 0;
  std::atomic<std::size_t> entered{0}, allocation_faults{0};
  std::atomic<bool> allocation_armed{false};
  std::atomic<bool> mismatch_released{false};
  Fault(Mode value, std::stop_source& source) : mode(value), stop(source) {}
  bool IsMismatch() const {
    return mode == Mode::kMismatch || mode == Mode::kMismatchCap ||
           mode == Mode::kMismatchSecondLaunch;
  }
  void ReleaseMismatch() noexcept {
    if (!mismatch_released.exchange(true)) mismatch_done.count_down();
  }
  static void Entry(detail::NativeWorkerRecord& record) noexcept {
    auto& slot = *static_cast<Slot*>(record.state);
    auto& self = *slot.owner;
    self.entered.fetch_add(1);
    if (self.mode == Mode::kStop) (void)self.stop.request_stop();
    const bool mismatch = self.IsMismatch();
    if (mismatch && record.ordinal == 0) self.mismatch_done.wait();
    slot.actual->entry(*slot.actual);
    if (mismatch && record.ordinal == 1) {
      (void)self.stop.request_stop();
      self.ReleaseMismatch();
    }
  }
  static std::uintptr_t Launch(detail::NativeWorkerRecord* actual,
                               void* opaque) {
    auto& self = *static_cast<Fault*>(opaque);
    ++self.attempted;
    if (self.mode == Mode::kSecondLaunch && self.attempted == 2)
      throw std::runtime_error("writer actual second launch fault");
    Require(self.started < self.slots.size(),
            "first eligible phase slot bound");
    auto& slot = self.slots[self.started];
    slot.actual = actual;
    slot.owner = &self;
    slot.forwarded = {Entry, &slot, actual->ordinal};
    try {
      // The injected failure releases worker0's held or future latch wait
      // through the same catch as an exception from the native launch.
      if (self.mode == Mode::kMismatchSecondLaunch && self.attempted == 2)
        throw std::runtime_error("writer actual second launch fault");
      const auto handle = self.real.launch(&slot.forwarded, self.real.state);
      ++self.started;
      return handle;
    } catch (...) {
      if (self.IsMismatch()) self.ReleaseMismatch();
      throw;
    }
  }
  static void Join(std::uintptr_t handle, void* opaque) noexcept {
    auto& self = *static_cast<Fault*>(opaque);
    self.real.join(handle, self.real.state);
    ++self.joined;
    if (self.mode == Mode::kAllocation && self.joined == 2)
      self.allocation_armed = true;
  }
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& self = *static_cast<Fault*>(opaque);
    if (self.allocation_armed.exchange(false)) {
      ++self.allocation_faults;
      throw std::bad_alloc();
    }
    return self.memory.allocate(bytes, alignment, self.memory.state);
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* opaque) noexcept {
    auto& self = *static_cast<Fault*>(opaque);
    self.memory.deallocate(pointer, bytes, alignment, self.memory.state);
  }
  writer::NativeWorkerBackend Workers() { return {Launch, Join, this}; }
  detail::AllocationBackend Memory() { return {Allocate, Free, this}; }
};

void Failures(const mv::GltfMaterializedPrimitive& source,
              geo::ExecutionContext& context) {
  // Resource1 + physical161 + primitive1 = 163 before face-map rows.
  // Identity faceMap1 leaves164 before the first two-job offsets range.
  // The two-face candidate has no attributes, preserving that source prefix.
  constexpr std::array<double, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
  constexpr std::array<std::uint32_t, 6> duplicated{0, 1, 2, 0, 1, 2};
  geo::MeshMaterializationInput input;
  input.positions = std::span<const double>(positions);
  input.topology = geo::TriangleTopologyInput{duplicated};
  const auto candidate = geo::MaterializeMesh(
      input, kMaterialization.materialization, kTransferWork, context);
  Require(candidate.mesh.has_value(), "real independent two-face geometry");
  const auto inspected = geo::InspectMaterializedMesh(
      *candidate.mesh, kMaterialization.inspection, kTransferWork);
  Require(
      inspected.inspection && inspected.inspection->IsStorageValid() &&
          inspected.inspection->Source().View().positions.size() == 9 &&
          inspected.inspection->Source().View().attributes.empty() &&
          inspected.inspection->Source().View().face_offsets.size() == 3 &&
          inspected.inspection->Source().View().corner_vertices.size() == 6,
      "two-face candidate preserves empty descriptors and source prefix163");
  constexpr std::array<geo::index_t, 1> identity_faces{0};
  constexpr std::array<geo::index_t, 3> identity_corners{0, 1, 2};
  constexpr std::array<geo::index_t, 2> wrong_faces{0, 0};
  constexpr std::array<geo::index_t, 6> wrong_corners{0, 1, 2, 0, 1, 2};
  constexpr std::array<Mode, 7> modes{Mode::kCap,
                                      Mode::kStop,
                                      Mode::kSecondLaunch,
                                      Mode::kAllocation,
                                      Mode::kMismatch,
                                      Mode::kMismatchCap,
                                      Mode::kMismatchSecondLaunch};
  const auto retained = context.ActiveTrackedPayload();
  for (const auto mode : modes) {
    std::stop_source stop;
    Fault fault(mode, stop);
    auto policy = kPolicy;
    policy.work.parallel_min_scalars = 1;
    policy.work.partition_scalars = 1;
    policy.work.parallel_min_bytes = kMiB;
    policy.work.max_jobs = 4;
    policy.work.max_relationship_block = 1;
    if (mode == Mode::kCap || mode == Mode::kMismatchCap)
      policy.limits.max_relationship_steps = 165;
    const bool mismatch = fault.IsMismatch();
    const auto& actual_candidate =
        mismatch ? *inspected.inspection : source.Inspection();
    const std::span<const geo::index_t> faces =
        mismatch ? std::span<const geo::index_t>(wrong_faces) : identity_faces;
    const std::span<const geo::index_t> corners =
        mismatch ? std::span<const geo::index_t>(wrong_corners)
                 : identity_corners;
    bool threw_launch = false, threw_allocation = false;
    try {
      const auto actual = writer::WriteWithBackend(
          {source, actual_candidate, faces, corners}, kWrite, kWriteWork,
          stop.get_token(), fault.Memory(), fault.Workers(), &policy);
      Require(
          !actual.bundle && actual.deformation && !actual.deformation->complete,
          "actual failure publishes no complete writer owner");
      if (mode == Mode::kCap || mode == Mode::kMismatchCap) {
        Require(actual.status == mv::GltfTopologyWriteStatus::kLimitExceeded &&
                    actual.diagnostic.code ==
                        mv::GltfTopologyWriteDiagnosticCode::kCountLimit &&
                    actual.deformation->counts.relationship_steps == 165,
                "actual parallel or recovery-prefix cap remains hard165");
      } else if (mode == Mode::kStop) {
        Require(
            actual.status == mv::GltfTopologyWriteStatus::kCanceled &&
                actual.deformation->counts.relationship_steps == 164,
            "real first-parallel stop retains preceding completed prefix164");
      } else {
        Require(mode == Mode::kMismatch &&
                    actual.status ==
                        mv::GltfTopologyWriteStatus::kInvalidCorrespondence &&
                    actual.diagnostic.code ==
                        mv::GltfTopologyWriteDiagnosticCode::kFaceMapOrder &&
                    actual.diagnostic.element == 1 &&
                    actual.deformation->counts.relationship_steps == 166,
                "known row1 mismatch survives later stop and actual repeated "
                "prefix");
      }
      Require(
          actual.deformation->execution.launched_workers == 2 &&
              actual.deformation->execution.joined_workers == 2,
          "new-profile failure actually entered/joined target native range");
      std::cout << "writer-failure-mode=" << static_cast<unsigned>(mode)
                << " status=" << static_cast<unsigned>(actual.status)
                << " code=" << static_cast<unsigned>(actual.diagnostic.code)
                << " R=" << actual.deformation->counts.relationship_steps
                << '\n';
    } catch (const std::bad_alloc&) {
      threw_allocation = true;
      Require(mode == Mode::kAllocation && fault.allocation_faults == 1 &&
                  fault.joined == 2,
              "first new-local-counter allocation after joined phase failed");
    } catch (const std::runtime_error& error) {
      if (mode != Mode::kSecondLaunch && mode != Mode::kMismatchSecondLaunch)
        throw;
      threw_launch = true;
      Require(
          (mode == Mode::kSecondLaunch ||
           mode == Mode::kMismatchSecondLaunch) &&
              std::string(error.what()) ==
                  "writer actual second launch fault" &&
              fault.attempted == 2 && fault.started == 1 && fault.joined == 1,
          "original second-launch exception survives joined local-count merge");
    }
    Require((mode == Mode::kAllocation) == threw_allocation &&
                (mode == Mode::kSecondLaunch ||
                 mode == Mode::kMismatchSecondLaunch) == threw_launch &&
                fault.started > 0 && fault.entered == fault.started &&
                fault.started == fault.joined,
            "actual target entries/handles and expected exception paths agree");
    Require(context.ActiveTrackedPayload() == retained &&
                context.ActiveWorkers() == 0,
            "failure releases every new original-ledger allocation and lease");
    const auto ordinary = mv::WriteGltfTopologySelection(
        {source, source.Inspection(), identity_faces, identity_corners},
        kPolicy, kWrite, kWriteWork, {});
    Require(ordinary.bundle && ordinary.deformation &&
                ordinary.deformation->complete,
            "next ordinary new-profile call is untouched by borrowed failure "
            "state");
  }
}

void Schedules(const mv::GltfMaterializedPrimitive& source,
               std::string_view json, geo::ExecutionContext& context) {
  constexpr std::array<geo::index_t, 1> faces{0};
  constexpr std::array<geo::index_t, 3> corners{0, 1, 2};
  auto check = [&](const char* label, mv::GltfDeformationPolicy policy,
                   bool workers) {
    const auto output = mv::WriteGltfTopologySelection(
        {source, source.Inspection(), faces, corners}, policy, kWrite,
        kWriteWork, {});
    Require(output.bundle && output.deformation && output.deformation->complete,
            "adequate-policy actual writer completes");
    const auto expected = std::as_bytes(std::span(json.data(), json.size()));
    Require(
        std::equal(expected.begin(), expected.end(),
                   output.bundle->Json().begin(), output.bundle->Json().end()),
        "identity physical JSON bytes remain exact in every schedule");
    const auto& verification = output.execution.verification;
    const auto& copy = output.execution.output_copy;
    const auto started = verification.launched_workers + copy.launched_workers;
    const auto joined = verification.joined_workers + copy.joined_workers;
    Require(started == joined && (started > 0) == workers &&
                context.ActiveWorkers() == 0,
            "real S/B-selected workers join; below-both/maxjobs1 uses caller");
    Require(verification.caller_jobs + copy.caller_jobs > 0,
            "actual caller jobs remain separately observed");
    std::cout << "schedule=" << label << " launched=" << started
              << " joined=" << joined
              << " caller=" << verification.caller_jobs + copy.caller_jobs
              << " complete=" << output.deformation->complete << '\n';
  };
  auto scheduling = kPolicy;
  scheduling.work.parallel_min_scalars = kMiB;
  scheduling.work.parallel_min_bytes = kMiB;
  scheduling.work.max_jobs = 4;
  check("below-both", scheduling, false);
  scheduling.work.parallel_min_scalars = 1;
  scheduling.work.partition_scalars = 1;
  check("S-only", scheduling, true);
  scheduling.work.parallel_min_scalars = kMiB;
  scheduling.work.parallel_min_bytes = 1;
  scheduling.work.partition_bytes = 16;
  check("B-only", scheduling, true);
  scheduling.work.max_jobs = 1;
  check("max-jobs-one", scheduling, false);
}

void ConcurrentWriters(const mv::GltfMaterializedPrimitive& source,
                       std::string_view json, geo::ExecutionContext& context) {
  constexpr std::array<geo::index_t, 1> kFaces{0};
  constexpr std::array<geo::index_t, 3> kCorners{0, 1, 2};
  constexpr std::size_t kCalls = 8;
  constexpr std::uint64_t kFullReferences = 531;
  const auto retained = context.ActiveTrackedPayload();
  std::array<std::exception_ptr, kCalls> failures{};
  std::latch all_entered{kCalls};
  std::atomic<std::size_t> entered{0};
  struct FirstAllocation {
    std::latch& all_entered;
    std::atomic<std::size_t>& entered;
    detail::AllocationBackend original = detail::DefaultAllocationBackend();
    bool first = true;
    static void* Allocate(std::size_t bytes, std::size_t alignment,
                          void* state) {
      auto& self = *static_cast<FirstAllocation*>(state);
      if (self.first) {
        self.first = false;
        ++self.entered;
        self.all_entered.count_down();
        self.all_entered.wait();
      }
      return self.original.allocate(bytes, alignment, self.original.state);
    }
    static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                     void* state) noexcept {
      auto& self = *static_cast<FirstAllocation*>(state);
      self.original.deallocate(pointer, bytes, alignment, self.original.state);
    }
  };
  {
    std::array<std::jthread, kCalls> threads;
    std::size_t created = 0;
    try {
      for (; created < kCalls; ++created) {
        const auto call = created;
        threads[call] = std::jthread([&, call] {
          FirstAllocation gate{all_entered, entered};
          try {
            const bool complete = call % 2 == 0;
            auto policy = kPolicy;
            policy.work.max_jobs = 1;
            policy.limits.max_relationship_steps = kFullReferences - !complete;
            const auto actual = writer::WriteWithBackend(
                {source, source.Inspection(), kFaces, kCorners}, kWrite,
                kWriteWork, {},
                {FirstAllocation::Allocate, FirstAllocation::Free, &gate},
                writer::DefaultNativeWorkerBackend(), &policy);
            Require(actual.deformation &&
                        actual.deformation->complete == complete &&
                        actual.bundle.has_value() == complete &&
                        actual.deformation->counts.relationship_steps ==
                            policy.limits.max_relationship_steps,
                    "concurrent retained writers own independent full/partial "
                    "counts");
            if (complete) {
              const auto expected =
                  std::as_bytes(std::span(json.data(), json.size()));
              Require(
                  actual.bundle->DeformationFacts() &&
                      actual.bundle->DeformationFacts()->complete &&
                      actual.bundle->DeformationFacts()
                              ->counts.relationship_steps == kFullReferences &&
                      std::equal(expected.begin(), expected.end(),
                                 actual.bundle->Json().begin(),
                                 actual.bundle->Json().end()),
                  "concurrent completed bundle owns exact facts and bytes");
            } else {
              Require(
                  actual.status ==
                          mv::GltfTopologyWriteStatus::kLimitExceeded &&
                      actual.diagnostic.code ==
                          mv::GltfTopologyWriteDiagnosticCode::kCountLimit,
                  "concurrent one-below writer keeps typed hard-cap refusal");
            }
          } catch (...) {
            if (gate.first) all_entered.count_down();
            failures[call] = std::current_exception();
          }
        });
      }
    } catch (...) {
      all_entered.count_down(kCalls - created);
      throw;
    }
  }
  for (const auto& failure : failures)
    if (failure) std::rethrow_exception(failure);
  Require(entered == kCalls && context.ActiveTrackedPayload() == retained &&
              context.ActiveWorkers() == 0,
          "concurrent writer result/local-counter owners return to retained "
          "baseline");
}

}  // namespace

// Independent ordered visit ledger for these literal fixtures and max_jobs=1:
// resource admission1; physical tree97/161; primitive lookup1; six typed
// topology passes14; lexical173/333; resource copy1; maps/provenance7;
// output search1+chunks7/8; late proof4. Totals306/531; the last visit is
// buffer0 proof validation. JSON tree N39/71, containers20/52, nonempty20:
// physical=2*N-1+nonempty; lexical=4*N-3+containers. No runtime calibration.
void ExerciseDeformationWriterBudgets(const std::filesystem::path& directory,
                                      geo::ExecutionContext& context) {
  struct Case {
    std::filesystem::path path;
    std::uint64_t total;
    std::size_t nodes;
  };
  const std::array<Case, 2> cases{
      {{directory / "boundary-minimal.gltf", 306, 1},
       {directory / "boundary-minimal-unused-nodes.gltf", 531,
        kUnusedNodes + 1}}};
  for (const auto& test : cases) {
    {
      std::ifstream file(test.path, std::ios::binary);
      Require(bool(file), "owned literal fixture opens");
      const std::string json{std::istreambuf_iterator<char>(file), {}};
      const auto bytes = std::as_bytes(std::span(json.data(), json.size()));
      const auto read = mv::ReadGltfAssetCapped(
          bytes, {},
          {mv::GltfAdmissionProfile::kRetainedCoreDeformationV1, kPolicy},
          kRead, kReadWork, context);
      Require(read.asset && read.asset->Nodes().size() == test.nodes,
              "real independent adequate-policy reader");
      const auto extracted = mv::ExtractGltfPrimitiveCapped(
          *read.asset, *read.asset->Primitive(0),
          {mv::GltfTriangleRequest::kRequireTriangles,
           mv::GltfValueRequest::kRawAndNormalized,
           mv::GltfProcessingProfile::kDeformationRetainedTrianglesV1,
           kPolicy,
           {}},
          kExtraction, {});
      Require(extracted.extraction.has_value(), "actual extraction owner");
      const auto transfer = mv::MaterializeGltfPrimitive(
          *extracted.extraction, kPolicy, kMaterialization, kTransferWork, {});
      Require(transfer.primitive.has_value(),
              "actual canonical transfer owner");
      const auto& source = *transfer.primitive;
      constexpr std::array<geo::index_t, 1> faces{0};
      constexpr std::array<geo::index_t, 3> corners{0, 1, 2};
      const auto retained = context.ActiveTrackedPayload();
      for (unsigned repeat = 0; repeat < 2; ++repeat) {
        for (const bool inclusive : {true, false}) {
          auto policy = kPolicy;
          policy.work.max_jobs = 1;
          policy.limits.max_relationship_steps = test.total - !inclusive;
          {
            const auto actual = mv::WriteGltfTopologySelection(
                {source, source.Inspection(), faces, corners}, policy, kWrite,
                kWriteWork, {});
            Require(actual.deformation.has_value(), "actual own writer facts");
            if (inclusive) {
              Require(actual.bundle && actual.deformation->complete &&
                          actual.deformation->counts.relationship_steps ==
                              test.total &&
                          actual.bundle->DeformationFacts() &&
                          actual.bundle->DeformationFacts()->complete &&
                          actual.bundle->DeformationFacts()
                                  ->counts.relationship_steps == test.total,
                      "independent inclusive total matches result and "
                      "immutable bundle");
              Require(std::equal(bytes.begin(), bytes.end(),
                                 actual.bundle->Json().begin(),
                                 actual.bundle->Json().end()),
                      "complete actual unchanged JSON output retained");
            } else {
              Require(
                  !actual.bundle && !actual.deformation->complete &&
                      actual.status ==
                          mv::GltfTopologyWriteStatus::kLimitExceeded &&
                      actual.diagnostic.code ==
                          mv::GltfTopologyWriteDiagnosticCode::kCountLimit &&
                      actual.deformation->counts.relationship_steps ==
                          test.total - 1,
                  "one-below refuses actual final source resource proof, "
                  "bounded partial count");
            }
            Require(
                actual.deformation->execution.launched_workers == 0 &&
                    actual.deformation->execution.joined_workers == 0,
                "selected one-job owning plan has no invented native workers");
            std::cout << "expected=" << test.total
                      << " cap=" << policy.limits.max_relationship_steps
                      << " repeat=" << repeat
                      << " complete=" << actual.deformation->complete
                      << " actual="
                      << actual.deformation->counts.relationship_steps << '\n';
          }
          Require(
              context.ActiveTrackedPayload() == retained &&
                  context.ActiveWorkers() == 0,
              "each success/refusal frees every fresh original-ledger owner");
        }
      }
      if (test.nodes > 1) {
        Schedules(source, json, context);
        Failures(source, context);
        ConcurrentWriters(source, json, context);
      }
    }
    Require(
        context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
        "source/canonical owners fully released between independent fixtures");
  }
}
