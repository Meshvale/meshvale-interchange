// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>
#include <meshvale/interchange/stl_files.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "stl_files_fixture.h"
#include "stl_files_internal.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::stl_files_fixture;
namespace detail = ic::stl_files_detail;
namespace allocation = ic::gltf_detail;
namespace fs = std::filesystem;
using f::Require;
using Status = ic::StlFileStatus;
using Phase = ic::StlFilePhase;
using State = ic::StlFileObservationState;
struct Marker {};

struct Backend {
  allocation::AllocationBackend original =
      allocation::DefaultAllocationBackend();
  std::atomic<std::size_t> requests{0}, live{0};
  bool fail = false, bad_alloc = false, frozen = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Backend*>(state);
    ++self.requests;
    if (self.fail || self.frozen) {
      if (self.bad_alloc) throw std::bad_alloc{};
      throw Marker{};
    }
    auto* result =
        self.original.allocate(bytes, alignment, self.original.state);
    self.live += bytes;
    return result;
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Backend*>(state);
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    self.live -= bytes;
  }
  allocation::AllocationBackend Options() { return {&Allocate, &Free, this}; }
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
    if (++self.attempts == self.fail_at) throw Marker{};
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
enum class TemporalOrder { kWorkerFirst, kLaunchFirst, kPrelaunchAllocation };
// Test driver owns real native wrapper records/gates through every join. The
// original production entry/state is invoked exactly once; no result slot or
// production failure ordering state is visible or changed here.
struct OrderedWorkers {
  static constexpr auto kGateTimeout = std::chrono::seconds(5);
  allocation::NativeWorkerBackend native =
      allocation::DefaultNativeWorkerBackend();
  allocation::AllocationBackend memory = allocation::DefaultAllocationBackend();
  const geo::ExecutionContext& context;
  TemporalOrder order;
  Phase target;
  const std::thread::id caller = std::this_thread::get_id();
  std::atomic<bool> in_target{false};
  std::atomic<std::size_t> entries{0}, returns{0}, live_bytes{0};
  std::size_t attempts = 0, launched = 0, joined = 0, target_attempts = 0,
              target_launched = 0, target_joined = 0;
  std::size_t prelaunch_throws = 0;
  std::uintptr_t target_handle = 0;
  std::stop_source stop;
  std::mutex mutex;
  std::condition_variable condition;
  bool gate = false, completed = false, timed_out = false;
  struct Wrapper {
    allocation::NativeWorkerRecord native_record;
    allocation::NativeWorkerRecord* original = nullptr;
    OrderedWorkers* owner = nullptr;
  } wrapper;
  OrderedWorkers(const geo::ExecutionContext& value, TemporalOrder selected,
                 Phase phase)
      : context(value), order(selected), target(phase) {}
  static std::error_code LaunchCode() {
    return std::make_error_code(std::errc::resource_unavailable_try_again);
  }
  static std::error_code AllocationCode() {
    return std::make_error_code(std::errc::permission_denied);
  }
  static void Entry(allocation::NativeWorkerRecord& record) noexcept {
    auto& wrap = *static_cast<Wrapper*>(record.state);
    auto& self = *wrap.owner;
    if (self.order == TemporalOrder::kLaunchFirst) {
      std::unique_lock lock(self.mutex);
      if (!self.condition.wait_for(lock, kGateTimeout,
                                   [&] { return self.gate; }))
        self.timed_out =
            true;  // Bounded fail-safe: execute and fail the witness.
    }
    ++self.entries;
    wrap.original->entry(*wrap.original);
    ++self.returns;
    {
      std::lock_guard lock(self.mutex);
      self.completed = true;  // Only after production recorded any job failure.
    }
    self.condition.notify_all();
  }
  static std::uintptr_t Launch(allocation::NativeWorkerRecord* record,
                               void* opaque) {
    auto& self = *static_cast<OrderedWorkers*>(opaque);
    ++self.attempts;
    if (!self.in_target) {
      const auto handle = self.native.launch(record, self.native.state);
      ++self.launched;
      return handle;
    }
    ++self.target_attempts;
    if (self.target_attempts == 1) {
      self.wrapper.original = record;
      self.wrapper.owner = &self;
      self.wrapper.native_record = {&Entry, &self.wrapper, record->ordinal};
      const auto handle =
          self.native.launch(&self.wrapper.native_record, self.native.state);
      self.target_handle = handle;
      ++self.launched;
      ++self.target_launched;
      return handle;
    }
    Require(self.target_attempts == 2, "exact second real launch boundary");
    if (self.order == TemporalOrder::kWorkerFirst) {
      std::unique_lock lock(self.mutex);
      if (!self.condition.wait_for(lock, kGateTimeout,
                                   [&] { return self.completed; }))
        self.timed_out = true;
    }
    throw std::system_error(LaunchCode(), "actual second-launch test fault");
  }
  static void Join(std::uintptr_t handle, void* opaque) noexcept {
    auto& self = *static_cast<OrderedWorkers*>(opaque);
    if (self.target_launched && handle == self.target_handle) {
      // Production catches/latches the launch error before calling real joins.
      {
        std::lock_guard lock(self.mutex);
        self.gate = true;
      }
      self.condition.notify_all();
      ++self.target_joined;
    }
    self.native.join(handle, self.native.state);
    ++self.joined;
  }
  static void Checkpoint(Phase phase, std::uint64_t, void* opaque) {
    auto& self = *static_cast<OrderedWorkers*>(opaque);
    self.in_target = phase == self.target;
    if (phase == self.target && std::this_thread::get_id() != self.caller)
      self.stop
          .request_stop();  // Real production checkpoint creates cancellation.
  }
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& self = *static_cast<OrderedWorkers*>(opaque);
    // A real live worker lease with no target launch attempts distinguishes
    // records/handles preparation from earlier charged plans, without request
    // ordinal/size calibration or a fabricated lease/context.
    if (self.order == TemporalOrder::kPrelaunchAllocation && self.in_target &&
        self.context.ActiveWorkers() > 0 && self.target_attempts == 0) {
      ++self.prelaunch_throws;
      throw std::system_error(AllocationCode(),
                              "actual leased prelaunch allocation fault");
    }
    auto* block = self.memory.allocate(bytes, alignment, self.memory.state);
    self.live_bytes += bytes;
    return block;
  }
  static void Free(void* block, std::size_t bytes, std::size_t alignment,
                   void* opaque) noexcept {
    auto& self = *static_cast<OrderedWorkers*>(opaque);
    self.memory.deallocate(block, bytes, alignment, self.memory.state);
    self.live_bytes -= bytes;
  }
  detail::TestOptions Options() {
    detail::TestOptions result;
    result.workers.backend = {&Launch, &Join, this};
    result.allocation_backend = {&Allocate, &Free, this};
    result.checkpoint = &Checkpoint;
    result.state = this;
    return result;
  }
  void CheckJoined() {
    Require(!timed_out && launched == joined && target_launched == 1 &&
                target_joined == 1 && target_attempts == 2 && entries == 1 &&
                returns == 1 && context.ActiveWorkers() == 0,
            "bounded deterministic genuine entry/return/launch/join witnesses");
  }
};
enum class Fault { kNone, kEntry, kReceipt, kForeign, kIdentity, kRace };
struct Probe {
  fs::path stage, destination, moved;
  std::atomic<std::size_t> opens{0}, closes{0};
  std::atomic<bool> changed{false};
  Fault fault = Fault::kNone;
  bool close_failure = false, committed = false, late_stop = false;
  Phase stop_phase = Phase::kComplete;
  std::uint64_t stop_offset = 0;
  std::atomic<bool> checkpoint_stopped{false};
  std::size_t commit_attempts = 0;
  std::stop_source stop;
  Backend* backend = nullptr;
  static void BeforeOpen(std::uint64_t, void* state) {
    ++static_cast<Probe*>(state)->opens;
  }
  static void Checkpoint(Phase phase, std::uint64_t offset, void* state) {
    auto& self = *static_cast<Probe*>(state);
    if (phase == self.stop_phase && offset >= self.stop_offset) {
      self.checkpoint_stopped = true;
      self.stop.request_stop();
    }
  }
  static void Closed(std::uint64_t, void* state) noexcept {
    ++static_cast<Probe*>(state)->closes;
  }
  static void Ready(detail::NativeView stage, void* state) {
    auto& self = *static_cast<Probe*>(state);
    self.stage = fs::path(stage);
  }
  static void BeforeRead(std::uint64_t ordinal, std::uint64_t offset,
                         void* state) {
    auto& self = *static_cast<Probe*>(state);
    const bool selected = (self.fault == Fault::kEntry && ordinal == 0) ||
                          (self.fault == Fault::kReceipt && ordinal == 1);
    if (selected && !self.stage.empty() && offset == 0 &&
        !self.changed.exchange(true)) {
      const auto member =
          self.stage / (ordinal == 0 ? "part.stl" : std::string(f::kReceipt));
      auto bytes = f::Read(member);
      Require(!bytes.empty(),
              "substitution targets actual nonempty staged file");
      bytes.back() ^= std::byte{1};
      f::Write(member, bytes);
    }
  }
  static void Inventory(detail::NativeView stage, void* state) {
    auto& self = *static_cast<Probe*>(state);
    if (self.changed.exchange(true)) return;
    if (self.fault == Fault::kForeign)
      f::Write(fs::path(stage) / "foreign", f::Bytes("foreign bytes"));
    if (self.fault == Fault::kIdentity) {
      self.moved = fs::path(stage);
      self.moved += "-original";
      fs::rename(fs::path(stage), self.moved);
      Require(fs::create_directory(fs::path(stage)),
              "actual stage directory substituted");
      f::Write(fs::path(stage) / "foreign", f::Bytes("replacement survives"));
    }
  }
  static std::error_code CommitError(void* state) {
    auto& self = *static_cast<Probe*>(state);
    ++self.commit_attempts;
    if (self.fault == Fault::kRace) {
      Require(fs::create_directory(self.destination),
              "actual racing destination created");
      f::Write(self.destination / "foreign", f::Bytes("race winner"));
    }
    return {};  // The real native no-replace call must decide this race.
  }
  static void Committed(void* state) noexcept {
    auto& self = *static_cast<Probe*>(state);
    self.committed = true;
    if (self.backend) self.backend->frozen = true;
    if (self.late_stop) self.stop.request_stop();
  }
  static std::error_code CloseError(std::uint64_t, void* state) {
    return static_cast<Probe*>(state)->close_failure
               ? std::make_error_code(std::errc::io_error)
               : std::error_code{};
  }
  detail::TestOptions Options() {
    detail::TestOptions result;
    result.files.before_open = &BeforeOpen;
    result.files.closed = &Closed;
    result.files.before_read = &BeforeRead;
    result.files.state = this;
    result.stage_ready = &Ready;
    result.before_inventory = &Inventory;
    result.commit_error = &CommitError;
    result.native_committed = &Committed;
    result.checkpoint = &Checkpoint;
    result.close_error = &CloseError;
    result.state = this;
    return result;
  }
};

using Leaves = std::vector<std::pair<std::string, std::uint64_t*>>;
void Append(Leaves& leaves, std::string prefix, ic::StlFileLimits& value) {
#define LEAF(name) leaves.emplace_back(prefix + "." #name, &value.name)
  LEAF(max_entry_bytes);
  LEAF(max_receipt_bytes);
  LEAF(max_total_file_bytes);
  LEAF(max_entry_name_bytes);
  LEAF(max_native_path_code_units);
  LEAF(max_unit_label_bytes);
  LEAF(max_caller_report_bytes);
  LEAF(max_file_new_requested_bytes);
#undef LEAF
}
void Append(Leaves& leaves, std::string prefix, ic::StlFileWorkPolicy& value) {
#define LEAF(name) leaves.emplace_back(prefix + "." #name, &value.name)
  LEAF(io_block_bytes);
  LEAF(parallel_min_bytes);
  LEAF(partition_bytes);
  LEAF(cancellation_block_bytes);
  LEAF(max_jobs);
#undef LEAF
}
void Append(Leaves& leaves, std::string prefix, ic::StlReadLimits& value) {
#define LEAF(name) leaves.emplace_back(prefix + "." #name, &value.name)
  LEAF(max_source_bytes);
  LEAF(max_facets);
  LEAF(max_name_bytes);
  LEAF(max_line_bytes);
  LEAF(max_token_bytes);
  LEAF(max_codec_new_requested_bytes);
  LEAF(max_total_new_requested_bytes);
  LEAF(materialization.max_vertices);
  LEAF(materialization.max_faces);
  LEAF(materialization.max_corners);
  LEAF(materialization.max_face_offsets);
  LEAF(materialization.max_attributes);
  LEAF(materialization.max_attribute_scalars);
  LEAF(materialization.max_attribute_offsets);
  LEAF(materialization.max_presence_bytes);
  LEAF(materialization.max_metadata_entries);
  LEAF(materialization.max_descriptor_bytes);
  LEAF(materialization.max_new_requested_bytes);
  LEAF(inspection.max_issues);
  LEAF(inspection.max_new_requested_bytes);
#undef LEAF
}
void Append(Leaves& leaves, std::string prefix, ic::StlWriteLimits& value) {
#define LEAF(name) leaves.emplace_back(prefix + "." #name, &value.name)
  LEAF(max_output_bytes);
  LEAF(max_facets);
  LEAF(max_name_bytes);
  LEAF(max_line_bytes);
  LEAF(max_token_bytes);
  LEAF(max_codec_new_requested_bytes);
  LEAF(max_total_new_requested_bytes);
  LEAF(inspection.max_issues);
  LEAF(inspection.max_new_requested_bytes);
#undef LEAF
}
void Append(Leaves& leaves, std::string prefix, ic::StlWorkPolicy& value) {
#define LEAF(name) leaves.emplace_back(prefix + "." #name, &value.name)
  LEAF(cancellation_block_bytes);
  LEAF(cancellation_block_tokens);
  LEAF(cancellation_block_facets);
  LEAF(geometry.parallel_min_bytes);
  LEAF(geometry.partition_bytes);
  LEAF(geometry.cancellation_block_scalars);
  LEAF(geometry.max_jobs);
#undef LEAF
}
void PositiveAdmission(const ic::StlFileAsset& asset, const fs::path& root,
                       const geo::ExecutionContext& context) {
  auto read_limits = f::ReadLimits();
  auto read_work = f::ReadWork();
  Leaves read;
  Append(read, "files", read_limits.files);
  Append(read, "read", read_limits.read);
  Append(read, "file_work", read_work.files);
  Append(read, "read_work", read_work.read);
  Require(read.size() == 40,
          "independent worksheet: 40 required native read leaves");
  Probe probe;
  const auto hooks = probe.Options();
  std::stop_source stopped;
  stopped.request_stop();
  for (auto& [name, field] : read) {
    const auto previous = *field;
    Require(previous > 0, name);
    *field = 0;
    const auto result = detail::ReadForTesting(
        root / "missing", ic::StlEncoding::kAscii, read_limits, read_work,
        context, stopped.get_token(), hooks);
    *field = previous;
    Require(result.status == Status::kInvalidConfiguration && !result.asset &&
                !result.captured && probe.opens == 0,
            "invalid positive configuration precedes stop and source open: " +
                name);
  }
  auto rewrite_limits = f::RewriteLimits();
  auto rewrite_work = f::RewriteWork();
  Leaves rewrite;
  Append(rewrite, "files", rewrite_limits.files);
  Append(rewrite, "write", rewrite_limits.write);
  Append(rewrite, "fresh_read", rewrite_limits.fresh_read);
  rewrite.emplace_back("total", &rewrite_limits.max_total_new_requested_bytes);
  Append(rewrite, "file_work", rewrite_work.files);
  Append(rewrite, "write_work", rewrite_work.write);
  Append(rewrite, "fresh_work", rewrite_work.fresh_read);
  Require(rewrite.size() == 57,
          "independent worksheet: 57 required native rewrite leaves");
  for (auto& [name, field] : rewrite) {
    const auto previous = *field;
    Require(previous > 0, name);
    *field = 0;
    const auto result = detail::RewriteForTesting(
        asset, root / "invalid", f::Retain(ic::StlEncoding::kAscii),
        rewrite_limits, rewrite_work, stopped.get_token(), hooks);
    *field = previous;
    Require(
        result.status == Status::kInvalidConfiguration && !result.output &&
            !result.bundle && result.source.Sha256() == asset.Sha256() &&
            probe.stage.empty() && probe.opens == 0,
        "invalid nested rewrite configuration precedes stop/staging: " + name);
  }
}

struct Provider {
  unsigned calls = 0;
  bool ordinary = false, bad_alloc = false;
  std::optional<ic::StlVerifiedRewrite> retained;
  std::string report = "quote \" and newline\nUTF-8 \xe9\x9b\xaa";
  static std::span<const std::byte> Call(
      const ic::StlFileVerifiedContent& content, void* state) {
    auto& self = *static_cast<Provider*>(state);
    ++self.calls;
    f::CheckDocument(content.verified.Document(), {});
    self.retained = content.verified;
    if (self.bad_alloc) throw std::bad_alloc{};
    if (self.ordinary) throw std::runtime_error("provider marker");
    return f::Bytes(self.report);
  }
};
void ExpectedFaults(const ic::StlFileAsset& source, const fs::path& root) {
  for (auto fault : {Fault::kEntry, Fault::kReceipt, Fault::kForeign,
                     Fault::kIdentity, Fault::kRace}) {
    Probe probe;
    probe.fault = fault;
    probe.destination =
        root / ("fault-" + std::to_string(static_cast<unsigned>(fault)));
    const auto result = detail::RewriteForTesting(
        source, probe.destination, f::Retain(ic::StlEncoding::kAscii),
        f::RewriteLimits(), f::RewriteWork(), {}, probe.Options());
    Require(
        result.status != Status::kSuccess && !result.bundle && !probe.committed,
        "physical substitution/race never produces publication owner");
    Require(result.output && result.source.Sha256() == source.Sha256(),
            "later expected failure retains actual writer and source owners");
    if (fault != Fault::kEntry)
      Require(result.fresh_document && result.verified &&
                  result.observations.verification.state == State::kSucceeded,
              "post-verification failure preserves genuine fresh/verified "
              "owners and facts");
    if (fault == Fault::kReceipt)
      Require(result.observations.receipt_verification.state == State::kFailed,
              "actual substituted receipt fails separate receipt verification");
    if (fault == Fault::kForeign || fault == Fault::kIdentity) {
      Require(result.cleanup == ic::StlFileCleanupOutcome::kFailed &&
                  fs::exists(probe.stage / "foreign"),
              "identity-safe cleanup preserves foreign member/directory");
      if (fault == Fault::kIdentity)
        Require(fs::exists(probe.moved / "part.stl"),
                "replaced owned directory is never recursively removed");
    } else
      Require(result.cleanup == ic::StlFileCleanupOutcome::kRemoved,
              "owned precommit stage removed after expected failure");
    if (fault == Fault::kRace)
      Require(result.status == Status::kDestinationExists &&
                  probe.commit_attempts == 1 &&
                  f::Read(probe.destination / "foreign") ==
                      std::vector<std::byte>(f::Bytes("race winner").begin(),
                                             f::Bytes("race winner").end()),
              "real native no-replace race preserves winner and never retries");
  }
  for (bool bad_alloc : {false, true}) {
    Provider provider;
    provider.ordinary = !bad_alloc;
    provider.bad_alloc = bad_alloc;
    auto options = f::Retain(ic::StlEncoding::kAscii);
    options.report = ic::StlFileReportProvider{&Provider::Call, &provider};
    Probe probe;
    const auto destination =
        root / (bad_alloc ? "provider-alloc" : "provider-error");
    bool returned = false, threw = false;
    try {
      const auto result = detail::RewriteForTesting(
          source, destination, options, f::RewriteLimits(), f::RewriteWork(),
          {}, probe.Options());
      returned = true;
      Require(!bad_alloc && result.status == Status::kProviderFailure &&
                  result.verified && result.fresh_document && result.output &&
                  !result.bundle &&
                  result.observations.provider.state == State::kFailed,
              "ordinary provider error preserves all reached genuine owners");
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    Require(provider.calls == 1 && provider.retained &&
                !fs::exists(destination) &&
                (bad_alloc ? threw && !returned : returned && !threw),
            "provider allocation propagates without invented result; provider "
            "exactly once");
  }
  Provider provider;
  auto options = f::Retain(ic::StlEncoding::kAscii);
  options.report = ic::StlFileReportProvider{&Provider::Call, &provider};
  options.unit_kind = ic::StlFileUnitKind::kLabel;
  options.unit_label = "mm";
  const auto with_report = ic::RewriteStlFile(
      source, root / "report", options, f::RewriteLimits(), f::RewriteWork());
  f::Published(with_report);
  f::CheckReceipt(*with_report.bundle, ic::StlFileMetadataMode::kRetain,
                  ic::StlMetadataPolicy::kRetainedSourceMetadata, 0,
                  provider.report, "mm");
  Require(provider.calls == 1,
          "provider only called once and report remains one escaped string");
  for (unsigned variant = 0; variant != 3; ++variant) {
    Provider selected;
    if (variant == 0) selected.report.clear();
    if (variant == 1) selected.report = "\xff";
    auto selected_options = f::Retain(ic::StlEncoding::kAscii);
    selected_options.report =
        ic::StlFileReportProvider{&Provider::Call, &selected};
    auto limits = f::RewriteLimits();
    if (variant == 2) limits.files.max_caller_report_bytes = 1;
    const auto result = ic::RewriteStlFile(
        source, root / ("report-domain-" + std::to_string(variant)),
        selected_options, limits, f::RewriteWork());
    Require(selected.calls == 1 && result.verified,
            "report import follows one real verified callback");
    if (variant == 0) {
      f::Published(result);
      f::CheckReceipt(*result.bundle, ic::StlFileMetadataMode::kRetain,
                      ic::StlMetadataPolicy::kRetainedSourceMetadata, 0,
                      std::string_view{});
    } else {
      Require(!result.bundle &&
                  result.observations.provider.state == State::kFailed &&
                  (variant == 1 ? result.diagnostic.code ==
                                      ic::StlFileDiagnosticCode::kReportUtf8
                                : result.status == Status::kLimitExceeded),
              "strict UTF-8/count import refusals keep verified owner and own "
              "diagnostic");
    }
  }
  Backend backend;
  Probe probe;
  probe.backend = &backend;
  probe.late_stop = true;
  auto hooks = probe.Options();
  hooks.allocation_backend = backend.Options();
  {
    const auto result = detail::RewriteForTesting(
        source, root / "late-stop", f::Retain(ic::StlEncoding::kAscii),
        f::RewriteLimits(), f::RewriteWork(), probe.stop.get_token(), hooks);
    f::Published(result);
    Require(probe.committed && probe.stop.stop_requested() && backend.frozen,
            "actual native success survives late stop with allocation backend "
            "frozen");
  }
  Require(backend.live == 0,
          "retained publication backend blocks freed after owners expire");
}

void CaptureRequests(const fs::path& root,
                     const geo::ExecutionContext& context) {
  const auto input = root / "capture.stl";
  // Physical data is intentionally larger than multiple partitions; it need
  // not be a codec document in this capture-only suite.
  std::vector<std::byte> bytes(4096, std::byte{0x51});
  f::Write(input, bytes);
  Probe probe;
  {
    Workers workers;
    auto options = probe.Options();
    options.workers = workers.Options();
    const auto captured = detail::CaptureForTesting(
        input, f::FileLimits(), f::FileWork(), context, {}, options);
    Require(captured.captured && workers.launched > 0 &&
                workers.launched == workers.joined && workers.entered > 0 &&
                context.ActiveWorkers() == 0,
            "actual native capture launches and joins real worker threads");
  }
  {
    Workers workers;
    workers.fail_at = 2;
    auto options = probe.Options();
    options.workers = workers.Options();
    bool returned = false, threw = false;
    try {
      const auto result = detail::CaptureForTesting(
          input, f::FileLimits(), f::FileWork(), context, {}, options);
      (void)result;
      returned = true;
    } catch (const Marker&) {
      threw = true;
    }
    Require(threw && !returned && workers.attempts == 2 &&
                workers.launched == 1 && workers.joined == 1 &&
                context.ActiveWorkers() == 0,
            "real partial launch exception propagates only after started "
            "worker joins");
  }
  probe.close_failure = true;
  const auto close = detail::CaptureForTesting(
      input, f::FileLimits(), f::FileWork(), context, {}, probe.Options());
  Require(close.status == Status::kIoFailure && !close.captured &&
              close.diagnostic.code == ic::StlFileDiagnosticCode::kClose,
          "actual native close error prevents captured owner/hash success");
  auto limits = f::FileLimits();
  limits.max_entry_bytes = bytes.size() - 1;
  const auto cap = ic::CaptureStlFile(input, limits, f::FileWork(), context);
  Require(cap.status == Status::kLimitExceeded && !cap.captured,
          "observed size admitted before payload capture");
  std::stop_source stopped;
  stopped.request_stop();
  const auto canceled = ic::CaptureStlFile(
      input, f::FileLimits(), f::FileWork(), context, stopped.get_token());
  Require(canceled.status == Status::kCanceled && !canceled.captured,
          "valid positive stopped capture");
  {
    Probe during;
    during.stop_phase = Phase::kCapture;
    during.stop_offset = 16;
    Workers workers;
    auto options = during.Options();
    options.workers = workers.Options();
    const auto interrupted =
        detail::CaptureForTesting(input, f::FileLimits(), f::FileWork(),
                                  context, during.stop.get_token(), options);
    Require(
        during.checkpoint_stopped && interrupted.status == Status::kCanceled &&
            !interrupted.captured && workers.launched == workers.joined &&
            context.ActiveWorkers() == 0,
        "actual in-flight capture cancellation joins every launched worker");
  }
  {
    geo::ExecutionContext tiny({4, 1, 1});
    const auto budget =
        ic::CaptureStlFile(input, f::FileLimits(), f::FileWork(), tiny);
    Require(budget.status == Status::kBudgetExceeded && !budget.captured &&
                tiny.ActiveWorkers() == 0 && tiny.ActiveTrackedPayload() == 0,
            "real shared ledger refusal releases every tracked request");
  }
  for (bool bad_alloc : {false, true}) {
    Backend backend;
    backend.fail = true;
    backend.bad_alloc = bad_alloc;
    detail::TestOptions options;
    options.allocation_backend = backend.Options();
    bool returned = false, expected = false;
    try {
      const auto result = detail::CaptureForTesting(
          input, f::FileLimits(), f::FileWork(), context, {}, options);
      (void)result;
      returned = true;
    } catch (const Marker&) {
      expected = !bad_alloc;
    } catch (const std::bad_alloc&) {
      expected = bad_alloc;
    }
    Require(expected && !returned && backend.live == 0 &&
                context.ActiveWorkers() == 0,
            "unexpected allocation backend exception propagates without "
            "invented return");
  }
}
void TemporalRequests(const fs::path& root,
                      const geo::ExecutionContext& context, bool unsupported) {
  const auto byte_input = root / "ordered-capture.stl";
  f::Write(byte_input, std::vector<std::byte>(4096, std::byte{0x51}));
  for (auto selected :
       {TemporalOrder::kWorkerFirst, TemporalOrder::kLaunchFirst,
        TemporalOrder::kPrelaunchAllocation}) {
    OrderedWorkers workers(context, selected, Phase::kCapture);
    const auto before = context.ActiveTrackedPayload();
    if (selected == TemporalOrder::kPrelaunchAllocation) {
      bool returned = false, threw = false;
      try {
        const auto result = detail::CaptureForTesting(
            byte_input, f::FileLimits(), f::FileWork(), context,
            workers.stop.get_token(), workers.Options());
        (void)result;
        returned = true;
      } catch (const std::system_error& error) {
        threw = true;
        Require(error.code() == OrderedWorkers::AllocationCode() &&
                    &error.code().category() ==
                        &OrderedWorkers::AllocationCode().category(),
                "byte prelaunch allocation preserves exact system_error "
                "category/value");
      }
      Require(threw && !returned && workers.prelaunch_throws == 1 &&
                  workers.target_attempts == 0 &&
                  workers.target_launched == 0 && workers.live_bytes == 0 &&
                  context.ActiveWorkers() == 0 &&
                  context.ActiveTrackedPayload() == before,
              "byte records/handles allocation exception propagates before any "
              "launch and releases charge");
    } else {
      {
        const auto result = detail::CaptureForTesting(
            byte_input, f::FileLimits(), f::FileWork(), context,
            workers.stop.get_token(), workers.Options());
        workers.CheckJoined();
        const bool worker_first = selected == TemporalOrder::kWorkerFirst;
        Require(
            !result.captured && result.phase == Phase::kCapture &&
                result.status ==
                    (worker_first ? Status::kCanceled : Status::kIoFailure) &&
                result.diagnostic.code ==
                    (worker_first ? ic::StlFileDiagnosticCode::kStopRequested
                                  : ic::StlFileDiagnosticCode::kOpen) &&
                result.observations.capture.state ==
                    (worker_first ? State::kCanceled : State::kFailed) &&
                std::holds_alternative<std::monostate>(
                    result.observations.capture.facts) &&
                result.observations.capture.diagnostic &&
                result.execution.capture.launched_workers == 1 &&
                result.execution.capture.joined_workers == 1,
            "byte worker-first cancellation or launch-first system error owns "
            "primary result/observation");
        if (!worker_first)
          Require(
              result.diagnostic.system_error == OrderedWorkers::LaunchCode(),
              "byte native launch failure keeps exact system error");
      }
      Require(
          workers.live_bytes == 0 && context.ActiveTrackedPayload() == before,
          "byte temporal failure releases charged worker plans");
    }
  }
  const auto value_input = root / "ordered-values.stl";
  f::Write(value_input, f::Binary(f::kFacetBits));
  const auto read =
      ic::ReadStlFileAsset(value_input, ic::StlEncoding::kBinary,
                           f::ReadLimits(), f::ReadWork(), context);
  if (f::Unsupported(read, unsupported)) return;
  f::CheckDocument(read.asset->Document(), f::kFacetBits);
  // Three packed facets occupy 234 bytes; independent comparisons cover 288
  // bytes of canonical doubles. A threshold between them makes verification
  // byte work serial and genuine nonempty value work parallel, without a hook
  // that guesses producer allocation ordinals or mutates genuine mesh storage.
  auto work = f::RewriteWork();
  work.files.parallel_min_bytes = 256;
  work.files.partition_bytes = 96;
  for (auto selected :
       {TemporalOrder::kWorkerFirst, TemporalOrder::kLaunchFirst,
        TemporalOrder::kPrelaunchAllocation}) {
    OrderedWorkers workers(context, selected, Phase::kVerification);
    const auto before = context.ActiveTrackedPayload();
    const auto destination =
        root /
        ("ordered-value-" + std::to_string(static_cast<unsigned>(selected)));
    if (selected == TemporalOrder::kPrelaunchAllocation) {
      bool returned = false, threw = false;
      try {
        const auto result = detail::RewriteForTesting(
            *read.asset, destination, f::Retain(ic::StlEncoding::kBinary),
            f::RewriteLimits(), work, workers.stop.get_token(),
            workers.Options());
        (void)result;
        returned = true;
      } catch (const std::system_error& error) {
        threw = true;
        Require(error.code() == OrderedWorkers::AllocationCode() &&
                    &error.code().category() ==
                        &OrderedWorkers::AllocationCode().category(),
                "value prelaunch allocation preserves exact system_error "
                "category/value");
      }
      Require(threw && !returned && workers.prelaunch_throws == 1 &&
                  workers.target_attempts == 0 &&
                  workers.target_launched == 0 &&
                  workers.launched == workers.joined &&
                  workers.live_bytes == 0 && context.ActiveWorkers() == 0 &&
                  context.ActiveTrackedPayload() == before &&
                  !fs::exists(destination),
              "actual value records/handles allocation failure propagates with "
              "real prior work joined and stage cleaned");
    } else {
      {
        const auto result = detail::RewriteForTesting(
            *read.asset, destination, f::Retain(ic::StlEncoding::kBinary),
            f::RewriteLimits(), work, workers.stop.get_token(),
            workers.Options());
        workers.CheckJoined();
        const bool worker_first = selected == TemporalOrder::kWorkerFirst;
        Require(
            result.status ==
                    (worker_first ? Status::kCanceled : Status::kIoFailure) &&
                result.phase == Phase::kVerification &&
                result.diagnostic.code ==
                    (worker_first ? ic::StlFileDiagnosticCode::kStopRequested
                                  : ic::StlFileDiagnosticCode::kOpen) &&
                result.observations.verification.state ==
                    (worker_first ? State::kCanceled : State::kFailed) &&
                std::holds_alternative<std::monostate>(
                    result.observations.verification.facts) &&
                result.observations.verification.diagnostic &&
                result.observations.capture.state == State::kSucceeded &&
                result.observations.read.state == State::kSucceeded &&
                result.observations.fresh_read.state == State::kSucceeded &&
                result.output && result.fresh_document && !result.verified &&
                !result.bundle &&
                result.execution.value_verification.launched_workers ==
                    result.execution.value_verification.joined_workers &&
                result.cleanup == ic::StlFileCleanupOutcome::kRemoved &&
                !fs::exists(destination),
            "value worker-first versus launch-first primary failure keeps "
            "genuine prior owners and canceled/failed observation");
        if (!worker_first)
          Require(
              result.diagnostic.system_error == OrderedWorkers::LaunchCode(),
              "value native launch failure keeps exact system error");
        f::CheckDocument(*result.fresh_document, f::kFacetBits);
      }
      Require(
          workers.live_bytes == 0 && context.ActiveTrackedPayload() == before,
          "value temporal failure releases every new charged owner and plan");
    }
  }
}
void DomainAndControls(const ic::StlFileAsset& source, const fs::path& root,
                       const geo::ExecutionContext& context) {
  const auto original = f::Read(root / "part.stl");
  for (const auto& destination : {root / "part.stl", root}) {
    const auto result = ic::RewriteStlFile(
        source, destination, f::Retain(ic::StlEncoding::kAscii),
        f::RewriteLimits(), f::RewriteWork());
    Require(!result.bundle && result.status != Status::kSuccess &&
                f::Read(root / "part.stl") == original,
            "source equality/ancestor destination is refused and original "
            "protected");
  }
  f::Write(root / "occupied", f::Bytes("existing file"));
  const auto occupied = ic::RewriteStlFile(
      source, root / "occupied", f::Retain(ic::StlEncoding::kAscii),
      f::RewriteLimits(), f::RewriteWork());
  Require(occupied.status == Status::kDestinationExists && !occupied.bundle &&
              f::Read(root / "occupied") ==
                  std::vector<std::byte>(f::Bytes("existing file").begin(),
                                         f::Bytes("existing file").end()),
          "existing regular destination preserved");
  struct NameCase {
    std::string_view name;
    Status status;
    ic::StlFileDiagnosticCode code;
  };
  using Code = ic::StlFileDiagnosticCode;
  constexpr std::array kNames{
      NameCase{"CON.stl", Status::kUnsupportedProfile, Code::kNameUnsupported},
      NameCase{"StL-PuBlIcAtIoN.JsOn", Status::kInvalidRequest,
               Code::kNameCollision},
      NameCase{"trailing.stl ", Status::kUnsupportedProfile,
               Code::kNameUnsupported},
#ifdef _WIN32
      NameCase{"bad:name.stl", Status::kUnsupportedProfile,
               Code::kPathUnsupported}
#else
      NameCase{"bad:name.stl", Status::kUnsupportedProfile,
               Code::kNameUnsupported}
#endif
  };
  for (const auto& selected : kNames) {
    // Rejected native characters need not be physically creatable. Name/path
    // admission precedes source open, observed with the private real I/O seam.
    Probe probe;
    const auto result = detail::ReadForTesting(
        root / std::string(selected.name), ic::StlEncoding::kAscii,
        f::ReadLimits(), f::ReadWork(), context, {}, probe.Options());
    Require(result.status == selected.status &&
                result.diagnostic.code == selected.code && !result.captured &&
                !result.asset && probe.opens == 0 &&
                f::Read(root / "part.stl") == original,
            "portable name/collision admission before payload open");
  }
  constexpr unsigned kInvalidOptions = 23;
  constexpr int kInvalidSelector = 99;
  const auto header = f::Header();
  std::array<std::byte, 81> overlong_header{};
  std::size_t provider_calls = 0;
  const ic::StlFileReportProvider no_op_provider{
      [](const ic::StlFileVerifiedContent&,
         void* opaque) -> std::span<const std::byte> {
        ++*static_cast<std::size_t*>(opaque);
        return {};
      },
      &provider_calls};
  std::stop_source stopped;
  stopped.request_stop();
  const auto live_before = context.ActiveTrackedPayload();
  for (unsigned variant = 0; variant != kInvalidOptions; ++variant) {
    auto options = f::Retain(ic::StlEncoding::kAscii);
    options.report = no_op_provider;
    Code expected = Code::kMetadata;
    Status expected_status = Status::kInvalidConfiguration;
    switch (variant) {
      case 0:
        options.acknowledge_corner_expansion = false;
        break;
      case 1:
        options.encoding = ic::StlEncoding::kInvalid;
        expected = Code::kEncoding;
        break;
      case 2:
        options.encoding = static_cast<ic::StlEncoding>(kInvalidSelector);
        expected = Code::kEncoding;
        break;
      case 3:
        options.metadata_mode =
            static_cast<ic::StlFileMetadataMode>(kInvalidSelector);
        break;
      case 4:
        options.ascii_name = "retain forbids replacement";
        break;
      case 5:
        options.binary_header = header;
        break;
      case 6:
        options.encoding = ic::StlEncoding::kBinary;
        break;
      case 7:
        options.metadata_mode = ic::StlFileMetadataMode::kReplace;
        options.binary_header = header;
        break;
      case 8:
        options.metadata_mode = ic::StlFileMetadataMode::kReplace;
        options.ascii_name = "\x01";
        break;
      case 9:
        options.metadata_mode = ic::StlFileMetadataMode::kReplace;
        options.ascii_name = "\xff";
        break;
      case 10:
        options.encoding = ic::StlEncoding::kBinary;
        options.metadata_mode = ic::StlFileMetadataMode::kReplace;
        options.binary_header = std::span(header).first(79);
        break;
      case 11:
        options.encoding = ic::StlEncoding::kBinary;
        options.metadata_mode = ic::StlFileMetadataMode::kReplace;
        options.binary_header = overlong_header;
        break;
      case 12:
        options.encoding = ic::StlEncoding::kBinary;
        options.metadata_mode = ic::StlFileMetadataMode::kReplace;
        options.binary_header = header;
        options.ascii_name = "inactive";
        break;
      case 13:
        options.unit_label = "unknown has no label";
        expected = Code::kUnitDeclaration;
        break;
      case 14:
        options.unit_kind = ic::StlFileUnitKind::kLabel;
        expected = Code::kUnitDeclaration;
        break;
      case 15:
        options.unit_kind = static_cast<ic::StlFileUnitKind>(kInvalidSelector);
        expected = Code::kUnitDeclaration;
        break;
      case 16:
        options.unit_kind = ic::StlFileUnitKind::kLabel;
        options.unit_label = "\xff";
        expected = Code::kUnitDeclaration;
        expected_status = Status::kInvalidRequest;
        break;
      case 17:
        options.unit_kind = ic::StlFileUnitKind::kLabel;
        options.unit_label = "\n";
        expected = Code::kUnitDeclaration;
        expected_status = Status::kInvalidRequest;
        break;
      case 18:
        options.unit_kind = ic::StlFileUnitKind::kLabel;
        options.unit_label = std::string_view("a\0b", 3);
        expected = Code::kUnitDeclaration;
        expected_status = Status::kInvalidRequest;
        break;
      case 19:
        options.report = ic::StlFileReportProvider{};
        expected = Code::kProvider;
        break;
      case 20:
        options.unit_kind = ic::StlFileUnitKind::kLabel;
        options.unit_label = "\x7f";
        expected = Code::kUnitDeclaration;
        expected_status = Status::kInvalidRequest;
        break;
      case 21:
        options.unit_kind = ic::StlFileUnitKind::kLabel;
        options.unit_label = "\xc2\x80";
        expected = Code::kUnitDeclaration;
        expected_status = Status::kInvalidRequest;
        break;
      case 22:
        options.encoding = ic::StlEncoding::kBinary;
        options.metadata_mode = ic::StlFileMetadataMode::kReplace;
        break;
    }
    Probe probe;
    Backend backend;
    backend.fail = true;
    auto hooks = probe.Options();
    hooks.allocation_backend = backend.Options();
    const auto result = detail::RewriteForTesting(
        source, root / "invalid-options", options, f::RewriteLimits(),
        f::RewriteWork(), stopped.get_token(), hooks);
    Require(
        result.status == expected_status &&
            result.diagnostic.code == expected &&
            result.phase == Phase::kConfiguration && !result.output &&
            !result.bundle && probe.stage.empty() && probe.opens == 0 &&
            backend.requests == 0 && backend.live == 0 && provider_calls == 0 &&
            context.ActiveTrackedPayload() == live_before &&
            result.source.Sha256() == source.Sha256() &&
            result.observations.capture.state == State::kSucceeded &&
            result.observations.read.state == State::kSucceeded &&
            std::get<ic::StlFileCaptureFacts>(result.observations.capture.facts)
                    .sha256 == source.Sha256() &&
            std::get<ic::StlFileReadFacts>(result.observations.read.facts)
                    .facets == 0,
        "all explicit encoding/metadata/acknowledgment/unit/provider "
        "admission precedes I/O");
  }
  Probe positive_probe;
  Backend positive_backend;
  positive_backend.fail = true;
  auto positive_hooks = positive_probe.Options();
  positive_hooks.allocation_backend = positive_backend.Options();
  auto valid_options = f::Retain(ic::StlEncoding::kAscii);
  valid_options.report = no_op_provider;
  const auto canceled = detail::RewriteForTesting(
      source, root / "pre-stopped", valid_options, f::RewriteLimits(),
      f::RewriteWork(), stopped.get_token(), positive_hooks);
  Require(canceled.status == Status::kCanceled && !canceled.output &&
              !canceled.bundle && !fs::exists(root / "pre-stopped") &&
              positive_backend.requests == 0 && positive_probe.opens == 0 &&
              positive_probe.stage.empty() && provider_calls == 0,
          "valid stopped rewrite has retained source and no output");
  const auto missing =
      ic::ReadStlFileAsset(root / "missing.stl", ic::StlEncoding::kAscii,
                           f::ReadLimits(), f::ReadWork(), context);
  Require(missing.status == Status::kIoFailure && !missing.captured &&
              !missing.asset,
          "actual missing source has neither capture nor document owner");
  {
    const auto live = context.ActiveTrackedPayload();
    auto held = geo::TryReservePayload(context, f::kContextBytes - live);
    Require(held.status == geo::ReservationStatus::kAccepted,
            "genuine shared remaining-budget lease");
    const auto budget = ic::RewriteStlFile(
        source, root / "shared-budget", f::Retain(ic::StlEncoding::kAscii),
        f::RewriteLimits(), f::RewriteWork());
    Require(budget.status == Status::kBudgetExceeded && !budget.output &&
                !budget.bundle && budget.source.Sha256() == source.Sha256() &&
                !fs::exists(root / "shared-budget"),
            "rewrite actually uses retained source ledger; unrelated new "
            "context cannot substitute");
    held.lease.Reset();
    Require(context.ActiveTrackedPayload() == live,
            "refused shared-budget attempt releases all new requests");
  }
  {
    Probe during;
    during.stop_phase = Phase::kStagedReread;
    const auto interrupted = detail::RewriteForTesting(
        source, root / "staged-stop", f::Retain(ic::StlEncoding::kAscii),
        f::RewriteLimits(), f::RewriteWork(), during.stop.get_token(),
        during.Options());
    Require(during.checkpoint_stopped &&
                interrupted.status == Status::kCanceled && interrupted.output &&
                !interrupted.verified && !interrupted.bundle &&
                interrupted.cleanup == ic::StlFileCleanupOutcome::kRemoved &&
                context.ActiveWorkers() == 0,
            "actual staged reread cancellation retains writer and cleans owned "
            "stage");
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "known request profile argument");
    f::TempTree tree;
    geo::ExecutionContext context({4, 1, f::kContextBytes});
    CaptureRequests(tree.root, context);
    TemporalRequests(tree.root, context, unsupported);
    {
      f::Write(tree.root / "part.stl", f::Bytes(f::kEmpty));
      const auto read =
          ic::ReadStlFileAsset(tree.root / "part.stl", ic::StlEncoding::kAscii,
                               f::ReadLimits(), f::ReadWork(), context);
      Require(read.asset.has_value(),
              "real empty codec domain admitted in both profiles");
      PositiveAdmission(*read.asset, tree.root, context);
      DomainAndControls(*read.asset, tree.root, context);
      ExpectedFaults(*read.asset, tree.root);
    }
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "all request owners, charged backend blocks and worker leases "
            "released");
    std::cout << "STL file admission/native fault fixtures passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "unexpected fixture exception\n";
    return 1;
  }
}
