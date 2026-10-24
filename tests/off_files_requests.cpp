// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "off_file_internal.h"
#include "off_files_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::off_files_detail;
namespace allocation = ic::gltf_detail;
namespace f = ic::off_files_fixture;
namespace fs = std::filesystem;
using f::Require;
using Status = ic::OffFileStatus;
using Stage = ic::OffFileStage;
using Code = ic::OffFileDiagnosticCode;
constexpr std::size_t kMaximumFaultAttempts = 512;
struct RuntimeMarker {};

struct Backend {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
  };
  std::array<Block, 4096> blocks{};
  allocation::AllocationBackend original =
      allocation::DefaultAllocationBackend();
  std::size_t requests = 0, live = 0, fail_at = 0;
  bool runtime = false, frozen = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Backend*>(state);
    ++self.requests;
    if (self.frozen || (self.fail_at && self.fail_at == self.requests)) {
      if (self.runtime) throw RuntimeMarker{};
      throw std::bad_alloc{};
    }
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
    detail::TestOptions test;
    test.allocation_backend = {&Allocate, &Free, this};
    return test;
  }
};

struct Workers {
  allocation::NativeWorkerBackend original =
      allocation::DefaultNativeWorkerBackend();
  std::size_t attempts = 0, launched = 0, joined = 0, fail_at = 0;
  std::atomic<std::size_t> entered{0};
  allocation::OperationWorkerObserver observer{nullptr, nullptr, &Entered,
                                               this};
  static std::uintptr_t Launch(allocation::NativeWorkerRecord* record,
                               void* state) {
    auto& self = *static_cast<Workers*>(state);
    if (++self.attempts == self.fail_at) throw RuntimeMarker{};
    const auto handle = self.original.launch(record, self.original.state);
    ++self.launched;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& self = *static_cast<Workers*>(state);
    self.original.join(handle, self.original.state);
    ++self.joined;
  }
  static void Entered(std::size_t, void* state) noexcept {
    ++static_cast<Workers*>(state)->entered;
  }
  allocation::NativeExecutionOptions Options() {
    return {{&Launch, &Join, this}, &observer};
  }
};

enum class Corruption {
  kNone,
  kEntry,
  kReceipt,
  kExtra,
  kDirectory,
  kIdentity
};
struct Probe {
  fs::path input, destination, stage, moved;
  std::string entry = "source.off";
  std::atomic<std::size_t> opened{0}, closed{0}, reads{0};
  std::atomic<bool> changed{false}, wrote{false}, stopped{false};
  std::stop_source stop;
  Stage stop_stage = Stage::kComplete;
  std::uint64_t stop_offset = 0;
  bool require_closed_for_stop = false;
  bool change_input = false, corrupt_after_write = false, corrupt_done = false;
  bool fail_close = false, fail_commit = false, unsupported_commit = false;
  bool fail_cleanup = false, throw_stage = false, race = false;
  bool committed = false;
  Backend* freeze = nullptr;
  bool stop_after_commit = false;
  Corruption corruption = Corruption::kNone;
  std::size_t corrupt_inventory = 2, inventories = 0, cleanup_calls = 0;
  ic::OffFileCleanupOutcome cleanup = ic::OffFileCleanupOutcome::kNotRequired;
  std::error_code cleanup_error;
  static void Opened(std::uint64_t, void* state) {
    ++static_cast<Probe*>(state)->opened;
  }
  static void Closed(std::uint64_t, void* state) noexcept {
    ++static_cast<Probe*>(state)->closed;
  }
  static void BeforeRead(std::uint64_t, std::uint64_t offset, void* state) {
    auto& self = *static_cast<Probe*>(state);
    ++self.reads;
    if (self.change_input && offset == 0 && !self.changed.exchange(true)) {
      std::ofstream output(self.input, std::ios::binary | std::ios::app);
      output.put(' ');
      output.close();
      Require(output.good(), "owned source modification after native metadata");
    }
  }
  static void Checkpoint(Stage stage, std::uint64_t offset, void* state) {
    auto& self = *static_cast<Probe*>(state);
    if (stage == Stage::kStaging) self.wrote = true;
    if (stage == self.stop_stage && offset >= self.stop_offset &&
        (!self.require_closed_for_stop || self.opened == self.closed)) {
      self.stopped = true;
      self.stop.request_stop();
    }
  }
  static void Ready(detail::NativeView stage, void* state) {
    auto& self = *static_cast<Probe*>(state);
    self.stage = fs::path(stage);
    if (self.throw_stage) throw RuntimeMarker{};
  }
  static void Inventory(detail::NativeView stage, void* state) {
    auto& self = *static_cast<Probe*>(state);
    ++self.inventories;
    if (self.inventories != self.corrupt_inventory) return;
    const auto path = fs::path(stage);
    if (self.corruption == Corruption::kEntry ||
        self.corruption == Corruption::kReceipt) {
      const auto member = self.corruption == Corruption::kEntry
                              ? path / self.entry
                              : path / "receipt.bin";
      auto bytes = f::Read(member);
      Require(!bytes.empty(), "owned staged corruption has actual payload");
      bytes[0] ^= std::byte{1};
      f::Write(member, bytes);
    } else if (self.corruption == Corruption::kExtra) {
      f::Write(path / "unexpected", "extra");
    } else if (self.corruption == Corruption::kDirectory) {
      Require(fs::remove(path / self.entry),
              "replace actual staged entry kind");
      Require(fs::create_directory(path / self.entry),
              "owned staged directory kind");
    } else if (self.corruption == Corruption::kIdentity) {
      self.moved = path;
      self.moved += ".original";
      fs::rename(path, self.moved);
      Require(fs::create_directory(path), "owned replacement stage identity");
      f::Write(path / "foreign", "preserve");
    }
  }
  static void BeforeCommit(detail::NativeView, void* state) {
    auto& self = *static_cast<Probe*>(state);
    if (self.race) {
      Require(fs::create_directory(self.destination),
              "actual racing destination");
      f::Write(self.destination / "sentinel", "preserve");
    }
  }
  static void Committed(void* state) noexcept {
    auto& self = *static_cast<Probe*>(state);
    self.committed = true;
    if (self.freeze) self.freeze->frozen = true;
    if (self.stop_after_commit) self.stop.request_stop();
  }
  static std::error_code Close(std::uint64_t, void* state) {
    auto& self = *static_cast<Probe*>(state);
    if (self.corrupt_after_write && self.wrote && !self.corrupt_done) {
      self.corrupt_done = true;
      f::Write(self.stage / self.entry, "NOT OFF\n");
    }
    return self.fail_close ? std::make_error_code(std::errc::io_error)
                           : std::error_code{};
  }
  static std::error_code Commit(void* state) {
    auto& self = *static_cast<Probe*>(state);
    if (self.unsupported_commit)
      return std::make_error_code(std::errc::function_not_supported);
    return self.fail_commit ? std::make_error_code(std::errc::permission_denied)
                            : std::error_code{};
  }
  static std::error_code Cleanup(detail::NativeView, void* state) noexcept {
    return static_cast<Probe*>(state)->fail_cleanup
               ? std::make_error_code(std::errc::device_or_resource_busy)
               : std::error_code{};
  }
  static void Cleaned(ic::OffFileCleanupOutcome outcome, std::error_code error,
                      void* state) noexcept {
    auto& self = *static_cast<Probe*>(state);
    ++self.cleanup_calls;
    self.cleanup = outcome;
    self.cleanup_error = error;
  }
  detail::TestOptions Options(Backend* backend = nullptr) {
    auto test = backend ? backend->Options() : detail::TestOptions{};
    test.files = {nullptr, &Opened, &Closed, &BeforeRead, this};
    test.checkpoint = &Checkpoint;
    test.stage_ready = &Ready;
    test.before_inventory = &Inventory;
    test.before_commit = &BeforeCommit;
    test.native_committed = &Committed;
    test.close_error = &Close;
    test.commit_error = &Commit;
    test.cleanup_error = &Cleanup;
    test.cleanup_finished = &Cleaned;
    test.state = this;
    return test;
  }
};

struct Receipt {
  std::vector<std::byte> bytes;
  std::size_t calls = 0;
  bool allocation_failure = false, runtime_failure = false;
  Backend* arm_import = nullptr;
  bool import_runtime_failure = false;
  explicit Receipt(std::size_t count = 1024) : bytes(count, std::byte{0xa7}) {}
  static std::span<const std::byte> Provide(
      const ic::OffFileVerifiedContent& content, void* state) {
    auto& self = *static_cast<Receipt*>(state);
    ++self.calls;
    Require(content.document.Inspection().IsStorageValid(),
            "receipt follows actual staged storage verification");
    if (self.allocation_failure) throw std::bad_alloc{};
    if (self.runtime_failure) throw RuntimeMarker{};
    if (self.arm_import) {
      self.arm_import->fail_at = self.arm_import->requests + 1;
      self.arm_import->runtime = self.import_runtime_failure;
    }
    return self.bytes;
  }
  ic::OffFilePublicationOptions Options() {
    return {ic::OffFileReceiptProvider{"receipt.bin", &Provide, this}};
  }
};

ic::OffFileAsset Capture(const fs::path& input,
                         const geo::ExecutionContext& context) {
  auto result =
      ic::ReadOffFileAsset(input, f::ReadLimits(), f::ReadWork(), context);
  Require(result.asset.has_value(),
          "request fixture captures actual regular file");
  return *result.asset;
}
void Failed(const ic::OffFilePublicationResult& result, Status status,
            Code code) {
  Require(result.outcome == ic::OffFilePublicationOutcome::kFailed &&
              !result.bundle && result.diagnostic.status == status &&
              result.diagnostic.code == code,
          "actual failed publication retains precise primary status/code");
  f::Joined(result.execution);
}
void Admission(bool unsupported) {
  f::Root root;
  const auto input = root.Path() / "source.off";
  f::Write(input, unsupported ? f::kEmpty : f::kSource);
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  Backend backend;
  for (auto member : {&ic::OffFileLimits::max_entry_bytes,
                      &ic::OffFileLimits::max_receipt_bytes,
                      &ic::OffFileLimits::max_total_file_bytes,
                      &ic::OffFileLimits::max_entry_name_bytes,
                      &ic::OffFileLimits::max_receipt_name_bytes,
                      &ic::OffFileLimits::max_native_path_code_units,
                      &ic::OffFileLimits::max_file_new_requested_bytes}) {
    auto invalid = f::ReadLimits();
    invalid.files.*member = 0;
    const auto refused = detail::ReadForTesting(input, invalid, f::ReadWork(),
                                                context, {}, backend.Options());
    Require(
        refused.status == Status::kInvalidConfiguration && !refused.asset &&
            refused.diagnostic.code == Code::kRequiredFieldZero &&
            backend.requests == 0,
        "every file limit, even unused receipt limits, validates before IO");
  }
  auto limits = f::ReadLimits();
  limits.off.inspection.max_issues = 0;
  auto result = detail::ReadForTesting(input, limits, f::ReadWork(), context,
                                       {}, backend.Options());
  Require(result.status == Status::kInvalidConfiguration && !result.asset &&
              result.diagnostic.code == Code::kRequiredFieldZero &&
              backend.requests == 0,
          "nested configuration rejected before path or source allocation");
  limits = f::ReadLimits();
  limits.files.max_native_path_code_units =
      std::numeric_limits<std::uint64_t>::max();
  result = detail::ReadForTesting(input, limits, f::ReadWork(), context, {},
                                  backend.Options());
  Require(result.status == Status::kInvalidConfiguration && !result.asset &&
              result.diagnostic.code == Code::kConfigurationExtent &&
              backend.requests == 0,
          "native path products/host overflow refused before allocation");
  limits = f::ReadLimits();
  limits.files.max_entry_bytes =
      (unsupported ? f::kEmpty : f::kSource).size() - 1;
  result = detail::ReadForTesting(input, limits, f::ReadWork(), context, {},
                                  backend.Options());
  Require(result.status == Status::kLimitExceeded && !result.asset &&
              backend.live == 0,
          "actual observed source size admitted before payload allocation");
  std::stop_source stopped;
  stopped.request_stop();
  result =
      detail::ReadForTesting(input, f::ReadLimits(), f::ReadWork(), context,
                             stopped.get_token(), backend.Options());
  Require(
      result.status == Status::kCanceled && !result.asset && backend.live == 0,
      "pre-stopped capture has no owner");
  auto exact = f::ReadLimits();
  exact.files.max_entry_bytes = (unsupported ? f::kEmpty : f::kSource).size();
  result = detail::ReadForTesting(input, exact, f::ReadWork(), context, {},
                                  backend.Options());
  Require(result.asset.has_value(), "exact inclusive physical source byte cap");
  result.asset.reset();
  auto path_cap = f::ReadLimits();
  path_cap.files.max_native_path_code_units = input.native().size();
  auto path_refused = detail::ReadForTesting(input, path_cap, f::ReadWork(),
                                             context, {}, backend.Options());
  Require(path_refused.status == Status::kLimitExceeded &&
              !path_refused.asset && backend.live == 0,
          "native path cap includes its required terminator");
  auto name_cap = f::ReadLimits();
  name_cap.files.max_entry_name_bytes = std::string_view("source.off").size();
  auto name_exact =
      ic::ReadOffFileAsset(input, name_cap, f::ReadWork(), context);
  Require(name_exact.asset.has_value(),
          "exact inclusive portable source-name cap");
  name_exact.asset.reset();
  --name_cap.files.max_entry_name_bytes;
  auto name_refused =
      ic::ReadOffFileAsset(input, name_cap, f::ReadWork(), context);
  Require(name_refused.status == Status::kLimitExceeded && !name_refused.asset,
          "one-less source-name cap refuses before capture");
  const auto missing = ic::ReadOffFileAsset(
      root.Path() / "missing.off", f::ReadLimits(), f::ReadWork(), context);
  Require(!missing.asset && missing.status == Status::kIoFailure,
          "missing source is an actual attempted IO outcome");
  f::Empty(context);
}

void CaptureRequests(bool empty) {
  f::Root root;
  const auto input = root.Path() / "source.off";
  const auto raw = f::LargeSource(empty);
  f::Write(input, raw);
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  bool reached = false;
  for (std::size_t nth = 1; nth <= kMaximumFaultAttempts; ++nth) {
    Backend backend;
    backend.fail_at = nth;
    Probe handles;
    handles.input = input;
    try {
      auto result =
          detail::ReadForTesting(input, f::ReadLimits(), f::ReadWork(), context,
                                 {}, handles.Options(&backend));
      Require(result.asset.has_value(),
              "fault sequence reaches actual capture success");
      Require(result.asset->NewRequestedBytes() == backend.live &&
                  context.ActiveTrackedPayload() ==
                      backend.live +
                          result.asset->Document().NewRequestedBytes().total,
              "physical original backend proves disjoint file/document live "
              "attribution");
      reached = true;
      const auto requests = backend.requests;
      backend.frozen = true;
      auto copy = *result.asset;
      auto other = copy;
      other = copy;
      Require(backend.requests == requests &&
                  copy.EntryBytes().data() == other.EntryBytes().data(),
              "strong file copies allocate nothing and share actual storage");
    } catch (const std::bad_alloc&) {
      Require(backend.requests == nth,
              "original backend fails exact native allocation");
    }
    Require(backend.live == 0 && handles.opened == handles.closed,
            "capture allocation failure/final release closes handles and frees "
            "original backend");
    f::Empty(context);
    if (reached) break;
  }
  Require(reached, "all actual file capture allocation points exercised");
  Backend unexpected;
  unexpected.fail_at = 3;
  unexpected.runtime = true;
  bool propagated = false;
  try {
    static_cast<void>(detail::ReadForTesting(input, f::ReadLimits(),
                                             f::ReadWork(), context, {},
                                             unexpected.Options()));
  } catch (const RuntimeMarker&) {
    propagated = true;
  }
  Require(propagated && unexpected.live == 0,
          "unexpected native backend exception propagates after cleanup");
  Probe change;
  change.input = input;
  change.change_input = true;
  auto changed = detail::ReadForTesting(input, f::ReadLimits(), f::ReadWork(),
                                        context, {}, change.Options());
  Require(change.changed && changed.status == Status::kFileChanged &&
              !changed.asset && changed.diagnostic.code == Code::kChanged &&
              change.opened == change.closed,
          "actual source modification after metadata is not silently parsed");
  f::Write(input, raw);
  Probe close;
  close.fail_close = true;
  auto failed_close = detail::ReadForTesting(
      input, f::ReadLimits(), f::ReadWork(), context, {}, close.Options());
  Require(failed_close.status == Status::kIoFailure && !failed_close.asset &&
              failed_close.diagnostic.code == Code::kClose &&
              close.opened == close.closed,
          "actual closed handle error remains typed capture close failure");
  {
    Probe cancel;
    cancel.stop_stage = Stage::kCapture;
    cancel.stop_offset = 64;
    auto canceled =
        detail::ReadForTesting(input, f::ReadLimits(), f::ReadWork(), context,
                               cancel.stop.get_token(), cancel.Options());
    Require(cancel.stopped && cancel.reads >= 4 &&
                canceled.status == Status::kCanceled && !canceled.asset &&
                cancel.opened == cancel.closed,
            "serial interior cancellation follows four genuine completed read "
            "blocks");
    Workers workers;
    auto test = detail::TestOptions{};
    test.workers = workers.Options();
    auto parallel = detail::ReadForTesting(
        input, f::ReadLimits(), f::ReadWork(true), context, {}, test);
    Require(parallel.asset && workers.launched > 0 &&
                workers.launched == workers.joined && workers.entered > 0 &&
                parallel.execution.capture.launched_workers == workers.launched,
            "actual shared native file workers launch and join");
    f::Joined(parallel.execution);
    parallel.asset.reset();
    Workers partial;
    partial.fail_at = 2;
    test.workers = partial.Options();
    propagated = false;
    try {
      static_cast<void>(detail::ReadForTesting(
          input, f::ReadLimits(), f::ReadWork(true), context, {}, test));
    } catch (const RuntimeMarker&) {
      propagated = true;
    }
    Require(propagated && partial.launched == 1 && partial.joined == 1,
            "real partial launch joins the already-started native worker "
            "before throw");
  }
  f::Empty(context);
}

void FileContentsAndKinds() {
  f::Root root;
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  const auto opaque = root.Path() / "native.meshdata";
  f::Write(opaque, f::kEmpty);
  {
    const auto captured =
        ic::ReadOffFileAsset(opaque, f::ReadLimits(), f::ReadWork(), context);
    Require(captured.asset && captured.asset->EntryPath() == "native.meshdata",
            "complete OFF file is admitted without suffix dispatch");
  }
  const auto malformed = root.Path() / "malformed.off";
  for (const std::string_view text : {"NOT OFF\n", ""}) {
    f::Write(malformed, text);
    const auto result = ic::ReadOffFileAsset(malformed, f::ReadLimits(),
                                             f::ReadWork(), context);
    Require(
        result.status == Status::kOffFailure && !result.asset &&
            result.diagnostic.stage == Stage::kOffRead &&
            result.diagnostic.off_status == ic::OffStatus::kMalformed,
        "real malformed/zero-byte source retains owning OFF delegate failure");
  }
  const auto directory = root.Path() / "directory.off";
  Require(fs::create_directory(directory), "owned non-regular capture control");
  const auto result =
      ic::ReadOffFileAsset(directory, f::ReadLimits(), f::ReadWork(), context);
#ifdef _WIN32
  Require(!result.asset && result.status == Status::kIoFailure &&
              result.diagnostic.code == Code::kOpen,
          "Windows ordinary file open refuses a directory handle");
#else
  Require(!result.asset && result.status == Status::kUnsupportedProfile &&
              result.diagnostic.code == Code::kNotRegular,
          "POSIX native profile refuses non-regular capture");
#endif
  f::Empty(context);
}

void PublicationRequests(bool unsupported) {
  f::Root root;
  const auto input = root.Path() / "source.off";
  f::Write(input, unsupported ? f::kEmpty : f::kSource);
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  {
    auto source = Capture(input, context);
    auto output = f::Output(source, source.Document().Mesh());
    const auto baseline = context.ActiveTrackedPayload();
    const ic::OffFilePublicationRequest request{
        source, source.Document().Mesh(), output};
    bool reached = false;
    std::stop_source before_work;
    before_work.request_stop();
    const auto before = ic::PublishOffBundle(
        request, root.Path() / "pre-stopped", {}, f::PublishLimits(),
        f::PublishWork(), before_work.get_token());
    Require(before.outcome == ic::OffFilePublicationOutcome::kCanceled &&
                !before.bundle &&
                before.cleanup == ic::OffFileCleanupOutcome::kNotRequired,
            "pre-stopped publication creates no stage or partial bundle");
    for (std::size_t nth = 1; nth <= kMaximumFaultAttempts; ++nth) {
      Backend backend;
      backend.fail_at = nth;
      Probe probe;
      const auto destination = root.Path() / std::to_string(nth);
      try {
        auto result = detail::PublishForTesting(
            request, destination, {}, f::PublishLimits(), f::PublishWork(), {},
            probe.Options(&backend));
        Require(result.bundle.has_value(),
                "publication faults reach actual native commit");
        reached = true;
        const auto count = backend.requests;
        backend.frozen = true;
        auto copy = *result.bundle;
        Require(
            copy.EntryBytes().data() == result.bundle->EntryBytes().data() &&
                backend.requests == count,
            "published strong copy has no allocation");
      } catch (const std::bad_alloc&) {
        Require(backend.requests == nth,
                "exact original publication allocation fault");
      }
      Require(backend.live == 0 && context.ActiveTrackedPayload() == baseline &&
                  context.ActiveWorkers() == 0 && probe.opened == probe.closed,
              "publication exception/final release restores exact original "
              "ledger and handles");
      if (!reached)
        Require(!fs::exists(destination),
                "failed allocation cannot publish a partial directory");
      if (reached) break;
    }
    Require(reached,
            "every file-module publication allocation point exercised");
    auto occupied = geo::TryReservePayload(
        context, context.TrackedPayloadBudget() - baseline);
    Require(occupied.status == geo::ReservationStatus::kAccepted,
            "genuine remaining shared budget is occupied");
    auto refused = ic::PublishOffBundle(request, root.Path() / "occupied", {},
                                        f::PublishLimits(), f::PublishWork());
    Failed(refused, Status::kBudgetExceeded, Code::kSharedBudget);
    occupied.lease.Reset();
    auto own = f::PublishLimits();
    own.files.max_file_new_requested_bytes = 1;
    refused = ic::PublishOffBundle(request, root.Path() / "own-cap", {}, own,
                                   f::PublishWork());
    Failed(refused, Status::kLimitExceeded, Code::kRequestBudget);
    Receipt provider;
    auto cap = f::PublishLimits();
    cap.files.max_receipt_bytes = provider.bytes.size() - 1;
    refused = ic::PublishOffBundle(request, root.Path() / "receipt-cap",
                                   provider.Options(), cap, f::PublishWork());
    Failed(refused, Status::kLimitExceeded, Code::kCountLimit);
    Require(provider.calls == 1 &&
                refused.cleanup == ic::OffFileCleanupOutcome::kRemoved,
            "actual supplied receipt extent refuses before import and removes "
            "owned stage");
    {
      Workers workers;
      auto test = detail::TestOptions{};
      test.workers = workers.Options();
      Receipt supplied;
      auto result = detail::PublishForTesting(
          request, root.Path() / "real-copy-workers", supplied.Options(),
          f::PublishLimits(), f::PublishWork(true), {}, test);
      Require(result.bundle && supplied.calls == 1 && workers.launched > 0 &&
                  workers.launched == workers.joined && workers.entered > 0 &&
                  result.execution.copying.launched_workers > 0 &&
                  result.execution.byte_verification.launched_workers > 0 &&
                  context.ActiveWorkers() == 0,
              "actual receipt import and staged byte verification use joined "
              "shared workers");
      f::Joined(result.execution);
    }
    provider.calls = 0;
    provider.runtime_failure = true;
    refused = ic::PublishOffBundle(request, root.Path() / "provider-runtime",
                                   provider.Options(), f::PublishLimits(),
                                   f::PublishWork());
    Failed(refused, Status::kProviderFailure, Code::kProvider);
    provider.runtime_failure = false;
    provider.allocation_failure = true;
    Probe cleanup;
    bool propagated = false;
    try {
      static_cast<void>(detail::PublishForTesting(
          request, root.Path() / "provider-alloc", provider.Options(),
          f::PublishLimits(), f::PublishWork(), {}, cleanup.Options()));
    } catch (const std::bad_alloc&) {
      propagated = true;
    }
    Require(
        propagated && cleanup.cleanup_calls == 1 &&
            cleanup.cleanup == ic::OffFileCleanupOutcome::kRemoved,
        "provider bad_alloc propagates only after actual joined stage cleanup");
    for (const bool runtime : {false, true}) {
      Backend importing;
      Receipt supplied;
      supplied.arm_import = &importing;
      supplied.import_runtime_failure = runtime;
      Probe cleaned;
      bool expected_exception = false;
      try {
        static_cast<void>(detail::PublishForTesting(
            request,
            root.Path() / (runtime ? "import-runtime" : "import-alloc"),
            supplied.Options(), f::PublishLimits(), f::PublishWork(), {},
            cleaned.Options(&importing)));
      } catch (const std::bad_alloc&) {
        Require(!runtime, "only armed import bad_alloc propagates");
        expected_exception = true;
      } catch (const RuntimeMarker&) {
        Require(runtime, "only armed import runtime exception propagates");
        expected_exception = true;
      }
      Require(expected_exception && supplied.calls == 1 &&
                  importing.requests == importing.fail_at &&
                  importing.live == 0 &&
                  cleaned.cleanup == ic::OffFileCleanupOutcome::kRemoved &&
                  context.ActiveTrackedPayload() == baseline,
              "actual post-provider receipt import allocation cleans up and "
              "propagates");
    }
    for (const auto stage :
         {Stage::kStaging, Stage::kVerification, Stage::kProvider}) {
      Probe stop;
      stop.stop_stage = stage;
      stop.stop_offset =
          stage == Stage::kProvider ? 64u : (unsupported ? 0u : 64u);
      Receipt supplied;
      auto result = detail::PublishForTesting(
          request, root.Path() / std::to_string(static_cast<int>(stage)),
          supplied.Options(), f::PublishLimits(), f::PublishWork(),
          stop.stop.get_token(), stop.Options());
      Require(stop.stopped &&
                  result.outcome == ic::OffFilePublicationOutcome::kCanceled &&
                  !result.bundle &&
                  result.cleanup == ic::OffFileCleanupOutcome::kRemoved &&
                  stop.opened == stop.closed,
              "actual publication phase stop exposes no partial bundle and "
              "cleans stage");
      f::Joined(result.execution);
    }
    Require(context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "all request failures leave only original strong source/output");
  }
  f::Empty(context);
}

void Transactions(bool unsupported) {
  f::Root root;
  const auto input = root.Path() / "source.off";
  f::Write(input, unsupported ? f::kEmpty : f::kSource);
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  {
    auto source = Capture(input, context);
    auto output = f::Output(source, source.Document().Mesh());
    const ic::OffFilePublicationRequest request{
        source, source.Document().Mesh(), output};
    const auto baseline = context.ActiveTrackedPayload();
    for (const auto kind :
         {Corruption::kEntry, Corruption::kReceipt, Corruption::kExtra,
          Corruption::kDirectory, Corruption::kIdentity}) {
      Probe p;
      p.corruption = kind;
      Receipt receipt;
      const auto dest =
          root.Path() / ("corrupt-" + std::to_string(static_cast<int>(kind)));
      auto result = detail::PublishForTesting(
          request, dest, receipt.Options(), f::PublishLimits(),
          f::PublishWork(), {}, p.Options());
      Require(!result.bundle &&
                  result.outcome == ic::OffFilePublicationOutcome::kFailed &&
                  !fs::exists(dest) &&
                  result.diagnostic.status == Status::kPreservationMismatch,
              "real staged byte/kind/inventory/identity corruption is never "
              "published");
      if (kind == Corruption::kIdentity) {
        Require(
            result.cleanup == ic::OffFileCleanupOutcome::kFailed &&
                result.cleanup_error && fs::exists(p.stage / "foreign") &&
                fs::exists(p.moved / input.filename()),
            "cleanup never removes a stage whose owned identity was replaced");
      }
    }
    Probe malformed;
    malformed.corrupt_after_write = true;
    auto result = detail::PublishForTesting(
        request, root.Path() / "malformed", {}, f::PublishLimits(),
        f::PublishWork(), {}, malformed.Options());
    Require(malformed.corrupt_done && !result.bundle &&
                result.diagnostic.status == Status::kOffFailure &&
                result.diagnostic.off_status == ic::OffStatus::kMalformed &&
                result.diagnostic.stage == Stage::kReload,
            "actual malformed staged entry is rejected by the real delegated "
            "reload");
    Probe close;
    close.fail_close = true;
    result = detail::PublishForTesting(request, root.Path() / "close", {},
                                       f::PublishLimits(), f::PublishWork(), {},
                                       close.Options());
    Failed(result, Status::kIoFailure, Code::kClose);
    Probe commit;
    commit.fail_commit = true;
    commit.fail_cleanup = true;
    result = detail::PublishForTesting(request, root.Path() / "commit", {},
                                       f::PublishLimits(), f::PublishWork(), {},
                                       commit.Options());
    Failed(result, Status::kPublicationFailed, Code::kCommit);
    Require(result.cleanup == ic::OffFileCleanupOutcome::kFailed &&
                result.cleanup_error == std::errc::device_or_resource_busy &&
                result.diagnostic.system_error == std::errc::permission_denied,
            "secondary cleanup error cannot overwrite original commit failure");
    Probe unsupported_commit;
    unsupported_commit.unsupported_commit = true;
    const auto unsupported_destination = root.Path() / "unsupported-commit";
    result = detail::PublishForTesting(request, unsupported_destination, {},
                                       f::PublishLimits(), f::PublishWork(), {},
                                       unsupported_commit.Options());
    Failed(result, Status::kUnsupportedProfile, Code::kCommitUnsupported);
    Require(result.phase == Stage::kPublication &&
                result.diagnostic.system_error ==
                    std::errc::function_not_supported &&
                !fs::exists(unsupported_destination) &&
                result.cleanup == ic::OffFileCleanupOutcome::kRemoved &&
                unsupported_commit.cleanup_calls == 1 &&
                !fs::exists(unsupported_commit.stage),
            "actual unsupported no-replace boundary retains its primary "
            "diagnostic and removes only the owned stage without publication");
    f::Joined(result.execution);
    Probe race;
    race.race = true;
    race.destination = root.Path() / "race";
    result = detail::PublishForTesting(request, race.destination, {},
                                       f::PublishLimits(), f::PublishWork(), {},
                                       race.Options());
    Failed(result, Status::kDestinationExists, Code::kDestinationExists);
    Require(f::Read(race.destination / "sentinel") ==
                std::vector<std::byte>(f::Bytes("preserve").begin(),
                                       f::Bytes("preserve").end()),
            "actual racing destination remains intact without replacement");
    Probe exception;
    exception.throw_stage = true;
    bool propagated = false;
    try {
      static_cast<void>(detail::PublishForTesting(
          request, root.Path() / "unexpected", {}, f::PublishLimits(),
          f::PublishWork(), {}, exception.Options()));
    } catch (const RuntimeMarker&) {
      propagated = true;
    }
    Require(propagated &&
                exception.cleanup == ic::OffFileCleanupOutcome::kRemoved &&
                exception.cleanup_calls == 1 && !fs::exists(exception.stage),
            "unexpected native exception propagates after owned stage cleanup");
    Backend backend;
    Probe late;
    late.stop_after_commit = true;
    late.freeze = &backend;
    result = detail::PublishForTesting(
        request, root.Path() / "late", {}, f::PublishLimits(), f::PublishWork(),
        late.stop.get_token(), late.Options(&backend));
    Require(late.committed && late.stop.stop_requested() && result.bundle &&
                result.outcome == ic::OffFilePublicationOutcome::kPublished &&
                result.phase == Stage::kComplete && backend.frozen,
            "actual successful commit survives late stop and frozen allocation "
            "handoff");
    Require(result.bundle->NewRequestedBytes() == backend.live &&
                context.ActiveTrackedPayload() ==
                    baseline + backend.live +
                        result.bundle->Document().NewRequestedBytes().total,
            "physical publisher backend plus one staged document proves live "
            "accounting");
    const auto count = backend.requests;
    auto copy = *result.bundle;
    Require(backend.requests == count,
            "published owner copy after allocator freeze has no allocation");
    const auto after_commit = context.ActiveTrackedPayload();
    bool fresh_refused = false;
    try {
      static_cast<void>(detail::ReadForTesting(input, f::ReadLimits(),
                                               f::ReadWork(), context, {},
                                               backend.Options()));
    } catch (const std::bad_alloc&) {
      fresh_refused = true;
    }
    Require(fresh_refused && backend.requests == count + 1 &&
                context.ActiveTrackedPayload() == after_commit,
            "frozen original backend refuses genuine subsequent allocation");
    result.bundle.reset();
    Require(copy.EntryBytes().size() == output.Bytes().size(),
            "last original backend owner remains usable");
    Require(context.ActiveWorkers() == 0,
            "no workers remain after successful handoff");
    // copy still owns this backend, which deliberately outlives every free.
    Require(context.ActiveTrackedPayload() > baseline,
            "actual committed owner remains charged");
  }
  f::Empty(context);
}

void NamesAndEmptyAttributes() {
  f::Root root;
  const auto input = root.Path() / "source.off";
  f::Write(input, f::kEmpty);
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  {
    auto source = Capture(input, context);
    auto output = f::Output(source, source.Document().Mesh());
    const ic::OffFilePublicationRequest request{
        source, source.Document().Mesh(), output};
    Receipt receipt;
    for (const std::string_view name :
         {"", ".", "..", "../receipt", "a/b", "a\\b", "CON", "nul.txt",
          "trailing.", "trailing ", "has:colon", "control\nname"}) {
      auto options = receipt.Options();
      options.receipt->basename = name;
      const auto result =
          ic::PublishOffBundle(request, root.Path() / "invalid-name", options,
                               f::PublishLimits(), f::PublishWork());
      Failed(result, Status::kUnsupportedProfile, Code::kNameUnsupported);
      Require(receipt.calls == 0 && !fs::exists(root.Path() / "invalid-name"),
              "invalid portable member refuses before provider or staging");
    }
    auto absent = receipt.Options();
    absent.receipt->provide = nullptr;
    auto refused =
        ic::PublishOffBundle(request, root.Path() / "no-provider", absent,
                             f::PublishLimits(), f::PublishWork());
    Failed(refused, Status::kInvalidRequest, Code::kProvider);
    const auto existing = root.Path() / "existing-file";
    f::Write(existing, "preserve");
    const auto sentinel = f::Read(existing);
    refused = ic::PublishOffBundle(request, existing, {}, f::PublishLimits(),
                                   f::PublishWork());
    Failed(refused, Status::kDestinationExists, Code::kDestinationExists);
    Require(f::Read(existing) == sentinel,
            "existing physical file destination is never replaced");
#ifndef _WIN32
    const auto real = root.Path() / "real";
    Require(fs::create_directory(real), "owned ordinary source parent");
    f::Write(real / "source.off", f::kEmpty);
    const auto alias = root.Path() / "alias";
    fs::create_directory_symlink(real, alias);
    auto aliased = ic::ReadOffFileAsset(alias / "source.off", f::ReadLimits(),
                                        f::ReadWork(), context);
    Require(
        aliased.status == Status::kUnsupportedProfile && !aliased.asset &&
            aliased.diagnostic.code == Code::kPathUnsupported,
        "actual directory symlink is refused without normalizing its argument");
    const auto file_alias = root.Path() / "file-alias.off";
    fs::create_symlink(real / "source.off", file_alias);
    aliased = ic::ReadOffFileAsset(file_alias, f::ReadLimits(), f::ReadWork(),
                                   context);
    Require(aliased.status == Status::kUnsupportedProfile && !aliased.asset &&
                aliased.diagnostic.code == Code::kPathUnsupported,
            "actual final-component file symlink is refused");
    refused = ic::PublishOffBundle(request, alias / "out", {},
                                   f::PublishLimits(), f::PublishWork());
    Failed(refused, Status::kUnsupportedProfile, Code::kPathUnsupported);
    Require(!fs::exists(real / "out"),
            "unsupported aliased parent creates no output");
    const auto dangling = root.Path() / "dangling";
    fs::create_directory_symlink(root.Path() / "absent-target", dangling);
    refused = ic::PublishOffBundle(request, dangling, {}, f::PublishLimits(),
                                   f::PublishWork());
    Failed(refused, Status::kDestinationExists, Code::kDestinationExists);
    Require(fs::is_symlink(dangling),
            "existing dangling destination link remains intact");
#endif
    auto exact = f::PublishLimits();
    exact.files.max_entry_bytes = output.Bytes().size();
    exact.files.max_receipt_bytes = receipt.bytes.size();
    exact.files.max_total_file_bytes =
        output.Bytes().size() + receipt.bytes.size();
    exact.files.max_receipt_name_bytes = std::string_view("receipt.bin").size();
    {
      auto accepted =
          ic::PublishOffBundle(request, root.Path() / "exact",
                               receipt.Options(), exact, f::PublishWork());
      Require(accepted.bundle.has_value() && receipt.calls == 1,
              "exact entry/receipt/name/aggregate physical ceilings all admit");
    }
    receipt.calls = 0;
    --exact.files.max_total_file_bytes;
    refused = ic::PublishOffBundle(request, root.Path() / "total-minus-one",
                                   receipt.Options(), exact, f::PublishWork());
    Failed(refused, Status::kLimitExceeded, Code::kCountLimit);
    Require(
        receipt.calls == 1 && !fs::exists(root.Path() / "total-minus-one"),
        "one-less physical total refuses real supplied receipt transaction");
    ++exact.files.max_total_file_bytes;
    --exact.files.max_receipt_name_bytes;
    receipt.calls = 0;
    refused = ic::PublishOffBundle(request, root.Path() / "name-minus-one",
                                   receipt.Options(), exact, f::PublishWork());
    Failed(refused, Status::kLimitExceeded, Code::kCountLimit);
    Require(receipt.calls == 0, "receipt-name extent refuses before callback");
    geo::AttributeMaterializationInput attribute;
    attribute.domain = geo::AttributeDomain::face;
    attribute.name = "raw";
    attribute.components = 1;
    attribute.values = std::span<const std::uint32_t>{};
    const auto attributed = geo::MaterializeMesh(
        {std::span<const double>{},
         geo::PolygonTopologyInput{f::kEmptyOffsets,
                                   std::span<const geo::index_t>{}},
         std::span(&attribute, 1)},
        ic::off_fixture::MeshLimits(), ic::off_fixture::Work().geometry,
        context);
    Require(attributed.mesh.has_value() &&
                attributed.mesh->View().attributes.size() == 1,
            "real empty strong channel is structural, not fabricated metadata");
    refused = ic::PublishOffBundle({source, *attributed.mesh, output},
                                   root.Path() / "empty-attribute", {},
                                   f::PublishLimits(), f::PublishWork());
    Failed(refused, Status::kInvalidRequest, Code::kAttributeUnsupported);
  }
  f::Empty(context);
}

void OwnerAndBackendLifetime() {
  f::Root root;
  Backend backend;
  const auto input = root.Path() / "retained.off";
  f::Write(input, f::kEmpty);
  std::optional<ic::OffFileAsset> source;
  {
    geo::ExecutionContext original({4, 1, f::kContextBytes});
    auto captured = detail::ReadForTesting(
        input, f::ReadLimits(), f::ReadWork(), original, {}, backend.Options());
    Require(captured.asset.has_value(), "injected-backend lifetime source");
    source.emplace(*captured.asset);
  }
  Require(fs::remove(input), "actual original file and context both disappear");
  f::Check(source->Document().Mesh(), false, true);
  std::optional<ic::OffPublishedBundle> retained;
  {
    auto output = f::Output(*source, source->Document().Mesh());
    auto result = detail::PublishForTesting(
        {*source, source->Document().Mesh(), output}, root.Path() / "retained",
        {}, f::PublishLimits(), f::PublishWork(), {}, backend.Options());
    Require(result.bundle.has_value(),
            "actual publication after producing context/options/file death");
    retained.emplace(*result.bundle);
  }
  source.reset();
  f::Check(retained->Document().Mesh(), false, true);
  auto context = retained->ExpectedMesh().ShareExecutionContext();
  const auto count = backend.requests;
  backend.frozen = true;
  std::optional<ic::OffPublishedBundle> copy(*retained);
  retained.reset();
  Require(backend.requests == count && backend.live > 0,
          "original injected backend remains pinned by last strong copy");
  f::Check(copy->Source().Document().Mesh(), false, true);
  copy.reset();
  Require(backend.live == 0, "last release frees every original backend block");
  f::Empty(context);
}

void PrefixBeforeLaterStop() {
  f::Root root;
  const auto input = root.Path() / "source.off";
  f::Write(input, f::kSource);
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  {
    auto source = Capture(input, context);
    auto output = f::Output(source, source.Document().Mesh());
    const auto baseline = context.ActiveTrackedPayload();
    auto occupied = geo::TryReserveWorkers(context, 4);
    Require(occupied.Count() == 4,
            "occupy the genuine entire shared worker cap");
    Probe probe;
    probe.corruption = Corruption::kEntry;
    probe.stop_stage = Stage::kInventory;
    probe.stop_offset = 16;
    probe.require_closed_for_stop = true;
    auto work = f::PublishWork(true);
    work.files.partition_bytes = 16;
    const auto result = detail::PublishForTesting(
        {source, source.Document().Mesh(), output}, root.Path() / "prefix", {},
        f::PublishLimits(), work, probe.stop.get_token(), probe.Options());
    Require(probe.stopped && probe.stop.stop_requested(),
            "later actual ordered caller chunk observes cancellation");
    Failed(result, Status::kPreservationMismatch, Code::kByteMismatch);
    Require(result.diagnostic.byte_offset == 0 &&
                result.execution.byte_verification.caller_jobs > 1 &&
                result.cleanup == ic::OffFileCleanupOutcome::kRemoved,
            "known earlier byte mismatch stays primary over later stop and "
            "cleanup");
    occupied.Reset();
    Require(context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "caller fallback and cleanup release only their own reservations");
  }
  f::Empty(context);
}

void GenuineMismatches() {
  f::Root root;
  auto input = root.Path() / "source.off";
  f::Write(input, f::kSource);
  geo::ExecutionContext context({4, 1, f::kContextBytes}),
      foreign({4, 1, f::kContextBytes});
  {
    auto source = Capture(input, context);
    auto output = f::Output(source, source.Document().Mesh());
    auto changed = f::Candidate(source);
    auto wrong_count =
        ic::PublishOffBundle({source, changed, output}, root.Path() / "counts",
                             {}, f::PublishLimits(), f::PublishWork());
    Failed(wrong_count, Status::kPreservationMismatch, Code::kOutputFacts);
    const auto other_input = root.Path() / "edge18.off";
    const auto edge18 =
        ic::off_fixture::Replace(std::string(f::kSource), "8 3 17", "8 3 18");
    f::Write(other_input, edge18);
    auto provenance = Capture(other_input, context);
    auto rewritten =
        ic::WriteOff(provenance.Document().Mesh(),
                     ic::off_fixture::Options(&provenance.Document()),
                     ic::off_fixture::WriteLimits(), ic::off_fixture::Work());
    Require(rewritten.output &&
                rewritten.output->Facts().source_declared_edge_count == 18,
            "genuine other source creates different edge-provenance facts");
    auto wrong_provenance = ic::PublishOffBundle(
        {source, source.Document().Mesh(), *rewritten.output},
        root.Path() / "provenance", {}, f::PublishLimits(), f::PublishWork());
    Failed(wrong_provenance, Status::kPreservationMismatch, Code::kOutputFacts);
    auto other = geo::MaterializeMesh(
        {source.Document().Mesh().View().positions,
         geo::PolygonTopologyInput{f::kOffsets, f::kCorners},
         {}},
        ic::off_fixture::MeshLimits(), ic::off_fixture::Work().geometry,
        foreign);
    Require(other.mesh.has_value(),
            "real equal-options but distinct-ledger strong mesh");
    auto foreign_result = ic::PublishOffBundle(
        {source, *other.mesh, output}, root.Path() / "foreign", {},
        f::PublishLimits(), f::PublishWork());
    Failed(foreign_result, Status::kForeignBudget, Code::kLedgerMismatch);
    auto positions =
        std::vector<double>(source.Document().Mesh().View().positions.begin(),
                            source.Document().Mesh().View().positions.end());
    positions[0] = 0;
    auto different = geo::MaterializeMesh(
        {std::span<const double>(positions),
         geo::PolygonTopologyInput{f::kOffsets, f::kCorners},
         {}},
        ic::off_fixture::MeshLimits(), ic::off_fixture::Work().geometry,
        context);
    Require(different.mesh.has_value(),
            "actual same-count signed-zero-altered strong mesh");
    auto altered = f::Output(source, *different.mesh);
    auto mismatch = ic::PublishOffBundle(
        {source, source.Document().Mesh(), altered}, root.Path() / "bits", {},
        f::PublishLimits(), f::PublishWork());
    Failed(mismatch, Status::kPreservationMismatch, Code::kByteMismatch);
    Receipt receipt;
    auto collision = receipt.Options();
    collision.receipt->basename = "SOURCE.OFF";
    mismatch = ic::PublishOffBundle({source, source.Document().Mesh(), output},
                                    root.Path() / "collision", collision,
                                    f::PublishLimits(), f::PublishWork());
    Failed(mismatch, Status::kInvalidRequest, Code::kNameCollision);
    Require(receipt.calls == 0,
            "portable case collision refused before provider call");
  }
  f::Empty(context);
  f::Empty(foreign);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "known source-selected profile only");
    Admission(unsupported);
    CaptureRequests(true);
    if (!unsupported) CaptureRequests(false);
    FileContentsAndKinds();
    PublicationRequests(unsupported);
    Transactions(unsupported);
    NamesAndEmptyAttributes();
    OwnerAndBackendLifetime();
    if (!unsupported) {
      GenuineMismatches();
      PrefixBeforeLaterStop();
    }
    std::cout << "OFF file requests, actual faults/workers, cancellation and "
                 "transaction controls passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
