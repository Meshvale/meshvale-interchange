// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "gltf_workflow_inspection_support.h"
#include "gltf_workflow_support_test_internal.h"

namespace gltf_workflow_inspection {
namespace {
namespace allocation = mv::gltf_detail;
namespace detail = mv::gltf_workflow_support_detail;
constexpr std::size_t kRecorderCapacity = 4096;
constexpr std::size_t kSkewVertices = 8190;
constexpr std::size_t kOpaqueBytes = 65536;
constexpr std::size_t kBytesPerShortRow = 4;
constexpr std::size_t kFailLaunchOrdinal = 2;
struct Block {
  void* pointer = nullptr;
  std::size_t bytes = 0;
  std::size_t alignment = 0;
};
struct Requests {
  explicit Requests(const geo::ExecutionContext& context)
      : context(context), baseline(context.ActiveTrackedPayload()) {}
  const geo::ExecutionContext& context;
  const std::size_t baseline;
  allocation::AllocationBackend original =
      allocation::DefaultAllocationBackend();
  std::array<Block, kRecorderCapacity> blocks{};
  std::mutex mutex;
  std::size_t calls = 0;
  std::size_t live = 0;
  std::size_t peak = 0;
  std::size_t fail = 0;
  bool fault_entered = false;
  bool admission = true;
  bool frees = true;
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Requests*>(state);
    std::lock_guard lock(self.mutex);
    ++self.calls;
    Require(bytes <= std::numeric_limits<std::size_t>::max() - self.live,
            "original request observation extent");
    self.admission &= self.context.ActiveTrackedPayload() >=
                      self.baseline + self.live + bytes;
    if (self.calls == self.fail) {
      self.fault_entered = true;
      throw std::bad_alloc();
    }
    const auto slot = std::ranges::find_if(self.blocks, [](const auto& block) {
      return block.pointer == nullptr;
    });
    Require(slot != self.blocks.end(),
            "fixed original request observation capacity");
    void* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    *slot = {pointer, bytes, alignment};
    self.live += bytes;
    self.peak = std::max(self.peak, self.live);
    return pointer;
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Requests*>(state);
    std::lock_guard lock(self.mutex);
    const auto slot = std::ranges::find_if(
        self.blocks,
        [pointer](const auto& block) { return block.pointer == pointer; });
    self.frees &= slot != self.blocks.end();
    if (slot == self.blocks.end()) return;
    self.frees &=
        slot->bytes == bytes && slot->alignment == alignment &&
        self.context.ActiveTrackedPayload() >= self.baseline + self.live;
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    self.frees &=
        self.context.ActiveTrackedPayload() >= self.baseline + self.live;
    self.live -= slot->bytes;
    *slot = {};
  }
  allocation::AllocationBackend Backend() { return {Allocate, Free, this}; }
  void Check(std::uint64_t bytes) const {
    Require(admission && frees && live == bytes &&
                context.ActiveTrackedPayload() == baseline + live,
            "all original inspection allocations admitted and exactly "
            "attributable");
    Require(context.ActiveWorkers() == 0,
            "inspection observer sees no live workers after return");
  }
};
struct Workers {
  std::atomic<std::size_t> launched{0};
  std::atomic<std::size_t> joined{0};
  std::atomic<std::size_t> entered{0};
  std::stop_source* cancel = nullptr;
  static void Launched(std::size_t, void* state) noexcept {
    ++static_cast<Workers*>(state)->launched;
  }
  static void Joined(std::size_t, void* state) noexcept {
    ++static_cast<Workers*>(state)->joined;
  }
  static void Entered(std::size_t, void* state) noexcept {
    auto& self = *static_cast<Workers*>(state);
    ++self.entered;
    if (self.cancel) self.cancel->request_stop();
  }
  allocation::OperationWorkerObserver Observer() {
    return {Launched, Joined, Entered, this};
  }
  void Check(bool positive) const {
    Require(launched == joined,
            "actual native observer launches and joins match");
    if (positive)
      Require(launched > 0 && entered > 0, "real native entries observed");
  }
};
class LaunchFailure : public std::exception {};
struct FailLaunch {
  allocation::NativeWorkerBackend original =
      allocation::DefaultNativeWorkerBackend();
  std::size_t calls = 0;
  static std::uintptr_t Launch(allocation::NativeWorkerRecord* record,
                               void* state) {
    auto& self = *static_cast<FailLaunch*>(state);
    if (++self.calls == kFailLaunchOrdinal) throw LaunchFailure();
    return self.original.launch(record, self.original.state);
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& self = *static_cast<FailLaunch*>(state);
    self.original.join(handle, self.original.state);
  }
  allocation::NativeWorkerBackend Backend() { return {Launch, Join, this}; }
};
authored::Fixture SkewFixture() {
  authored::Fixture fixture;
  fixture.kind = authored::FixtureKind::kTriangles;
  fixture.primitive_ordinal = 0;
  fixture.source_mode = 4;
  fixture.vertex_count = kSkewVertices;
  const auto buffer_bytes = kSkewVertices * kBytesPerShortRow;
  fixture.json =
      R"({"asset":{"version":"2.0"},"extras":{"opaque":")" +
      std::string(kOpaqueBytes, 'x') +
      R"("},"buffers":[{"uri":"skew.bin","byteLength":)" +
      std::to_string(buffer_bytes) +
      R"(}],"bufferViews":[{"buffer":0,"byteLength":)" +
      std::to_string(buffer_bytes) +
      R"(}],"accessors":[{"componentType":5126,"type":"VEC3","count":8190,"min":[0,0,0],"max":[0,0,0]},{"bufferView":0,"componentType":5122,"type":"VEC2","normalized":true,"count":8190}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"_SHORT":1}}]}]})";
  fixture.external_buffer.resize(buffer_bytes + kOpaqueBytes, std::byte{0xa5});
  constexpr std::array<std::byte, kBytesPerShortRow> kExtrema{
      std::byte{0}, std::byte{0x80}, std::byte{0xff}, std::byte{0x7f}};
  for (std::size_t row = 0; row < kSkewVertices; ++row) {
    std::ranges::copy(
        kExtrema, fixture.external_buffer.begin() + row * kBytesPerShortRow);
    fixture.source_triangles.push_back(static_cast<std::uint32_t>(row));
    fixture.stream_corners.push_back(row);
  }
  return fixture;
}
}  // namespace
void CheckRequests() {
  const auto context = Context();
  const auto fixture = SkewFixture();
  const auto source = Source(fixture, context);
  std::vector<geo::index_t> faces;
  std::vector<geo::index_t> corners;
  constexpr std::size_t kTriangleWidth = 3;
  for (std::size_t face = 0; face < kSkewVertices / kTriangleWidth; face += 2) {
    faces.push_back(face);
    for (std::size_t corner = 0; corner < kTriangleWidth; ++corner)
      corners.push_back(face * kTriangleWidth + corner);
  }
  const auto selection = geo::SelectMeshFaces(
      source.Mesh(), faces, TransferLimits().materialization, GeometryWork());
  Require(selection.selection.has_value(), "skew independent face selection");
  const auto candidate = Inspect(selection.selection->Candidate());
  const auto output = Write(source, candidate, faces, corners);
  const auto reload = Reload(output, context);
  const auto extraction = Extract(reload, 0, false);
  const mv::GltfTopologyOutputInspectionRequest request{
      source, candidate, faces, corners, output, reload, extraction};
  const auto input_bytes = context.ActiveTrackedPayload();
  std::size_t peak = 0;
  std::size_t calls = 0;
  const auto attempt = [&](Requests& requests, Workers& workers,
                           std::uint64_t cap, std::stop_token stop = {},
                           allocation::NativeWorkerBackend backend =
                               allocation::DefaultNativeWorkerBackend()) {
    auto limits = Limits();
    limits.max_new_requested_bytes = cap;
    const auto observer = workers.Observer();
    return detail::InspectGltfTopologyOutputForTest(
        request, limits, Work(), stop,
        {requests.Backend(), backend, &observer});
  };
  {
    Requests requests(context);
    Workers workers;
    auto result = attempt(requests, workers, kRequestBytes);
    CheckComplete(result, false);
    requests.Check(result.inspection->NewRequestedBytes());
    peak = requests.peak;
    calls = requests.calls;
    workers.Check(true);
    const auto& e = result.execution;
    for (const auto phase :
         std::array{e.source_candidate, e.correspondence, e.lexical,
                    e.resources, e.reloaded_primitive, e.provenance})
      Require(phase.launched_workers > 0 &&
                  phase.launched_workers == phase.joined_workers,
              "all six substantial inspection phases have real joined native "
              "workers");
    const auto bytes = result.inspection->NewRequestedBytes();
    auto copy = *result.inspection;
    result.inspection.reset();
    requests.Check(bytes);
  }
  for (const auto cap : {peak, peak - 1}) {
    Requests requests(context);
    Workers workers;
    auto result = attempt(requests, workers, cap);
    Require(result.inspection.has_value() == (cap == peak),
            "inspection whole original-request peak boundary");
    if (cap != peak)
      Require(result.status == Status::kLimitExceeded &&
                  result.diagnostic.code == Code::kInspectionRequestLimit,
              "one byte below actual original-request peak is refused");
    result.inspection.reset();
    workers.Check(cap == peak);
    requests.Check(0);
  }
  {
    auto occupied =
        geo::TryReservePayload(context, context.TrackedPayloadBudget() -
                                            context.ActiveTrackedPayload() - 1);
    Require(occupied.status == geo::ReservationStatus::kAccepted,
            "concurrent stage reserves the shared ledger's remaining bytes");
    CheckRejected(request, Status::kBudgetExceeded, Code::kRequestBudget,
                  Stage::kAdmission);
  }
  for (std::size_t ordinal = 1; ordinal <= calls; ++ordinal) {
    Requests requests(context);
    requests.fail = ordinal;
    Workers workers;
    bool caught = false;
    try {
      (void)attempt(requests, workers, kRequestBytes);
    } catch (const std::bad_alloc&) {
      caught = true;
    }
    Require(caught && requests.fault_entered,
            "every actual inspector allocation ordinal fault propagates");
    workers.Check(false);
    requests.Check(0);
  }
  {
    Requests requests(context);
    Workers workers;
    FailLaunch failing;
    bool caught = false;
    try {
      (void)attempt(requests, workers, kRequestBytes, {}, failing.Backend());
    } catch (const LaunchFailure&) {
      caught = true;
    }
    Require(
        caught && failing.calls == kFailLaunchOrdinal && workers.launched == 1,
        "second native launch exception propagates after first real launch");
    workers.Check(true);
    requests.Check(0);
  }
  // A separate caller holds a real successful inspection while another fresh
  // inspection competes on the same exact ledger. Its independent fresh cap is
  // generous; the retained first owner's bytes cause shared-ledger refusal.
  {
    const geo::ExecutionContext overlapping(
        {kWorkerBudget, 1, input_bytes + peak});
    const auto overlap_source = Source(fixture, overlapping);
    const auto overlap_selection =
        geo::SelectMeshFaces(overlap_source.Mesh(), faces,
                             TransferLimits().materialization, GeometryWork());
    Require(overlap_selection.selection.has_value(),
            "overlap candidate capture");
    const auto overlap_candidate =
        Inspect(overlap_selection.selection->Candidate());
    const auto overlap_output =
        Write(overlap_source, overlap_candidate, faces, corners);
    const auto overlap_reload = Reload(overlap_output, overlapping);
    const auto overlap_extraction = Extract(overlap_reload, 0, false);
    const mv::GltfTopologyOutputInspectionRequest overlap_request{
        overlap_source, overlap_candidate, faces, corners, overlap_output,
        overlap_reload, overlap_extraction};
    Require(overlapping.ActiveTrackedPayload() == input_bytes,
            "overlap reproduces exact live input requests");
    std::barrier phases(2);
    bool accepted = false;
    std::exception_ptr first_exception;
    std::jthread first([&] {
      std::optional<mv::GltfTopologyOutputInspection> held;
      try {
        const auto result =
            mv::InspectGltfTopologyOutput(overlap_request, Limits(), Work());
        accepted = result.inspection.has_value();
        held = result.inspection;
      } catch (...) {
        first_exception = std::current_exception();
      }
      phases.arrive_and_wait();
      phases.arrive_and_wait();
    });
    phases.arrive_and_wait();
    const auto before_second = overlapping.ActiveTrackedPayload();
    std::optional<mv::GltfTopologyOutputInspectionResult> second;
    std::exception_ptr second_exception;
    try {
      second = mv::InspectGltfTopologyOutput(overlap_request, Limits(), Work());
    } catch (...) {
      second_exception = std::current_exception();
    }
    const auto after_second = overlapping.ActiveTrackedPayload();
    phases.arrive_and_wait();
    first.join();
    if (first_exception) std::rethrow_exception(first_exception);
    if (second_exception) std::rethrow_exception(second_exception);
    Require(accepted && before_second > input_bytes && !second->inspection &&
                second->status == Status::kBudgetExceeded &&
                second->diagnostic.code == Code::kRequestBudget &&
                after_second == before_second &&
                overlapping.ActiveTrackedPayload() == input_bytes &&
                overlapping.ActiveWorkers() == 0,
            "concurrent retained inspection shares the ledger, but stays "
            "outside a fresh cap");
  }
  {
    Requests requests(context);
    Workers workers;
    std::stop_source stop;
    workers.cancel = &stop;
    const auto result =
        attempt(requests, workers, kRequestBytes, stop.get_token());
    Require(!result.inspection && result.status == Status::kCanceled &&
                result.diagnostic.code == Code::kStopRequested,
            "cancellation from actual native job publishes no partial owner");
    workers.Check(true);
    requests.Check(0);
  }
  std::cout << "original inspection request peak " << peak << ", ordinals "
            << calls << '\n';
}
}  // namespace gltf_workflow_inspection
