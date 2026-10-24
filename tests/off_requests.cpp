// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include "gltf_allocation.h"
#include "off_fixture.h"
#include "off_test_internal.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::off_detail;
namespace f = ic::off_fixture;
using f::Require;
constexpr std::uint64_t kContextBytes = 64 * f::kPhysicalLimit;
constexpr std::size_t kBackendBlocks = 4096;
constexpr std::size_t kFaultAttemptLimit = 256;
constexpr std::string_view kUnsupportedOption = "--unsupported-profile";
struct RuntimeMarker {};
struct Backend {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
  };
  std::array<Block, kBackendBlocks> blocks{};
  ic::gltf_detail::AllocationBackend original =
      ic::gltf_detail::DefaultAllocationBackend();
  std::size_t requests = 0;
  std::size_t live = 0;
  std::size_t fail_at = 0;
  bool runtime_failure = false;
  bool frozen = false;

  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Backend*>(state);
    ++self.requests;
    if (self.frozen || (self.fail_at != 0 && self.requests == self.fail_at)) {
      if (self.runtime_failure) throw RuntimeMarker{};
      throw std::bad_alloc{};
    }
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks) {
      if (block.pointer == nullptr) {
        block = {pointer, bytes};
        self.live += bytes;
        return pointer;
      }
    }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::bad_alloc{};
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Backend*>(state);
    for (auto& block : self.blocks) {
      if (block.pointer == pointer) {
        if (block.bytes != bytes) std::terminate();
        self.live -= bytes;
        block = {};
        self.original.deallocate(pointer, bytes, alignment,
                                 self.original.state);
        return;
      }
    }
    std::terminate();
  }
  detail::TestOptions Options() {
    return {{&Allocate, &Free, this}, nullptr, nullptr};
  }
};

void EmptyLedger(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "failed/released OFF operation leaves no payload or workers");
}

void EarlyLimitsAndLexicalBoundaries() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  Backend backend;
  auto limits = f::ReadLimits();
  limits.max_source_bytes = f::kWire.size() - 1;
  auto result = detail::ReadOffForTest(f::Bytes(f::kWire), limits, f::Work(),
                                       context, {}, backend.Options());
  Require(result.status == ic::OffStatus::kLimitExceeded && !result.document &&
              result.diagnostic.code == ic::OffDiagnosticCode::kSourceLimit &&
              backend.requests == 0,
          "source cap precedes codec allocation");
  limits = f::ReadLimits();
  --limits.max_total_new_requested_bytes;
  result = detail::ReadOffForTest(f::Bytes(f::kWire), limits, f::Work(),
                                  context, {}, backend.Options());
  Require(result.status == ic::OffStatus::kInvalidConfiguration &&
              !result.document && backend.requests == 0,
          "aggregate allowance must cover actual separate declared caps before "
          "work");
  limits = f::ReadLimits();
  limits.max_comment_bytes = 0;
  result = detail::ReadOffForTest(f::Bytes(f::kWire), limits, f::Work(),
                                  context, {}, backend.Options());
  Require(result.status == ic::OffStatus::kInvalidConfiguration &&
              backend.requests == 0,
          "zero required limit is never unlimited");
  auto work = f::Work();
  work.cancellation_block_records = 0;
  result = detail::ReadOffForTest(f::Bytes(f::kWire), f::ReadLimits(), work,
                                  context, {}, backend.Options());
  Require(result.status == ic::OffStatus::kInvalidConfiguration &&
              backend.requests == 0,
          "zero work block refuses before allocation");
  std::stop_source stopped;
  stopped.request_stop();
  result =
      detail::ReadOffForTest(f::Bytes(f::kWire), f::ReadLimits(), f::Work(),
                             context, stopped.get_token(), backend.Options());
  Require(result.status == ic::OffStatus::kCanceled && !result.document &&
              backend.requests == 0,
          "prestop performs no allocation");
  const auto refuse = [&](ic::OffReadLimits cap,
                          ic::OffDiagnosticCode expected) {
    const auto rejected =
        ic::ReadOff(f::Bytes(f::kWire), cap, f::Work(), context);
    Require(rejected.status == ic::OffStatus::kLimitExceeded &&
                !rejected.document && rejected.diagnostic.code == expected,
            "actual count/lexical cap typed refusal");
    EmptyLedger(context);
  };
  limits = f::ReadLimits();
  limits.max_vertices = 7;
  refuse(limits, ic::OffDiagnosticCode::kVertexLimit);
  limits = f::ReadLimits();
  limits.max_faces = 2;
  refuse(limits, ic::OffDiagnosticCode::kFaceLimit);
  limits = f::ReadLimits();
  limits.max_corners = 10;
  refuse(limits, ic::OffDiagnosticCode::kCornerLimit);
  limits = f::ReadLimits();
  limits.max_corners_per_face = 3;
  refuse(limits, ic::OffDiagnosticCode::kPerFaceCornerLimit);
  limits = f::ReadLimits();
  limits.max_line_bytes = 4;
  refuse(limits, ic::OffDiagnosticCode::kLineLimit);
  limits = f::ReadLimits();
  limits.max_token_bytes = 1;
  refuse(limits, ic::OffDiagnosticCode::kTokenLimit);
  limits = f::ReadLimits();
  limits.max_comment_bytes = 1;
  refuse(limits, ic::OffDiagnosticCode::kCommentLimit);
  constexpr std::string_view kBoundary = "#1234\r\n#abcd\r\nOFF\r\n0 0 0";
  limits = f::ReadLimits();
  limits.max_source_bytes = kBoundary.size();
  limits.max_line_bytes = 5;
  limits.max_comment_bytes = 4;
  {
    const auto accepted =
        ic::ReadOff(f::Bytes(kBoundary), limits, f::Work(), context);
    Require(accepted.status == ic::OffStatus::kSuccess && accepted.document,
            "exact comment cap excludes marker; exact line cap excludes CRLF; "
            "no final newline needed");
  }
  limits.max_comment_bytes = 3;
  result = ic::ReadOff(f::Bytes(kBoundary), limits, f::Work(), context);
  Require(result.status == ic::OffStatus::kLimitExceeded && !result.document &&
              result.diagnostic.code == ic::OffDiagnosticCode::kCommentLimit,
          "one-less comment cap genuinely refuses");
  limits.max_comment_bytes = 4;
  limits.max_line_bytes = 4;
  result = ic::ReadOff(f::Bytes(kBoundary), limits, f::Work(), context);
  Require(result.status == ic::OffStatus::kLimitExceeded && !result.document &&
              result.diagnostic.code == ic::OffDiagnosticCode::kLineLimit,
          "one-less data line cap genuinely refuses");
  EmptyLedger(context);
}

void OwnershipAndAccounting() {
  Backend input_backend;
  Backend output_backend;
  struct HandoffSeal {
    Backend* backend;
    bool observed = false;
    static void Checkpoint(ic::OffStage stage, std::uint64_t,
                           void* state) noexcept {
      auto& self = *static_cast<HandoffSeal*>(state);
      if (stage == ic::OffStage::kPublication) {
        self.observed = true;
        self.backend->frozen = true;
      }
    }
    detail::TestOptions Options() {
      auto result = backend->Options();
      result.checkpoint = &Checkpoint;
      result.state = this;
      return result;
    }
  } read_handoff{&input_backend}, write_handoff{&output_backend};
  std::optional<ic::OffDocument> document;
  {
    geo::ExecutionContext original({4, 1, kContextBytes});
    const std::string bytes(f::kWire);
    auto read =
        detail::ReadOffForTest(f::Bytes(bytes), f::ReadLimits(), f::Work(),
                               original, {}, read_handoff.Options());
    Require(read.document.has_value(), "actual original allocator document");
    Require(
        read_handoff.observed,
        "real read final handoff succeeds with original codec backend sealed");
    const auto charge = read.document->NewRequestedBytes();
    Require(charge.codec == input_backend.live &&
                charge.materialization ==
                    read.document->Mesh().NewRequestedBytes() &&
                charge.inspection ==
                    read.document->Inspection().NewRequestedBytes() &&
                charge.total ==
                    charge.codec + charge.materialization + charge.inspection &&
                original.ActiveTrackedPayload() == charge.total,
            "physical backend plus actual disjoint retained Geometry owners "
            "equal shared ledger");
    document = read.document;
  }
  input_backend.frozen = true;
  f::Check(*document);
  Require(std::ranges::equal(document->OriginalBytes(), f::Bytes(f::kWire)) &&
              document->DeclaredEdgeCount() == 17,
          "document works after original bytes/context die without external "
          "context copy");
  const auto requests = input_backend.requests;
  auto doc_copy = document;
  document.reset();
  document = doc_copy;
  doc_copy.reset();
  Require(input_backend.requests == requests,
          "document sharing allocates nothing");
  std::optional<ic::OffOutput> output;
  {
    auto options = f::Options(&*document);
    auto write =
        detail::WriteOffForTest(document->Mesh(), options, f::WriteLimits(),
                                f::Work(), {}, write_handoff.Options());
    Require(write.output.has_value(),
            "actual writer continues through retained document ledger");
    Require(
        write_handoff.observed,
        "real write final handoff succeeds with original codec backend sealed");
    const auto charges = write.output->NewRequestedBytes();
    Require(charges.codec == output_backend.live &&
                charges.materialization == 0 && charges.inspection == 0 &&
                charges.total == charges.codec,
            "only independent output codec blocks survive handoff");
    output = write.output;
  }
  document.reset();
  Require(input_backend.live == 0,
          "output does not retain caller document or geometry owner");
  output_backend.frozen = true;
  f::CheckFacts(output->Facts(), 8, 3, 11);
  Require(
      output->Facts().source_declared_edge_count == 17 &&
          !output->Bytes().empty(),
      "output bytes/facts survive all original source/options/context owners");
  {
    geo::ExecutionContext reload_context({4, 1, kContextBytes});
    const auto actual = ic::ReadOff(output->Bytes(), f::ReadLimits(), f::Work(),
                                    reload_context);
    Require(actual.document.has_value(),
            "retained output actually reloads after original context death");
    f::Check(*actual.document);
  }
  const auto output_requests = output_backend.requests;
  auto copy = output;
  output.reset();
  f::CheckFacts(copy->Facts(), 8, 3, 11);
  copy.reset();
  Require(
      output_backend.live == 0 && output_backend.requests == output_requests,
      "last output copy frees original allocator without new allocation");
}

struct StopAt {
  std::stop_source source;
  ic::OffStage stage;
  std::uint64_t minimum;
  bool observed = false;
  static void Checkpoint(ic::OffStage stage, std::uint64_t ordinal,
                         void* state) noexcept {
    auto& self = *static_cast<StopAt*>(state);
    if (stage == self.stage && ordinal >= self.minimum) {
      self.observed = true;
      self.source.request_stop();
    }
  }
  detail::TestOptions Options() {
    auto test = detail::TestOptions{};
    test.checkpoint = &Checkpoint;
    test.state = this;
    return test;
  }
};

void FaultsAndCancellation() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  bool success = false;
  for (std::size_t request = 1; request < kFaultAttemptLimit; ++request) {
    Backend backend;
    backend.fail_at = request;
    try {
      auto result =
          detail::ReadOffForTest(f::Bytes(f::kWire), f::ReadLimits(), f::Work(),
                                 context, {}, backend.Options());
      Require(result.document.has_value(),
              "fault loop reaches genuine read success");
      success = true;
    } catch (const std::bad_alloc&) {
      Require(backend.requests == request, "actual injected allocation failed");
    }
    Require(backend.live == 0,
            "every failing read request rolls back original blocks");
    EmptyLedger(context);
    if (success) break;
  }
  Require(success, "all codec read allocation sites traversed");
  Backend throwing;
  throwing.fail_at = 1;
  throwing.runtime_failure = true;
  bool propagated = false;
  try {
    static_cast<void>(detail::ReadOffForTest(f::Bytes(f::kWire),
                                             f::ReadLimits(), f::Work(),
                                             context, {}, throwing.Options()));
  } catch (const RuntimeMarker&) {
    propagated = true;
  }
  Require(propagated && throwing.live == 0,
          "unexpected original backend exception propagates after cleanup");
  EmptyLedger(context);
  for (const auto stage : {ic::OffStage::kScan, ic::OffStage::kDecode,
                           ic::OffStage::kPublication}) {
    StopAt stop{
        {}, stage, stage == ic::OffStage::kPublication ? 0u : 64u, false};
    const auto result = detail::ReadOffForTest(
        f::Bytes(f::kWire), f::ReadLimits(), f::Work(), context,
        stop.source.get_token(), stop.Options());
    Require(stop.observed && result.status == ic::OffStatus::kCanceled &&
                !result.document && result.diagnostic.stage == stage &&
                result.diagnostic.code == ic::OffDiagnosticCode::kStopRequested,
            "actual mid-scan/decode or final handoff cancellation returns no "
            "document");
    EmptyLedger(context);
  }
  auto source =
      ic::ReadOff(f::Bytes(f::kWire), f::ReadLimits(), f::Work(), context);
  Require(source.document.has_value(),
          "write fault controls use actual strong source");
  const auto baseline = context.ActiveTrackedPayload();
  success = false;
  for (std::size_t request = 1; request < kFaultAttemptLimit; ++request) {
    Backend backend;
    backend.fail_at = request;
    try {
      const auto result = detail::WriteOffForTest(
          source.document->Mesh(), f::Options(), f::WriteLimits(), f::Work(),
          {}, backend.Options());
      Require(result.output.has_value(), "write fault loop reaches success");
      success = true;
    } catch (const std::bad_alloc&) {
      Require(backend.requests == request,
              "actual configured write allocation failed");
    }
    Require(backend.live == 0 && context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "write fault leaves only actual original source owners");
    if (success) break;
  }
  Require(success, "all write codec allocation sites traversed");
  for (const auto stage : {ic::OffStage::kEncode, ic::OffStage::kPublication}) {
    StopAt stop{{}, stage, stage == ic::OffStage::kEncode ? 64u : 0u, false};
    const auto result = detail::WriteOffForTest(
        source.document->Mesh(), f::Options(), f::WriteLimits(), f::Work(),
        stop.source.get_token(), stop.Options());
    Require(stop.observed && result.status == ic::OffStatus::kCanceled &&
                !result.output && result.diagnostic.stage == stage &&
                context.ActiveTrackedPayload() == baseline,
            "actual mid-encode/final publication stop has no partial output");
  }
}

void SharedLedgerExactLimitsAndConcurrentCalls() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  auto limits = f::ReadLimits();
  limits.max_source_bytes = f::kWire.size();
  limits.max_vertices = 8;
  limits.max_faces = 3;
  limits.max_corners = 11;
  limits.max_corners_per_face = 4;
  auto source = ic::ReadOff(f::Bytes(f::kWire), limits, f::Work(), context);
  Require(source.document.has_value(),
          "actual source/count exact inclusive limits pass");
  auto output = ic::WriteOff(source.document->Mesh(), f::Options(),
                             f::WriteLimits(), f::Work());
  Require(output.output.has_value(),
          "derive actual bounded canonical output extent");
  const auto size = output.output->Bytes().size();
  output.output.reset();
  auto write_limits = f::WriteLimits();
  write_limits.max_output_bytes = size;
  output = ic::WriteOff(source.document->Mesh(), f::Options(), write_limits,
                        f::Work());
  Require(output.output.has_value(), "exact actual output extent admitted");
  output.output.reset();
  --write_limits.max_output_bytes;
  output = ic::WriteOff(source.document->Mesh(), f::Options(), write_limits,
                        f::Work());
  Require(output.status == ic::OffStatus::kLimitExceeded && !output.output &&
              output.diagnostic.code == ic::OffDiagnosticCode::kOutputLimit,
          "one-less actual output cap refuses whole owner");
  const auto refused_limit = [&](ic::OffWriteLimits cap,
                                 ic::OffDiagnosticCode code) {
    const auto refused =
        ic::WriteOff(source.document->Mesh(), f::Options(), cap, f::Work());
    Require(refused.status == ic::OffStatus::kLimitExceeded &&
                !refused.output && refused.diagnostic.code == code,
            "actual writer count/line/token bound refuses a whole owner");
  };
  write_limits = f::WriteLimits();
  write_limits.max_vertices = 7;
  refused_limit(write_limits, ic::OffDiagnosticCode::kVertexLimit);
  write_limits = f::WriteLimits();
  write_limits.max_faces = 2;
  refused_limit(write_limits, ic::OffDiagnosticCode::kFaceLimit);
  write_limits = f::WriteLimits();
  write_limits.max_corners = 10;
  refused_limit(write_limits, ic::OffDiagnosticCode::kCornerLimit);
  write_limits = f::WriteLimits();
  write_limits.max_corners_per_face = 3;
  refused_limit(write_limits, ic::OffDiagnosticCode::kPerFaceCornerLimit);
  write_limits = f::WriteLimits();
  write_limits.max_line_bytes = 4;
  refused_limit(write_limits, ic::OffDiagnosticCode::kLineLimit);
  write_limits = f::WriteLimits();
  write_limits.max_token_bytes = 1;
  refused_limit(write_limits, ic::OffDiagnosticCode::kTokenLimit);
  const auto baseline = context.ActiveTrackedPayload();
  auto occupied = geo::TryReservePayload(
      context, context.TrackedPayloadBudget() - baseline);
  Require(occupied.status == geo::ReservationStatus::kAccepted,
          "occupy genuine remaining shared headroom");
  output = ic::WriteOff(source.document->Mesh(), f::Options(), f::WriteLimits(),
                        f::Work());
  Require(output.status == ic::OffStatus::kBudgetExceeded && !output.output,
          "writer cannot bypass source retained ledger");
  const auto read_refused =
      ic::ReadOff(f::Bytes(f::kWire), f::ReadLimits(), f::Work(), context);
  Require(read_refused.status == ic::OffStatus::kBudgetExceeded &&
              !read_refused.document,
          "overlapping new reader obeys same occupied ledger");
  occupied.lease.Reset();
  source.document.reset();
  EmptyLedger(context);
  std::array<bool, 2> accepted{};
  std::array<std::exception_ptr, 2> errors{};
  {
    std::array<std::jthread, 2> callers;
    for (std::size_t i = 0; i < callers.size(); ++i)
      callers[i] = std::jthread([&, i] {
        try {
          const auto read = ic::ReadOff(f::Bytes(f::kWire), f::ReadLimits(),
                                        f::Work(), context);
          Require(read.document.has_value(),
                  "actual concurrent independent read");
          f::Check(*read.document);
          accepted[i] = true;
        } catch (...) {
          errors[i] = std::current_exception();
        }
      });
  }
  for (std::size_t i = 0; i < accepted.size(); ++i) {
    if (errors[i]) std::rethrow_exception(errors[i]);
    Require(accepted[i], "both genuine concurrent reads finish");
  }
  EmptyLedger(context);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported = argc == 2 && argv[1] == kUnsupportedOption;
    Require(argc == 1 || unsupported, "unknown OFF request fixture argument");
    EarlyLimitsAndLexicalBoundaries();
    if (!unsupported) {
      OwnershipAndAccounting();
      FaultsAndCancellation();
      SharedLedgerExactLimitsAndConcurrentCalls();
    } else {
      geo::ExecutionContext context({4, 1, kContextBytes});
      const auto result =
          ic::ReadOff(f::Bytes(f::kWire), f::ReadLimits(), f::Work(), context);
      Require(
          result.status == ic::OffStatus::kGeometryFailure &&
              !result.document &&
              result.diagnostic.geometry_status ==
                  geo::MaterializationStatus::kUnsupportedAllocationProfile,
          "unsupported producer actually refuses nonempty numerical capture");
      EmptyLedger(context);
    }
    std::cout << "OFF actual limits, ledger, allocation, cancellation and "
                 "lifetime fixtures passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
