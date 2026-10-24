// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "finite_double_format.h"
#include "gltf_allocation.h"
#include "ply_fixture.h"
#include "ply_test_internal.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::ply_detail;
namespace f = ic::ply_fixture;
using f::Require;
constexpr std::uint64_t kContextBytes = 64 * f::kPhysicalLimit;
constexpr std::size_t kBackendBlocks = 4096;
constexpr std::size_t kFaultAttemptLimit = 1024;
struct RuntimeMarker {};
struct Backend {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
  };
  std::array<Block, kBackendBlocks> blocks{};
  ic::gltf_detail::AllocationBackend original =
      ic::gltf_detail::DefaultAllocationBackend();
  std::size_t requests = 0, live = 0, fail_at = 0;
  std::size_t before_last_allocation = 0, last_allocation_bytes = 0;
  bool runtime_failure = false, frozen = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Backend*>(state);
    ++self.requests;
    if (self.frozen || (self.fail_at != 0 && self.requests == self.fail_at)) {
      if (self.runtime_failure) throw RuntimeMarker{};
      throw std::bad_alloc{};
    }
    self.before_last_allocation = self.live;
    self.last_allocation_bytes = bytes;
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks)
      if (!block.pointer) {
        block = {pointer, bytes};
        self.live += bytes;
        return pointer;
      }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::bad_alloc{};
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Backend*>(state);
    for (auto& block : self.blocks)
      if (block.pointer == pointer) {
        if (block.bytes != bytes) std::terminate();
        self.live -= bytes;
        block = {};
        self.original.deallocate(pointer, bytes, alignment,
                                 self.original.state);
        return;
      }
    std::terminate();
  }
  detail::TestOptions Options() {
    return {{&Allocate, &Free, this}, nullptr, nullptr};
  }
};
void Empty(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "no payload/workers after final owner/failure");
}

void Admission() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  Backend backend;
  const auto wire = f::Wire(ic::PlyEncoding::kAscii);
  auto limits = f::ReadLimits();
  limits.max_source_bytes = wire.size() - 1;
  auto result = detail::ReadPlyForTest(wire, limits, f::Work(), context, {},
                                       backend.Options());
  Require(result.status == ic::PlyStatus::kLimitExceeded &&
              result.diagnostic.code == ic::PlyDiagnosticCode::kSourceLimit &&
              !result.document && backend.requests == 0,
          "source extent checked before allocation");
  limits = f::ReadLimits();
  --limits.max_total_new_requested_bytes;
  result = detail::ReadPlyForTest(wire, limits, f::Work(), context, {},
                                  backend.Options());
  Require(result.status == ic::PlyStatus::kInvalidConfiguration &&
              backend.requests == 0,
          "declared cap sum validated before work");
  limits = f::ReadLimits();
  limits.max_list_items_per_row = 0;
  result = detail::ReadPlyForTest(wire, limits, f::Work(), context, {},
                                  backend.Options());
  Require(result.status == ic::PlyStatus::kInvalidConfiguration &&
              backend.requests == 0,
          "all required positive caps");
  auto work = f::Work();
  work.cancellation_block_values = 0;
  result = detail::ReadPlyForTest(wire, f::ReadLimits(), work, context, {},
                                  backend.Options());
  Require(result.status == ic::PlyStatus::kInvalidConfiguration &&
              backend.requests == 0,
          "all required positive work fields");
  std::stop_source stop;
  stop.request_stop();
  result = detail::ReadPlyForTest(wire, f::ReadLimits(), f::Work(), context,
                                  stop.get_token(), backend.Options());
  Require(result.status == ic::PlyStatus::kCanceled && !result.document &&
              backend.requests == 0,
          "prestop allocates nothing");
  struct Cap {
    std::uint64_t ic::PlyReadLimits::* field;
    std::uint64_t value;
    ic::PlyDiagnosticCode code;
  };
  const std::array caps{
      Cap{&ic::PlyReadLimits::max_header_bytes,
          f::Header(ic::PlyEncoding::kAscii).size() - 1,
          ic::PlyDiagnosticCode::kHeaderLimit},
      Cap{&ic::PlyReadLimits::max_header_lines, 19,
          ic::PlyDiagnosticCode::kHeaderLineLimit},
      Cap{&ic::PlyReadLimits::max_line_bytes, 16,
          ic::PlyDiagnosticCode::kLineLimit},
      Cap{&ic::PlyReadLimits::max_token_bytes, 3,
          ic::PlyDiagnosticCode::kTokenLimit},
      Cap{&ic::PlyReadLimits::max_comment_bytes, 22,
          ic::PlyDiagnosticCode::kCommentLimit},
      Cap{&ic::PlyReadLimits::max_name_bytes, 13,
          ic::PlyDiagnosticCode::kNameLimit},
      Cap{&ic::PlyReadLimits::max_total_name_bytes, f::kNameBytes - 1,
          ic::PlyDiagnosticCode::kTotalNameLimit},
      Cap{&ic::PlyReadLimits::max_properties, f::kPropertyCount - 1,
          ic::PlyDiagnosticCode::kPropertyLimit},
      Cap{&ic::PlyReadLimits::max_vertices, 7,
          ic::PlyDiagnosticCode::kVertexLimit},
      Cap{&ic::PlyReadLimits::max_faces, 2, ic::PlyDiagnosticCode::kFaceLimit},
      Cap{&ic::PlyReadLimits::max_corners, 10,
          ic::PlyDiagnosticCode::kCornerLimit},
      Cap{&ic::PlyReadLimits::max_corners_per_face, 3,
          ic::PlyDiagnosticCode::kPerFaceCornerLimit},
      Cap{&ic::PlyReadLimits::max_list_rows, f::kListRows - 1,
          ic::PlyDiagnosticCode::kListRowLimit},
      Cap{&ic::PlyReadLimits::max_list_items_per_row, 3,
          ic::PlyDiagnosticCode::kListItemLimit},
      Cap{&ic::PlyReadLimits::max_property_values, f::kPropertyValues - 1,
          ic::PlyDiagnosticCode::kPropertyValueLimit}};
  for (const auto& cap : caps) {
    limits = f::ReadLimits();
    limits.*cap.field = cap.value;
    result = ic::ReadPly(wire, limits, f::Work(), context);
    Require(result.status == ic::PlyStatus::kLimitExceeded &&
                !result.document && result.diagnostic.code == cap.code,
            "each independent real descriptor/row/value cap refuses");
    Empty(context);
  }
}

void Ownership() {
  Backend input, output;
  struct Seal {
    Backend* backend;
    bool observed = false;
    static void Check(ic::PlyStage stage, std::uint64_t, void* state) noexcept {
      auto& self = *static_cast<Seal*>(state);
      if (stage == ic::PlyStage::kPublication) {
        self.observed = true;
        self.backend->frozen = true;
      }
    }
    detail::TestOptions Options() {
      auto value = backend->Options();
      value.checkpoint = &Check;
      value.state = this;
      return value;
    }
  } read_seal{&input}, write_seal{&output};
  std::optional<ic::PlyDocument> document;
  {
    geo::ExecutionContext original({4, 1, kContextBytes});
    const auto bytes = f::Wire(ic::PlyEncoding::kBinaryBigEndian);
    auto result = detail::ReadPlyForTest(bytes, f::ReadLimits(), f::Work(),
                                         original, {}, read_seal.Options());
    Require(result.document && read_seal.observed,
            "real noalloc final read handoff");
    const auto charge = result.document->NewRequestedBytes();
    Require(charge.codec == input.live &&
                charge.materialization ==
                    result.document->Mesh().NewRequestedBytes() &&
                charge.inspection ==
                    result.document->Inspection().NewRequestedBytes() &&
                charge.total ==
                    charge.codec + charge.materialization + charge.inspection &&
                original.ActiveTrackedPayload() == charge.total,
            "physical original codec blocks plus disjoint Geometry charges "
            "equal ledger");
    document = result.document;
  }
  f::Check(*document);
  Require(
      std::ranges::equal(document->OriginalBytes(),
                         f::Wire(ic::PlyEncoding::kBinaryBigEndian)),
      "bytes/schema/inspection work after original source/context destruction");
  const auto requests = input.requests;
  auto copy = document;
  document.reset();
  document = copy;
  copy.reset();
  Require(input.requests == requests, "document copies allocate nothing");
  std::optional<ic::PlyOutput> retained;
  {
    const auto options = f::Options(ic::PlyEncoding::kBinaryLittleEndian);
    auto result = detail::WritePlyForTest(document->Mesh(), *document, options,
                                          f::WriteLimits(), f::Work(), {},
                                          write_seal.Options());
    Require(result.output && write_seal.observed,
            "writer continues through owner-held context and final noalloc "
            "handoff");
    const auto charge = result.output->NewRequestedBytes();
    Require(charge.codec == output.live && charge.total == charge.codec &&
                charge.materialization == 0 && charge.inspection == 0,
            "output retains only its physical codec requests");
    retained = result.output;
  }
  document.reset();
  Require(input.live == 0, "output retains neither schema nor source mesh");
  f::CheckOutput(*retained, ic::PlyEncoding::kBinaryBigEndian,
                 ic::PlyEncoding::kBinaryLittleEndian);
  {
    geo::ExecutionContext next({4, 1, kContextBytes});
    const auto reload =
        ic::ReadPly(retained->Bytes(), f::ReadLimits(), f::Work(), next);
    Require(reload.document.has_value(),
            "output after options/source/context death actually reloads");
    f::Check(*reload.document);
  }
  const auto output_requests = output.requests;
  auto last = retained;
  retained.reset();
  f::CheckOutput(*last, ic::PlyEncoding::kBinaryBigEndian,
                 ic::PlyEncoding::kBinaryLittleEndian);
  last.reset();
  Require(output.live == 0 && output.requests == output_requests,
          "last output release frees original backend with no allocation");
}

struct StopAt {
  std::stop_source source;
  ic::PlyStage stage;
  std::uint64_t minimum;
  bool observed = false;
  static void Check(ic::PlyStage stage, std::uint64_t ordinal,
                    void* state) noexcept {
    auto& self = *static_cast<StopAt*>(state);
    if (stage == self.stage && ordinal >= self.minimum) {
      self.observed = true;
      self.source.request_stop();
    }
  }
  detail::TestOptions Options() {
    auto value = detail::TestOptions{};
    value.checkpoint = &Check;
    value.state = this;
    return value;
  }
};
void FaultsAndCancellation() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  const auto bytes = f::Wire(ic::PlyEncoding::kAscii);
  bool success = false;
  for (std::size_t request = 1; request < kFaultAttemptLimit; ++request) {
    Backend backend;
    backend.fail_at = request;
    try {
      const auto result = detail::ReadPlyForTest(
          bytes, f::ReadLimits(), f::Work(), context, {}, backend.Options());
      Require(result.document.has_value(), "read faults reach actual success");
      success = true;
    } catch (const std::bad_alloc&) {
      Require(backend.requests == request,
              "real read backend allocation index");
    }
    Require(backend.live == 0, "read faults rollback every original block");
    Empty(context);
    if (success) break;
  }
  Require(success, "all codec read allocations exercised");
  Backend unexpected;
  unexpected.fail_at = 1;
  unexpected.runtime_failure = true;
  bool propagated = false;
  try {
    static_cast<void>(detail::ReadPlyForTest(
        bytes, f::ReadLimits(), f::Work(), context, {}, unexpected.Options()));
  } catch (const RuntimeMarker&) {
    propagated = true;
  }
  Require(propagated && unexpected.live == 0,
          "unexpected backend runtime exception propagates");
  Empty(context);
  for (const auto stage : {ic::PlyStage::kScan, ic::PlyStage::kDecode,
                           ic::PlyStage::kPublication}) {
    StopAt stop{
        {}, stage, stage == ic::PlyStage::kPublication ? 0u : 64u, false};
    const auto result =
        detail::ReadPlyForTest(bytes, f::ReadLimits(), f::Work(), context,
                               stop.source.get_token(), stop.Options());
    Require(stop.observed && result.status == ic::PlyStatus::kCanceled &&
                !result.document && result.diagnostic.stage == stage,
            "actual mid/final read stop has no owner");
    Empty(context);
  }
  const auto read = ic::ReadPly(bytes, f::ReadLimits(), f::Work(), context);
  Require(read.document.has_value(), "genuine writer fault source");
  const auto baseline = context.ActiveTrackedPayload();
  success = false;
  for (std::size_t request = 1; request < kFaultAttemptLimit; ++request) {
    Backend backend;
    backend.fail_at = request;
    try {
      const auto result = detail::WritePlyForTest(
          read.document->Mesh(), *read.document,
          f::Options(ic::PlyEncoding::kAscii), f::WriteLimits(), f::Work(), {},
          backend.Options());
      Require(result.output.has_value(), "writer faults reach real success");
      success = true;
    } catch (const std::bad_alloc&) {
      Require(backend.requests == request, "actual write allocation index");
    }
    Require(backend.live == 0 && context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "write faults leave original source only");
    if (success) break;
  }
  Require(success, "all codec write allocations exercised");
  unexpected.requests = 0;
  propagated = false;
  try {
    static_cast<void>(detail::WritePlyForTest(
        read.document->Mesh(), *read.document,
        f::Options(ic::PlyEncoding::kAscii), f::WriteLimits(), f::Work(), {},
        unexpected.Options()));
  } catch (const RuntimeMarker&) {
    propagated = true;
  }
  Require(propagated && unexpected.live == 0 &&
              context.ActiveTrackedPayload() == baseline,
          "unexpected writer exception rolls back and propagates");
  for (const auto stage : {ic::PlyStage::kEncode, ic::PlyStage::kPublication}) {
    StopAt stop{
        {}, stage, stage == ic::PlyStage::kPublication ? 0u : 64u, false};
    const auto result = detail::WritePlyForTest(
        read.document->Mesh(), *read.document,
        f::Options(ic::PlyEncoding::kAscii), f::WriteLimits(), f::Work(),
        stop.source.get_token(), stop.Options());
    Require(stop.observed && result.status == ic::PlyStatus::kCanceled &&
                !result.output && result.diagnostic.stage == stage &&
                context.ActiveTrackedPayload() == baseline,
            "actual bounded/final write stop returns no partial owner");
  }
}

void ExactAndSharedLimits() {
  geo::ExecutionContext schema_context({4, 1, kContextBytes}),
      mesh_context({4, 1, kContextBytes});
  const auto bytes = f::Wire(ic::PlyEncoding::kBinaryLittleEndian);
  auto limits = f::ReadLimits();
  limits.max_source_bytes = bytes.size();
  limits.max_header_bytes =
      f::Header(ic::PlyEncoding::kBinaryLittleEndian).size();
  limits.max_header_lines = 20;
  limits.max_comment_bytes = 23;
  limits.max_name_bytes = 14;
  limits.max_total_name_bytes = f::kNameBytes;
  limits.max_properties = f::kPropertyCount;
  limits.max_vertices = 8;
  limits.max_faces = 3;
  limits.max_corners = 11;
  limits.max_corners_per_face = 4;
  limits.max_list_rows = f::kListRows;
  limits.max_list_items_per_row = 4;
  limits.max_property_values = f::kPropertyValues;
  auto schema = ic::ReadPly(bytes, limits, f::Work(), schema_context);
  auto mesh = ic::ReadPly(bytes, limits, f::Work(), mesh_context);
  Require(schema.document && mesh.document,
          "all literal exact inclusive descriptor/data caps");
  const auto schema_baseline = schema_context.ActiveTrackedPayload(),
             mesh_baseline = mesh_context.ActiveTrackedPayload();
  auto schema_hold = geo::TryReservePayload(
      schema_context, schema_context.TrackedPayloadBudget() - schema_baseline);
  Require(schema_hold.status == geo::ReservationStatus::kAccepted,
          "genuinely occupy independent schema ledger");
  auto output = ic::WritePly(mesh.document->Mesh(), *schema.document,
                             f::Options(ic::PlyEncoding::kBinaryBigEndian),
                             f::WriteLimits(), f::Work());
  Require(output.output &&
              mesh_context.ActiveTrackedPayload() ==
                  mesh_baseline + output.output->NewRequestedBytes().total &&
              schema_context.ActiveTrackedPayload() ==
                  schema_context.TrackedPayloadBudget(),
          "all fresh writer requests derive only supplied mesh ledger despite "
          "fully occupied schema ledger");
  const auto exact_size =
      f::Wire(ic::PlyEncoding::kBinaryBigEndian, {}, true).size();
  Require(output.output->Bytes().size() == exact_size,
          "independent packed output extent");
  output.output.reset();
  schema_hold.lease.Reset();
  auto cap = f::WriteLimits();
  cap.max_output_bytes = exact_size;
  output = ic::WritePly(mesh.document->Mesh(), *schema.document,
                        f::Options(ic::PlyEncoding::kBinaryBigEndian), cap,
                        f::Work());
  Require(output.output.has_value(),
          "exact independently packed output cap succeeds");
  output.output.reset();
  --cap.max_output_bytes;
  output = ic::WritePly(mesh.document->Mesh(), *schema.document,
                        f::Options(ic::PlyEncoding::kBinaryBigEndian), cap,
                        f::Work());
  Require(output.status == ic::PlyStatus::kLimitExceeded && !output.output &&
              output.diagnostic.code == ic::PlyDiagnosticCode::kOutputLimit,
          "one byte less refuses output");
  struct Cap {
    std::uint64_t ic::PlyWriteLimits::* field;
    std::uint64_t value;
  };
  const std::array caps{Cap{&ic::PlyWriteLimits::max_properties, 13},
                        Cap{&ic::PlyWriteLimits::max_total_name_bytes, 111},
                        Cap{&ic::PlyWriteLimits::max_name_bytes, 13},
                        Cap{&ic::PlyWriteLimits::max_vertices, 7},
                        Cap{&ic::PlyWriteLimits::max_faces, 2},
                        Cap{&ic::PlyWriteLimits::max_corners, 10},
                        Cap{&ic::PlyWriteLimits::max_corners_per_face, 3},
                        Cap{&ic::PlyWriteLimits::max_list_rows, 13},
                        Cap{&ic::PlyWriteLimits::max_list_items_per_row, 3},
                        Cap{&ic::PlyWriteLimits::max_property_values, 72},
                        Cap{&ic::PlyWriteLimits::max_header_bytes, 32},
                        Cap{&ic::PlyWriteLimits::max_header_lines, 18},
                        Cap{&ic::PlyWriteLimits::max_line_bytes, 16},
                        Cap{&ic::PlyWriteLimits::max_token_bytes, 3}};
  for (const auto& item : caps) {
    cap = f::WriteLimits();
    cap.*item.field = item.value;
    output = ic::WritePly(mesh.document->Mesh(), *schema.document,
                          f::Options(ic::PlyEncoding::kAscii), cap, f::Work());
    Require(output.status == ic::PlyStatus::kLimitExceeded && !output.output,
            "current writer schema/count/token caps independently enforced");
  }
  auto occupied = geo::TryReservePayload(
      mesh_context, mesh_context.TrackedPayloadBudget() - mesh_baseline);
  Require(occupied.status == geo::ReservationStatus::kAccepted,
          "occupy actual supplied strong mesh headroom");
  output = ic::WritePly(mesh.document->Mesh(), *schema.document,
                        f::Options(ic::PlyEncoding::kAscii), f::WriteLimits(),
                        f::Work());
  Require(output.status == ic::PlyStatus::kBudgetExceeded && !output.output,
          "free schema ledger cannot subsidize occupied supplied mesh ledger");
  const auto refused =
      ic::ReadPly(bytes, f::ReadLimits(), f::Work(), mesh_context);
  Require(refused.status == ic::PlyStatus::kBudgetExceeded && !refused.document,
          "overlapping reader obeys occupied ledger");
  occupied.lease.Reset();
  schema.document.reset();
  mesh.document.reset();
  Empty(schema_context);
  Empty(mesh_context);
  std::array<bool, 2> accepted{};
  std::array<std::exception_ptr, 2> errors{};
  {
    std::array<std::jthread, 2> callers;
    for (std::size_t i = 0; i < 2; ++i)
      callers[i] = std::jthread([&, i] {
        try {
          const auto result =
              ic::ReadPly(bytes, f::ReadLimits(), f::Work(), mesh_context);
          Require(result.document.has_value(), "concurrent genuine reader");
          f::Check(*result.document);
          accepted[i] = true;
        } catch (...) {
          errors[i] = std::current_exception();
        }
      });
  }
  for (std::size_t i = 0; i < 2; ++i) {
    if (errors[i]) std::rethrow_exception(errors[i]);
    Require(accepted[i], "both shared-ledger operations complete");
  }
  Empty(mesh_context);
}

// Independently authored data forces four distinct real relocation paths.
// Its custom rows are ordinary measurements, not imported face references.
std::string GrowthWire() {
  constexpr std::size_t kProperties = 256;
  constexpr std::size_t kItems = 4096;
  constexpr std::size_t kFaces = 2048;
  std::string wire =
      "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty "
      "float y\nproperty float z\n";
  for (std::size_t i = 0; i < kProperties; ++i)
    wire += "property int measure" + std::to_string(i) + "\n";
  wire += "property list uint int samples\nelement face " +
          std::to_string(kFaces) +
          "\nproperty list uchar int vertex_indices\nend_header\n";
  for (std::size_t vertex = 0; vertex < 3; ++vertex) {
    wire += vertex == 0 ? "0 0 0" : (vertex == 1 ? "1 0 0" : "0 1 0");
    for (std::size_t i = 0; i < kProperties; ++i) wire += " 7";
    wire += vertex == 0 ? " 4096" : " 0";
    if (vertex == 0)
      for (std::size_t i = 0; i < kItems; ++i) wire += " -7";
    wire += '\n';
  }
  for (std::size_t i = 0; i < kFaces; ++i) wire += "3 0 1 2\n";
  return wire;
}

enum class GrowthAction { kObserve, kStop, kOccupy, kBadAlloc, kRuntime };
struct GrowthProbe {
  detail::RelocationKind kind;
  GrowthAction action;
  Backend* backend;
  const geo::ExecutionContext* context;
  std::stop_source stop;
  geo::PayloadLease occupied;
  std::size_t baseline;
  bool observed = false, physical_overlap = false, valid_progress = true;
  std::uint64_t previous = 0;
  std::uint64_t stop_copied = 0, stop_total = 0;

  static void Check(detail::RelocationKind kind, ic::PlyStage,
                    std::uint64_t copied, std::uint64_t total,
                    void* state) noexcept {
    auto& self = *static_cast<GrowthProbe*>(state);
    constexpr std::uint64_t kTransferMinimum = 128;
    constexpr std::uint64_t kInteriorStop = 64;
    if (kind != self.kind || total < kTransferMinimum) return;
    const std::uint64_t interval =
        kind == detail::RelocationKind::kProperties
            ? 1u
            : (kind == detail::RelocationKind::kOutputBytes ? 16u : 4u);
    if (copied == 0) self.previous = 0;
    self.valid_progress &= copied <= total && copied >= self.previous &&
                           copied - self.previous <= interval;
    self.previous = copied;
    if (self.observed || copied < kInteriorStop || copied == total) return;
    self.observed = true;
    self.stop_copied = copied;
    self.stop_total = total;
    try {
      const auto width = kind == detail::RelocationKind::kProperties
                             ? sizeof(ic::PlyPropertyView)
                             : (kind == detail::RelocationKind::kAttributeValues
                                    ? sizeof(std::int32_t)
                                    : (kind == detail::RelocationKind::kCorners
                                           ? sizeof(geo::index_t)
                                           : sizeof(std::byte)));
      const auto alignment =
          kind == detail::RelocationKind::kProperties
              ? alignof(ic::PlyPropertyView)
              : (kind == detail::RelocationKind::kAttributeValues
                     ? alignof(std::int32_t)
                     : (kind == detail::RelocationKind::kCorners
                            ? alignof(geo::index_t)
                            : alignof(std::byte)));
      const auto old_request = ic::gltf_detail::AllocationRequestBytes(
          static_cast<std::size_t>(total) * width, alignment);
      bool original_block_live = false;
      for (const auto& block : self.backend->blocks)
        if (block.pointer && block.bytes == old_request)
          original_block_live = true;
      self.physical_overlap =
          original_block_live &&
          self.backend->before_last_allocation >= old_request &&
          self.backend->last_allocation_bytes > old_request &&
          self.backend->live == self.backend->before_last_allocation +
                                    self.backend->last_allocation_bytes &&
          self.context->ActiveTrackedPayload() >=
              self.baseline + self.backend->live;
      if (self.action == GrowthAction::kStop) self.stop.request_stop();
      if (self.action == GrowthAction::kBadAlloc ||
          self.action == GrowthAction::kRuntime) {
        self.backend->fail_at = self.backend->requests + 1;
        self.backend->runtime_failure = self.action == GrowthAction::kRuntime;
      }
      if (self.action == GrowthAction::kOccupy) {
        auto reserve = geo::TryReservePayload(
            *self.context, self.context->TrackedPayloadBudget() -
                               self.context->ActiveTrackedPayload());
        self.physical_overlap &=
            reserve.status == geo::ReservationStatus::kAccepted;
        self.occupied = std::move(reserve.lease);
      }
    } catch (...) {
      self.valid_progress = false;
      self.stop.request_stop();
    }
  }
  detail::TestOptions Options() {
    auto options = backend->Options();
    options.relocation_checkpoint = &Check;
    options.state = this;
    return options;
  }
};

void RelocationControls() {
  const auto wire = GrowthWire();
  auto read_limits = f::ReadLimits();
  read_limits.max_line_bytes = f::kPhysicalLimit;
  auto write_limits = f::WriteLimits();
  write_limits.max_line_bytes = f::kPhysicalLimit;
  geo::ExecutionContext context({4, 1, kContextBytes});
  for (const auto kind : {detail::RelocationKind::kProperties,
                          detail::RelocationKind::kAttributeValues,
                          detail::RelocationKind::kCorners}) {
    for (const auto action :
         {GrowthAction::kObserve, GrowthAction::kStop, GrowthAction::kOccupy,
          GrowthAction::kBadAlloc, GrowthAction::kRuntime}) {
      Backend backend;
      GrowthProbe probe{kind, action, &backend, &context, {}, {}, 0};
      bool propagated = false;
      try {
        const auto result = detail::ReadPlyForTest(
            f::Bytes(wire), read_limits, f::Work(), context,
            probe.stop.get_token(), probe.Options());
        if (action == GrowthAction::kObserve)
          Require(result.document.has_value(),
                  "large real schema/property/topology growth completes");
        else {
          const auto expected = action == GrowthAction::kStop
                                    ? ic::PlyStatus::kCanceled
                                    : ic::PlyStatus::kBudgetExceeded;
          Require(result.status == expected && !result.document,
                  "real mid-relocation cancellation/overlapping budget refuses "
                  "whole document");
        }
      } catch (const std::bad_alloc&) {
        Require(action == GrowthAction::kBadAlloc,
                "only armed allocation fault may throw bad_alloc");
        propagated = true;
      } catch (const RuntimeMarker&) {
        Require(action == GrowthAction::kRuntime,
                "only armed runtime exception propagates");
        propagated = true;
      }
      Require(probe.observed && probe.physical_overlap &&
                  probe.valid_progress && probe.stop_copied < probe.stop_total,
              "actual completed-copy indices obey intervals while old and new "
              "original blocks are both charged");
      Require(propagated == (action == GrowthAction::kBadAlloc ||
                             action == GrowthAction::kRuntime),
              "actual armed next growth allocation exception propagated");
      probe.occupied.Reset();
      Require(backend.live == 0,
              "all real relocation scratch blocks rollback or release");
      Empty(context);
    }
  }
  auto source = ic::ReadPly(f::Bytes(wire), read_limits, f::Work(), context);
  Require(source.document.has_value(),
          "genuine large writer relocation source");
  const auto baseline = context.ActiveTrackedPayload();
  for (const auto action :
       {GrowthAction::kObserve, GrowthAction::kStop, GrowthAction::kOccupy,
        GrowthAction::kBadAlloc, GrowthAction::kRuntime}) {
    Backend backend;
    GrowthProbe probe{detail::RelocationKind::kOutputBytes,
                      action,
                      &backend,
                      &context,
                      {},
                      {},
                      baseline};
    bool propagated = false;
    try {
      const auto result = detail::WritePlyForTest(
          source.document->Mesh(), *source.document,
          f::Options(ic::PlyEncoding::kAscii), write_limits, f::Work(),
          probe.stop.get_token(), probe.Options());
      if (action == GrowthAction::kObserve) {
        Require(result.output.has_value(),
                "large actual output relocation completes");
        const auto reload = ic::ReadPly(result.output->Bytes(), read_limits,
                                        f::Work(), context);
        Require(
            reload.document &&
                reload.document->Mesh().View().positions.size() == 9 &&
                reload.document->Mesh().View().corner_vertices.size() == 6144 &&
                reload.document->Mesh().View().attributes.size() == 257,
            "actual large serialized output reloads full independent row "
            "inventory");
      } else
        Require(result.status == (action == GrowthAction::kStop
                                      ? ic::PlyStatus::kCanceled
                                      : ic::PlyStatus::kBudgetExceeded) &&
                    !result.output,
                "mid-output transfer stop/overlap cap has no partial owner");
    } catch (const std::bad_alloc&) {
      Require(action == GrowthAction::kBadAlloc, "armed output bad_alloc");
      propagated = true;
    } catch (const RuntimeMarker&) {
      Require(action == GrowthAction::kRuntime,
              "armed output runtime exception");
      propagated = true;
    }
    Require(probe.observed && probe.physical_overlap && probe.valid_progress,
            "actual output copied indices and physical overlap observed");
    Require(propagated == (action == GrowthAction::kBadAlloc ||
                           action == GrowthAction::kRuntime),
            "actual next output growth exception propagates");
    probe.occupied.Reset();
    Require(backend.live == 0 && context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "large output relocation leaves only original owner");
  }
  source.document.reset();
  Empty(context);
}
struct NumericStop {
  std::stop_source stop;
  std::uint64_t previous = 0;
  std::uint64_t completed = 0;
  std::uint64_t total = 0;
  bool observed = false;
  bool valid_progress = true;
  static void Checkpoint(ic::PlyStage stage,
                         ic::numeric_format_detail::Phase phase,
                         std::uint64_t completed, std::uint64_t total,
                         void* state) noexcept {
    auto& self = *static_cast<NumericStop*>(state);
    if (phase != ic::numeric_format_detail::Phase::kScaleCoefficient) return;
    self.valid_progress = self.valid_progress &&
                          stage == ic::PlyStage::kEncode && completed > 0 &&
                          completed <= total && completed > self.previous &&
                          (self.total == 0 || self.total == total);
    self.previous = completed;
    self.total = total;
    constexpr std::uint64_t kStopAfterActualScalingPasses = 64;
    if (completed >= kStopAfterActualScalingPasses && completed < total) {
      self.observed = true;
      self.completed = completed;
      self.stop.request_stop();
    }
  }
};

void NumericFormattingCancellation() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  Backend backend;
  {
    constexpr std::array<std::uint64_t, 3> kWords{1, 0x8000000000000001,
                                                  0x8000000000000000};
    const auto wire =
        f::PointWire(ic::PlyEncoding::kAscii, true, kWords,
                     "4.9406564584124654e-324 -4.9406564584124654e-324 -0\n");
    auto source = detail::ReadPlyForTest(wire, f::ReadLimits(), f::Work(),
                                         context, {}, backend.Options());
    Require(source.document.has_value(),
            "real binary64 subnormal source owns canonical geometry");
    Require(std::bit_cast<std::uint64_t>(
                source.document->Mesh().View().positions[0]) == 1,
            "numeric stop starts from independent minimum-subnormal bits");
    const auto baseline = context.ActiveTrackedPayload();
    const auto physical_baseline = backend.live;
    NumericStop probe;
    auto test = backend.Options();
    test.state = &probe;
    test.numeric_format_checkpoint = &NumericStop::Checkpoint;
    auto work = f::Work();
    work.cancellation_block_bytes = 1;
    const auto result = detail::WritePlyForTest(
        source.document->Mesh(), *source.document,
        f::Options(ic::PlyEncoding::kAscii), f::WriteLimits(), work,
        probe.stop.get_token(), test);
    Require(probe.observed && probe.valid_progress && probe.completed > 0 &&
                probe.completed < probe.total,
            "stop follows genuine interior numeric coefficient work");
    Require(result.status == ic::PlyStatus::kCanceled && !result.output &&
                result.diagnostic.stage == ic::PlyStage::kEncode,
            "interior numeric cancellation exposes no partial output");
    Require(result.execution.codec.launched_workers == 0 &&
                result.execution.inspection.launched_workers ==
                    result.execution.inspection.joined_workers &&
                context.ActiveWorkers() == 0,
            "numeric stop joins all actual delegated workers");
    Require(backend.live == physical_baseline &&
                context.ActiveTrackedPayload() == baseline,
            "numeric cancellation releases every writer request");
    Require(std::ranges::equal(source.document->OriginalBytes(), wire),
            "canceled formatter leaves captured source bytes intact");
  }
  Require(backend.live == 0, "numeric source releases original backend blocks");
  Empty(context);
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "known profile argument");
    Admission();
    if (unsupported) {
      geo::ExecutionContext context({4, 1, kContextBytes});
      const auto result = ic::ReadPly(f::Wire(ic::PlyEncoding::kAscii),
                                      f::ReadLimits(), f::Work(), context);
      Require(result.status == ic::PlyStatus::kGeometryFailure &&
                  !result.document &&
                  result.diagnostic.geometry_status ==
                      geo::MaterializationStatus::kUnsupportedAllocationProfile,
              "actual unsupported nonempty profile");
      Empty(context);
    } else {
      Ownership();
      FaultsAndCancellation();
      ExactAndSharedLimits();
      RelocationControls();
      NumericFormattingCancellation();
    }
    std::cout << "PLY physical requests, caps and lifecycle checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
