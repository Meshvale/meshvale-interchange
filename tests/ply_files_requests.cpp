// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>

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
#include "ply_file_internal.h"
#include "ply_files_request_fixture.h"
#include "ply_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::ply_files_detail;
namespace allocation = ic::gltf_detail;
namespace f = ic::ply_files_request_fixture;
namespace fs = std::filesystem;
using f::Require;
using Status = ic::PlyFileStatus;
using Stage = ic::PlyFileStage;
using Code = ic::PlyFileDiagnosticCode;
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
  std::string entry = "source.ply";
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
  ic::PlyFileCleanupOutcome cleanup = ic::PlyFileCleanupOutcome::kNotRequired;
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
      f::Write(self.stage / self.entry, "NOT PLY\n");
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
  static void Cleaned(ic::PlyFileCleanupOutcome outcome, std::error_code error,
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
      const ic::PlyFileVerifiedContent& content, void* state) {
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
  ic::PlyFilePublicationOptions Options() {
    return {ic::PlyFileReceiptProvider{"receipt.bin", &Provide, this}};
  }
};

ic::PlyFileAsset Capture(const fs::path& input,
                         const geo::ExecutionContext& context) {
  auto result =
      ic::ReadPlyFileAsset(input, f::ReadLimits(), f::ReadWork(), context);
  Require(result.asset.has_value(),
          "request fixture captures actual regular file");
  return *result.asset;
}
void Failed(const ic::PlyFilePublicationResult& result, Status status,
            Code code) {
  Require(result.outcome == ic::PlyFilePublicationOutcome::kFailed &&
              !result.bundle && result.diagnostic.status == status &&
              result.diagnostic.code == code,
          "actual failed publication retains precise primary status/code");
  f::Joined(result.execution);
}
std::array<std::uint64_t*, 31> PlyLeaves(ic::PlyReadLimits& limits) {
  auto& mesh = limits.materialization;
  return {&limits.max_source_bytes,
          &limits.max_header_bytes,
          &limits.max_header_lines,
          &limits.max_line_bytes,
          &limits.max_token_bytes,
          &limits.max_comment_bytes,
          &limits.max_name_bytes,
          &limits.max_total_name_bytes,
          &limits.max_properties,
          &limits.max_vertices,
          &limits.max_faces,
          &limits.max_corners,
          &limits.max_corners_per_face,
          &limits.max_list_rows,
          &limits.max_list_items_per_row,
          &limits.max_property_values,
          &limits.max_codec_new_requested_bytes,
          &limits.max_total_new_requested_bytes,
          &mesh.max_vertices,
          &mesh.max_faces,
          &mesh.max_corners,
          &mesh.max_face_offsets,
          &mesh.max_attributes,
          &mesh.max_attribute_scalars,
          &mesh.max_attribute_offsets,
          &mesh.max_presence_bytes,
          &mesh.max_metadata_entries,
          &mesh.max_descriptor_bytes,
          &mesh.max_new_requested_bytes,
          &limits.inspection.max_issues,
          &limits.inspection.max_new_requested_bytes};
}
std::array<std::uint64_t*, 7> FileLeaves(ic::PlyFileLimits& limits) {
  return {&limits.max_entry_bytes,
          &limits.max_receipt_bytes,
          &limits.max_total_file_bytes,
          &limits.max_entry_name_bytes,
          &limits.max_receipt_name_bytes,
          &limits.max_native_path_code_units,
          &limits.max_file_new_requested_bytes};
}
std::array<std::uint64_t*, 4> GeometryWorkLeaves(
    geo::MeshMaterializationWorkPolicy& work) {
  return {&work.parallel_min_bytes, &work.partition_bytes,
          &work.cancellation_block_scalars, &work.max_jobs};
}
std::array<std::uint64_t*, 7> PlyWorkLeaves(ic::PlyWorkPolicy& work) {
  const auto geometry = GeometryWorkLeaves(work.geometry);
  return {&work.cancellation_block_bytes,
          &work.cancellation_block_values,
          &work.cancellation_block_records,
          geometry[0],
          geometry[1],
          geometry[2],
          geometry[3]};
}
std::array<std::uint64_t*, 5> FileWorkLeaves(ic::PlyFileWorkPolicy& work) {
  return {&work.io_block_bytes, &work.parallel_min_bytes, &work.partition_bytes,
          &work.cancellation_block_bytes, &work.max_jobs};
}
void ConfigurationAdmission() {
  f::Root root;
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  auto limits = f::ReadLimits();
  auto work = f::ReadWork();
  Probe probe;
  const auto check_read = [&] {
    const auto result = detail::ReadForTesting(
        root.Path() / "absent.ply", limits, work, context, {}, probe.Options());
    Require(result.status == Status::kInvalidConfiguration && !result.asset &&
                result.diagnostic.stage == Stage::kConfiguration &&
                probe.opened == 0 && probe.reads == 0,
            "every required read leaf refuses before any source open");
    f::Empty(context);
  };
  const auto zero_read = [&](auto leaves) {
    for (auto* leaf : leaves) {
      const auto original = *leaf;
      Require(original > 0, "selected read fixture starts fully bounded");
      *leaf = 0;
      check_read();
      *leaf = original;
    }
  };
  zero_read(PlyLeaves(limits.ply));
  zero_read(FileLeaves(limits.files));
  zero_read(PlyWorkLeaves(work.ply));
  zero_read(FileWorkLeaves(work.files));
  // An individual host extent and an aggregate request budget are distinct.
  limits.ply.max_codec_new_requested_bytes =
      std::numeric_limits<std::uint64_t>::max();
  check_read();
  limits = f::ReadLimits();
  --limits.ply.max_total_new_requested_bytes;
  check_read();
  limits = f::ReadLimits();
  constexpr auto kLargestHostExtent =
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
  limits.ply.max_codec_new_requested_bytes = kLargestHostExtent;
  limits.ply.materialization.max_new_requested_bytes = kLargestHostExtent;
  limits.ply.inspection.max_new_requested_bytes = kLargestHostExtent;
  limits.ply.max_total_new_requested_bytes = kLargestHostExtent;
  check_read();
  limits = f::ReadLimits();
  {
    const auto input = root.Path() / "empty.ply";
    f::Write(input, f::Source(true));
    auto source = Capture(input, context);
    auto output = f::Output(source, source.Document().Mesh());
    const ic::PlyFilePublicationRequest request{
        source, source.Document().Mesh(), output};
    const auto baseline = context.ActiveTrackedPayload();
    auto publication = f::PublishLimits();
    auto publication_work = f::PublishWork();
    Probe before_io;
    const auto destination = root.Path() / "configuration-only";
    const auto check_publication = [&] {
      const auto result =
          detail::PublishForTesting(request, destination, {}, publication,
                                    publication_work, {}, before_io.Options());
      Require(result.outcome == ic::PlyFilePublicationOutcome::kFailed &&
                  !result.bundle &&
                  result.diagnostic.status == Status::kInvalidConfiguration &&
                  result.diagnostic.stage == Stage::kConfiguration &&
                  result.cleanup == ic::PlyFileCleanupOutcome::kNotRequired &&
                  before_io.opened == 0 && before_io.stage.empty() &&
                  !fs::exists(destination) &&
                  context.ActiveTrackedPayload() == baseline &&
                  context.ActiveWorkers() == 0,
              "every required publication leaf refuses before staging or IO");
    };
    const auto zero_publication = [&](auto leaves) {
      for (auto* leaf : leaves) {
        const auto original = *leaf;
        Require(original > 0, "selected publication fixture starts bounded");
        *leaf = 0;
        check_publication();
        *leaf = original;
      }
    };
    zero_publication(PlyLeaves(publication.reload));
    zero_publication(FileLeaves(publication.files));
    zero_publication(PlyWorkLeaves(publication_work.reload));
    zero_publication(FileWorkLeaves(publication_work.files));
    zero_publication(GeometryWorkLeaves(publication_work.expected_inspection));
    zero_publication(
        std::array{&publication.expected_inspection.max_issues,
                   &publication.expected_inspection.max_new_requested_bytes});
    publication.reload.max_codec_new_requested_bytes =
        std::numeric_limits<std::uint64_t>::max();
    check_publication();
    publication = f::PublishLimits();
    --publication.reload.max_total_new_requested_bytes;
    check_publication();
    publication = f::PublishLimits();
    publication.reload.max_codec_new_requested_bytes = kLargestHostExtent;
    publication.reload.materialization.max_new_requested_bytes =
        kLargestHostExtent;
    publication.reload.inspection.max_new_requested_bytes = kLargestHostExtent;
    publication.reload.max_total_new_requested_bytes = kLargestHostExtent;
    check_publication();
  }
  f::Empty(context);
}

void LedgerAndProviderAdmission() {
  f::Root root;
  geo::ExecutionContext source_context({4, 1, f::kContextBytes});
  geo::ExecutionContext other_context({4, 1, f::kContextBytes});
  {
    const auto original = f::Source(true);
    const auto input = root.Path() / "source.ply";
    const auto alternate = root.Path() / "alternate.ply";
    f::Write(input, original);
    f::Write(alternate, original);
    auto source = Capture(input, source_context);
    auto other = Capture(alternate, other_context);
    auto output = f::Output(source, source.Document().Mesh());
    auto foreign_output = f::Output(other, other.Document().Mesh());
    Receipt receipt;
    Probe probe;
    const auto source_baseline = source_context.ActiveTrackedPayload();
    const auto other_baseline = other_context.ActiveTrackedPayload();
    const auto foreign = detail::PublishForTesting(
        {source, other.Document().Mesh(), foreign_output},
        root.Path() / "foreign-expected", receipt.Options(), f::PublishLimits(),
        f::PublishWork(), {}, probe.Options());
    Failed(foreign, Status::kForeignBudget, Code::kLedgerMismatch);
    Require(
        receipt.calls == 0 && probe.stage.empty() && probe.opened == 0 &&
            source_context.ActiveTrackedPayload() == source_baseline &&
            other_context.ActiveTrackedPayload() == other_baseline,
        "foreign expected mesh refuses before IO, provider or reservations");
    {
      const auto published = ic::PublishPlyBundle(
          {source, source.Document().Mesh(), foreign_output},
          root.Path() / "foreign-output-accepted", receipt.Options(),
          f::PublishLimits(), f::PublishWork());
      Require(
          published.bundle.has_value() && receipt.calls == 1 &&
              other_context.ActiveTrackedPayload() == other_baseline,
          "equivalent output and schema may belong to another actual ledger");
      f::Check(published.bundle->Document().Mesh(), false, true);
      f::Inventory(root.Path() / "foreign-output-accepted", "source.ply",
                   "receipt.bin");
      f::Joined(published.execution);
    }
    Require(source_context.ActiveTrackedPayload() == source_baseline,
            "foreign-output acceptance releases only its own staged owner");
    const ic::PlyFilePublicationRequest request{
        source, source.Document().Mesh(), output};
    for (const auto name : {"", "../receipt.bin", "nested/receipt.bin", "CON",
                            "source.ply", "SOURCE.PLY"}) {
      receipt.calls = 0;
      Probe before_io;
      const ic::PlyFilePublicationOptions options{
          ic::PlyFileReceiptProvider{name, &Receipt::Provide, &receipt}};
      const auto result = detail::PublishForTesting(
          request, root.Path() / "invalid-receipt", options, f::PublishLimits(),
          f::PublishWork(), {}, before_io.Options());
      Require(result.outcome == ic::PlyFilePublicationOutcome::kFailed &&
                  !result.bundle && receipt.calls == 0 &&
                  result.cleanup == ic::PlyFileCleanupOutcome::kNotRequired &&
                  before_io.stage.empty() && before_io.opened == 0 &&
                  source_context.ActiveTrackedPayload() == source_baseline,
              "invalid or colliding receipt leaf refuses before native IO");
    }
    receipt.calls = 0;
    Probe before_io;
    const ic::PlyFilePublicationOptions missing_callback{
        ic::PlyFileReceiptProvider{"receipt.bin", nullptr, &receipt}};
    const auto missing = detail::PublishForTesting(
        request, root.Path() / "missing-callback", missing_callback,
        f::PublishLimits(), f::PublishWork(), {}, before_io.Options());
    Require(!missing.bundle && receipt.calls == 0 && before_io.stage.empty() &&
                missing.cleanup == ic::PlyFileCleanupOutcome::kNotRequired,
            "missing callback is rejected before creating a stage");
    constexpr std::uint64_t kExpectedAttributeCount = 10;
    for (unsigned kind = 0; kind < 3; ++kind) {
      auto limits = f::PublishLimits();
      if (kind == 0)
        limits.reload.max_properties = ic::ply_fixture::kPropertyCount - 1;
      if (kind == 1)
        limits.reload.materialization.max_attributes =
            kExpectedAttributeCount - 1;
      if (kind == 2) limits.reload.materialization.max_descriptor_bytes = 1;
      Probe bounded;
      const auto result = detail::PublishForTesting(
          request, root.Path() / ("typed-bound-" + std::to_string(kind)),
          receipt.Options(), limits, f::PublishWork(), {}, bounded.Options());
      Require(
          !result.bundle && receipt.calls == 0 && bounded.stage.empty() &&
              result.diagnostic.status == Status::kLimitExceeded &&
              source_context.ActiveTrackedPayload() == source_baseline,
          "typed schema and empty attribute descriptors are bounded before IO");
    }
    Require(f::Read(input) == original && f::Read(alternate) == original &&
                source_context.ActiveWorkers() == 0 &&
                other_context.ActiveWorkers() == 0,
            "all admission cases preserve physical sources and shared workers");
  }
  f::Empty(source_context);
  f::Empty(other_context);
}

void CaptureRequests(bool empty) {
  f::Root root;
  const auto input = root.Path() / "source.ply";
  const auto raw = f::LargeSource(empty);
  f::Write(input, raw);
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  {
    const auto positive =
        ic::ReadPlyFileAsset(input, f::ReadLimits(), f::ReadWork(), context);
    Require(
        positive.asset.has_value(),
        "unmodified padded literal is actually admitted before fault sweep");
    f::Check(positive.asset->Document().Mesh(), false, empty);
  }
  f::Empty(context);
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

void PublicationRequests(bool unsupported) {
  f::Root root;
  const auto input = root.Path() / "source.ply";
  f::Write(input, unsupported ? f::Source(true) : f::Source());
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  {
    auto source = Capture(input, context);
    auto output = f::Output(source, source.Document().Mesh());
    const auto baseline = context.ActiveTrackedPayload();
    const ic::PlyFilePublicationRequest request{
        source, source.Document().Mesh(), output};
    bool reached = false;
    std::stop_source before_work;
    before_work.request_stop();
    const auto before = ic::PublishPlyBundle(
        request, root.Path() / "pre-stopped", {}, f::PublishLimits(),
        f::PublishWork(), before_work.get_token());
    Require(before.outcome == ic::PlyFilePublicationOutcome::kCanceled &&
                !before.bundle &&
                before.cleanup == ic::PlyFileCleanupOutcome::kNotRequired,
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
    auto refused = ic::PublishPlyBundle(request, root.Path() / "occupied", {},
                                        f::PublishLimits(), f::PublishWork());
    Failed(refused, Status::kBudgetExceeded, Code::kSharedBudget);
    occupied.lease.Reset();
    auto own = f::PublishLimits();
    own.files.max_file_new_requested_bytes = 1;
    refused = ic::PublishPlyBundle(request, root.Path() / "own-cap", {}, own,
                                   f::PublishWork());
    Failed(refused, Status::kLimitExceeded, Code::kRequestBudget);
    Receipt provider;
    auto cap = f::PublishLimits();
    cap.files.max_receipt_bytes = provider.bytes.size() - 1;
    refused = ic::PublishPlyBundle(request, root.Path() / "receipt-cap",
                                   provider.Options(), cap, f::PublishWork());
    Failed(refused, Status::kLimitExceeded, Code::kCountLimit);
    Require(provider.calls == 1 &&
                refused.cleanup == ic::PlyFileCleanupOutcome::kRemoved,
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
    refused = ic::PublishPlyBundle(request, root.Path() / "provider-runtime",
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
            cleanup.cleanup == ic::PlyFileCleanupOutcome::kRemoved,
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
                  cleaned.cleanup == ic::PlyFileCleanupOutcome::kRemoved &&
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
                  result.outcome == ic::PlyFilePublicationOutcome::kCanceled &&
                  !result.bundle &&
                  result.cleanup == ic::PlyFileCleanupOutcome::kRemoved &&
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
  const auto input = root.Path() / "source.ply";
  f::Write(input, unsupported ? f::Source(true) : f::Source());
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  {
    auto source = Capture(input, context);
    auto output = f::Output(source, source.Document().Mesh());
    const ic::PlyFilePublicationRequest request{
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
                  result.outcome == ic::PlyFilePublicationOutcome::kFailed &&
                  !fs::exists(dest) &&
                  result.diagnostic.status == Status::kPreservationMismatch,
              "real staged byte/kind/inventory/identity corruption is never "
              "published");
      if (kind == Corruption::kIdentity) {
        Require(
            result.cleanup == ic::PlyFileCleanupOutcome::kFailed &&
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
                result.diagnostic.status == Status::kPlyFailure &&
                result.diagnostic.ply_status == ic::PlyStatus::kMalformed &&
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
    Require(result.cleanup == ic::PlyFileCleanupOutcome::kFailed &&
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
                result.cleanup == ic::PlyFileCleanupOutcome::kRemoved &&
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
                exception.cleanup == ic::PlyFileCleanupOutcome::kRemoved &&
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
                result.outcome == ic::PlyFilePublicationOutcome::kPublished &&
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

void OwnerAndBackendLifetime() {
  f::Root root;
  Backend backend;
  const auto input = root.Path() / "retained.ply";
  f::Write(input, f::Source(true));
  std::optional<ic::PlyFileAsset> source;
  {
    geo::ExecutionContext original({4, 1, f::kContextBytes});
    auto captured = detail::ReadForTesting(
        input, f::ReadLimits(), f::ReadWork(), original, {}, backend.Options());
    Require(captured.asset.has_value(), "injected-backend lifetime source");
    source.emplace(*captured.asset);
  }
  Require(fs::remove(input), "actual original file and context both disappear");
  f::Check(source->Document().Mesh(), false, true);
  std::optional<ic::PlyPublishedBundle> retained;
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
  std::optional<ic::PlyPublishedBundle> copy(*retained);
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
  const auto input = root.Path() / "source.ply";
  f::Write(input, f::Source());
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
                result.cleanup == ic::PlyFileCleanupOutcome::kRemoved,
            "known earlier byte mismatch stays primary over later stop and "
            "cleanup");
    occupied.Reset();
    Require(context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "caller fallback and cleanup release only their own reservations");
  }
  f::Empty(context);
}

}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "closed request fixture arguments");
    ConfigurationAdmission();
    LedgerAndProviderAdmission();
    CaptureRequests(unsupported);
    PublicationRequests(unsupported);
    Transactions(unsupported);
    OwnerAndBackendLifetime();
    if (!unsupported) PrefixBeforeLaterStop();
    std::cout
        << "Typed PLY file request, lifetime and transaction checks passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
