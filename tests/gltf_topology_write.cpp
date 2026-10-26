// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
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
#include <initializer_list>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_topology_write_internal.h"
#include "gltf_topology_write_workers.h"
#include "gltf_translation_output_fixture.h"

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = mv::gltf_topology_write_detail;
namespace allocation = mv::gltf_detail;
constexpr std::size_t kWorkerBudget = 2;
constexpr std::size_t kMinimumParallelVertices = 1;
constexpr std::size_t kPayloadBudget = 134217728;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kChannelLimit = 64;
constexpr std::uint64_t kByteLimit = 4194304;
constexpr std::uint64_t kRequestLimit = 16777216;
constexpr std::uint64_t kDepthLimit = 128;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kParallelMinimumComponents = 1;
constexpr std::uint64_t kChannelPartitionElements = 64;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCancellationBlockScalars = 64;
constexpr std::uint64_t kCancellationBlockBytes = 64;
constexpr std::uint64_t kMaximumJobs = 32;
constexpr std::size_t kVertexCount = 4098;
constexpr std::size_t kTriangleCorners = 3;
constexpr std::size_t kPositionComponents = 3;
constexpr std::size_t kOpaqueBytes = 131072;
constexpr char kOpaqueCharacter = 'x';
constexpr std::size_t kRequestRecordCapacity = 2048;
constexpr std::size_t kFailedLaunchOrdinal = 2;
constexpr std::size_t kPositionPhase = 1;
constexpr std::size_t kLaterPositionScalar = 512;
constexpr std::string_view kWriterFailurePrefix = "writer rejected: status=";
constexpr std::string_view kStageLabel = " stage=";
constexpr std::string_view kCodeLabel = " code=";
constexpr std::string_view kAttributeLabel = " attribute=";
constexpr std::string_view kElementLabel = " element=";
constexpr std::string_view kSpanLabel = " json_span=";
constexpr std::string_view kAbsentDiagnostic = "none";
constexpr std::string_view kDocumentPrefix =
    R"({"asset":{"version":"2.0"},"extras":{"opaque":")";
constexpr std::string_view kDocumentSuffix =
    R"("},"accessors":[{"componentType":5126,"type":"VEC3","count":4098,"min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
constexpr std::string_view kUnsupportedDocument =
    R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":3,"min":[0,0,0],"max":[0,0,0],"extensions":{"CUSTOM":{}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
void Require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
void ReportWriteFailure(const mv::GltfTopologyWriteResult& result) {
  if (result.bundle) return;
  std::cerr << kWriterFailurePrefix << static_cast<int>(result.status)
            << kStageLabel << static_cast<int>(result.diagnostic.stage)
            << kCodeLabel << static_cast<int>(result.diagnostic.code)
            << kAttributeLabel;
  if (result.diagnostic.attribute)
    std::cerr << *result.diagnostic.attribute;
  else
    std::cerr << kAbsentDiagnostic;
  std::cerr << kElementLabel;
  if (result.diagnostic.element)
    std::cerr << *result.diagnostic.element;
  else
    std::cerr << kAbsentDiagnostic;
  std::cerr << kSpanLabel;
  if (result.diagnostic.json_span)
    std::cerr << result.diagnostic.json_span->offset << ':'
              << result.diagnostic.json_span->length;
  else
    std::cerr << kAbsentDiagnostic;
  std::cerr << '\n';
}
geo::MeshMaterializationLimits GeometryLimits() {
  return {kCountLimit,   kCountLimit, kCountLimit,  kCountLimit,
          kChannelLimit, kCountLimit, kCountLimit,  kCountLimit,
          kCountLimit,   kByteLimit,  kRequestLimit};
}
geo::MeshMaterializationWorkPolicy GeometryWork() {
  return {kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
          kMaximumJobs};
}
mv::GltfReadLimits ReadLimits() {
  return {kByteLimit,    kByteLimit,   kByteLimit,    kCountLimit,
          kChannelLimit, kByteLimit,   kChannelLimit, kChannelLimit,
          kChannelLimit, kCountLimit,  kCountLimit,   kCountLimit,
          kCountLimit,   kRequestLimit};
}
mv::GltfTopologyWriteWorkPolicy Work(bool parallel) {
  return {parallel ? kParallelMinimumBytes
                   : std::numeric_limits<std::uint64_t>::max(),
          kPartitionBytes, kCancellationBlockBytes, kMaximumJobs};
}
mv::GltfTopologyWriteLimits Limits(std::uint64_t cap = kRequestLimit) {
  return {kByteLimit,
          kCountLimit,
          kDepthLimit,
          kCountLimit,
          kChannelLimit,
          kCountLimit,
          kByteLimit,
          kCountLimit,
          kCountLimit,
          kByteLimit,
          {kByteLimit, kChannelLimit, kByteLimit, kByteLimit, cap}};
}
struct Fixture {
  geo::ExecutionContext context{
      {kWorkerBudget, kMinimumParallelVertices, kPayloadBudget}};
  std::stop_source original_stop;
  std::string document = std::string(kDocumentPrefix) +
                         std::string(kOpaqueBytes, kOpaqueCharacter) +
                         std::string(kDocumentSuffix);
  mv::GltfMaterializedPrimitive source;
  geo::MaterializedFaceSelection selected;
  geo::MeshInspection inspection;
  Fixture() : source(Capture()), selected(Select()), inspection(Inspect()) {}
  mv::GltfMaterializedPrimitive Capture() {
    auto read = mv::ReadGltfAsset(
        std::as_bytes(std::span(document.data(), document.size())), {},
        ReadLimits(),
        {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumComponents,
         kChannelPartitionElements},
        context, original_stop.get_token());
    Require(read.asset.has_value(), "focused public Read failed");
    Require(read.asset->Capability(*read.asset->Primitive(0)).reason ==
                mv::GltfCapabilityReason::kBinaryAdmissionPending,
            "fixture must retain preliminary binary-pending capability");
    auto extracted =
        mv::ExtractGltfPrimitive(*read.asset, *read.asset->Primitive(0),
                                 {mv::GltfTriangleRequest::kRequireTriangles,
                                  mv::GltfValueRequest::kRawAndNormalized},
                                 {kCountLimit, kCountLimit, kRequestLimit}, {});
    Require(extracted.extraction.has_value(), "focused public Extract failed");
    auto result = mv::MaterializeGltfPrimitive(*extracted.extraction,
                                               {kChannelLimit,
                                                kRequestLimit,
                                                GeometryLimits(),
                                                {kCountLimit, kRequestLimit}},
                                               GeometryWork());
    Require(result.primitive.has_value(), "focused public transfer failed");
    Require(
        result.primitive->SourceAsset()
                .Capability(result.primitive->SourcePrimitive())
                .reason == mv::GltfCapabilityReason::kBinaryAdmissionPending,
        "successful extraction must not rewrite immutable capability");
    return *result.primitive;
  }
  geo::MaterializedFaceSelection Select() {
    std::vector<geo::index_t> faces(kVertexCount / kTriangleCorners - 1);
    for (std::size_t i = 0; i < faces.size(); ++i) faces[i] = i;
    auto result = geo::SelectMeshFaces(source.Mesh(), faces, GeometryLimits(),
                                       GeometryWork());
    Require(result.selection.has_value(), "focused public selection failed");
    return *result.selection;
  }
  geo::MeshInspection Inspect() {
    auto result = geo::InspectMaterializedMesh(
        selected.Candidate(), {kCountLimit, kRequestLimit}, GeometryWork());
    Require(result.inspection && result.inspection->IsStorageValid(),
            "focused public inspection failed");
    return *result.inspection;
  }
  mv::GltfTopologyWriteRequest Request() const {
    return {source, inspection, selected.SourceFaces(),
            selected.SourceCorners()};
  }
  void CheckSource(std::size_t baseline) const {
    const auto bytes = source.SourceAsset().OriginalJson();
    Require(std::ranges::equal(bytes, std::as_bytes(std::span(
                                          document.data(), document.size()))),
            "source bytes changed");
    Require(context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "operation leaked charges or worker slots");
  }
};

void UnsupportedSourceHasNoBridge() {
  geo::ExecutionContext context(
      {kWorkerBudget, kMinimumParallelVertices, kPayloadBudget});
  const auto baseline = context.ActiveTrackedPayload();
  {
    const auto read = mv::ReadGltfAsset(
        std::as_bytes(std::span(kUnsupportedDocument.data(),
                                kUnsupportedDocument.size())),
        {}, ReadLimits(),
        {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumComponents,
         kChannelPartitionElements},
        context, {});
    Require(read.asset &&
                read.asset->Capability(*read.asset->Primitive(0)).reason ==
                    mv::GltfCapabilityReason::kOptionalExtensionEffect,
            "optional extension fixture must remain unsupported");
    const auto extracted =
        mv::ExtractGltfPrimitive(*read.asset, *read.asset->Primitive(0),
                                 {mv::GltfTriangleRequest::kRequireTriangles,
                                  mv::GltfValueRequest::kRawAndNormalized},
                                 {kCountLimit, kCountLimit, kRequestLimit}, {});
    Require(!extracted.extraction &&
                extracted.diagnostic.status ==
                    mv::GltfStatus::kUnsupportedProfile &&
                extracted.diagnostic.code == mv::GltfDiagnosticCode::kIndexRole,
            "unsupported source acquired extraction usable by the writer");
  }
  Require(context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "unsupported source rejection leaked charges or workers");
}

// Fixed test-owned records observe the real private original allocator route.
// No replacement new/malloc, process hook, TLS, or guessed payload accounting.
struct RequestObserver {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
  };
  const geo::ExecutionContext& context;
  std::size_t baseline;
  std::array<Block, kRequestRecordCapacity> blocks{};
  std::size_t live = 0;
  std::size_t peak = 0;
  std::size_t requests = 0;
  std::size_t fail_request = 0;
  bool valid = true;
  allocation::AllocationBackend original =
      allocation::DefaultAllocationBackend();
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& observer = *static_cast<RequestObserver*>(state);
    ++observer.requests;
    observer.valid &= observer.context.ActiveTrackedPayload() ==
                      observer.baseline + observer.live + bytes;
    if (observer.requests == observer.fail_request) throw std::bad_alloc();
    auto* pointer =
        observer.original.allocate(bytes, alignment, observer.original.state);
    const auto slot = std::find_if(
        observer.blocks.begin(), observer.blocks.end(),
        [](const Block& block) { return block.pointer == nullptr; });
    if (slot == observer.blocks.end()) {
      observer.original.deallocate(pointer, bytes, alignment,
                                   observer.original.state);
      throw std::bad_alloc();
    }
    *slot = {pointer, bytes};
    observer.live += bytes;
    observer.peak = std::max(observer.peak, observer.live);
    return pointer;
  }
  static void Deallocate(void* pointer, std::size_t bytes,
                         std::size_t alignment, void* state) noexcept {
    auto& observer = *static_cast<RequestObserver*>(state);
    observer.valid &= observer.context.ActiveTrackedPayload() ==
                      observer.baseline + observer.live;
    const auto slot = std::find_if(
        observer.blocks.begin(), observer.blocks.end(),
        [pointer](const Block& block) { return block.pointer == pointer; });
    observer.valid &= slot != observer.blocks.end() && slot->bytes == bytes;
    observer.original.deallocate(pointer, bytes, alignment,
                                 observer.original.state);
    if (slot != observer.blocks.end()) {
      observer.live -= slot->bytes;
      *slot = {};
    }
  }
  allocation::AllocationBackend Backend() {
    return {Allocate, Deallocate, this};
  }
};

struct LaunchFailure : std::exception {};
struct WorkerObserver {
  struct Entry {
    detail::NativeWorkerRecord forwarded;
    detail::NativeWorkerRecord* original = nullptr;
    WorkerObserver* observer = nullptr;
    std::size_t phase = 0;
  };
  std::array<Entry, kWorkerBudget> entries{};
  detail::NativeWorkerBackend original = detail::DefaultNativeWorkerBackend();
  std::stop_source* stop = nullptr;
  std::size_t attempts = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::atomic<std::size_t> completed = 0;
  std::atomic<bool> later_done = false;
  std::atomic<bool> first_waiting = false;
  bool fail_second = false;
  bool fail_second_position = false;
  bool stop_after_later_position = false;
  static void EntryPoint(detail::NativeWorkerRecord& record) noexcept {
    auto& entry = *static_cast<Entry*>(record.state);
    auto& observer = *entry.observer;
    const bool controlled =
        observer.stop_after_later_position && entry.phase == kPositionPhase;
    if (controlled && record.ordinal == 0) {
      observer.first_waiting.store(true);
      while (!observer.later_done.load()) std::this_thread::yield();
    }
    entry.original->entry(*entry.original);
    if (controlled && record.ordinal == 1) {
      observer.stop->request_stop();
      observer.later_done.store(true);
    }
    ++observer.completed;
  }
  static std::uintptr_t Launch(detail::NativeWorkerRecord* record,
                               void* state) {
    auto& observer = *static_cast<WorkerObserver*>(state);
    ++observer.attempts;
    try {
      const auto phase = (observer.attempts - 1) / kWorkerBudget;
      if (observer.fail_second && observer.attempts == kFailedLaunchOrdinal)
        throw LaunchFailure();
      if (observer.fail_second_position && phase == kPositionPhase &&
          record->ordinal == kWorkerBudget - 1) {
        while (!observer.first_waiting.load()) std::this_thread::yield();
        throw LaunchFailure();
      }
      auto& entry = observer.entries[record->ordinal];
      entry.original = record;
      entry.observer = &observer;
      entry.phase = phase;
      entry.forwarded = {EntryPoint, &entry, record->ordinal};
      const auto handle =
          observer.original.launch(&entry.forwarded, observer.original.state);
      ++observer.launched;
      return handle;
    } catch (...) {
      // Release a controlled first worker before RunRange joins it on unwind,
      // including actual original-backend failures and either injected path.
      observer.later_done.store(true);
      throw;
    }
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& observer = *static_cast<WorkerObserver*>(state);
    observer.original.join(handle, observer.original.state);
    ++observer.joined;
  }
  detail::NativeWorkerBackend Backend() { return {Launch, Join, this}; }
};

void CheckJoined(const mv::GltfTopologyWriteExecutionStats& stats) {
  Require(stats.launched_workers > 0 &&
              stats.launched_workers == stats.joined_workers &&
              stats.planned_jobs >= stats.launched_workers,
          "substantial writer phase did not execute joined workers");
}
void ParallelAndLifetime() {
  Fixture fixture;
  fixture.original_stop.request_stop();
  const auto baseline = fixture.context.ActiveTrackedPayload();
  {
    auto serial = mv::WriteGltfTopologySelection(fixture.Request(), Limits(),
                                                 Work(false));
    auto parallel =
        mv::WriteGltfTopologySelection(fixture.Request(), Limits(), Work(true));
    ReportWriteFailure(serial);
    ReportWriteFailure(parallel);
    Require(serial.bundle && parallel.bundle, "writer rejected valid subset");
    Require(
        std::ranges::equal(serial.bundle->Json(), parallel.bundle->Json()) &&
            std::ranges::equal(serial.bundle->SourceFaces(),
                               parallel.bundle->SourceFaces()) &&
            std::ranges::equal(serial.bundle->SourceCorners(),
                               parallel.bundle->SourceCorners()) &&
            std::ranges::equal(serial.bundle->Resources().back().bytes,
                               parallel.bundle->Resources().back().bytes),
        "serial and parallel output differ");
    CheckJoined(parallel.execution.verification);
    CheckJoined(parallel.execution.index_encoding);
    CheckJoined(parallel.execution.base64_encoding);
    CheckJoined(parallel.execution.output_copy);
    Require(serial.execution.verification.launched_workers == 0 &&
                serial.execution.output_copy.launched_workers == 0,
            "serial policy unexpectedly launched workers");
    auto retained = *parallel.bundle;
    parallel.bundle.reset();
    Require(!retained.Json().empty() &&
                retained.Source().Mesh().View().positions.size() ==
                    kVertexCount * kPositionComponents &&
                retained.OriginalAsset().OriginalJson().size() ==
                    fixture.document.size() &&
                retained.OriginalCorners().size() ==
                    fixture.selected.SourceCorners().size(),
            "copied output lost visible owner or provenance");
  }
  fixture.CheckSource(baseline);
}
void OriginalRequestsAndFaults() {
  Fixture fixture;
  const auto baseline = fixture.context.ActiveTrackedPayload();
  RequestObserver measured{fixture.context, baseline};
  {
    auto result = detail::WriteWithBackend(
        fixture.Request(), Limits(), Work(false), {}, measured.Backend(),
        detail::DefaultNativeWorkerBackend());
    Require(result.bundle && measured.valid && measured.peak > 0 &&
                measured.live == result.bundle->NewRequestedBytes(),
            "original complete writer requests do not match public counter");
  }
  Require(measured.live == 0 && measured.valid,
          "original numeric/control requests freed after charge release");
  fixture.CheckSource(baseline);
  for (const auto cap : {measured.peak, measured.peak - 1}) {
    RequestObserver observed{fixture.context, baseline};
    {
      auto result = detail::WriteWithBackend(
          fixture.Request(), Limits(cap), Work(false), {}, observed.Backend(),
          detail::DefaultNativeWorkerBackend());
      if (cap == measured.peak)
        Require(result.bundle.has_value(), "measured exact peak was rejected");
      else
        Require(
            !result.bundle &&
                result.status == mv::GltfTopologyWriteStatus::kLimitExceeded &&
                result.diagnostic.code ==
                    mv::GltfTopologyWriteDiagnosticCode::kWriterRequestLimit,
            "one byte below original peak did not reject transactionally");
      Require(observed.valid && observed.peak <= cap,
              "original request prefix exceeded the operation cap");
    }
    Require(observed.live == 0 && observed.valid, "capped request leak");
    fixture.CheckSource(baseline);
  }
  for (std::size_t ordinal = 1; ordinal <= measured.requests; ++ordinal) {
    RequestObserver fault{fixture.context, baseline};
    fault.fail_request = ordinal;
    bool threw = false;
    try {
      const auto result = detail::WriteWithBackend(
          fixture.Request(), Limits(), Work(false), {}, fault.Backend(),
          detail::DefaultNativeWorkerBackend());
      (void)result;
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    Require(threw && fault.valid && fault.live == 0,
            "original request fault did not propagate and roll back");
    fixture.CheckSource(baseline);
  }
  std::stop_source stop;
  stop.request_stop();
  RequestObserver canceled{fixture.context, baseline};
  auto result = detail::WriteWithBackend(
      fixture.Request(), Limits(), Work(false), stop.get_token(),
      canceled.Backend(), detail::DefaultNativeWorkerBackend());
  Require(result.status == mv::GltfTopologyWriteStatus::kCanceled &&
              !result.bundle && canceled.requests == 0,
          "pre-stop allocated or published output");
  fixture.CheckSource(baseline);
}
void PartialLaunch() {
  Fixture fixture;
  const auto baseline = fixture.context.ActiveTrackedPayload();
  WorkerObserver workers;
  workers.fail_second = true;
  bool threw = false;
  try {
    const auto result = detail::WriteWithBackend(
        fixture.Request(), Limits(), Work(true), {},
        allocation::DefaultAllocationBackend(), workers.Backend());
    (void)result;
  } catch (const LaunchFailure&) {
    threw = true;
  }
  Require(threw && workers.launched == 1 && workers.joined == 1 &&
              workers.completed.load() == 1,
          "partial actual launch was not joined before exception");
  fixture.CheckSource(baseline);
}
void ControlledPositionPartialLaunch() {
  Fixture fixture;
  const auto baseline = fixture.context.ActiveTrackedPayload();
  std::stop_source stop;
  WorkerObserver workers;
  workers.stop = &stop;
  workers.stop_after_later_position = true;
  workers.fail_second_position = true;
  bool threw = false;
  try {
    const auto result = detail::WriteWithBackend(
        fixture.Request(), Limits(), Work(true), stop.get_token(),
        allocation::DefaultAllocationBackend(), workers.Backend());
    (void)result;
  } catch (const LaunchFailure&) {
    threw = true;
  }
  const auto expected_launched = kPositionPhase * kWorkerBudget + 1;
  Require(threw && workers.first_waiting.load() && workers.later_done.load() &&
              workers.launched == expected_launched &&
              workers.joined == workers.launched &&
              workers.completed.load() == workers.launched,
          "controlled position launch failure stranded a held real worker");
  fixture.CheckSource(baseline);
}
void FirstFailureAfterJoinedCancel() {
  Fixture fixture;
  const auto view = fixture.selected.Candidate().View();
  std::vector<double> positions(view.positions.begin(), view.positions.end());
  positions[0] = 1;
  positions[kLaterPositionScalar] = 1;
  auto changed = geo::MaterializeMesh(
      {std::span<const double>(positions),
       geo::PolygonTopologyInput{view.face_offsets, view.corner_vertices},
       {}},
      GeometryLimits(), GeometryWork(), fixture.context);
  Require(changed.mesh.has_value(), "altered finite candidate capture failed");
  auto inspected = geo::InspectMaterializedMesh(
      *changed.mesh, {kCountLimit, kRequestLimit}, GeometryWork());
  Require(inspected.inspection && inspected.inspection->IsStorageValid(),
          "altered finite candidate inspection failed");
  const auto baseline = fixture.context.ActiveTrackedPayload();
  std::stop_source stop;
  WorkerObserver workers;
  workers.stop = &stop;
  workers.stop_after_later_position = true;
  const auto result = detail::WriteWithBackend(
      {fixture.source, *inspected.inspection, fixture.selected.SourceFaces(),
       fixture.selected.SourceCorners()},
      Limits(), Work(true), stop.get_token(),
      allocation::DefaultAllocationBackend(), workers.Backend());
  Require(result.status == mv::GltfTopologyWriteStatus::kPreservationMismatch &&
              result.diagnostic.code ==
                  mv::GltfTopologyWriteDiagnosticCode::kPositionBits &&
              result.diagnostic.element == 0 && !result.bundle &&
              workers.later_done.load() && workers.launched >= kWorkerBudget &&
              workers.launched == workers.joined &&
              workers.completed.load() == workers.launched,
          "late cancel displaced the deterministic first position failure");
  fixture.CheckSource(baseline);
}
void SharedLedgerSaturation() {
  Fixture fixture;
  const auto input_baseline = fixture.context.ActiveTrackedPayload();
  {
    auto first = mv::WriteGltfTopologySelection(fixture.Request(), Limits(),
                                                Work(false));
    Require(first.bundle.has_value(), "first retained writer output failed");
    const auto retained_json = std::vector<std::byte>(
        first.bundle->Json().begin(), first.bundle->Json().end());
    const auto retained_resource =
        std::vector<std::byte>(first.bundle->Resources().back().bytes.begin(),
                               first.bundle->Resources().back().bytes.end());
    const auto retained_live = first.bundle->NewRequestedBytes();
    const auto retained_baseline = fixture.context.ActiveTrackedPayload();
    Require(retained_live > 0 &&
                retained_baseline == input_baseline + retained_live &&
                retained_baseline < kPayloadBudget,
            "retained first output does not share the authoritative ledger");
    {
      auto foreign = geo::TryReservePayload(fixture.context,
                                            kPayloadBudget - retained_baseline);
      Require(
          foreign.status == geo::ReservationStatus::kAccepted &&
              fixture.context.ActiveTrackedPayload() == kPayloadBudget,
          "independent reservation did not saturate the same public ledger");
      RequestObserver observed{fixture.context, kPayloadBudget};
      const auto rejected = detail::WriteWithBackend(
          fixture.Request(), Limits(), Work(false), {}, observed.Backend(),
          detail::DefaultNativeWorkerBackend());
      Require(
          !rejected.bundle &&
              rejected.status == mv::GltfTopologyWriteStatus::kBudgetExceeded &&
              rejected.diagnostic.code ==
                  mv::GltfTopologyWriteDiagnosticCode::kRequestBudget &&
              observed.requests == 0 && observed.live == 0,
          "saturated shared ledger did not reject before original allocation");
      Require(first.bundle->NewRequestedBytes() == retained_live &&
                  std::ranges::equal(first.bundle->Json(), retained_json) &&
                  std::ranges::equal(first.bundle->Resources().back().bytes,
                                     retained_resource),
              "second writer rejection changed the earlier retained output");
      fixture.CheckSource(kPayloadBudget);
    }
    fixture.CheckSource(retained_baseline);
  }
  fixture.CheckSource(input_baseline);
}

struct TranslationStopBackend {
  struct Slot {
    detail::NativeWorkerRecord original;
    std::stop_source* stop = nullptr;
  };
  std::array<Slot, gltf_translation_output_fixture::kJobs> slots;
  std::stop_source stop;
  detail::NativeWorkerBackend real = detail::DefaultNativeWorkerBackend();
  std::size_t launched = 0;
  std::size_t joined = 0;
  static void Invoke(detail::NativeWorkerRecord& record) noexcept {
    auto& slot = *static_cast<Slot*>(record.state);
    slot.original.entry(slot.original);
    // An actual native callback has run; cancellation is requested inside the
    // validator, before proof comparison/ordinary output construction.
    slot.stop->request_stop();
  }
  static std::uintptr_t Launch(detail::NativeWorkerRecord* record,
                               void* opaque) {
    auto& self = *static_cast<TranslationStopBackend*>(opaque);
    if (record->ordinal >= self.slots.size())
      throw std::runtime_error("translation fixture worker slot extent");
    auto& slot = self.slots[record->ordinal];
    slot.original = *record;
    slot.stop = &self.stop;
    record->entry = Invoke;
    record->state = &slot;
    const auto handle = self.real.launch(record, self.real.state);
    ++self.launched;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* opaque) noexcept {
    auto& self = *static_cast<TranslationStopBackend*>(opaque);
    self.real.join(handle, self.real.state);
    ++self.joined;
  }
};
void DirectTranslationWriting() {
  namespace moving = gltf_translation_output_fixture;
  geo::ExecutionContext context({4, 1, 2 * moving::kMiB});
  for (bool step : {false, true})
    for (bool identity : {false, true})
      for (bool serial : {false, true}) {
        const auto source = moving::Source(context, step);
        const auto candidate = moving::Candidate(source, identity);
        const auto result = mv::WriteGltfTopologySelection(
            {source, candidate, moving::Faces(identity),
             moving::Corners(identity)},
            moving::WriteLimits(), moving::WriteWork(serial));
        Require(result.status == mv::GltfTopologyWriteStatus::kSuccess &&
                    result.bundle && result.direct_translation &&
                    result.direct_translation->complete,
                "real moving writer revalidation and returned owner");
        moving::CheckLiteral(*result.bundle, identity, step);
        Require(
            result.bundle->DirectTranslationFacts() &&
                result.bundle->DirectTranslationFacts()->keys_checked == 2 &&
                result.direct_translation->work.parallel_min_keys ==
                    moving::TranslationWork(serial).parallel_min_keys &&
                result.direct_translation->execution.launched_workers ==
                    result.direct_translation->execution.joined_workers,
            "actual unique sampler keys, explicit W work and joined workers");
        if (serial)
          Require(result.direct_translation->execution.launched_workers == 0,
                  "explicit serial W selection");
      }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "moving writer owns and releases every source/output request");
  {
    const auto source = moving::Source(context);
    const auto candidate = moving::Candidate(source);
    const mv::GltfTopologyWriteRequest request{
        source, candidate, moving::kChangedFaces, moving::kChangedCorners};
    auto invalid_work = moving::WriteWork();
    invalid_work.direct_translation.max_jobs = 0;
    const auto invalid = mv::WriteGltfTopologySelection(
        request, moving::WriteLimits(), invalid_work);
    Require(!invalid.bundle &&
                invalid.status ==
                    mv::GltfTopologyWriteStatus::kInvalidConfiguration &&
                invalid.diagnostic.stage ==
                    mv::GltfTopologyWriteStage::kAdmission &&
                !invalid.direct_translation,
            "new W work is admitted before helper entry");
    geo::ExecutionContext other_context({4, 1, 2 * moving::kMiB});
    const auto other_source = moving::Source(other_context);
    const auto other_candidate = moving::Candidate(other_source);
    const auto foreign_candidate = mv::WriteGltfTopologySelection(
        {source, other_candidate, moving::kChangedFaces,
         moving::kChangedCorners},
        moving::WriteLimits(), moving::WriteWork());
    Require(!foreign_candidate.bundle &&
                foreign_candidate.status ==
                    mv::GltfTopologyWriteStatus::kForeignBudget &&
                !foreign_candidate.direct_translation,
            "foreign real candidate ledger refuses before W validation");
    constexpr std::uint64_t kSmallOwnerBytes = 4 * moving::kMiB;
    auto contained_limits = moving::WriteLimits();
    contained_limits.output.max_output_requested_bytes = kSmallOwnerBytes;
    auto contained = mv::WriteGltfTopologySelection(request, contained_limits,
                                                    moving::WriteWork());
    Require(contained.bundle && contained.direct_translation &&
                contained.direct_translation->complete &&
                contained.direct_translation->effective_request_cap > 0 &&
                contained.direct_translation->effective_request_cap <
                    kSmallOwnerBytes &&
                contained_limits.direct_translation.max_new_requested_bytes ==
                    8 * moving::kMiB &&
                contained.bundle->NewRequestedBytes() <= kSmallOwnerBytes,
            "positive small W owner cap contains transient E below unchanged "
            "validator subcap");
    contained.bundle.reset();
    auto limits = moving::WriteLimits();
    limits.direct_translation.max_total_keys = 1;
    auto result =
        mv::WriteGltfTopologySelection(request, limits, moving::WriteWork());
    Require(!result.bundle &&
                result.status == mv::GltfTopologyWriteStatus::kLimitExceeded &&
                result.diagnostic.code ==
                    mv::GltfTopologyWriteDiagnosticCode::kCountLimit &&
                result.diagnostic.stage ==
                    mv::GltfTopologyWriteStage::kVerification &&
                result.direct_translation &&
                !result.direct_translation->complete,
            "W one-under logical keys maps to count rather than unsupported "
            "profile");
    limits = moving::WriteLimits();
    limits.direct_translation.max_new_requested_bytes = 1;
    result =
        mv::WriteGltfTopologySelection(request, limits, moving::WriteWork());
    Require(!result.bundle &&
                result.status == mv::GltfTopologyWriteStatus::kLimitExceeded &&
                result.diagnostic.code ==
                    mv::GltfTopologyWriteDiagnosticCode::kWriterRequestLimit,
            "W actual transient request seed refuses under its own cap");
    limits = moving::WriteLimits();
    limits.output.max_output_requested_bytes = 1;
    result =
        mv::WriteGltfTopologySelection(request, limits, moving::WriteWork());
    Require(!result.bundle &&
                result.status == mv::GltfTopologyWriteStatus::kLimitExceeded &&
                !result.direct_translation,
            "tiny whole-operation cap refuses before validator spending");
    limits = moving::WriteLimits();
    limits.output.max_json_bytes = 1;
    result =
        mv::WriteGltfTopologySelection(request, limits, moving::WriteWork());
    Require(!result.bundle &&
                result.status == mv::GltfTopologyWriteStatus::kLimitExceeded &&
                result.diagnostic.stage ==
                    mv::GltfTopologyWriteStage::kLexicalPlanning &&
                result.direct_translation &&
                result.direct_translation->complete,
            "completed W facts retained through genuine later output-count "
            "failure");
    TranslationStopBackend stop_backend;
    auto stop_work = moving::WriteWork();
    // Existing geometric verification fits one byte partition, so this backend
    // reaches native launches only for the explicit two-key validator schedule.
    stop_work.partition_bytes = moving::kMiB;
    const auto stopped = detail::WriteWithBackend(
        request, moving::WriteLimits(), stop_work,
        stop_backend.stop.get_token(), allocation::DefaultAllocationBackend(),
        {TranslationStopBackend::Launch, TranslationStopBackend::Join,
         &stop_backend});
    Require(
        !stopped.bundle &&
            stopped.status == mv::GltfTopologyWriteStatus::kCanceled &&
            stopped.diagnostic.stage ==
                mv::GltfTopologyWriteStage::kVerification &&
            stopped.direct_translation &&
            !stopped.direct_translation->complete &&
            stop_backend.launched > 0 &&
            stop_backend.launched == stop_backend.joined &&
            stopped.direct_translation->execution.launched_workers ==
                stop_backend.launched &&
            stopped.direct_translation->execution.joined_workers ==
                stop_backend.joined &&
            context.ActiveWorkers() == 0,
        "real validator callback stop preserves actual joined/partial facts");
    using Owner = std::vector<std::byte, allocation::Allocator<std::byte>>;
    constexpr std::byte kPayload{0x5a};
    constexpr std::size_t kRemaining = 1;
    const auto occupy = [&](const geo::ExecutionContext& ledger) {
      const auto available =
          ledger.TrackedPayloadBudget() - ledger.ActiveTrackedPayload();
      const auto prefix =
          allocation::AllocationRequestBytes(1, alignof(std::byte)) - 1;
      Require(available > prefix + kRemaining,
              "owned pressure construction headroom");
      const auto count = available - prefix - kRemaining;
      const auto charge =
          allocation::AllocationRequestBytes(count, alignof(std::byte));
      const auto before = ledger.ActiveTrackedPayload();
      const allocation::AllocationContext allocator(ledger, {});
      Owner owner(count, kPayload, allocation::Allocator<std::byte>(allocator));
      Require(
          owner.size() == count && owner.capacity() == count &&
              owner.front() == kPayload && owner.back() == kPayload &&
              ledger.ActiveTrackedPayload() - before == charge &&
              ledger.TrackedPayloadBudget() - ledger.ActiveTrackedPayload() ==
                  kRemaining &&
              owner.get_allocator().Context().SharesExecutionBudget(allocator),
          "genuine owned payload/ledger/charge validated before workflow "
          "expectation");
      return owner;
    };
    const auto baseline = context.ActiveTrackedPayload();
    geo::ExecutionContext foreign({4, 1, 2 * moving::kMiB});
    {
      const auto unrelated = occupy(foreign);
      result = mv::WriteGltfTopologySelection(request, moving::WriteLimits(),
                                              moving::WriteWork());
      Require(result.bundle.has_value(),
              "occupied foreign ledger does not charge W");
      result.bundle.reset();
      {
        const auto pressure = occupy(context);
        result = mv::WriteGltfTopologySelection(request, moving::WriteLimits(),
                                                moving::WriteWork());
        Require(
            !result.bundle &&
                result.status == mv::GltfTopologyWriteStatus::kBudgetExceeded &&
                result.diagnostic.code ==
                    mv::GltfTopologyWriteDiagnosticCode::kRequestBudget,
            "real occupied same ledger refuses independently from roomy "
            "operation caps");
      }
      Require(context.ActiveTrackedPayload() == baseline,
              "actual pressure freed");
      result = mv::WriteGltfTopologySelection(request, moving::WriteLimits(),
                                              moving::WriteWork());
      Require(result.bundle && result.direct_translation->complete,
              "same unchanged caps succeed after owned pressure release");
    }
    result.bundle.reset();
    Require(foreign.ActiveTrackedPayload() == 0 &&
                context.ActiveTrackedPayload() == baseline,
            "foreign backend/retained source lifetimes release correctly");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "all moving request control owners/workers released");
}

}  // namespace
int main() {
  try {
    DirectTranslationWriting();
    UnsupportedSourceHasNoBridge();
    ParallelAndLifetime();
    OriginalRequestsAndFaults();
    SharedLedgerSaturation();
    PartialLaunch();
    ControlledPositionPartialLaunch();
    FirstFailureAfterJoinedCancel();
    std::cout
        << "topology writer workers, original requests and rollback passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
