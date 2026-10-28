// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/surface_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "../src/surface_files_test_internal.h"
#include "surface_files_fixture.h"

namespace {
namespace mv = meshvale::interchange;
namespace geom = meshvale::geometry;
namespace sf = mv::surface_files_detail;
namespace alloc = mv::gltf_detail;
namespace test = mv::surface_files_fixture;
namespace fs = std::filesystem;
using test::Require;
constexpr auto kEncoding = mv::SurfaceEncoding::kGltfJson;
constexpr auto kRepresentation = mv::SurfaceRepresentation::kAnnotatedBundleV1;

struct Observation {
  fs::path stage;
  std::atomic<std::size_t> opened{0}, closed{0}, writes{0}, late{0};
  std::atomic<std::size_t> payload_write_closes{0};
  std::atomic<bool> payload_inventory_entered{false};
  std::atomic<bool> committed{false};
  const std::atomic<bool>* callback_free_window = nullptr;
  void Check() noexcept {
    if (committed.load() ||
        (callback_free_window && callback_free_window->load()))
      late.fetch_add(1);
  }
  sf::TestOptions Options() {
    sf::TestOptions options;
    options.state = this;
    options.files.state = this;
    options.stage_ready = [](sf::NativeView path, void* opaque) {
      auto& self = *static_cast<Observation*>(opaque);
      self.Check();
      self.stage = fs::path{std::basic_string<sf::NativeChar>{path}};
    };
    options.files.before_open = [](std::uint64_t, void* opaque) {
      static_cast<Observation*>(opaque)->Check();
    };
    options.files.opened = [](std::uint64_t, void* opaque) {
      auto& self = *static_cast<Observation*>(opaque);
      self.Check();
      ++self.opened;
    };
    options.files.closed = [](std::uint64_t, void* opaque) noexcept {
      auto& self = *static_cast<Observation*>(opaque);
      self.Check();
      ++self.closed;
    };
    options.files.after_write = [](const sf::IoProgress& progress,
                                   void* opaque) {
      auto& self = *static_cast<Observation*>(opaque);
      self.Check();
      Require(progress.bytes != 0,
              "progress callback follows an actual nonempty store");
      ++self.writes;
    };
    options.native_commit_observed = &committed;
    options.before_inventory = [](sf::NativeView, bool receipt_present,
                                  void* opaque) {
      auto& self = *static_cast<Observation*>(opaque);
      self.Check();
      if (!receipt_present && !self.payload_inventory_entered.exchange(true))
        self.payload_write_closes = self.closed.load();
    };
    options.cleanup_finished = [](mv::SurfaceFileCleanupOutcome,
                                  std::error_code, void* opaque) noexcept {
      static_cast<Observation*>(opaque)->Check();
    };
    return options;
  }
};
void FailedClean(const mv::SurfaceFilePublicationResult& result,
                 const fs::path& destination, const Observation& observation) {
  test::NoOwner(result);
  Require(!fs::exists(destination),
          "failed operation leaves destination absent");
  if (!observation.stage.empty())
    Require(result.cleanup.outcome == mv::SurfaceFileCleanupOutcome::kRemoved &&
                !fs::exists(observation.stage),
            "owned staging tree actually removed");
  Require(observation.opened == observation.closed,
          "all actually opened files closed after failure");
}

void Configuration(const mv::SurfaceOutputInspection& input) {
  test::TempTree tree;
  std::stop_source stopped;
  stopped.request_stop();
  using Member = std::uint64_t mv::SurfaceFileLimits::*;
  constexpr std::array<Member, 11> kFileLimitMembers{
      &mv::SurfaceFileLimits::max_payload_members,
      &mv::SurfaceFileLimits::max_payload_member_bytes,
      &mv::SurfaceFileLimits::max_total_payload_bytes,
      &mv::SurfaceFileLimits::max_receipt_bytes,
      &mv::SurfaceFileLimits::max_caller_report_bytes,
      &mv::SurfaceFileLimits::max_path_bytes,
      &mv::SurfaceFileLimits::max_total_path_bytes,
      &mv::SurfaceFileLimits::max_native_path_code_units,
      &mv::SurfaceFileLimits::max_inventory_entries,
      &mv::SurfaceFileLimits::max_stage_name_attempts,
      &mv::SurfaceFileLimits::max_new_requested_bytes};
  for (auto field : kFileLimitMembers) {
    auto limits = test::FileLimits(kEncoding);
    limits.files.*field = 0;
    Observation observation;
    const auto options = observation.Options();
    const auto result = sf::PublishForTest(input, tree.root / "zero", {},
                                           limits, test::FileWork(kEncoding),
                                           stopped.get_token(), options);
    test::NoOwner(result);
    Require(
        result.diagnostic.status ==
                mv::SurfaceFileStatus::kInvalidConfiguration &&
            result.diagnostic.stage == mv::SurfaceFileStage::kConfiguration &&
            result.diagnostic.code == mv::SurfaceFileCode::kRequiredFieldZero &&
            observation.stage.empty() && !result.facts.payload_write &&
            !result.facts.commit,
        "all eleven file limits validated before stop or staging");
  }
  using WorkMember = std::uint64_t mv::SurfaceFileWorkPolicy::*;
  constexpr std::array<WorkMember, 5> kFileWorkMembers{
      &mv::SurfaceFileWorkPolicy::io_block_bytes,
      &mv::SurfaceFileWorkPolicy::parallel_min_bytes,
      &mv::SurfaceFileWorkPolicy::partition_bytes,
      &mv::SurfaceFileWorkPolicy::cancellation_block_bytes,
      &mv::SurfaceFileWorkPolicy::max_jobs};
  for (auto field : kFileWorkMembers) {
    auto work = test::FileWork(kEncoding);
    work.files.*field = 0;
    const auto result = mv::PublishSurfaceBundle(
        input, tree.root / "zero-work", {}, test::FileLimits(kEncoding), work,
        stopped.get_token());
    test::NoOwner(result);
    Require(
        result.diagnostic.status ==
                mv::SurfaceFileStatus::kInvalidConfiguration &&
            result.diagnostic.code == mv::SurfaceFileCode::kRequiredFieldZero,
        "all file work fields validated before stop");
  }
  auto limits = test::FileLimits(kEncoding);
  limits.max_total_requested_allowance =
      limits.files.max_new_requested_bytes +
      limits.inspection.max_total_requested_allowance - 1;
  const auto insufficient =
      mv::PublishSurfaceBundle(input, tree.root / "allowance", {}, limits,
                               test::FileWork(kEncoding), stopped.get_token());
  Require(insufficient.diagnostic.status ==
                  mv::SurfaceFileStatus::kInvalidConfiguration &&
              insufficient.diagnostic.code ==
                  mv::SurfaceFileCode::kAllowanceTooSmall &&
              !insufficient.facts.payload_write,
          "P plus fresh V allowance checked before stop");
  limits = test::FileLimits(kEncoding);
  limits.inspection.max_total_requested_allowance =
      std::numeric_limits<std::uint64_t>::max();
  const auto overflow =
      mv::PublishSurfaceBundle(input, tree.root / "overflow", {}, limits,
                               test::FileWork(kEncoding), stopped.get_token());
  Require(
      overflow.diagnostic.status ==
              mv::SurfaceFileStatus::kInvalidConfiguration &&
          overflow.diagnostic.code == mv::SurfaceFileCode::kConfigurationExtent,
      "combined allowance addition does not wrap");
  mv::SurfaceFilePublicationOptions provider;
  provider.receipt.state = &tree;
  const auto malformed = mv::PublishSurfaceBundle(
      input, tree.root / "provider", provider, test::FileLimits(kEncoding),
      test::FileWork(kEncoding), stopped.get_token());
  Require(malformed.diagnostic.status ==
                  mv::SurfaceFileStatus::kInvalidConfiguration &&
              malformed.diagnostic.code ==
                  mv::SurfaceFileCode::kProviderConfiguration,
          "null provider with nonnull state is invalid before stop");
  const auto canceled = mv::PublishSurfaceBundle(
      input, tree.root / "stopped", {}, test::FileLimits(kEncoding),
      test::FileWork(kEncoding), stopped.get_token());
  Require(
      canceled.outcome == mv::SurfaceFilePublicationOutcome::kCanceled &&
          canceled.diagnostic.status == mv::SurfaceFileStatus::kCanceled &&
          !canceled.bundle && !canceled.facts.payload_write &&
          !canceled.facts.commit,
      "valid already-stopped request reaches no staging or completed phase");
  std::set<std::string> directories;
  for (const auto& member : input.CapturedMembers())
    for (auto parent = fs::path{member.path}.parent_path(); !parent.empty();
         parent = parent.parent_path())
      directories.insert(parent.generic_string());
  const auto known_inventory = static_cast<std::uint64_t>(
      input.CapturedMembers().size() + 1 + directories.size());
  Require(known_inventory > 1,
          "positive insufficient complete-inventory cap precondition");
  limits = test::FileLimits(kEncoding);
  limits.files.max_inventory_entries = known_inventory - 1;
  Observation observation;
  const auto testing = observation.Options();
  const auto walk_cap = sf::PublishForTest(
      input, tree.root / "stopped-walk-cap", {}, limits,
      test::FileWork(kEncoding), stopped.get_token(), testing);
  Require(
      !walk_cap.bundle &&
          walk_cap.diagnostic.status == mv::SurfaceFileStatus::kLimitExceeded &&
          walk_cap.diagnostic.code == mv::SurfaceFileCode::kCountLimit &&
          !walk_cap.facts.payload_write && !walk_cap.facts.commit &&
          observation.stage.empty() && observation.opened == 0,
      "known positive inventory extent refusal precedes stop and staging");
  limits.files.max_payload_members = 0;
  const auto required_first = mv::PublishSurfaceBundle(
      input, tree.root / "stopped-required-first", {}, limits,
      test::FileWork(kEncoding), stopped.get_token());
  Require(required_first.diagnostic.status ==
                  mv::SurfaceFileStatus::kInvalidConfiguration &&
              required_first.diagnostic.code ==
                  mv::SurfaceFileCode::kRequiredFieldZero,
          "required field validation still precedes known cap and stop");
}

void Paths(const mv::SurfaceOutputInspection& input) {
  test::TempTree tree;
  std::vector<std::string> paths{
      "",          "../receipt.json", "/receipt.json",
      "a\\b.json", "a//b.json",       "./receipt.json",
      "CON.json",  "trailing.",       "C:receipt.json"};
  const auto first = std::string(input.CapturedMembers().front().path);
  paths.push_back(first);
  paths.push_back(first + "/receipt.json");
  auto alias = first;
  for (auto& c : alias)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  paths.push_back(alias);
  for (const auto& path : paths) {
    Observation observation;
    const auto testing = observation.Options();
    mv::SurfaceFilePublicationOptions options;
    options.receipt_path = path;
    const auto result = sf::PublishForTest(
        input, tree.root / "paths", options, test::FileLimits(kEncoding),
        test::FileWork(kEncoding), {}, testing);
    test::NoOwner(result);
    Require(result.diagnostic.status ==
                    mv::SurfaceFileStatus::kInvalidConfiguration &&
                observation.stage.empty() && !result.facts.payload_write,
            "portable receipt/alias/prefix paths refused before filesystem "
            "mutation");
  }
  const auto destination = tree.root / "already-present";
  fs::create_directory(destination);
  constexpr std::array<std::byte, 3> kSentinelBytes{std::byte{1}, std::byte{2},
                                                    std::byte{3}};
  test::Write(destination / "marker", kSentinelBytes);
  const auto before = test::Inventory(destination);
  const auto result = mv::PublishSurfaceBundle(input, destination, {},
                                               test::FileLimits(kEncoding),
                                               test::FileWork(kEncoding));
  if (result.diagnostic.status != mv::SurfaceFileStatus::kDestinationExists ||
      test::Inventory(destination) != before)
    test::PublicationDiagnostic(result,
                                "expected existing destination refusal");
  test::NoOwner(result);
  Require(
      result.diagnostic.status == mv::SurfaceFileStatus::kDestinationExists &&
          test::Inventory(destination) == before,
      "existing destination never replaced or cleaned");
  const auto missing_parent = mv::PublishSurfaceBundle(
      input, tree.root / "absent" / "output", {}, test::FileLimits(kEncoding),
      test::FileWork(kEncoding));
  Require(!missing_parent.bundle &&
              missing_parent.diagnostic.status ==
                  mv::SurfaceFileStatus::kIoFailure &&
              missing_parent.diagnostic.code ==
                  mv::SurfaceFileCode::kDestinationParent &&
              !fs::exists(tree.root / "absent"),
          "publisher never creates an absent destination parent");
  const auto unicode = tree.root / fs::path{u8"physical-\u00e9-\u4e2d"};
  const auto unicode_result =
      mv::PublishSurfaceBundle(input, unicode, {}, test::FileLimits(kEncoding),
                               test::FileWork(kEncoding));
  test::Published(unicode_result);
  Require(
      test::Inventory(unicode).size() == input.CapturedMembers().size() + 1,
      "native Unicode destination does not change portable member spellings");
#ifdef _WIN32
  const std::array<std::wstring, 4> kInvalidUtf16Names{
      std::wstring(1, static_cast<wchar_t>(0xd800)),
      std::wstring(1, static_cast<wchar_t>(0xdc00)),
      std::wstring{static_cast<wchar_t>(0xd800), L'x'},
      std::wstring{static_cast<wchar_t>(0xdc00), static_cast<wchar_t>(0xd800)}};
  std::stop_source stopped;
  stopped.request_stop();
  for (const auto& name : kInvalidUtf16Names) {
    Observation observation;
    const auto testing = observation.Options();
    const auto invalid = sf::PublishForTest(
        input, tree.root / fs::path{name}, {}, test::FileLimits(kEncoding),
        test::FileWork(kEncoding), stopped.get_token(), testing);
    Require(
        !invalid.bundle &&
            invalid.diagnostic.status ==
                mv::SurfaceFileStatus::kInvalidConfiguration &&
            invalid.diagnostic.code == mv::SurfaceFileCode::kDestinationPath &&
            observation.stage.empty() && !invalid.facts.payload_write,
        "raw unpaired/reversed UTF-16 destination rejected before stop or "
        "staging");
  }
  std::wstring paired = L"valid-pair-";
  paired.push_back(static_cast<wchar_t>(0xd83d));
  paired.push_back(static_cast<wchar_t>(0xde00));
  const auto paired_destination = tree.root / fs::path{paired};
  const auto valid_pair = mv::PublishSurfaceBundle(
      input, paired_destination, {}, test::FileLimits(kEncoding),
      test::FileWork(kEncoding));
  test::Published(valid_pair);
  Require(test::Inventory(paired_destination).size() ==
              input.CapturedMembers().size() + 1,
          "genuine valid UTF-16 surrogate pair reaches native publication");
#endif
}

void LogicalCaps(const mv::SurfaceOutputInspection& input) {
  std::uint64_t total = 0, largest = 0, path_total = 16, path_max = 16;
  std::set<std::string> directories;
  for (const auto& member : input.CapturedMembers()) {
    const auto size = static_cast<std::uint64_t>(member.bytes.size());
    total += size;
    largest = std::max(largest, size);
    path_total += member.path.size();
    path_max =
        std::max(path_max, static_cast<std::uint64_t>(member.path.size()));
    for (auto parent = fs::path{member.path}.parent_path(); !parent.empty();
         parent = parent.parent_path())
      directories.insert(parent.generic_string());
  }
  const auto count = static_cast<std::uint64_t>(input.CapturedMembers().size());
  Require(count > 1 && largest > 1 && total > 1,
          "actual rich positive logical-cap preconditions");
  using Member = std::uint64_t mv::SurfaceFileLimits::*;
  const std::array<std::pair<Member, std::uint64_t>, 6> caps{
      {{&mv::SurfaceFileLimits::max_payload_members, count},
       {&mv::SurfaceFileLimits::max_payload_member_bytes, largest},
       {&mv::SurfaceFileLimits::max_total_payload_bytes, total},
       {&mv::SurfaceFileLimits::max_path_bytes, path_max},
       {&mv::SurfaceFileLimits::max_total_path_bytes, path_total},
       {&mv::SurfaceFileLimits::max_inventory_entries,
        count + 1 + directories.size()}}};
  for (const auto& [field, exact] : caps)
    for (bool below : {false, true}) {
      test::TempTree tree;
      auto limits = test::FileLimits(kEncoding);
      limits.files.*field = exact - (below ? 1u : 0u);
      Observation observation;
      const auto testing = observation.Options();
      const auto destination = tree.root / "cap";
      const auto result =
          sf::PublishForTest(input, destination, {}, limits,
                             test::FileWork(kEncoding), {}, testing);
      if (!below)
        test::Published(result);
      else {
        FailedClean(result, destination, observation);
        Require(
            result.diagnostic.status == mv::SurfaceFileStatus::kLimitExceeded &&
                result.diagnostic.code == mv::SurfaceFileCode::kCountLimit,
            "positive actual logical extent minus one is a count refusal");
      }
    }
  test::TempTree tree;
  std::uint64_t actual_receipt = 0;
  {
    const auto result = mv::PublishSurfaceBundle(
        input, tree.root / "receipt", {}, test::FileLimits(kEncoding),
        test::FileWork(kEncoding, 1));
    test::Published(result);
    actual_receipt = result.bundle->Receipt().bytes.size();
  }
  fs::remove_all(tree.root / "receipt");
  for (bool below : {false, true}) {
    auto limits = test::FileLimits(kEncoding);
    limits.files.max_receipt_bytes = actual_receipt - (below ? 1u : 0u);
    Observation observation;
    const auto testing = observation.Options();
    const auto result =
        sf::PublishForTest(input, tree.root / "receipt", {}, limits,
                           test::FileWork(kEncoding, 1), {}, testing);
    if (!below)
      test::Published(result);
    else {
      FailedClean(result, tree.root / "receipt", observation);
      Require(
          result.diagnostic.status == mv::SurfaceFileStatus::kLimitExceeded &&
              result.diagnostic.code == mv::SurfaceFileCode::kReceiptSize &&
              result.facts.inspection && !result.facts.commit,
          "actual escaped receipt exact/minus-one after real fresh V");
    }
    fs::remove_all(tree.root / "receipt");
  }
}

struct Backend {
  std::atomic<std::size_t> calls{0}, live{0}, peak{0}, late{0};
  std::size_t throw_at = 0;
  const Backend* peer = nullptr;
  std::atomic<std::size_t> overlap{0};
  const std::atomic<bool>* commit_observed = nullptr;
  alloc::AllocationBackend Get() {
    return {
        [](std::size_t bytes, std::size_t alignment, void* opaque) -> void* {
          auto& self = *static_cast<Backend*>(opaque);
          if (self.commit_observed && self.commit_observed->load()) ++self.late;
          if (++self.calls == self.throw_at) throw std::bad_alloc{};
          const auto backend = alloc::DefaultAllocationBackend();
          auto* block = backend.allocate(bytes, alignment, backend.state);
          const auto current = self.live.fetch_add(bytes) + bytes;
          if (self.peer && self.peer->live.load() != 0) ++self.overlap;
          auto old = self.peak.load();
          while (current > old &&
                 !self.peak.compare_exchange_weak(old, current)) {
          }
          return block;
        },
        [](void* block, std::size_t bytes, std::size_t alignment,
           void* opaque) noexcept {
          auto& self = *static_cast<Backend*>(opaque);
          if (self.commit_observed && self.commit_observed->load()) ++self.late;
          self.live.fetch_sub(bytes);
          const auto backend = alloc::DefaultAllocationBackend();
          backend.deallocate(block, bytes, alignment, backend.state);
        },
        this};
  }
};
void AllocationAndFinalWindow(const mv::SurfaceOutputInspection& input,
                              const geom::ExecutionContext& context) {
  const auto baseline = context.ActiveTrackedPayload();
  std::size_t publisher_calls = 0, inspector_calls = 0;
  {
    test::TempTree tree;
    Backend publisher;
    Backend inspector;
    inspector.peer = &publisher;
    Observation observation;
    publisher.commit_observed = &observation.committed;
    inspector.commit_observed = &observation.committed;
    auto testing = observation.Options();
    testing.allocation_backend = publisher.Get();
    testing.inspection.allocation = inspector.Get();
    {
      const auto result = sf::PublishForTest(
          input, tree.root / "commit", {}, test::FileLimits(kEncoding),
          test::FileWork(kEncoding, 1), {}, testing);
      test::Published(result);
      Require(observation.committed && observation.late == 0 &&
                  publisher.late == 0 && inspector.late == 0 &&
                  observation.opened == observation.closed,
              "actual native success followed by no file "
              "callback/allocation/free before return");
      Require(observation.payload_inventory_entered &&
                  result.facts.payload_write &&
                  result.facts.payload_write->completed_files ==
                      input.CapturedMembers().size() &&
                  result.facts.payload_write->closed_files ==
                      observation.payload_write_closes.load(),
              "payload write closes match independent actual close-hook events "
              "before its inventory");
      publisher_calls = publisher.calls;
      inspector_calls = inspector.calls;
      Require(publisher.live != 0 && inspector.live != 0 &&
                  inspector.overlap > 0 &&
                  context.ActiveTrackedPayload() > baseline &&
                  result.bundle->NewRequestedBytes() != 0 &&
                  result.bundle->Inspection().NewRequestedBytes() != 0,
              "separate actual P/fresh-V allocations overlap under the "
              "existing input root");
      observation.committed = false;
    }
    Require(publisher.live == 0 && inspector.live == 0,
            "both separate scopes release retained allocations with their last "
            "owners");
  }
  Require(publisher_calls > 2 && inspector_calls > 2,
          "real P and V allocation-fault schedules measured from successful "
          "operation");
  for (bool inspecting : {false, true}) {
    const auto calls = inspecting ? inspector_calls : publisher_calls;
    for (auto point : {std::size_t{1}, calls / 2, calls}) {
      test::TempTree tree;
      Backend publisher;
      Backend inspector;
      (inspecting ? inspector : publisher).throw_at = point;
      Observation observation;
      auto testing = observation.Options();
      testing.allocation_backend = publisher.Get();
      testing.inspection.allocation = inspector.Get();
      bool caught = false;
      try {
        (void)sf::PublishForTest(input, tree.root / "alloc", {},
                                 test::FileLimits(kEncoding),
                                 test::FileWork(kEncoding, 1), {}, testing);
      } catch (const std::bad_alloc&) {
        caught = true;
      }
      Require(caught && publisher.live == 0 && inspector.live == 0 &&
                  !fs::exists(tree.root / "alloc") &&
                  context.ActiveTrackedPayload() == baseline &&
                  context.ActiveWorkers() == 0,
              "real early/interior/final P or V bad_alloc propagates after "
              "cleanup/joins");
      if (!observation.stage.empty())
        Require(!fs::exists(observation.stage),
                "throwing cleanup stage removed");
    }
  }
}
struct FinalWitness {
  std::atomic<bool> entered{false}, released{false};
  std::atomic<std::uint64_t> read{0}, joined{0};
  void Bind(sf::TestOptions& options) {
    options.final_window_entered = &entered;
    options.final_lease_released = &released;
    options.final_read_bytes = &read;
    options.final_joined_workers = &joined;
  }
};
bool SameExecution(const mv::SurfaceFileExecutionStats& a,
                   const mv::SurfaceFileExecutionStats& b) {
  return a.planned_jobs == b.planned_jobs &&
         a.launched_workers == b.launched_workers &&
         a.caller_jobs == b.caller_jobs &&
         a.joined_workers == b.joined_workers &&
         a.completed_jobs == b.completed_jobs;
}
void FinalGroup(const mv::SurfaceOutputInspection& input,
                const geom::ExecutionContext& context) {
  const auto baseline = context.ActiveTrackedPayload();
  std::uint64_t payload_bytes = 0;
  for (const auto& member : input.CapturedMembers())
    payload_bytes += member.bytes.size();
  const auto work = test::FileWork(kEncoding);
  Require(payload_bytes > work.files.max_jobs * work.files.io_block_bytes,
          "genuine final group exceeds one bounded read per admitted worker");
  {
    test::TempTree tree;
    FinalWitness final;
    Observation observation;
    observation.callback_free_window = &final.entered;
    Backend publisher;
    Backend inspector;
    publisher.commit_observed = &final.entered;
    inspector.commit_observed = &final.entered;
    auto testing = observation.Options();
    testing.allocation_backend = publisher.Get();
    testing.inspection.allocation = inspector.Get();
    final.Bind(testing);
    {
      const auto result =
          sf::PublishForTest(input, tree.root / "final-success", {},
                             test::FileLimits(kEncoding), work, {}, testing);
      test::Published(result);
      Require(final.entered && final.released && final.joined > 1 &&
                  final.read > 0 && result.facts.final_payload_verification &&
                  result.facts.final_receipt_verification,
              "one actual parallel final native group is joined and its lease "
              "released before handoff");
      const auto& payload = *result.facts.final_payload_verification;
      const auto& receipt = *result.facts.final_receipt_verification;
      Require(
          SameExecution(payload.execution, receipt.execution) &&
              payload.execution.planned_jobs > 1 &&
              payload.execution.planned_jobs <= work.files.max_jobs &&
              payload.execution.joined_workers == final.joined.load() &&
              payload.execution.launched_workers == final.joined.load() &&
              payload.execution.completed_jobs ==
                  payload.execution.planned_jobs &&
              final.read.load() == payload.bytes_read + receipt.bytes_read &&
              payload.bytes_read == payload_bytes &&
              payload.bytes_compared == payload_bytes &&
              receipt.bytes_read == result.bundle->Receipt().bytes.size() &&
              receipt.bytes_compared == result.bundle->Receipt().bytes.size(),
          "equal final execution views are one nonadditive snapshot; per-kind "
          "bytes remain disjoint");
      Require(
          observation.late == 0 && publisher.late == 0 && inspector.late == 0 &&
              observation.opened == observation.closed &&
              context.ActiveWorkers() == 0,
          "final success window has no injected file/allocation/free callback");
      // Caller destruction after Publish returns may release retained backing.
      final.entered = false;
      observation.committed = false;
    }
    Require(publisher.live == 0 && inspector.live == 0 &&
                context.ActiveTrackedPayload() == baseline,
            "final success owners release both charged scopes after return");
  }
  {
    test::TempTree tree;
    FinalWitness final;
    Observation observation;
    auto testing = observation.Options();
    final.Bind(testing);
    std::atomic<bool> proceed{false}, finished{false}, observed{false},
        timed_out{false};
    testing.final_continue = &proceed;
    std::stop_source stop;
    constexpr auto kProgressWait = std::chrono::seconds{30};
    std::thread controller([&] {
      const auto deadline = std::chrono::steady_clock::now() + kProgressWait;
      while (!finished.load()) {
        if (final.read.load() != 0) {
          observed = true;
          stop.request_stop();
          proceed = true;
          return;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
          timed_out = true;
          stop.request_stop();
          proceed = true;
          return;
        }
        std::this_thread::yield();
      }
      proceed = true;
    });
    std::optional<mv::SurfaceFilePublicationResult> canceled;
    try {
      canceled.emplace(sf::PublishForTest(input, tree.root / "final-stop", {},
                                          test::FileLimits(kEncoding), work,
                                          stop.get_token(), testing));
    } catch (...) {
      finished = true;
      proceed = true;
      controller.join();
      throw;
    }
    finished = true;
    proceed = true;
    controller.join();
    FailedClean(*canceled, tree.root / "final-stop", observation);
    Require(
        observed && !timed_out && final.entered && final.released &&
            final.joined > 1 && final.read > 0 && final.read < payload_bytes &&
            canceled->outcome == mv::SurfaceFilePublicationOutcome::kCanceled &&
            canceled->diagnostic.status == mv::SurfaceFileStatus::kCanceled &&
            !canceled->facts.commit && context.ActiveWorkers() == 0,
        "external stop follows actual final reads, aborts, joins, releases "
        "lease and removes stage");
    const auto& payload = canceled->facts.final_payload_verification;
    const auto& receipt = canceled->facts.final_receipt_verification;
    Require(payload || receipt,
            "actual final read has a reached phase observation");
    const auto read_bytes = (payload ? payload->bytes_read : 0) +
                            (receipt ? receipt->bytes_read : 0);
    const auto& execution = payload ? payload->execution : receipt->execution;
    Require(read_bytes == final.read.load() &&
                execution.joined_workers == final.joined.load() &&
                execution.planned_jobs > 1 &&
                execution.planned_jobs <= work.files.max_jobs &&
                execution.completed_jobs < execution.planned_jobs &&
                (!payload || !receipt ||
                 SameExecution(payload->execution, receipt->execution)),
            "canceled final group preserves partial bytes and one shared "
            "execution snapshot");
    canceled.reset();
    Require(context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "returned fresh-inspection facts release their legitimate retained "
            "budget with the result");
  }
  {
    test::TempTree tree;
    FinalWitness final;
    Observation observation;
    auto testing = observation.Options();
    final.Bind(testing);
    testing.final_launch_fail_after = 1;
    bool propagated = false;
    try {
      (void)sf::PublishForTest(input, tree.root / "final-launch", {},
                               test::FileLimits(kEncoding), work, {}, testing);
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::system_error& error) {
      propagated =
          error.code() ==
          std::make_error_code(std::errc::resource_unavailable_try_again);
    }
    Require(propagated && !final.entered && final.read == 0 &&
                final.joined == 1 && final.released && !observation.committed &&
                observation.opened == observation.closed &&
                !observation.stage.empty() && !fs::exists(observation.stage) &&
                !fs::exists(tree.root / "final-launch") &&
                context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == baseline,
            "partial default final launch aborts waiting worker, joins and "
            "releases before propagation");
  }
}

void FinalShortRead(const mv::SurfaceOutputInspection& input,
                    const geom::ExecutionContext& context) {
#ifndef _WIN32
  test::TempTree tree;
  const auto baseline = context.ActiveTrackedPayload();
  std::uint64_t payload_bytes = 0;
  for (const auto& member : input.CapturedMembers())
    payload_bytes += member.bytes.size();
  Observation observation;
  FinalWitness final;
  auto testing = observation.Options();
  final.Bind(testing);
  std::atomic<bool> proceed{false}, finished{false}, changed{false},
      timed_out{false};
  testing.final_continue = &proceed;
  constexpr std::uint64_t kShortReadBytes = 7;
  constexpr auto kReadWait = std::chrono::seconds{30};
  std::error_code mutation_error;
  std::thread truncator([&] {
    const auto deadline = std::chrono::steady_clock::now() + kReadWait;
    while (!finished.load() && final.read.load() == 0) {
      if (std::chrono::steady_clock::now() >= deadline) {
        timed_out = true;
        proceed = true;
        return;
      }
      std::this_thread::yield();
    }
    if (!finished.load()) {
      // With one job the receipt's single later range has not started. Change
      // only that already-open regular file, after a real payload read.
      fs::resize_file(observation.stage / "publication.json", kShortReadBytes,
                      mutation_error);
      changed = !mutation_error;
    }
    proceed = true;
  });
  std::optional<mv::SurfaceFilePublicationResult> result;
  try {
    result.emplace(sf::PublishForTest(
        input, tree.root / "short-final-read", {}, test::FileLimits(kEncoding),
        test::FileWork(kEncoding, 1), {}, testing));
  } catch (...) {
    finished = true;
    proceed = true;
    truncator.join();
    throw;
  }
  finished = true;
  proceed = true;
  truncator.join();
  FailedClean(*result, tree.root / "short-final-read", observation);
  Require(
      changed && !mutation_error && !timed_out && final.entered &&
          final.released && final.joined == 0 &&
          result->facts.final_payload_verification &&
          result->facts.final_receipt_verification && !result->facts.commit &&
          result->diagnostic.status == mv::SurfaceFileStatus::kIoFailure &&
          result->diagnostic.stage == mv::SurfaceFileStage::kInventory &&
          result->diagnostic.code == mv::SurfaceFileCode::kFileRead &&
          result->diagnostic.member_ordinal == input.CapturedMembers().size() &&
          result->diagnostic.byte_offset == kShortReadBytes &&
          !result->diagnostic.system_error,
      "actual positive POSIX final short read preserves typed EOF progress "
      "without invented OS error");
  const auto& payload = *result->facts.final_payload_verification;
  const auto& receipt = *result->facts.final_receipt_verification;
  Require(payload.bytes_read == payload_bytes &&
              receipt.bytes_read == kShortReadBytes &&
              payload.completed_files == input.CapturedMembers().size() &&
              receipt.completed_files == 0 &&
              final.read.load() == payload_bytes + kShortReadBytes &&
              SameExecution(payload.execution, receipt.execution) &&
              receipt.execution.planned_jobs == 1 &&
              receipt.execution.caller_jobs == 1 &&
              receipt.execution.completed_jobs == 0 &&
              context.ActiveWorkers() == 0,
          "one caller job retains actual trailing bytes and separate file "
          "completion, not duplicate group stats");
  result.reset();
  Require(context.ActiveTrackedPayload() == baseline,
          "short-read result facts release their retained fresh-V charges "
          "after reset");
#else
  (void)input;
  (void)context;
#endif
}

void Budget(const mv::SurfaceOutputInspection& input,
            const geom::ExecutionContext& context) {
  test::TempTree tree;
  const auto destination = tree.root / "budget";
  const auto baseline = context.ActiveTrackedPayload();
  auto run = [&](std::uint64_t cap) {
    bool success = false;
    {
      auto limits = test::FileLimits(kEncoding);
      limits.files.max_new_requested_bytes = cap;
      limits.max_total_requested_allowance =
          cap + limits.inspection.max_total_requested_allowance;
      Observation observation;
      const auto testing = observation.Options();
      const auto result =
          sf::PublishForTest(input, destination, {}, limits,
                             test::FileWork(kEncoding, 1), {}, testing);
      success = result.bundle.has_value();
      if (success)
        test::Published(result);
      else {
        FailedClean(result, destination, observation);
        Require(
            result.diagnostic.status ==
                    mv::SurfaceFileStatus::kBudgetExceeded &&
                result.diagnostic.code == mv::SurfaceFileCode::kRequestBudget,
            "actual P request-budget refusal, not arithmetic overflow");
      }
    }
    fs::remove_all(destination);
    Require(context.ActiveTrackedPayload() == baseline,
            "each budget probe releases its actual scopes");
    return success;
  };
  std::uint64_t low = 1, high = test::kScopeBytes;
  Require(run(high) && !run(low),
          "genuine high-cap success and actual bootstrap denial");
  while (low + 1 < high) {
    const auto middle = low + (high - low) / 2;
    if (run(middle))
      high = middle;
    else
      low = middle;
  }
  Require(run(high) && !run(high - 1),
          "measured deterministic P request exact/minus-one boundary");
  auto pressure = geom::TryReservePayload(
      context, context.TrackedPayloadBudget() - baseline - 1);
  Require(pressure.status == geom::ReservationStatus::kAccepted,
          "actual existing root pressure lease");
  const auto denied = mv::PublishSurfaceBundle(input, destination, {},
                                               test::FileLimits(kEncoding),
                                               test::FileWork(kEncoding));
  test::NoOwner(denied);
  Require(denied.diagnostic.status == mv::SurfaceFileStatus::kBudgetExceeded &&
              denied.diagnostic.code == mv::SurfaceFileCode::kRequestBudget &&
              !fs::exists(destination),
          "existing source/input owners and independent pressure constrain the "
          "same real root");
  pressure.lease.Reset();
}

void WorkersAndCancel(const mv::SurfaceOutputInspection& input,
                      const geom::ExecutionContext& context) {
  test::TempTree tree;
  struct Workers {
    std::atomic<std::size_t> launched{0}, joined{0}, entered{0};
  };
  Workers workers;
  alloc::OperationWorkerObserver observer{
      [](std::size_t, void* p) noexcept {
        ++static_cast<Workers*>(p)->launched;
      },
      [](std::size_t, void* p) noexcept { ++static_cast<Workers*>(p)->joined; },
      [](std::size_t, void* p) noexcept {
        ++static_cast<Workers*>(p)->entered;
      },
      &workers};
  sf::TestOptions testing;
  testing.workers.observer = &observer;
  {
    const auto result = sf::PublishForTest(
        input, tree.root / "parallel", {}, test::FileLimits(kEncoding),
        test::FileWork(kEncoding), {}, testing);
    test::Published(result);
    Require(workers.launched > 0 && workers.joined == workers.launched &&
                workers.entered > 1 && result.facts.payload_write &&
                result.facts.payload_write->execution.planned_jobs > 1 &&
                result.facts.payload_write->execution.launched_workers > 0,
            "real charged physical workers, not a silently serial or synthetic "
            "job count");
  }
  struct Launch {
    std::size_t calls = 0, launched = 0, joined = 0;
  };
  Launch launch;
  testing.workers.observer = nullptr;
  testing.workers.backend = {
      [](alloc::NativeWorkerRecord* record, void* opaque) -> std::uintptr_t {
        auto& injected = *static_cast<Launch*>(opaque);
        if (++injected.calls == 2)
          throw std::runtime_error("surface-files partial launch marker");
        const auto backend = alloc::DefaultNativeWorkerBackend();
        const auto handle = backend.launch(record, backend.state);
        ++injected.launched;
        return handle;
      },
      [](std::uintptr_t handle, void* opaque) noexcept {
        auto& injected = *static_cast<Launch*>(opaque);
        const auto backend = alloc::DefaultNativeWorkerBackend();
        backend.join(handle, backend.state);
        ++injected.joined;
      },
      &launch};
  bool caught = false;
  try {
    (void)sf::PublishForTest(input, tree.root / "launch", {},
                             test::FileLimits(kEncoding),
                             test::FileWork(kEncoding), {}, testing);
  } catch (const std::runtime_error& error) {
    caught =
        std::string_view(error.what()) == "surface-files partial launch marker";
  }
  Require(caught && launch.launched == 1 && launch.joined == 1 &&
              context.ActiveWorkers() == 0 && !fs::exists(tree.root / "launch"),
          "real partial launch joins successful thread and propagates engine "
          "exception");
  struct Cancel {
    std::stop_source source;
    std::size_t observed = 0;
  };
  Cancel cancel;
  testing = {};
  testing.files.state = &cancel;
  testing.files.after_write = [](const sf::IoProgress& progress, void* opaque) {
    auto& injected = *static_cast<Cancel*>(opaque);
    if (progress.stage == mv::SurfaceFileStage::kStaging &&
        progress.offset >= 64 && progress.bytes != 0) {
      ++injected.observed;
      injected.source.request_stop();
    }
  };
  const auto canceled = sf::PublishForTest(
      input, tree.root / "cancel", {}, test::FileLimits(kEncoding),
      test::FileWork(kEncoding, 1), cancel.source.get_token(), testing);
  test::NoOwner(canceled);
  Require(
      cancel.observed > 0 &&
          canceled.outcome == mv::SurfaceFilePublicationOutcome::kCanceled &&
          canceled.diagnostic.status == mv::SurfaceFileStatus::kCanceled &&
          canceled.facts.payload_write &&
          canceled.facts.payload_write->bytes_written > 0 &&
          !canceled.facts.inspection && !canceled.facts.commit &&
          !fs::exists(tree.root / "cancel") && context.ActiveWorkers() == 0,
      "stop observed after genuine interior stores leaves truthful partial "
      "work and no owner");
}

void ParallelProgressCancel(const mv::SurfaceOutputInspection& input,
                            const geom::ExecutionContext& context) {
  test::TempTree tree;
  const auto destination = tree.root / "parallel-progress-stop";
  const auto baseline = context.ActiveTrackedPayload();
  std::vector<mv::SurfaceMemberView> members(input.CapturedMembers().begin(),
                                             input.CapturedMembers().end());
  std::sort(members.begin(), members.end(),
            [](const auto& a, const auto& b) { return a.path < b.path; });
  std::size_t target = 0;
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < members.size(); ++i) {
    total += members[i].bytes.size();
    if (members[i].bytes.size() > members[target].bytes.size()) target = i;
  }
  constexpr std::uint64_t kJobs = 4;
  constexpr std::uint64_t kStoreBlockBytes = 32;
  Require(!members.empty() &&
              members[target].bytes.size() > kJobs * kStoreBlockBytes,
          "actual large payload supports bounded multiworker progress-stop "
          "control");
  Observation observation;
  struct State {
    Observation* observation;
    std::uint64_t ordinal;
    std::uint64_t member_bytes;
    std::thread::id caller;
    std::stop_source stop{};
    std::atomic<std::uint64_t> stored{0}, winning_offset{0}, winning_bytes{0};
    std::atomic<std::size_t> launched{0}, joined{0}, entered{0};
    std::atomic<bool> won{false};
  } state{&observation, static_cast<std::uint64_t>(target),
          static_cast<std::uint64_t>(members[target].bytes.size()),
          std::this_thread::get_id()};
  auto testing = observation.Options();
  testing.files.state = &state;
  testing.files.before_open = nullptr;
  testing.files.opened = [](std::uint64_t, void* opaque) {
    ++static_cast<State*>(opaque)->observation->opened;
  };
  testing.files.closed = [](std::uint64_t, void* opaque) noexcept {
    ++static_cast<State*>(opaque)->observation->closed;
  };
  testing.files.after_write = [](const sf::IoProgress& progress,
                                 void* opaque) noexcept {
    auto& injected = *static_cast<State*>(opaque);
    if (progress.stage != mv::SurfaceFileStage::kStaging) return;
    injected.stored.fetch_add(progress.bytes);
    if (progress.member_ordinal == injected.ordinal && progress.bytes != 0 &&
        progress.offset + progress.bytes < injected.member_bytes &&
        std::this_thread::get_id() != injected.caller &&
        !injected.won.exchange(true)) {
      injected.winning_offset = progress.offset;
      injected.winning_bytes = progress.bytes;
      injected.stop.request_stop();
    }
  };
  alloc::OperationWorkerObserver workers{
      [](std::size_t, void* opaque) noexcept {
        ++static_cast<State*>(opaque)->launched;
      },
      [](std::size_t, void* opaque) noexcept {
        ++static_cast<State*>(opaque)->joined;
      },
      [](std::size_t, void* opaque) noexcept {
        ++static_cast<State*>(opaque)->entered;
      },
      &state};
  testing.workers.observer = &workers;
  auto work = test::FileWork(kEncoding, kJobs);
  work.files.io_block_bytes = kStoreBlockBytes;
  work.files.cancellation_block_bytes = kStoreBlockBytes;
  const auto result =
      sf::PublishForTest(input, destination, {}, test::FileLimits(kEncoding),
                         work, state.stop.get_token(), testing);
  FailedClean(result, destination, observation);
  Require(
      state.won && state.winning_bytes > 0 &&
          state.winning_offset.load() + state.winning_bytes.load() <
              state.member_bytes &&
          result.outcome == mv::SurfaceFilePublicationOutcome::kCanceled &&
          result.diagnostic.status == mv::SurfaceFileStatus::kCanceled &&
          result.facts.payload_write && !result.facts.inspection &&
          !result.facts.commit,
      "actual worker requests stop after a completed interior physical store");
  const auto& actual = *result.facts.payload_write;
  Require(state.launched > 0 && state.joined == state.launched &&
              state.entered > 0 &&
              actual.execution.launched_workers == state.launched.load() &&
              actual.execution.joined_workers == state.joined.load() &&
              actual.execution.completed_jobs < actual.execution.planned_jobs &&
              actual.bytes_written == state.stored.load() &&
              actual.bytes_written > 0 && actual.bytes_written < total &&
              context.ActiveWorkers() == 0 &&
              context.ActiveTrackedPayload() == baseline &&
              fs::is_empty(tree.root),
          "parallel stop preserves real partial bytes, closes/joins/cleanup "
          "and shared-root charges");
}

void IoFailures(const mv::SurfaceOutputInspection& input) {
  constexpr std::array<std::pair<sf::IoOperation, mv::SurfaceFileCode>, 5>
      kIoErrorCases{
          {{sf::IoOperation::kOpen, mv::SurfaceFileCode::kFileOpen},
           {sf::IoOperation::kRead, mv::SurfaceFileCode::kFileRead},
           {sf::IoOperation::kWrite, mv::SurfaceFileCode::kFileWrite},
           {sf::IoOperation::kClose, mv::SurfaceFileCode::kFileClose},
           {sf::IoOperation::kEnumerate, mv::SurfaceFileCode::kEnumeration}}};
  for (const auto& [operation, expected_code] : kIoErrorCases) {
    test::TempTree tree;
    Observation observation;
    auto testing = observation.Options();
    struct Fault {
      sf::IoOperation operation;
      Observation* observation;
      std::atomic<bool> fired{false};
      mv::SurfaceFileStage fired_stage = mv::SurfaceFileStage::kConfiguration;
    };
    Fault fault{operation, &observation};
    testing.files.state = &fault;
    testing.files.before_open = nullptr;
    testing.files.after_write = nullptr;
    testing.files.opened = [](std::uint64_t, void* opaque) {
      ++static_cast<Fault*>(opaque)->observation->opened;
    };
    testing.files.closed = [](std::uint64_t, void* opaque) noexcept {
      ++static_cast<Fault*>(opaque)->observation->closed;
    };
    testing.files.io_error = [](sf::IoOperation op, mv::SurfaceFileStage stage,
                                std::uint64_t, std::uint64_t, void* opaque) {
      auto& injected = *static_cast<Fault*>(opaque);
      if (op == injected.operation && !injected.fired.exchange(true)) {
        injected.fired_stage = stage;
        return std::make_error_code(std::errc::io_error);
      }
      return std::error_code{};
    };
    const auto destination = tree.root / "io";
    const auto result =
        sf::PublishForTest(input, destination, {}, test::FileLimits(kEncoding),
                           test::FileWork(kEncoding, 1), {}, testing);
    FailedClean(result, destination, observation);
    Require(fault.fired &&
                result.diagnostic.status == mv::SurfaceFileStatus::kIoFailure &&
                result.diagnostic.code == expected_code &&
                result.diagnostic.stage == fault.fired_stage &&
                result.diagnostic.system_error ==
                    std::make_error_code(std::errc::io_error) &&
                !result.facts.commit,
            "actual open/read/write/close/enumeration errors preserve first "
            "typed I/O cause");
  }
}

void Provider(const mv::SurfaceOutputInspection& input) {
  for (auto kind : {0, 1, 2, 3, 4, 5}) {
    test::TempTree tree;
    Observation observation;
    const auto testing = observation.Options();
    struct State {
      int kind;
      std::string bytes;
      std::size_t calls = 0;
    };
    State state{kind, "report", 0};
    if (kind == 2) state.bytes = std::string{"\xc0\x80", 2};
    if (kind == 3) state.bytes = std::string{"\xed\xa0\x80", 3};
    if (kind == 4) state.bytes = std::string{"\xf4\x90\x80\x80", 4};
    if (kind == 5) state.bytes = std::string{"\xe2\x82", 2};
    mv::SurfaceFilePublicationOptions options;
    options.receipt = {
        [](const mv::SurfaceFileVerifiedContent&,
           void* opaque) -> std::span<const std::byte> {
          auto& self = *static_cast<State*>(opaque);
          ++self.calls;
          if (self.kind == 0) throw std::runtime_error("provider marker");
          if (self.kind == 1) throw std::bad_alloc{};
          return std::as_bytes(std::span{self.bytes.data(), self.bytes.size()});
        },
        &state};
    bool bad_alloc = false;
    try {
      const auto result = sf::PublishForTest(
          input, tree.root / "provider", options, test::FileLimits(kEncoding),
          test::FileWork(kEncoding, 1), {}, testing);
      FailedClean(result, tree.root / "provider", observation);
      Require(result.facts.inspection && result.facts.provider &&
                  !result.facts.commit &&
                  result.diagnostic.stage == mv::SurfaceFileStage::kReceipt,
              "provider failures occur only after real physical V");
      if (kind == 0)
        Require(result.diagnostic.status ==
                        mv::SurfaceFileStatus::kCallbackFailure &&
                    result.diagnostic.code == mv::SurfaceFileCode::kProvider &&
                    !result.facts.provider->returned_bytes,
                "nonbadalloc provider exception has no fabricated return");
      else
        Require(result.diagnostic.status ==
                        mv::SurfaceFileStatus::kInvalidStorage &&
                    result.diagnostic.code ==
                        mv::SurfaceFileCode::kCallerReportEncoding,
                "actual malformed UTF-8 refused before receipt commit");
    } catch (const std::bad_alloc&) {
      bad_alloc = true;
    }
    Require(state.calls == 1 && bad_alloc == (kind == 1) &&
                !fs::exists(tree.root / "provider"),
            "provider called once; actual bad_alloc propagates after stage "
            "cleanup");
    if (!observation.stage.empty())
      Require(!fs::exists(observation.stage),
              "provider exception stage removed");
  }
  for (bool below : {false, true}) {
    test::TempTree tree;
    auto limits = test::FileLimits(kEncoding);
    static constexpr std::string_view kQuotedReport = "a\"\\\n";
    limits.files.max_caller_report_bytes =
        kQuotedReport.size() - (below ? 1u : 0u);
    mv::SurfaceFilePublicationOptions options;
    options.receipt = {[](const mv::SurfaceFileVerifiedContent&, void*) {
                         return std::as_bytes(std::span{kQuotedReport.data(),
                                                        kQuotedReport.size()});
                       },
                       nullptr};
    const auto result =
        mv::PublishSurfaceBundle(input, tree.root / "report-cap", options,
                                 limits, test::FileWork(kEncoding, 1));
    if (!below)
      test::Published(result);
    else
      Require(!result.bundle &&
                  result.diagnostic.status ==
                      mv::SurfaceFileStatus::kLimitExceeded &&
                  result.diagnostic.code == mv::SurfaceFileCode::kCountLimit,
              "bounded report uses original UTF-8 bytes before JSON escaping");
  }
}

void FreshInspection(const mv::SurfaceOutputInspection& input) {
  for (bool budget : {false, true}) {
    test::TempTree tree;
    Observation observation;
    const auto testing = observation.Options();
    auto limits = test::FileLimits(kEncoding);
    if (budget)
      limits.inspection.max_new_requested_bytes = 1;
    else
      limits.inspection.counts.max_vertices = 1;
    const auto result =
        sf::PublishForTest(input, tree.root / "fresh-v", {}, limits,
                           test::FileWork(kEncoding, 1), {}, testing);
    FailedClean(result, tree.root / "fresh-v", observation);
    Require(result.facts.payload_reread && result.facts.inspection &&
                result.diagnostic.stage == mv::SurfaceFileStage::kInspection &&
                result.diagnostic.inspection_status ==
                    result.facts.inspection->status &&
                result.diagnostic.inspection_diagnostic &&
                !result.facts.provider && !result.facts.receipt_write &&
                !result.facts.commit,
            "actual independent fresh V refusal retains reached inner status "
            "and diagnostics");
    Require(result.diagnostic.status ==
                (budget ? mv::SurfaceFileStatus::kBudgetExceeded
                        : mv::SurfaceFileStatus::kLimitExceeded),
            "fresh inspection budget/count refusal is not flattened to "
            "verification failure");
  }
  for (bool receipt : {false, true}) {
    test::TempTree tree;
    struct State {
      bool receipt;
      std::string member;
      bool reached = false;
    };
    State state{receipt, std::string(input.CapturedMembers().front().path),
                false};
    sf::TestOptions testing;
    testing.state = &state;
    testing.before_final_verify = [](sf::NativeView native, void* opaque) {
      auto& injected = *static_cast<State*>(opaque);
      injected.reached = true;
      const fs::path stage{std::basic_string<sf::NativeChar>{native}};
      const auto path =
          stage / (injected.receipt ? "publication.json" : injected.member);
      auto bytes = test::Read(path);
      Require(!bytes.empty(), "real final-verification mutation bytes");
      bytes.back() ^= std::byte{1};
      test::Write(path, bytes);
    };
    const auto result = sf::PublishForTest(
        input, tree.root / "final-verify", {}, test::FileLimits(kEncoding),
        test::FileWork(kEncoding, 1), {}, testing);
    test::NoOwner(result);
    Require(
        state.reached && result.facts.inspection &&
            result.facts.inspection->status == mv::SurfaceStatus::kSuccess &&
            result.facts.receipt_reread && !result.facts.commit &&
            result.diagnostic.status ==
                mv::SurfaceFileStatus::kVerificationFailed &&
            result.cleanup.outcome == mv::SurfaceFileCleanupOutcome::kRemoved &&
            !fs::exists(tree.root / "final-verify"),
        "changes after successful receipt reread/fresh V still fail final "
        "physical verification");
  }
  test::TempTree tree;
  Observation observation;
  auto testing = observation.Options();
  testing.files.before_open = [](std::uint64_t, void*) {
    throw std::runtime_error("file engine exception marker");
  };
  bool propagated = false;
  try {
    (void)sf::PublishForTest(input, tree.root / "engine", {},
                             test::FileLimits(kEncoding),
                             test::FileWork(kEncoding, 1), {}, testing);
  } catch (const std::runtime_error& error) {
    propagated =
        std::string_view(error.what()) == "file engine exception marker";
  }
  Require(propagated && !observation.stage.empty() &&
              !fs::exists(observation.stage) &&
              !fs::exists(tree.root / "engine"),
          "unexpected file-engine exception propagates after cleanup");
}

void ReceiptRereadMismatch(const mv::SurfaceOutputInspection& input) {
  test::TempTree tree;
  Observation observation;
  struct State {
    Observation* observation;
    std::uint64_t ordinal;
    std::atomic<bool> stored{false}, changed{false};
    std::uint64_t receipt_bytes = 0;
  } state{&observation,
          static_cast<std::uint64_t>(input.CapturedMembers().size())};
  auto testing = observation.Options();
  testing.files.state = &state;
  testing.files.opened = [](std::uint64_t, void* opaque) {
    ++static_cast<State*>(opaque)->observation->opened;
  };
  testing.files.closed = [](std::uint64_t, void* opaque) noexcept {
    ++static_cast<State*>(opaque)->observation->closed;
  };
  testing.files.after_write = [](const sf::IoProgress& progress,
                                 void* opaque) noexcept {
    auto& injected = *static_cast<State*>(opaque);
    if (progress.stage == mv::SurfaceFileStage::kReceipt &&
        progress.member_ordinal == injected.ordinal && progress.bytes != 0)
      injected.stored = true;
  };
  testing.files.before_open = [](std::uint64_t ordinal, void* opaque) {
    auto& injected = *static_cast<State*>(opaque);
    // All range handles precede the first actual store. The next receipt open
    // follows the joined/closed write and precedes its independent reread.
    if (ordinal != injected.ordinal || !injected.stored ||
        injected.changed.exchange(true))
      return;
    const auto path = injected.observation->stage / "publication.json";
    auto bytes = test::Read(path);
    Require(!bytes.empty(), "actual closed receipt before reread mutation");
    injected.receipt_bytes = bytes.size();
    bytes.back() ^= std::byte{1};
    test::Write(path, bytes);
  };
  const auto destination = tree.root / "receipt-reread";
  const auto result =
      sf::PublishForTest(input, destination, {}, test::FileLimits(kEncoding),
                         test::FileWork(kEncoding), {}, testing);
  FailedClean(result, destination, observation);
  Require(state.stored && state.changed && result.facts.receipt_write &&
              result.facts.receipt_reread && !result.facts.final_inventory &&
              !result.facts.commit &&
              result.diagnostic.status ==
                  mv::SurfaceFileStatus::kVerificationFailed &&
              result.diagnostic.stage == mv::SurfaceFileStage::kReceipt &&
              result.diagnostic.code == mv::SurfaceFileCode::kReceiptBytes &&
              result.facts.receipt_reread->outcome ==
                  mv::SurfaceFilePhaseOutcome::kFailed &&
              result.facts.receipt_reread->bytes_read == state.receipt_bytes &&
              result.facts.receipt_reread->completed_files == 0,
          "actual receipt reread comparison fails with ReceiptBytes and no "
          "completed-file claim");
}

void WriteKindSubstitution(const mv::SurfaceOutputInspection& input) {
#ifndef _WIN32
  test::TempTree tree;
  Observation observation;
  struct State {
    Observation* observation;
    std::uint64_t opens = 0;
    bool changed = false;
    int reader = -1;
    ~State() {
      if (reader != -1) (void)close(reader);
    }
  } state{&observation};
  auto testing = observation.Options();
  testing.files.state = &state;
  testing.files.opened = [](std::uint64_t, void* opaque) {
    ++static_cast<State*>(opaque)->observation->opened;
  };
  testing.files.closed = [](std::uint64_t, void* opaque) noexcept {
    ++static_cast<State*>(opaque)->observation->closed;
  };
  testing.files.after_write = nullptr;
  testing.files.before_open = [](std::uint64_t ordinal, void* opaque) {
    auto& injected = *static_cast<State*>(opaque);
    if (ordinal != 0 || ++injected.opens != 2) return;
    std::vector<fs::path> files;
    for (const auto& entry :
         fs::recursive_directory_iterator(injected.observation->stage))
      if (entry.is_regular_file()) files.push_back(entry.path());
    Require(files.size() == 1,
            "first payload create closed before its write reopen");
    const auto& path = files.front();
    fs::remove(path);
    constexpr mode_t kFifoMode = S_IRUSR | S_IWUSR;
    Require(mkfifo(path.c_str(), kFifoMode) == 0,
            "actual POSIX FIFO substitution");
    // The guardian prevents the old blocking O_WRONLY regression from hanging
    // this test; it must still be rejected as FileKind before any store.
    injected.reader =
        open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    Require(injected.reader != -1, "bounded test-owned FIFO reader");
    injected.changed = true;
  };
  const auto destination = tree.root / "write-kind";
  const auto result =
      sf::PublishForTest(input, destination, {}, test::FileLimits(kEncoding),
                         test::FileWork(kEncoding, 1), {}, testing);
  FailedClean(result, destination, observation);
  Require(state.changed && result.facts.payload_write &&
              result.facts.payload_write->bytes_written == 0 &&
              result.diagnostic.status ==
                  mv::SurfaceFileStatus::kVerificationFailed &&
              result.diagnostic.code == mv::SurfaceFileCode::kFileKind &&
              !result.facts.commit,
          "actual reopened nonregular write handle refused before storing");
#else
  (void)input;
#endif
}

void UnsafeMember(const mv::SurfaceOutputInspection& input) {
  test::TempTree tree;
  struct State {
    std::string member;
    std::vector<std::byte> bytes;
    bool reached = false, available = true;
  };
  const auto first = input.CapturedMembers().front();
  State state{std::string(first.path),
              {first.bytes.begin(), first.bytes.end()},
              false,
              true};
  sf::TestOptions testing;
  testing.state = &state;
  testing.before_inventory = [](sf::NativeView native, bool final,
                                void* opaque) {
    auto& injected = *static_cast<State*>(opaque);
    if (final || injected.reached) return;
    injected.reached = true;
    const fs::path stage{std::basic_string<sf::NativeChar>{native}};
    const auto member = stage / injected.member;
    fs::remove(member);
    std::error_code error;
    fs::create_symlink(stage.parent_path() / "absent-link-target", member,
                       error);
    if (error) {
#ifdef _WIN32
      constexpr int kWindowsLinkPrivilegeError = 1314;
      Require(error == std::errc::permission_denied ||
                  error.value() == kWindowsLinkPrivilegeError,
              "Windows symlink injection either succeeds or reports actual "
              "missing privilege");
      injected.available = false;
      test::Write(member, injected.bytes);
#else
      Require(false, "POSIX actual symlink injection");
#endif
    }
  };
  const auto result = sf::PublishForTest(
      input, tree.root / "unsafe", {}, test::FileLimits(kEncoding),
      test::FileWork(kEncoding, 1), {}, testing);
  Require(state.reached, "actual unsafe-member injection site reached");
  if (state.available) {
    test::NoOwner(result);
    Require(
        result.diagnostic.status ==
                mv::SurfaceFileStatus::kVerificationFailed &&
            result.diagnostic.code == mv::SurfaceFileCode::kFileKind &&
            result.cleanup.outcome == mv::SurfaceFileCleanupOutcome::kRemoved &&
            !fs::exists(tree.root / "unsafe"),
        "real symlink payload refused without following target");
  } else {
    test::Published(result);
    std::cout << "Windows actual symlink-injection control unavailable without "
                 "privilege\n";
  }
}

void DirectoryKindSubstitution(const mv::SurfaceOutputInspection& input) {
  test::TempTree tree;
  struct State {
    fs::path directory;
    fs::path stage;
    bool reached = false;
  };
  State state;
  for (const auto& member : input.CapturedMembers()) {
    const auto parent = fs::path{std::string(member.path)}.parent_path();
    if (!parent.empty()) {
      state.directory = parent;
      break;
    }
  }
  Require(!state.directory.empty(),
          "genuine rich captured payload has an expected nested directory");
  sf::TestOptions testing;
  testing.state = &state;
  testing.before_inventory = [](sf::NativeView native, bool final,
                                void* opaque) {
    auto& injected = *static_cast<State*>(opaque);
    if (final || injected.reached) return;
    injected.stage = fs::path{std::basic_string<sf::NativeChar>{native}};
    const auto directory = injected.stage / injected.directory;
    Require(fs::is_directory(directory),
            "expected captured payload parent is an actual staged directory");
    fs::remove_all(directory);
    test::Write(directory, {});
    Require(fs::is_regular_file(directory),
            "actual expected directory replaced with a regular file");
    injected.reached = true;
  };
  const auto destination = tree.root / "directory-kind";
  const auto result =
      sf::PublishForTest(input, destination, {}, test::FileLimits(kEncoding),
                         test::FileWork(kEncoding, 1), {}, testing);
  test::NoOwner(result);
  Require(
      state.reached &&
          result.diagnostic.status ==
              mv::SurfaceFileStatus::kVerificationFailed &&
          result.diagnostic.code == mv::SurfaceFileCode::kFileKind &&
          !result.facts.commit && !fs::exists(destination) &&
          result.cleanup.outcome == mv::SurfaceFileCleanupOutcome::kRemoved &&
          !fs::exists(state.stage),
      "known directory replaced with a regular file has exact FileKind "
      "diagnosis and actual staging cleanup");
}

void InventoryAndCleanup(const mv::SurfaceOutputInspection& input) {
  for (int mutation = 0; mutation < 5; ++mutation) {
    test::TempTree tree;
    Observation observation;
    auto testing = observation.Options();
    struct State {
      int mutation;
      std::string member;
      bool reached = false;
    };
    State state{mutation, std::string(input.CapturedMembers().front().path),
                false};
    testing.state = &state;
    testing.stage_ready = nullptr;
    testing.cleanup_finished = nullptr;
    testing.before_inventory = [](sf::NativeView native, bool final,
                                  void* opaque) {
      auto& injected = *static_cast<State*>(opaque);
      if (final || injected.reached) return;
      injected.reached = true;
      const fs::path stage{std::basic_string<sf::NativeChar>{native}};
      const auto member = stage / injected.member;
      if (injected.mutation == 0) {
        auto bytes = test::Read(member);
        Require(!bytes.empty(), "actual tamper member");
        bytes[0] ^= std::byte{1};
        test::Write(member, bytes);
      }
      if (injected.mutation == 1) fs::remove(member);
      if (injected.mutation == 2) test::Write(stage / "extra.json", {});
      if (injected.mutation == 3) {
        fs::remove(member);
        fs::create_directory(member);
      }
      if (injected.mutation == 4)
        fs::create_directory(stage / "extra-empty-directory");
    };
    const auto result = sf::PublishForTest(
        input, tree.root / "inventory", {}, test::FileLimits(kEncoding),
        test::FileWork(kEncoding, 1), {}, testing);
    test::NoOwner(result);
    Require(
        state.reached && !result.facts.commit &&
            !fs::exists(tree.root / "inventory") &&
            result.cleanup.outcome == mv::SurfaceFileCleanupOutcome::kRemoved,
        "actual altered/missing/extra/nonregular/extra-directory physical "
        "inventory refused");
    if (mutation == 3)
      Require(result.diagnostic.status ==
                      mv::SurfaceFileStatus::kVerificationFailed &&
                  result.diagnostic.code == mv::SurfaceFileCode::kFileKind,
              "known payload replaced with a directory has exact FileKind "
              "diagnosis");
    if (mutation == 1 || mutation == 2 || mutation == 4)
      Require(
          result.diagnostic.status ==
                  mv::SurfaceFileStatus::kVerificationFailed &&
              result.diagnostic.code == mv::SurfaceFileCode::kMemberInventory,
          "missing/extra closed inventory retains exact MemberInventory "
          "diagnosis");
  }
  {
    test::TempTree tree;
    struct State {
      fs::path stage;
      std::size_t finished = 0;
    };
    State state;
    sf::TestOptions testing;
    testing.state = &state;
    testing.stage_ready = [](sf::NativeView path, void* opaque) {
      static_cast<State*>(opaque)->stage =
          fs::path{std::basic_string<sf::NativeChar>{path}};
    };
    testing.files.io_error = [](sf::IoOperation operation, mv::SurfaceFileStage,
                                std::uint64_t, std::uint64_t, void*) {
      return operation == sf::IoOperation::kWrite
                 ? std::make_error_code(std::errc::io_error)
                 : std::error_code{};
    };
    testing.cleanup_error = [](sf::NativeView, void*) noexcept {
      return std::make_error_code(std::errc::permission_denied);
    };
    testing.cleanup_finished = [](mv::SurfaceFileCleanupOutcome outcome,
                                  std::error_code, void* opaque) noexcept {
      if (outcome == mv::SurfaceFileCleanupOutcome::kFailed)
        ++static_cast<State*>(opaque)->finished;
    };
    {
      const auto result = sf::PublishForTest(
          input, tree.root / "cleanup", {}, test::FileLimits(kEncoding),
          test::FileWork(kEncoding, 1), {}, testing);
      Require(
          !result.bundle &&
              result.diagnostic.status == mv::SurfaceFileStatus::kIoFailure &&
              result.diagnostic.code == mv::SurfaceFileCode::kFileWrite &&
              result.cleanup.outcome ==
                  mv::SurfaceFileCleanupOutcome::kFailed &&
              result.cleanup.error ==
                  std::make_error_code(std::errc::permission_denied) &&
              state.finished == 1 && fs::is_directory(state.stage),
          "typed cleanup failure preserves original cause and actual residue");
    }
    Require(state.finished == 1 && fs::is_directory(state.stage),
            "result destructor never silently retries cleanup");
  }
  {
    test::TempTree tree;
    struct State {
      fs::path displaced;
      fs::path stage;
      bool reached = false;
    };
    State state{tree.root / "displaced-owned", {}, false};
    sf::TestOptions testing;
    testing.state = &state;
    testing.before_inventory = [](sf::NativeView native, bool final,
                                  void* opaque) {
      auto& injected = *static_cast<State*>(opaque);
      if (final || injected.reached) return;
      injected.reached = true;
      injected.stage = fs::path{std::basic_string<sf::NativeChar>{native}};
      fs::rename(injected.stage, injected.displaced);
      fs::create_directory(injected.stage);
      constexpr std::array<std::byte, 1> kSentinelBytes{std::byte{0x5a}};
      test::Write(injected.stage / "foreign-owner", kSentinelBytes);
    };
    const auto result = sf::PublishForTest(
        input, tree.root / "identity", {}, test::FileLimits(kEncoding),
        test::FileWork(kEncoding, 1), {}, testing);
    test::NoOwner(result);
    Require(
        state.reached && fs::exists(state.stage / "foreign-owner") &&
            test::Read(state.stage / "foreign-owner") ==
                std::vector<std::byte>{std::byte{0x5a}} &&
            result.cleanup.outcome == mv::SurfaceFileCleanupOutcome::kFailed &&
            !fs::exists(tree.root / "identity"),
        "identity-safe cleanup never removes substituted foreign stage");
  }
}

void CollisionsAndRace(const mv::SurfaceOutputInspection& input) {
  for (std::uint64_t attempts : {2, 3}) {
    test::TempTree tree;
    struct State {
      std::size_t calls = 0;
      std::vector<fs::path> collisions;
    };
    State state;
    sf::TestOptions testing;
    testing.state = &state;
    testing.before_stage_create = [](sf::NativeView path, std::uint64_t,
                                     void* opaque) {
      auto& injected = *static_cast<State*>(opaque);
      if (injected.calls++ < 2) {
        const fs::path candidate{std::basic_string<sf::NativeChar>{path}};
        Require(fs::create_directory(candidate),
                "actual exclusive-stage collision fixture");
        injected.collisions.push_back(candidate);
      }
    };
    auto limits = test::FileLimits(kEncoding);
    limits.files.max_stage_name_attempts = attempts;
    const auto result =
        sf::PublishForTest(input, tree.root / "collisions", {}, limits,
                           test::FileWork(kEncoding, 1), {}, testing);
    if (attempts == 3)
      test::Published(result);
    else
      Require(!result.bundle &&
                  result.diagnostic.code ==
                      mv::SurfaceFileCode::kStageAttemptsExhausted &&
                  state.calls == 2,
              "stage retry cap counts actual create attempts");
    Require(state.collisions.size() == 2,
            "two genuine occupied stage candidates");
    for (const auto& path : state.collisions)
      Require(fs::is_directory(path),
              "module does not clean unrelated collisions");
  }
  test::TempTree tree;
  const auto destination = tree.root / "race";
  struct State {
    const fs::path* destination;
    std::size_t called = 0;
  };
  State state{&destination, 0};
  sf::TestOptions testing;
  testing.state = &state;
  testing.commit_error = [](void* opaque) {
    auto& injected = *static_cast<State*>(opaque);
    ++injected.called;
    Require(fs::create_directory(*injected.destination),
            "actual destination race before native commit window");
    constexpr std::array<std::byte, 1> kSentinelBytes{std::byte{0x71}};
    test::Write(*injected.destination / "racing-owner", kSentinelBytes);
    return std::error_code{};
  };
  const auto result =
      sf::PublishForTest(input, destination, {}, test::FileLimits(kEncoding),
                         test::FileWork(kEncoding, 1), {}, testing);
  Require(state.called == 1 && !result.bundle &&
              result.diagnostic.status ==
                  mv::SurfaceFileStatus::kDestinationExists &&
              result.facts.commit &&
              result.facts.commit->outcome ==
                  mv::SurfaceFileCommitOutcome::kDestinationExists &&
              test::Inventory(destination).size() == 1 &&
              test::Read(destination / "racing-owner") ==
                  std::vector<std::byte>{std::byte{0x71}},
          "actual native no-replace preserves racing destination and records "
          "unsuccessful commit");
}
}  // namespace

int main() {
  try {
    geom::ExecutionContext context({4, 1, 1024 * test::kMiB});
    {
      const auto proof = test::MakeProof(kEncoding, kRepresentation,
                                         test::Shape::kRich, context);
      if (proof.inspection) {
        Configuration(*proof.inspection);
        Paths(*proof.inspection);
        LogicalCaps(*proof.inspection);
        AllocationAndFinalWindow(*proof.inspection, context);
        FinalGroup(*proof.inspection, context);
        FinalShortRead(*proof.inspection, context);
        Budget(*proof.inspection, context);
        WorkersAndCancel(*proof.inspection, context);
        ParallelProgressCancel(*proof.inspection, context);
        IoFailures(*proof.inspection);
        Provider(*proof.inspection);
        if (!proof.empty) FreshInspection(*proof.inspection);
        ReceiptRereadMismatch(*proof.inspection);
        WriteKindSubstitution(*proof.inspection);
        UnsafeMember(*proof.inspection);
        if (!proof.empty) DirectoryKindSubstitution(*proof.inspection);
        InventoryAndCleanup(*proof.inspection);
        CollisionsAndRace(*proof.inspection);
      }
    }
    Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
            "all fault paths release real workers/charged owners");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
