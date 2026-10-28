// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/surface_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <utility>

#include "surface_files_internal.h"
#include "surface_output_internal.h"
namespace meshvale::interchange::surface_files_detail {
namespace {
using Status = SurfaceFileStatus;
using Stage = SurfaceFileStage;
using Code = SurfaceFileCode;
using Outcome = SurfaceFilePhaseOutcome;
}  // namespace
[[noreturn]] void Fail(Status status, Stage stage, Code code,
                       std::optional<std::uint64_t> member,
                       std::optional<std::uint64_t> offset,
                       std::optional<std::error_code> error) {
  SurfaceFileDiagnostic d;
  d.status = status;
  d.stage = stage;
  d.code = code;
  d.member_ordinal = member;
  d.byte_offset = offset;
  d.system_error = error;
  throw Failure{d};
}
std::size_t Host(std::uint64_t n) {
  if (n > static_cast<std::uint64_t>(
              std::numeric_limits<std::ptrdiff_t>::max()) ||
      n > std::numeric_limits<std::size_t>::max())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  return static_cast<std::size_t>(n);
}
std::uint64_t Sum(std::uint64_t a, std::uint64_t b) {
  if (b > UINT64_MAX - a)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  auto n = a + b;
  (void)Host(n);
  return n;
}
void Count(std::uint64_t n, std::uint64_t maximum, Stage stage) {
  if (n > maximum) Fail(Status::kLimitExceeded, stage, Code::kCountLimit);
}
namespace {
void Positive(std::uint64_t n) {
  if (!n)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kRequiredFieldZero);
  (void)Host(n);
}
char Lower(char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
bool Alias(std::string_view a, std::string_view b) {
  auto n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i)
    if (Lower(a[i]) != Lower(b[i])) return false;
  return a.size() == b.size() ||
         (a.size() < b.size() ? b[n] == '/' : a[n] == '/');
}
void DestinationSyntax(const std::filesystem::path& path) {
  const auto& v = path.native();
  if (!ValidNativeEncoding(v))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kDestinationPath);
  if (v.empty())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kDestinationPath);
  std::size_t start = 0;
#ifdef _WIN32
  bool drive =
      v.size() >= 3 &&
      ((v[0] >= L'A' && v[0] <= L'Z') || (v[0] >= L'a' && v[0] <= L'z')) &&
      v[1] == L':' && (v[2] == L'/' || v[2] == L'\\');
  if (drive)
    start = 3;
  else if (v.front() == L'/' || v.front() == L'\\')
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kDestinationPath);
#else
  if (v.front() == '/') start = 1;
#endif
  if (start == v.size())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kDestinationPath);
  for (std::size_t i = start; i <= v.size(); ++i) {
    bool sep = i == v.size() || v[i] == static_cast<NativeChar>('/');
#ifdef _WIN32
    sep = sep || (i < v.size() && v[i] == L'\\');
#endif
    if (i < v.size() && v[i] == 0)
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kDestinationPath);
    if (sep) {
      auto part = NativeView(v).substr(start, i - start);
      if (part.empty() ||
          (part.size() == 1 && part[0] == static_cast<NativeChar>('.')) ||
          (part.size() == 2 && part[0] == static_cast<NativeChar>('.') &&
           part[1] == static_cast<NativeChar>('.')))
        Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
             Code::kDestinationPath);
#ifdef _WIN32
      if (part.back() == L'.' || part.back() == L' ')
        Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
             Code::kDestinationPath);
      for (auto c : part)
        if (c == L':' || c == L'<' || c == L'>' || c == L'"' || c == L'|' ||
            c == L'?' || c == L'*')
          Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
               Code::kDestinationPath);
#endif
      start = i + 1;
    }
  }
}
void Validate(const SurfaceOutputInspection& input,
              const std::filesystem::path& destination,
              const SurfaceFilePublicationOptions& o,
              const SurfaceFilePublicationLimits& l,
              const SurfaceFilePublicationWorkPolicy& w) {
  const auto& f = l.files;
  for (auto n : {f.max_payload_members, f.max_payload_member_bytes,
                 f.max_total_payload_bytes, f.max_receipt_bytes,
                 f.max_caller_report_bytes, f.max_path_bytes,
                 f.max_total_path_bytes, f.max_native_path_code_units,
                 f.max_inventory_entries, f.max_stage_name_attempts,
                 f.max_new_requested_bytes, w.files.io_block_bytes,
                 w.files.parallel_min_bytes, w.files.partition_bytes,
                 w.files.cancellation_block_bytes, w.files.max_jobs})
    Positive(n);
  if (f.max_caller_report_bytes >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
          kMaximumJsonEscapeBytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  try {
    surface_detail::ValidateInspectConfiguration(input.Expected().Encoding(),
                                                 l.inspection, w.inspection);
  } catch (const surface_detail::Failure& e) {
    SurfaceFileDiagnostic d;
    d.status = Status::kInvalidConfiguration;
    d.code = e.Diagnostic().code == SurfaceCode::kRequiredFieldZero
                 ? Code::kRequiredFieldZero
             : e.Diagnostic().code == SurfaceCode::kAllowanceTooSmall
                 ? Code::kAllowanceTooSmall
             : e.Diagnostic().code == SurfaceCode::kEncodingLimitsMismatch
                 ? Code::kEncodingLimitsMismatch
             : e.Diagnostic().code == SurfaceCode::kUnknownEnum
                 ? Code::kUnknownEnum
                 : Code::kConfigurationExtent;
    throw Failure{d};
  }
  Positive(l.max_total_requested_allowance);
  if (l.max_total_requested_allowance <
      Sum(f.max_new_requested_bytes,
          l.inspection.max_total_requested_allowance))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kAllowanceTooSmall);
  if (!o.receipt.provide && o.receipt.state)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kProviderConfiguration);
  DestinationSyntax(destination);
  const auto& native = destination.native();
  if (native.empty())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kDestinationPath);
  for (auto c : native)
    if (c == 0)
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kDestinationPath);
  Count(native.size() + 1, f.max_native_path_code_units);
  PortablePath(o.receipt_path, f.max_path_bytes);
  auto members = input.CapturedMembers();
  Count(members.size(), f.max_payload_members);
  std::uint64_t total = 0, paths = o.receipt_path.size();
  for (const auto& m : members) {
    PortablePath(m.path, f.max_path_bytes);
    for (std::size_t ri = 0; ri < o.receipt_path.size(); ++ri) {
      if (o.receipt_path[ri] != '/') continue;
      for (std::size_t pi = 0; pi < m.path.size(); ++pi) {
        if (m.path[pi] != '/') continue;
        const auto rp = o.receipt_path.substr(0, ri);
        const auto pp = m.path.substr(0, pi);
        if (rp != pp && EqualFold(rp, pp))
          Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
               Code::kPathCollision);
      }
    }
    if (Alias(m.path, o.receipt_path))
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kPathCollision);
    Count(m.bytes.size(), f.max_payload_member_bytes);
    total = Sum(total, m.bytes.size());
    paths = Sum(paths, m.path.size());
  }
  Count(total, f.max_total_payload_bytes);
  Count(paths, f.max_total_path_bytes);
  auto path_at = [&](std::size_t i) -> std::string_view {
    return i == members.size() ? o.receipt_path : members[i].path;
  };
  std::uint64_t walk = Sum(members.size(), 1);
  for (std::size_t i = 0; i <= members.size(); ++i) {
    const auto path = path_at(i);
    for (std::size_t end = 0; end < path.size(); ++end) {
      if (path[end] != '/') continue;
      const auto prefix = path.substr(0, end);
      bool seen = false;
      for (std::size_t before = 0; before < i && !seen; ++before) {
        const auto previous = path_at(before);
        seen = previous.size() > end && previous[end] == '/' &&
               previous.substr(0, end) == prefix;
      }
      if (!seen) walk = Sum(walk, 1);
    }
  }
  Count(walk, f.max_inventory_entries);
}
SurfaceFileDiagnostic Admission(const allocation::AdmissionFailure& e,
                                Stage stage) {
  SurfaceFileDiagnostic d;
  d.stage = stage;
  if (e.Code() == allocation::AdmissionCode::kCanceled) {
    d.status = Status::kCanceled;
    d.code = Code::kStopRequested;
  } else if (e.Code() == allocation::AdmissionCode::kBudgetExceeded ||
             e.Provenance() ==
                 allocation::AdmissionProvenance::kOperationRequestLimit) {
    d.status = Status::kBudgetExceeded;
    d.code = Code::kRequestBudget;
  } else {
    d.status = Status::kLimitExceeded;
    d.code = Code::kCountLimit;
  }
  return d;
}
void Add(std::uint64_t& to, std::uint64_t n) { to = Sum(to, n); }
void Merge(SurfaceFileIoObservation& to, const SurfaceFileIoObservation& from) {
  Add(to.bytes_read, from.bytes_read);
  Add(to.bytes_written, from.bytes_written);
  Add(to.bytes_copied, from.bytes_copied);
  Add(to.bytes_compared, from.bytes_compared);
  Add(to.bytes_hashed, from.bytes_hashed);
  Add(to.closed_files, from.closed_files);
  Add(to.execution.completed_jobs, from.execution.completed_jobs);
}
void Job(void* opaque, std::size_t index) noexcept {
  auto& j = static_cast<ByteJob*>(opaque)[index];
  try {
    std::size_t done = 0;
    while (done < j.source.size()) {
      j.allocation->Checkpoint();
      auto count = std::min<std::size_t>(
          {j.source.size() - done, Host(j.work->io_block_bytes),
           Host(j.work->cancellation_block_bytes),
           Host(MaximumReadBlockBytes())});
      auto src = j.source.subspan(done, count);
      switch (j.action) {
        case ByteAction::kWrite:
          WriteBlock(j.handle, j.offset + done, src);
          Add(j.observation.bytes_written, count);
          if (j.hooks.after_write)
            j.hooks.after_write(
                IoProgress{j.stage, j.ordinal, j.offset + done, count},
                j.hooks.state);
          break;
        case ByteAction::kRead:
          ReadBlock(j.handle, j.offset + done,
                    j.destination.subspan(done, count));
          Add(j.observation.bytes_read, count);
          break;
        case ByteAction::kCopy:
          std::copy(src.begin(), src.end(),
                    j.destination.begin() + static_cast<std::ptrdiff_t>(done));
          Add(j.observation.bytes_copied, count);
          break;
        case ByteAction::kCompare:
          for (std::size_t i = 0; i < count; ++i) {
            Add(j.observation.bytes_compared, 1);
            if (src[i] != j.destination[done + i])
              Fail(Status::kVerificationFailed, j.stage, j.mismatch_code,
                   j.ordinal, j.offset + done + i);
          }
          break;
      }
      done += count;
    }
    if (j.action == ByteAction::kRead || j.action == ByteAction::kWrite) {
      CloseChecked(j.handle);
      ++j.observation.closed_files;
    }
    j.observation.execution.completed_jobs = 1;
    j.observation.outcome = Outcome::kCompleted;
  } catch (const Failure& e) {
    j.failure = e.diagnostic;
    Add(j.observation.bytes_read, e.read_bytes);
    Add(j.observation.bytes_written, e.written_bytes);
  } catch (const allocation::AdmissionFailure& e) {
    j.failure = Admission(e, j.stage);
  } catch (...) {
    j.exception = std::current_exception();
  }
}
void Bytes(PublishedState& s, ByteAction action,
           std::span<const std::byte> source, std::span<std::byte> dest,
           NativeView path, std::uint64_t ordinal, Stage stage,
           SurfaceFileIoObservation& obs) {
  auto jobs = std::max<std::size_t>(
      1,
      std::min(Host(s.work.files.max_jobs),
               source.size() / Host(s.work.files.partition_bytes) +
                   (source.size() % Host(s.work.files.partition_bytes) != 0)));
  auto lease = source.size() >= s.work.files.parallel_min_bytes
                   ? geometry::TryReserveWorkers(s.context, jobs)
                   : geometry::WorkerLease{};
  bool parallel = lease.Count() >= 2;
  if (!parallel) jobs = 1;
  s.jobs.clear();
  s.jobs.resize(jobs);
  struct HandleGuard {
    Vector<ByteJob>& jobs;
    ~HandleGuard() noexcept {
      for (auto& j : jobs) j.handle.Close();
    }
  } guard{s.jobs};
  Add(obs.execution.planned_jobs, jobs);
  auto width = source.size() / jobs, remainder = source.size() % jobs;
  std::size_t at = 0;
  for (std::size_t i = 0; i < jobs; ++i) {
    auto& j = s.jobs[i];
    j.allocation = &s.allocation;
    j.work = &s.work.files;
    j.hooks = s.testing.files;
    j.action = action;
    j.ordinal = ordinal;
    j.mismatch_code = ordinal == s.members.size() && stage == Stage::kReceipt
                          ? Code::kReceiptBytes
                          : Code::kFileChanged;
    j.stage = stage;
    j.offset = at;
    auto n = width + (i < remainder ? 1 : 0);
    j.source = source.subspan(at, n);
    if (action != ByteAction::kWrite) j.destination = dest.subspan(at, n);
    if (action == ByteAction::kWrite)
      j.handle = OpenWrite(path, ordinal, s.testing.files, stage, false);
    if (action == ByteAction::kRead)
      j.handle = OpenFile(path, ordinal, s.testing.files, false, stage);
    at += n;
  }
  allocation::OperationWorkerStats stats;
  std::exception_ptr launch;
  try {
    if (parallel)
      allocation::RunOperationJobs(s.allocation, lease, jobs, Job,
                                   s.jobs.data(), s.testing.workers, stats);
    else {
      ++obs.execution.caller_jobs;
      Job(s.jobs.data(), 0);
    }
  } catch (...) {
    launch = std::current_exception();
  }
  Add(obs.execution.launched_workers, stats.launched);
  Add(obs.execution.joined_workers, stats.joined);
  for (const auto& j : s.jobs) Merge(obs, j.observation);
  if (launch) std::rethrow_exception(launch);
  for (const auto& j : s.jobs)
    if (j.exception) std::rethrow_exception(j.exception);
  for (const auto& j : s.jobs)
    if (j.failure) throw Failure{*j.failure};
}
void Write(PublishedState& s, NativeView path, std::span<const std::byte> bytes,
           std::uint64_t ordinal, Stage stage, SurfaceFileIoObservation& obs) {
  auto file = OpenWrite(path, ordinal, s.testing.files, stage);
  CloseChecked(file);
  ++obs.closed_files;
  Bytes(s, ByteAction::kWrite, bytes, {}, path, ordinal, stage, obs);
  ++obs.completed_files;
}
void Capture(PublishedState& s, NativeView path,
             std::span<const std::byte> expected, Buffer& out,
             std::uint64_t ordinal, Stage stage,
             SurfaceFileIoObservation& obs) {
  auto file = OpenFile(path, ordinal, s.testing.files, false, stage);
  const auto mismatch =
      stage == Stage::kReceipt ? Code::kReceiptBytes : Code::kFileChanged;
  auto before = Metadata(file);
  if (before.size != expected.size())
    Fail(Status::kVerificationFailed, stage, mismatch, ordinal);
  CloseChecked(file);
  ++obs.closed_files;
  out.resize(expected.size());
  Bytes(s, ByteAction::kRead, expected, out, path, ordinal, stage, obs);
  auto after = OpenFile(path, ordinal, s.testing.files, false, stage);
  const auto after_metadata = Metadata(after);
  if (after_metadata != before)
    Fail(Status::kVerificationFailed, stage,
         after_metadata.size != before.size ? mismatch : Code::kFileChanged,
         ordinal);
  try {
    RequireEnd(after, expected.size());
  } catch (const Failure& e) {
    Add(obs.bytes_read, e.read_bytes);
    auto diagnostic = e.diagnostic;
    if (diagnostic.code == Code::kFileChanged) diagnostic.code = mismatch;
    throw Failure{diagnostic};
  }
  CloseChecked(after);
  ++obs.closed_files;
  if (stage != Stage::kReceipt) ++obs.completed_files;
}
void FinalReadProgress(PublishedState& s, std::uint64_t bytes) noexcept {
  if (s.testing.final_read_bytes)
    s.testing.final_read_bytes->fetch_add(bytes, std::memory_order_release);
}
void FinalRangeRun(PublishedState& s, FinalRange& j) noexcept {
  j.entered = true;
  try {
    const auto mismatch = j.ordinal == s.members.size() ? Code::kReceiptBytes
                                                        : Code::kFileChanged;
    if (j.before.size != j.file_size)
      Fail(Status::kVerificationFailed, Stage::kInventory, mismatch, j.ordinal);
    std::size_t at = 0;
    while (at < j.expected.size()) {
      s.allocation.Checkpoint();
      const auto n = std::min(j.scratch.size(), j.expected.size() - at);
      const auto chunk = std::span(j.scratch).first(n);
      try {
        ReadBlock(j.handle, j.offset + at, chunk);
      } catch (const Failure& e) {
        Add(j.observation.bytes_read, e.read_bytes);
        FinalReadProgress(s, e.read_bytes);
        throw;
      }
      Add(j.observation.bytes_read, n);
      FinalReadProgress(s, n);
      while (s.testing.final_continue &&
             !s.testing.final_continue->load(std::memory_order_acquire)) {
        s.allocation.Checkpoint();
        NativeYield();
      }
      s.allocation.Checkpoint();
      for (std::size_t i = 0; i < n; ++i) {
        Add(j.observation.bytes_compared, 1);
        if (chunk[i] != j.expected[at + i])
          Fail(Status::kVerificationFailed, Stage::kInventory, mismatch,
               j.ordinal, j.offset + at + i);
      }
      at += n;
    }
    try {
      RequireEnd(j.handle, j.file_size);
    } catch (const Failure& e) {
      Add(j.observation.bytes_read, e.read_bytes);
      FinalReadProgress(s, e.read_bytes);
      auto diagnostic = e.diagnostic;
      if (diagnostic.code == Code::kFileChanged) diagnostic.code = mismatch;
      throw Failure{diagnostic};
    }
    if (Metadata(j.handle) != j.before)
      Fail(Status::kVerificationFailed, Stage::kInventory, Code::kFileChanged,
           j.ordinal);
    CloseChecked(j.handle);
    j.observation.closed_files = 1;
    j.observation.outcome = Outcome::kCompleted;
  } catch (const Failure& e) {
    j.failure = e.diagnostic;
  } catch (const allocation::AdmissionFailure& e) {
    j.failure = Admission(e, Stage::kInventory);
  } catch (...) {
    j.exception = std::current_exception();
  }
}
void FinalJobRun(PublishedState& s, std::size_t index) noexcept {
  auto& job = s.final_jobs[index];
  job.entered = true;
  bool complete = true;
  for (auto range = index; range < s.final_ranges.size();) {
    FinalRangeRun(s, s.final_ranges[range]);
    complete = complete &&
               s.final_ranges[range].observation.outcome == Outcome::kCompleted;
    if (s.final_jobs.size() >= s.final_ranges.size() - range) break;
    range += s.final_jobs.size();
  }
  job.completed = complete;
}
void FinalWorker(allocation::NativeWorkerRecord& record) noexcept {
  auto& s = *static_cast<PublishedState*>(record.state);
  if (!s.final_gate.Wait()) return;
  for (auto job = record.ordinal; job < s.final_jobs.size();) {
    FinalJobRun(s, job);
    if (s.final_records.size() >= s.final_jobs.size() - job) break;
    job += s.final_records.size();
  }
}
void JoinFinal(PublishedState& s, bool abort) noexcept {
  if (s.final_gate.initialized) s.final_gate.Release(abort);
  const auto backend = allocation::DefaultNativeWorkerBackend();
  for (std::size_t i =
           static_cast<std::size_t>(s.final_execution.joined_workers);
       i < s.final_handles.size(); ++i) {
    backend.join(s.final_handles[i], backend.state);
    ++s.final_execution.joined_workers;
    if (s.testing.final_joined_workers)
      s.testing.final_joined_workers->fetch_add(1, std::memory_order_release);
  }
  s.final_lease.Reset();
  if (s.testing.final_lease_released)
    s.testing.final_lease_released->store(true, std::memory_order_release);
  s.final_gate.Close();
  for (auto& range : s.final_ranges) range.handle.Close();
}
struct FinalJoinGuard {
  PublishedState& state;
  ~FinalJoinGuard() noexcept { JoinFinal(state, true); }
};
void PrepareFinal(PublishedState& s) {
  std::uint64_t complete_bytes = 0;
  for (const auto& member : s.members)
    complete_bytes = Sum(complete_bytes, member.expected.size());
  complete_bytes = Sum(complete_bytes, s.receipt.size());
  // This one invocation admits at most max_jobs job records. A job may own
  // several disjoint member subranges; those are not additional executor jobs.
  const auto desired =
      complete_bytes >= s.work.files.parallel_min_bytes
          ? std::max<std::size_t>(
                1, std::min(Host(s.work.files.max_jobs),
                            Host(complete_bytes) /
                                    Host(s.work.files.partition_bytes) +
                                (Host(complete_bytes) %
                                     Host(s.work.files.partition_bytes) !=
                                 0)))
          : 1;
  auto ranges_for = [&](std::size_t bytes) {
    return desired >= 2 && bytes >= s.work.files.parallel_min_bytes
               ? std::max<std::size_t>(
                     1,
                     std::min(
                         desired,
                         bytes / Host(s.work.files.partition_bytes) +
                             (bytes % Host(s.work.files.partition_bytes) != 0)))
               : 1;
  };
  std::uint64_t total_ranges = 0;
  for (const auto& member : s.members)
    total_ranges = Sum(total_ranges, ranges_for(member.expected.size()));
  total_ranges = Sum(total_ranges, ranges_for(s.receipt.size()));
  s.final_ranges.reserve(Host(total_ranges));
  for (std::size_t ordinal = 0; ordinal <= s.members.size(); ++ordinal) {
    const auto expected =
        ordinal == s.members.size()
            ? std::span<const std::byte>(s.receipt.data(), s.receipt.size())
            : s.members[ordinal].expected;
    const NativeView path = ordinal == s.members.size()
                                ? NativeView(s.receipt_native)
                                : NativeView(s.members[ordinal].native);
    const auto range_count = ranges_for(expected.size());
    const auto width = expected.size() / range_count;
    const auto remainder = expected.size() % range_count;
    std::size_t offset = 0;
    for (std::size_t i = 0; i < range_count; ++i) {
      s.final_ranges.emplace_back(s.allocation);
      auto& job = s.final_ranges.back();
      job.ordinal = ordinal;
      job.offset = offset;
      job.file_size = expected.size();
      const auto n = width + (i < remainder ? 1 : 0);
      job.expected = expected.subspan(offset, n);
      job.scratch.resize(std::min({Host(s.work.files.io_block_bytes),
                                   Host(s.work.files.cancellation_block_bytes),
                                   Host(MaximumReadBlockBytes()), n}));
      // Both opens and closes deliberately omit arbitrary file callbacks.
      job.handle = OpenFile(path, ordinal, {}, false, Stage::kInventory);
      job.before = Metadata(job.handle);
      offset += n;
    }
  }
  const auto group_jobs = std::min(desired, s.final_ranges.size());
  if (group_jobs >= 2)
    s.final_lease = geometry::TryReserveWorkers(s.context, group_jobs);
  const bool parallel = s.final_lease.Count() >= 2;
  s.final_jobs.resize(parallel ? group_jobs : 1);
  s.final_execution.planned_jobs = s.final_jobs.size();
  if (!parallel) return;
  s.final_records.resize(s.final_lease.Count());
  s.final_handles.reserve(s.final_lease.Count());
  s.final_gate.Initialize();
  const auto backend = allocation::DefaultNativeWorkerBackend();
  for (std::size_t i = 0; i < s.final_records.size(); ++i) {
    s.allocation.Checkpoint();
    if (s.testing.final_launch_fail_after &&
        s.final_handles.size() == *s.testing.final_launch_fail_after)
      throw std::system_error(
          std::make_error_code(std::errc::resource_unavailable_try_again));
    s.final_records[i] = {FinalWorker, &s, i};
    s.final_handles.push_back(
        backend.launch(&s.final_records[i], backend.state));
    ++s.final_execution.launched_workers;
  }
}
void VerifyFinal(PublishedState& s) {
  if (s.final_records.empty()) {
    for (std::size_t i = 0; i < s.final_jobs.size(); ++i) {
      ++s.final_execution.caller_jobs;
      FinalJobRun(s, i);
    }
  } else {
    s.final_gate.Release(false);
  }
  JoinFinal(s, false);
  // Both reached observations view this one group's actual execution stats.
  for (const auto& job : s.final_jobs)
    if (job.entered && job.completed) Add(s.final_execution.completed_jobs, 1);
  for (std::size_t ordinal = 0; ordinal <= s.members.size(); ++ordinal) {
    auto& target = ordinal == s.members.size()
                       ? s.facts.final_receipt_verification
                       : s.facts.final_payload_verification;
    bool entered = false, complete = true;
    for (const auto& job : s.final_ranges) {
      if (job.ordinal != ordinal) continue;
      entered = entered || job.entered;
      complete = complete && job.observation.outcome == Outcome::kCompleted;
      if (job.entered) {
        if (!target) target.emplace();
        Merge(*target, job.observation);
      }
    }
    if (entered && complete) Add(target->completed_files, 1);
  }
  for (auto* target : {&s.facts.final_payload_verification,
                       &s.facts.final_receipt_verification})
    if (*target) (*target)->execution = s.final_execution;
  for (const auto& job : s.final_ranges)
    if (job.exception) std::rethrow_exception(job.exception);
  for (const auto& job : s.final_ranges)
    if (job.failure) throw Failure{*job.failure};
  if (s.facts.final_payload_verification)
    s.facts.final_payload_verification->outcome = Outcome::kCompleted;
  if (s.facts.final_receipt_verification)
    s.facts.final_receipt_verification->outcome = Outcome::kCompleted;
}
void SuccessScrub(PublishedState& s) noexcept {
  s.publication.Scrub();
  for (auto& member : s.members) {
    for (auto& unit : member.native) unit = NativeChar{};
    member.native.clear();
  }
  for (auto& directory : s.directories) {
    for (auto& unit : directory.native) unit = NativeChar{};
    directory.native.clear();
  }
  for (auto& unit : s.receipt_native) unit = NativeChar{};
  s.receipt_native.clear();
  // Retain allocator/backend state and inert stop ownership. These pointers
  // belong only to operation/test callbacks and are not deallocator state.
  s.testing.files = {};
  s.testing.workers.backend = {nullptr, nullptr, nullptr};
  s.testing.workers.observer = nullptr;
  s.testing.inspection.execution.backend = {nullptr, nullptr, nullptr};
  s.testing.inspection.execution.observer = nullptr;
  s.testing.inspection.checkpoint = nullptr;
  s.testing.inspection.checkpoint_state = nullptr;
  s.testing.inspection.catalog_copy_progress = nullptr;
  s.testing.inspection.catalog_copy_state = nullptr;
  s.testing.before_stage_create = nullptr;
  s.testing.stage_ready = nullptr;
  s.testing.before_inventory = nullptr;
  s.testing.before_final_verify = nullptr;
  s.testing.commit_error = nullptr;
  s.testing.cleanup_error = nullptr;
  s.testing.cleanup_finished = nullptr;
  s.testing.native_commit_observed = nullptr;
  s.testing.final_window_entered = nullptr;
  s.testing.final_read_bytes = nullptr;
  s.testing.final_joined_workers = nullptr;
  s.testing.final_lease_released = nullptr;
  s.testing.final_continue = nullptr;
  s.testing.state = nullptr;
}

struct VisitState {
  PublishedState* state;
  SurfaceFileInventoryObservation* observation;
  bool wrong_kind = false;
  std::optional<std::uint64_t> wrong_kind_member;
};
bool Visit(NativeView path, bool directory, void* opaque) noexcept {
  auto& v = *static_cast<VisitState*>(opaque);
  auto& s = *v.state;
  if (directory) {
    ++v.observation->observed_directories;
    for (auto& d : s.directories)
      if (d.native == path && !d.seen) {
        d.seen = true;
        return true;
      }
    for (std::size_t i = 0; i < s.members.size(); ++i)
      if (s.members[i].native == path) {
        v.wrong_kind = true;
        v.wrong_kind_member = i;
        return false;
      }
    if (s.receipt_present && s.receipt_native == path) {
      v.wrong_kind = true;
      v.wrong_kind_member = s.members.size();
    }
  } else {
    ++v.observation->observed_files;
    for (auto& m : s.members)
      if (m.native == path && !m.seen) {
        m.seen = true;
        return true;
      }
    if (s.receipt_present && s.receipt_native == path) return true;
    for (const auto& d : s.directories)
      if (d.native == path) {
        v.wrong_kind = true;
        return false;
      }
  }
  return false;
}
void Inventory(PublishedState& s, SurfaceFileInventoryObservation& obs,
               bool final) {
  for (auto& m : s.members) m.seen = false;
  for (auto& d : s.directories) d.seen = false;
  VisitState visit{&s, &obs, false, std::nullopt};
  try {
    s.publication.Inventory(Visit, &visit, s.receipt_present, final);
  } catch (const Failure& e) {
    if (!visit.wrong_kind ||
        e.diagnostic.status != Status::kVerificationFailed ||
        e.diagnostic.code != Code::kMemberInventory)
      throw;
    auto diagnostic = e.diagnostic;
    diagnostic.code = Code::kFileKind;
    diagnostic.member_ordinal = visit.wrong_kind_member;
    throw Failure{diagnostic, e.read_bytes, e.written_bytes};
  }
  if (obs.observed_files != s.members.size() + (s.receipt_present ? 1 : 0) ||
      obs.observed_directories != s.directories.size() ||
      std::any_of(s.members.begin(), s.members.end(),
                  [](auto& m) { return !m.seen; }) ||
      std::any_of(s.directories.begin(), s.directories.end(),
                  [](auto& d) { return !d.seen; }))
    Fail(Status::kVerificationFailed, Stage::kInventory,
         Code::kMemberInventory);
  obs.outcome = Outcome::kCompleted;
}
void Directories(PublishedState& s, std::string_view path) {
  for (std::size_t i = 0; i < path.size(); ++i)
    if (path[i] == '/') {
      auto prefix = path.substr(0, i);
      bool found = false;
      for (const auto& d : s.directories)
        if (d.logical == prefix) found = true;
      if (!found) {
        s.directories.emplace_back(s.allocation);
        Assign(s.directories.back().logical, prefix, s.allocation);
      }
    }
}
void FinishFailure(SurfaceFileFacts& f, Status status) {
  if (status != Status::kCanceled) return;
  auto mark = [](auto& o) {
    if (o && o->outcome != Outcome::kCompleted) o->outcome = Outcome::kCanceled;
  };
  mark(f.payload_write);
  mark(f.payload_inventory);
  mark(f.payload_reread);
  mark(f.provider);
  mark(f.report_import);
  mark(f.receipt_write);
  mark(f.receipt_reread);
  mark(f.final_inventory);
  mark(f.final_payload_verification);
  mark(f.final_receipt_verification);
}
}  // namespace
void PortablePath(std::string_view path, std::uint64_t maximum) {
  Count(path.size(), maximum);
  if (path.empty())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kReceiptPath);
  std::size_t at = 0;
  while (at < path.size()) {
    auto end = path.find('/', at);
    if (end == path.npos) end = path.size();
    auto part = path.substr(at, end - at);
    auto alnum = [](char c) {
      return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9');
    };
    if (part.empty() || !alnum(part.front()) || part.back() == '.')
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kReceiptPath);
    for (char c : part)
      if (!alnum(c) && c != '.' && c != '_' && c != '-')
        Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
             Code::kReceiptPath);
    auto dot = part.find('.');
    auto stem = part.substr(0, dot);
    for (auto device : {"con", "prn", "aux", "nul"})
      if (EqualFold(stem, device))
        Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
             Code::kReceiptPath);
    if (stem.size() == 4 &&
        (EqualFold(stem.substr(0, 3), "com") ||
         EqualFold(stem.substr(0, 3), "lpt")) &&
        stem.back() >= '1' && stem.back() <= '9')
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kReceiptPath);
    at = end + 1;
    if (end + 1 == path.size())
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kReceiptPath);
  }
}
PublishedState::PublishedState(const allocation::AllocationContext& a,
                               const geometry::ExecutionContext& c,
                               const SurfaceOutputInspection& i,
                               const SurfaceFilePublicationLimits& l,
                               const SurfaceFilePublicationWorkPolicy& w,
                               std::stop_token token, const TestOptions& t)
    : allocation(a),
      context(c),
      input(i),
      limits(l),
      work(w),
      testing(t),
      stop(token),
      publication(allocation, limits.files, testing, stop),
      members(allocation::Allocator<Member>(a)),
      directories(allocation::Allocator<Directory>(a)),
      supplied(allocation::Allocator<SuppliedSurfaceMember>(a)),
      jobs(allocation::Allocator<ByteJob>(a)),
      final_ranges(allocation::Allocator<FinalRange>(a)),
      final_jobs(allocation::Allocator<FinalJob>(a)),
      final_records(allocation::Allocator<allocation::NativeWorkerRecord>(a)),
      final_handles(allocation::Allocator<std::uintptr_t>(a)),
      scratch(a),
      receipt_path(allocation::Allocator<char>(a)),
      receipt_native(allocation::Allocator<NativeChar>(a)),
      receipt(a),
      report(a) {}
SurfacePublishedBundle Access::Make(
    std::shared_ptr<const PublishedState> s) noexcept {
  return SurfacePublishedBundle(std::move(s));
}
SurfaceFilePublicationResult PublishForTest(
    const SurfaceOutputInspection& input,
    const std::filesystem::path& destination,
    const SurfaceFilePublicationOptions& options,
    const SurfaceFilePublicationLimits& limits,
    const SurfaceFilePublicationWorkPolicy& work, std::stop_token stop,
    const TestOptions& testing) {
  SurfaceFilePublicationResult result;
  std::shared_ptr<PublishedState> state;
  Stage reached = Stage::kConfiguration;
  try {
    Validate(input, destination, options, limits, work);
    if (stop.stop_requested())
      Fail(Status::kCanceled, Stage::kPathPlanning, Code::kStopRequested);
    reached = Stage::kPathPlanning;
    auto context = input.Expected().Original().Source().ShareExecutionContext();
    if (!geometry::SharesExecutionBudget(
            context, input.Expected().Triangles().ShareExecutionContext()))
      Fail(Status::kInvalidStorage, reached, Code::kSourceContract);
    allocation::AllocationContext root(context, stop,
                                       testing.allocation_backend);
    auto own =
        root.WithRequestLimit(Host(limits.files.max_new_requested_bytes));
    state = std::allocate_shared<PublishedState>(
        allocation::Allocator<PublishedState>(own), own, context, input, limits,
        work, stop, testing);
    auto& s = *state;
    Assign(s.receipt_path, options.receipt_path, own);
    s.members.reserve(input.CapturedMembers().size());
    for (const auto& m : input.CapturedMembers()) {
      s.members.emplace_back(own);
      auto& n = s.members.back();
      Assign(n.logical, m.path, own);
      n.expected = m.bytes;
      n.kind = m.kind;
      n.digest = m.sha256;
      Directories(s, m.path);
    }
    Directories(s, options.receipt_path);
    std::sort(
        s.members.begin(), s.members.end(),
        [](const auto& a, const auto& b) { return a.logical < b.logical; });
    std::sort(
        s.directories.begin(), s.directories.end(),
        [](const auto& a, const auto& b) { return a.logical < b.logical; });
    Count(Sum(Sum(s.members.size(), 1), s.directories.size()),
          limits.files.max_inventory_entries);
    s.scratch.resize(std::min({Host(work.files.io_block_bytes),
                               Host(work.files.cancellation_block_bytes),
                               Host(MaximumReadBlockBytes())}));
    s.publication.Plan(destination.native());
    s.publication.AdmitParent();
    for (const auto& m : s.members)
      s.publication.CheckChildExtent(m.logical.size());
    s.publication.CheckChildExtent(s.receipt_path.size());
    for (auto& d : s.directories)
      d.native.reserve(Host(limits.files.max_native_path_code_units));
    for (auto& m : s.members)
      m.native.reserve(Host(limits.files.max_native_path_code_units));
    s.receipt_native.reserve(Host(limits.files.max_native_path_code_units));
    std::size_t maximum_jobs = 1;
    for (const auto& m : s.members)
      maximum_jobs = std::max(
          maximum_jobs,
          std::min(
              Host(work.files.max_jobs),
              m.expected.size() / Host(work.files.partition_bytes) +
                  (m.expected.size() % Host(work.files.partition_bytes) != 0)));
    s.jobs.reserve(maximum_jobs);
    reached = Stage::kStaging;
    s.publication.CreateStage();
    auto buildpath = [&](NativeString& out, std::string_view relative) {
      Assign(out, s.publication.Stage(), own);
      if (out.back() != kSeparator) out.push_back(kSeparator);
      for (char c : relative) {
        own.Checkpoint();
        out.push_back(c == '/' ? kSeparator : static_cast<NativeChar>(c));
      }
    };
    for (auto& d : s.directories) {
      buildpath(d.native, d.logical);
      s.publication.CreateSubdirectory(d.native);
    }
    for (auto& m : s.members) buildpath(m.native, m.logical);
    buildpath(s.receipt_native, s.receipt_path);
    auto& write = s.facts.payload_write.emplace();
    for (std::size_t i = 0; i < s.members.size(); ++i)
      Write(s, s.members[i].native, s.members[i].expected, i, reached, write);
    write.outcome = Outcome::kCompleted;
    reached = Stage::kInventory;
    Inventory(s, s.facts.payload_inventory.emplace(), false);
    reached = Stage::kReread;
    auto& read = s.facts.payload_reread.emplace();
    s.supplied.reserve(s.members.size());
    for (std::size_t i = 0; i < s.members.size(); ++i) {
      auto& m = s.members[i];
      Capture(s, m.native, m.expected, m.captured, i, reached, read);
      s.supplied.push_back({m.logical, m.captured});
    }
    read.outcome = Outcome::kCompleted;
    reached = Stage::kInspection;
    s.allocation.Checkpoint();
    auto& iv = s.facts.inspection.emplace();
    auto inspected = surface_detail::InspectForTest(
        input.Expected(), s.supplied, limits.inspection, work.inspection, stop,
        testing.inspection);
    iv.status = inspected.status;
    iv.diagnostic = inspected.diagnostic;
    iv.facts = inspected.facts;
    if (!inspected.inspection) {
      SurfaceFileDiagnostic d;
      d.stage = reached;
      d.code = Code::kInspection;
      d.status = inspected.status == SurfaceStatus::kCanceled
                     ? Status::kCanceled
                 : inspected.status == SurfaceStatus::kBudgetExceeded
                     ? Status::kBudgetExceeded
                 : inspected.status == SurfaceStatus::kLimitExceeded
                     ? Status::kLimitExceeded
                 : inspected.status == SurfaceStatus::kUnsupported
                     ? Status::kUnsupportedProfile
                     : Status::kVerificationFailed;
      d.inspection_status = inspected.status;
      d.inspection_diagnostic = inspected.diagnostic;
      throw Failure{d};
    }
    s.inspection = *inspected.inspection;
    reached = Stage::kReceipt;
    bool report_present = options.receipt.provide != nullptr;
    if (report_present) {
      s.allocation.Checkpoint();
      auto& provider = s.facts.provider.emplace();
      std::span<const std::byte> bytes;
      try {
        bytes = options.receipt.provide(
            SurfaceFileVerifiedContent{
                *s.inspection, EntryPath(s.input.Expected().Encoding())},
            options.receipt.state);
      } catch (const std::bad_alloc&) {
        throw;
      } catch (...) {
        Fail(Status::kCallbackFailure, reached, Code::kProvider);
      }
      provider.returned_bytes = bytes.size();
      provider.outcome = Outcome::kCompleted;
      Count(bytes.size(), limits.files.max_caller_report_bytes, reached);
      auto& copy = s.facts.report_import.emplace();
      s.report.resize(bytes.size());
      Bytes(s, ByteAction::kCopy, bytes, s.report, {}, s.members.size(),
            reached, copy);
      copy.outcome = Outcome::kCompleted;
    }
    auto& rw = s.facts.receipt_write.emplace();
    Receipt(s, report_present);
    s.receipt_digest = Digest(s.receipt, s);
    Add(rw.bytes_hashed, s.receipt.size());
    Write(s, s.receipt_native, s.receipt, s.members.size(), reached, rw);
    rw.outcome = Outcome::kCompleted;
    s.receipt_present = true;
    auto& rr = s.facts.receipt_reread.emplace();
    Buffer captured(own);
    Capture(s, s.receipt_native, s.receipt, captured, s.members.size(), reached,
            rr);
    Bytes(s, ByteAction::kCompare, s.receipt, captured, {}, s.members.size(),
          reached, rr);
    ++rr.completed_files;
    rr.outcome = Outcome::kCompleted;
    // All callback/fault evaluation and temporary frees finish before the last
    // pass.
    if (s.testing.before_final_verify)
      s.testing.before_final_verify(s.publication.Stage(), s.testing.state);
    if (s.testing.before_inventory)
      s.testing.before_inventory(s.publication.Stage(), true, s.testing.state);
    auto commit_error = s.testing.commit_error
                            ? s.testing.commit_error(s.testing.state)
                            : std::error_code{};
    if (commit_error)
      Fail(Status::kPublicationFailed, Stage::kPublication, Code::kCommitFailed,
           {}, {}, commit_error);
    captured.clear();
    captured.shrink_to_fit();
    inspected.inspection.reset();
    inspected.facts.reset();
    s.supplied.clear();
    for (auto& m : s.members) {
      m.captured.clear();
      m.captured.shrink_to_fit();
    }
    for (auto& job : s.jobs) {
      job.hooks = {};
      job.handle.hooks = {};
    }
    s.jobs.clear();
    s.testing.files = {};
    result.bundle.emplace(Access::Make(state));
    FinalJoinGuard final_guard{s};
    PrepareFinal(s);
    if (s.testing.final_window_entered)
      s.testing.final_window_entered->store(true, std::memory_order_release);
    reached = Stage::kInventory;
    Inventory(s, s.facts.final_inventory.emplace(), true);
    VerifyFinal(s);
    own.Checkpoint();
    reached = Stage::kPublication;
    s.facts.commit.emplace();
    s.publication.Commit(*s.facts.commit, {});
    SuccessScrub(s);
    result.facts = s.facts;
    result.outcome = SurfaceFilePublicationOutcome::kPublished;
    result.diagnostic.status = Status::kSuccess;
    result.diagnostic.stage = Stage::kComplete;
    result.diagnostic.code = Code::kNone;
    return result;
  } catch (const Failure& e) {
    result.diagnostic = e.diagnostic;
  } catch (const allocation::AdmissionFailure& e) {
    result.diagnostic = Admission(e, reached);
  } catch (...) {
    if (state) {
      std::error_code error;
      (void)state->publication.Cleanup(error);
    }
    throw;
  }
  result.bundle.reset();
  result.outcome = result.diagnostic.status == Status::kCanceled
                       ? SurfaceFilePublicationOutcome::kCanceled
                       : SurfaceFilePublicationOutcome::kFailed;
  if (state) {
    FinishFailure(state->facts, result.diagnostic.status);
    std::error_code error;
    result.cleanup.outcome = state->publication.Cleanup(error);
    if (error) result.cleanup.error = error;
    result.facts = state->facts;
  }
  return result;
}
}  // namespace meshvale::interchange::surface_files_detail
namespace meshvale::interchange {
SurfacePublishedBundle::SurfacePublishedBundle(
    std::shared_ptr<const surface_files_detail::PublishedState> s) noexcept
    : state_(std::move(s)) {}
SurfacePublishedBundle::SurfacePublishedBundle(
    const SurfacePublishedBundle&) noexcept = default;
SurfacePublishedBundle& SurfacePublishedBundle::operator=(
    const SurfacePublishedBundle&) noexcept = default;
SurfacePublishedBundle::~SurfacePublishedBundle() noexcept = default;
const SurfaceOutputInspection& SurfacePublishedBundle::Inspection()
    const noexcept {
  return *state_->inspection;
}
SurfaceFileReceiptView SurfacePublishedBundle::Receipt() const noexcept {
  return {state_->receipt_path, state_->receipt, state_->receipt_digest};
}
std::uint64_t SurfacePublishedBundle::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
SurfaceFilePublicationResult PublishSurfaceBundle(
    const SurfaceOutputInspection& i, const std::filesystem::path& d,
    const SurfaceFilePublicationOptions& o,
    const SurfaceFilePublicationLimits& l,
    const SurfaceFilePublicationWorkPolicy& w, std::stop_token stop) {
  return surface_files_detail::PublishForTest(i, d, o, l, w, stop, {});
}
}  // namespace meshvale::interchange
