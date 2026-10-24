// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "gltf_allocation.h"
#include "stl_fixture.h"
#include "stl_test_internal.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::stl_detail;
namespace f = ic::stl_fixture;
using f::Require;
constexpr std::size_t kContextBytes = 64 * f::kPhysicalLimit;
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
  std::size_t before_last_allocation = 0;
  std::size_t last_allocation_bytes = 0;
  bool runtime_failure = false;
  bool frozen = false;

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

struct StopAt {
  std::stop_source source;
  ic::StlStage stage;
  std::size_t observed = 0;
  static void Checkpoint(ic::StlStage stage, std::uint64_t,
                         void* state) noexcept {
    auto& self = *static_cast<StopAt*>(state);
    if (stage == self.stage) {
      ++self.observed;
      self.source.request_stop();
    }
  }
};

void EmptyLedger(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "failed/released STL operation leaves no ledger payload/workers");
}

void LimitsAndEarlyControls() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  Backend backend;
  const auto test = backend.Options();
  auto limits = f::ReadLimits();
  limits.max_source_bytes = f::kAscii.size() - 1;
  auto result =
      detail::ReadStlForTest(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                             limits, f::Work(), context, {}, test);
  Require(result.status == ic::StlStatus::kLimitExceeded && !result.document &&
              result.diagnostic.code == ic::StlDiagnosticCode::kSourceLimit,
          "one byte below actual source size refuses before decoding");
  Require(backend.requests == 0, "source byte limit precedes codec allocation");
  for (auto encoding :
       {ic::StlEncoding::kInvalid, static_cast<ic::StlEncoding>(999)}) {
    result =
        detail::ReadStlForTest(f::Bytes(f::kAscii), encoding, f::ReadLimits(),
                               f::Work(), context, {}, test);
    Require(result.status == ic::StlStatus::kInvalidConfiguration &&
                !result.document && backend.requests == 0,
            "foreign encoding fails before allocations");
  }
  limits = f::ReadLimits();
  limits.max_total_new_requested_bytes =
      limits.max_codec_new_requested_bytes +
      limits.materialization.max_new_requested_bytes +
      limits.inspection.max_new_requested_bytes - 1;
  result = detail::ReadStlForTest(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                                  limits, f::Work(), context, {}, test);
  Require(result.status == ic::StlStatus::kInvalidConfiguration &&
              !result.document && backend.requests == 0,
          "combined allowance is checked before work");
  auto zero_work = f::Work();
  zero_work.cancellation_block_tokens = 0;
  result =
      detail::ReadStlForTest(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                             f::ReadLimits(), zero_work, context, {}, test);
  Require(result.status == ic::StlStatus::kInvalidConfiguration &&
              backend.requests == 0,
          "zero work block is not an unlimited/default policy");
  std::stop_source stopped;
  stopped.request_stop();
  result = detail::ReadStlForTest(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                                  f::ReadLimits(), f::Work(), context,
                                  stopped.get_token(), test);
  Require(result.status == ic::StlStatus::kCanceled && !result.document &&
              backend.requests == 0,
          "pre-cancellation performs no codec allocation");
  const auto refuse_limit = [&](ic::StlReadLimits reduced,
                                ic::StlDiagnosticCode expected) {
    const auto refused =
        ic::ReadStl(f::Bytes(f::kAscii), ic::StlEncoding::kAscii, reduced,
                    f::Work(), context);
    Require(refused.status == ic::StlStatus::kLimitExceeded &&
                !refused.document && refused.diagnostic.code == expected,
            "actual scanner cardinality/name/token/line bound");
    EmptyLedger(context);
  };
  limits = f::ReadLimits();
  limits.max_facets = 1;
  refuse_limit(limits, ic::StlDiagnosticCode::kFacetLimit);
  limits = f::ReadLimits();
  limits.max_name_bytes = f::kName.size() - 1;
  refuse_limit(limits, ic::StlDiagnosticCode::kNameLimit);
  limits = f::ReadLimits();
  limits.max_line_bytes = 4;
  refuse_limit(limits, ic::StlDiagnosticCode::kLineLimit);
  limits = f::ReadLimits();
  limits.max_token_bytes = 2;
  refuse_limit(limits, ic::StlDiagnosticCode::kTokenLimit);
  EmptyLedger(context);
}

void OwnershipAndAccounting() {
  Backend backend;
  geo::ExecutionContext context({4, 1, kContextBytes});
  std::optional<ic::StlDocument> held;
  {
    std::string source(f::kAscii);
    auto read = detail::ReadStlForTest(
        f::Bytes(source), ic::StlEncoding::kAscii, f::ReadLimits(), f::Work(),
        context, {}, backend.Options());
    Require(read.document.has_value(), "real original-backend owner");
    held = read.document;
    source.assign(source.size(), 'x');
  }
  f::CheckDocument(*held);
  Require(std::ranges::equal(held->OriginalBytes(), f::Bytes(f::kAscii)),
          "source bytes survive caller source/options destruction");
  const auto bytes = held->NewRequestedBytes();
  Require(
      bytes.codec == backend.live &&
          bytes.materialization == held->Mesh().NewRequestedBytes() &&
          bytes.inspection == held->Inspection().NewRequestedBytes() &&
          bytes.total ==
              bytes.codec + bytes.materialization + bytes.inspection &&
          context.ActiveTrackedPayload() == bytes.total,
      "independent physical backend plus actual disjoint Geometry live owners");
  const auto attempts = backend.requests;
  backend.frozen = true;
  auto copy = held;
  held.reset();
  Require(copy->NewRequestedBytes().total == bytes.total &&
              backend.requests == attempts,
          "nothrow ownership copies keep charges without new codec requests");
  f::CheckDocument(*copy);
  copy.reset();
  Require(backend.live == 0 && backend.requests == attempts,
          "last copy frees original backend without an allocation");
  EmptyLedger(context);
  backend.frozen = false;
  {
    auto read = ic::ReadStl(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                            f::ReadLimits(), f::Work(), context);
    Require(read.document.has_value(), "write ownership source");
    const auto baseline = context.ActiveTrackedPayload();
    std::string name = "owned output metadata";
    auto options = f::Options(*read.document, ic::StlEncoding::kAscii);
    options.ascii_name = name;
    auto output = detail::WriteStlForTest(read.document->Mesh(), options,
                                          f::WriteLimits(), f::Work(), {},
                                          backend.Options());
    Require(output.output.has_value(), "real source-backed output owner");
    const auto charges = output.output->NewRequestedBytes();
    Require(charges.codec == backend.live &&
                context.ActiveTrackedPayload() == baseline + charges.total,
            "write charges exclude the already-retained input");
    read.document.reset();
    name.assign(name.size(), 'x');
    options = {};
    Require(context.ActiveTrackedPayload() == charges.total,
            "serialized output does not pin source geometry/metadata storage");
    backend.frozen = true;
    auto output_copy = output.output;
    output.output.reset();
    const auto text = output_copy->Bytes();
    const std::string_view wire(reinterpret_cast<const char*>(text.data()),
                                text.size());
    Require(wire.starts_with("solid owned output metadata\n"),
            "output metadata is owned after caller destruction");
    output_copy.reset();
    Require(backend.live == 0, "output last copy frees original backend");
    EmptyLedger(context);
  }
}

void OwnersOutliveOriginalContext() {
  // Both original allocator states outlive their last retained block. There is
  // deliberately no external context copy outside the capture scope.
  Backend input_backend;
  Backend output_backend;
  std::optional<ic::StlDocument> document;
  {
    geo::ExecutionContext original_context({4, 1, kContextBytes});
    const std::string source(f::kAscii);
    auto read = detail::ReadStlForTest(
        f::Bytes(source), ic::StlEncoding::kAscii, f::ReadLimits(), f::Work(),
        original_context, {}, input_backend.Options());
    Require(read.document.has_value(), "lifetime source genuinely captured");
    document = read.document;
  }
  input_backend.frozen = true;
  f::CheckDocument(*document);
  Require(std::ranges::equal(document->OriginalBytes(), f::Bytes(f::kAscii)) &&
              document->Inspection().IsStorageValid(),
          "document bytes and inspection survive original source/context "
          "destruction");
  std::optional<ic::StlOutput> output;
  constexpr std::string_view kOutputName =
      "continued after context destruction";
  {
    const std::string name(kOutputName);
    auto options = f::Options(*document, ic::StlEncoding::kAscii);
    options.ascii_name = name;
    auto written =
        detail::WriteStlForTest(document->Mesh(), options, f::WriteLimits(),
                                f::Work(), {}, output_backend.Options());
    Require(written.output.has_value(),
            "actual writer continues using only document-retained context");
    output = written.output;
  }
  document.reset();
  Require(input_backend.live == 0,
          "continued output does not pin original document/Geometry storage");
  output_backend.frozen = true;
  const auto bytes = output->Bytes();
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                              bytes.size());
  constexpr std::array<geo::index_t, 6> kCorners{0, 1, 2, 3, 4, 5};
  Require(
      text.starts_with("solid continued after context destruction\n") &&
          output->Facts().source_vertex_count == 6 &&
          output->Facts().source_face_count == 2 &&
          std::ranges::equal(output->CornerSourceVertices(), kCorners) &&
          output->NewRequestedBytes().codec == output_backend.live,
      "output bytes/facts/map remain usable after source/options/context die");
  const auto attempts = output_backend.requests;
  auto copy = output;
  output.reset();
  Require(!copy->Bytes().empty() && output_backend.requests == attempts,
          "retained output copy requires no allocation after original context "
          "death");
  copy.reset();
  Require(output_backend.live == 0 && output_backend.requests == attempts,
          "last output copy frees original backend without allocation");
}

void ActualFaultsAndStops() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  bool reached_success = false;
  for (std::size_t request = 1; request < kFaultAttemptLimit; ++request) {
    Backend backend;
    backend.fail_at = request;
    try {
      auto result = detail::ReadStlForTest(
          f::Bytes(f::kAscii), ic::StlEncoding::kAscii, f::ReadLimits(),
          f::Work(), context, {}, backend.Options());
      Require(result.document.has_value(),
              "fault loop eventually reaches real success");
      reached_success = true;
    } catch (const std::bad_alloc&) {
      Require(backend.requests == request,
              "genuine configured backend allocation failed");
    }
    Require(backend.live == 0,
            "every attempted read allocation rolls back exactly");
    EmptyLedger(context);
    if (reached_success) break;
  }
  Require(reached_success, "all read codec allocation sites were traversed");
  Backend throwing;
  throwing.fail_at = 1;
  throwing.runtime_failure = true;
  bool propagated = false;
  try {
    static_cast<void>(detail::ReadStlForTest(
        f::Bytes(f::kAscii), ic::StlEncoding::kAscii, f::ReadLimits(),
        f::Work(), context, {}, throwing.Options()));
  } catch (const RuntimeMarker&) {
    propagated = true;
  }
  Require(propagated && throwing.live == 0,
          "unexpected original backend exception propagates with cleanup");
  for (auto stage : {ic::StlStage::kScan, ic::StlStage::kDecode,
                     ic::StlStage::kPublication}) {
    StopAt stop{{}, stage, 0};
    auto options = detail::TestOptions{};
    options.checkpoint = &StopAt::Checkpoint;
    options.state = &stop;
    const auto binary = f::Binary();
    const bool decoding = stage == ic::StlStage::kDecode;
    const auto input =
        decoding ? std::span<const std::byte>(binary) : f::Bytes(f::kAscii);
    auto result = detail::ReadStlForTest(
        input, decoding ? ic::StlEncoding::kBinary : ic::StlEncoding::kAscii,
        f::ReadLimits(), f::Work(), context, stop.source.get_token(), options);
    Require(stop.observed != 0 && result.status == ic::StlStatus::kCanceled &&
                !result.document,
            "real scanner/decode/pre-handoff checkpoint cancellation");
    EmptyLedger(context);
  }
  auto source = ic::ReadStl(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                            f::ReadLimits(), f::Work(), context);
  Require(source.document.has_value(), "write faults use genuine source owner");
  const auto baseline = context.ActiveTrackedPayload();
  const auto options = f::Options(*source.document, ic::StlEncoding::kBinary);
  reached_success = false;
  for (std::size_t request = 1; request < kFaultAttemptLimit; ++request) {
    Backend backend;
    backend.fail_at = request;
    try {
      auto result = detail::WriteStlForTest(source.document->Mesh(), options,
                                            f::WriteLimits(), f::Work(), {},
                                            backend.Options());
      Require(result.output.has_value(),
              "write fault loop reaches real success");
      reached_success = true;
    } catch (const std::bad_alloc&) {
      Require(backend.requests == request, "real write backend failure");
    }
    Require(backend.live == 0 && context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "write failure keeps only original input owner");
    if (reached_success) break;
  }
  Require(reached_success, "all write allocation sites exercised");
  for (auto stage : {ic::StlStage::kEncode, ic::StlStage::kPublication}) {
    StopAt stop{{}, stage, 0};
    auto test = detail::TestOptions{};
    test.checkpoint = &StopAt::Checkpoint;
    test.state = &stop;
    const auto result = detail::WriteStlForTest(
        source.document->Mesh(), options, f::WriteLimits(), f::Work(),
        stop.source.get_token(), test);
    Require(stop.observed != 0 && result.status == ic::StlStatus::kCanceled &&
                !result.output && context.ActiveTrackedPayload() == baseline,
            "real write/pre-handoff cancellation has no partial output");
  }
}

void MetadataComparisonCancellation() {
  constexpr std::size_t kLongNameBytes = 240;
  constexpr std::uint64_t kInsideComparison = 64;
  const std::string name(kLongNameBytes, 'n');
  auto text = f::Replace(std::string(f::kAscii), f::kName, name);
  text = f::Replace(std::move(text), f::kName, name);
  geo::ExecutionContext context({4, 1, kContextBytes});
  struct CompareStop {
    std::stop_source stop;
    std::uint64_t closing_name_offset = 0;
    bool writing = false;
    bool decoded = false;
    bool closing_name_scanned = false;
    bool inspecting = false;
    bool in_comparison = false;
    bool fired = false;
    static void Checkpoint(ic::StlStage stage, std::uint64_t ordinal,
                           void* state) noexcept {
      auto& self = *static_cast<CompareStop*>(state);
      if (stage == ic::StlStage::kDecode) self.decoded = true;
      if (stage == ic::StlStage::kInspection) self.inspecting = true;
      if (!self.writing && stage == ic::StlStage::kScan && self.decoded) {
        if (ordinal >= self.closing_name_offset)
          self.closing_name_scanned = true;
        if (self.closing_name_scanned && ordinal == 0)
          self.in_comparison = true;
      }
      // Earlier output-name validation occurs before inspection; actual facet
      // validation ordinals are 0/1. The later local byte ordinal identifies
      // provenance equality, rather than canceling either earlier operation.
      if (self.writing && self.inspecting &&
          stage == ic::StlStage::kValidation && ordinal >= kInsideComparison)
        self.in_comparison = true;
      if (self.in_comparison && ordinal == kInsideComparison) {
        self.fired = true;
        self.stop.request_stop();
      }
    }
    detail::TestOptions Options() {
      auto result = detail::TestOptions{};
      result.checkpoint = &Checkpoint;
      result.state = this;
      return result;
    }
  };
  CompareStop read_stop;
  read_stop.closing_name_offset = text.rfind(name);
  const auto canceled = detail::ReadStlForTest(
      f::Bytes(text), ic::StlEncoding::kAscii, f::ReadLimits(), f::Work(),
      context, read_stop.stop.get_token(), read_stop.Options());
  Require(
      read_stop.fired && read_stop.in_comparison &&
          canceled.status == ic::StlStatus::kCanceled &&
          canceled.diagnostic.stage == ic::StlStage::kScan &&
          canceled.diagnostic.code == ic::StlDiagnosticCode::kStopRequested &&
          !canceled.document,
      "actual closing-name equality observes cancellation inside long name");
  EmptyLedger(context);
  for (const auto encoding :
       {ic::StlEncoding::kAscii, ic::StlEncoding::kBinary}) {
    const auto binary = f::Binary();
    const auto bytes = encoding == ic::StlEncoding::kAscii
                           ? f::Bytes(text)
                           : std::span<const std::byte>(binary);
    auto source =
        ic::ReadStl(bytes, encoding, f::ReadLimits(), f::Work(), context);
    Require(source.document.has_value(),
            "uncanceled metadata control succeeds");
    const auto baseline = context.ActiveTrackedPayload();
    const auto options = f::Options(*source.document, encoding);
    {
      const auto control = ic::WriteStl(source.document->Mesh(), options,
                                        f::WriteLimits(), f::Work());
      Require(
          control.output && control.output->Facts().metadata_policy ==
                                ic::StlMetadataPolicy::kRetainedSourceMetadata,
          "same metadata provenance is genuinely retained without stop");
    }
    CompareStop write_stop;
    write_stop.writing = true;
    const auto refused = detail::WriteStlForTest(
        source.document->Mesh(), options, f::WriteLimits(), f::Work(),
        write_stop.stop.get_token(), write_stop.Options());
    Require(
        write_stop.fired && write_stop.inspecting &&
            refused.status == ic::StlStatus::kCanceled && !refused.output &&
            refused.diagnostic.stage == ic::StlStage::kValidation &&
            refused.diagnostic.code == ic::StlDiagnosticCode::kStopRequested &&
            context.ActiveTrackedPayload() == baseline &&
            context.ActiveWorkers() == 0,
        "actual ASCII/binary provenance equality cancels without partial "
        "output");
  }
  EmptyLedger(context);
}

void ExactLimitsLedgerAndConcurrency() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  auto limits = f::ReadLimits();
  limits.max_source_bytes = f::kAscii.size();
  limits.max_name_bytes = f::kName.size();
  limits.max_facets = 2;
  auto result = ic::ReadStl(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                            limits, f::Work(), context);
  Require(result.document.has_value(),
          "inclusive real source/name/facet bounds");
  const auto options = f::Options(*result.document, ic::StlEncoding::kBinary);
  auto write_limits = f::WriteLimits();
  write_limits.max_output_bytes = f::kRecordOffset + 2 * f::kRecordBytes;
  auto output =
      ic::WriteStl(result.document->Mesh(), options, write_limits, f::Work());
  Require(output.output.has_value(),
          "exact literal binary output bound passes");
  output.output.reset();
  --write_limits.max_output_bytes;
  output =
      ic::WriteStl(result.document->Mesh(), options, write_limits, f::Work());
  Require(!output.output && output.status == ic::StlStatus::kLimitExceeded &&
              output.diagnostic.code == ic::StlDiagnosticCode::kOutputLimit,
          "one-less output cap refuses before any accepted partial bytes");
  const auto baseline = context.ActiveTrackedPayload();
  auto occupied = geo::TryReservePayload(
      context, context.TrackedPayloadBudget() - baseline);
  Require(occupied.status == geo::ReservationStatus::kAccepted,
          "actual remaining ledger headroom is occupied");
  output = ic::WriteStl(result.document->Mesh(), options, f::WriteLimits(),
                        f::Work());
  Require(output.status == ic::StlStatus::kBudgetExceeded && !output.output,
          "writer keeps original mesh ledger instead of bypassing it");
  occupied.lease.Reset();
  result.document.reset();
  EmptyLedger(context);
  std::array<bool, 2> successes{};
  std::array<std::exception_ptr, 2> errors{};
  {
    std::array<std::jthread, 2> callers;
    for (std::size_t i = 0; i < callers.size(); ++i)
      callers[i] = std::jthread([&, i] {
        try {
          const auto read =
              ic::ReadStl(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                          f::ReadLimits(), f::Work(), context);
          successes[i] = read.status == ic::StlStatus::kSuccess &&
                         read.document.has_value();
          if (read.document) f::CheckDocument(*read.document);
        } catch (...) {
          errors[i] = std::current_exception();
        }
      });
  }
  for (std::size_t i = 0; i < successes.size(); ++i) {
    if (errors[i]) std::rethrow_exception(errors[i]);
    Require(successes[i], "independent concurrent codec calls complete");
  }
  EmptyLedger(context);
}

std::string GrowthWire() {
  constexpr std::size_t kFacets = 1024;
  constexpr std::string_view kFacet =
      "facet normal 0 0 -2\nouter loop\nvertex -0 0 0\nvertex 1 0 0\nvertex 0 "
      "1 0\nendloop\nendfacet\n";
  std::string wire = "solid growth\n";
  for (std::size_t i = 0; i < kFacets; ++i) wire += kFacet;
  return wire + "endsolid growth\n";
}

enum class GrowthAction { kObserve, kStop, kOccupy, kBadAlloc, kRuntime };
struct GrowthProbe {
  GrowthAction action;
  Backend* backend;
  const geo::ExecutionContext* context;
  std::stop_source stop;
  geo::PayloadLease occupied;
  std::size_t baseline;
  bool observed = false, physical_overlap = false, valid_progress = true;
  std::uint64_t previous = 0, stop_copied = 0, stop_total = 0;
  static void Check(ic::StlStage, std::uint64_t copied, std::uint64_t total,
                    void* state) noexcept {
    auto& self = *static_cast<GrowthProbe*>(state);
    constexpr std::uint64_t kMinimumTransfer = 4096, kInteriorStop = 64,
                            kByteInterval = 16;
    if (total < kMinimumTransfer) return;
    if (copied == 0) self.previous = 0;
    self.valid_progress &= copied <= total && copied >= self.previous &&
                           copied - self.previous <= kByteInterval;
    self.previous = copied;
    if (self.observed || copied < kInteriorStop || copied == total) return;
    self.observed = true;
    self.stop_copied = copied;
    self.stop_total = total;
    try {
      const auto old_request = ic::gltf_detail::AllocationRequestBytes(
          static_cast<std::size_t>(total), alignof(std::byte));
      bool original_live = false;
      for (const auto& block : self.backend->blocks)
        if (block.pointer && block.bytes == old_request) original_live = true;
      self.physical_overlap =
          original_live &&
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
    auto result = backend->Options();
    result.relocation_checkpoint = &Check;
    result.state = this;
    return result;
  }
};

void RelocationControls() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  const auto wire = GrowthWire();
  auto source = ic::ReadStl(f::Bytes(wire), ic::StlEncoding::kAscii,
                            f::ReadLimits(), f::Work(), context);
  Require(source.document.has_value(), "genuine large relocation source");
  const auto baseline = context.ActiveTrackedPayload();
  for (const auto action :
       {GrowthAction::kObserve, GrowthAction::kStop, GrowthAction::kOccupy,
        GrowthAction::kBadAlloc, GrowthAction::kRuntime}) {
    Backend backend;
    GrowthProbe probe{action, &backend, &context, {}, {}, baseline};
    bool propagated = false;
    try {
      const auto result = detail::WriteStlForTest(
          source.document->Mesh(),
          f::Options(*source.document, ic::StlEncoding::kAscii),
          f::WriteLimits(), f::Work(), probe.stop.get_token(), probe.Options());
      if (action == GrowthAction::kObserve) {
        Require(result.output.has_value(),
                "large actual output relocation completes");
        const auto reload =
            ic::ReadStl(result.output->Bytes(), ic::StlEncoding::kAscii,
                        f::ReadLimits(), f::Work(), context);
        Require(reload.document.has_value(),
                "actual large serialized bytes independently reload");

        const auto view = reload.document->Mesh().View();
        Require(view.positions.size() == 9216 &&
                    view.face_offsets.size() == 1025 &&
                    view.corner_vertices.size() == 3072 &&
                    view.attributes.size() == 1,
                "large literal STL soup and normal rows reload");
        for (std::size_t i = 0; i < view.corner_vertices.size(); ++i)
          Require(view.corner_vertices[i] == i,
                  "no-weld soup IDs retain identity");

      } else
        Require(result.status == (action == GrowthAction::kStop
                                      ? ic::StlStatus::kCanceled
                                      : ic::StlStatus::kBudgetExceeded) &&
                    !result.output,
                "real mid-transfer cancellation/overlapping shared cap refuses "
                "complete output");
    } catch (const std::bad_alloc&) {
      Require(action == GrowthAction::kBadAlloc,
              "only armed next growth bad_alloc");
      propagated = true;
    } catch (const RuntimeMarker&) {
      Require(action == GrowthAction::kRuntime,
              "only armed next growth runtime exception");
      propagated = true;
    }
    Require(probe.observed && probe.physical_overlap && probe.valid_progress &&
                probe.stop_copied < probe.stop_total,
            "actual completed-copy indices obey byte intervals while original "
            "old/new blocks overlap");
    Require(propagated == (action == GrowthAction::kBadAlloc ||
                           action == GrowthAction::kRuntime),
            "actual next growth allocation exception propagated");
    probe.occupied.Reset();
    Require(backend.live == 0 && context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "all temporary growth blocks rollback, original source only");
  }
  source.document.reset();
  EmptyLedger(context);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported = argc == 2 && argv[1] == kUnsupportedOption;
    Require(argc == 1 || unsupported, "unknown STL request test argument");
    LimitsAndEarlyControls();
    if (!unsupported) {
      RelocationControls();
      OwnershipAndAccounting();
      OwnersOutliveOriginalContext();
      ActualFaultsAndStops();
      MetadataComparisonCancellation();
      ExactLimitsLedgerAndConcurrency();
    } else {
      geo::ExecutionContext context({4, 1, kContextBytes});
      const auto result =
          ic::ReadStl(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                      f::ReadLimits(), f::Work(), context);
      Require(
          result.status == ic::StlStatus::kGeometryFailure &&
              result.diagnostic.geometry_status ==
                  geo::MaterializationStatus::kUnsupportedAllocationProfile &&
              !result.document,
          "actual unsupported numerical profile is not skipped");
      EmptyLedger(context);
    }
    std::cout << "STL real limits, allocation, ownership, cancellation and "
                 "ledger controls passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
