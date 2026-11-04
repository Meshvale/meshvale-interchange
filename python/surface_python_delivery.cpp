// SPDX-License-Identifier: Apache-2.0
#include "surface_python_delivery.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace meshvale::interchange::surface_python_detail {
namespace alloc = gltf_detail;
namespace {
constexpr std::size_t kMinimumCopyWorkers = 2;

std::size_t Host(std::uint64_t value) {
  if (value > std::numeric_limits<std::size_t>::max() ||
      value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::ptrdiff_t>::max()))
    throw std::overflow_error("surface delivery host extent overflow");
  return static_cast<std::size_t>(value);
}
void CheckStop(std::stop_token stop) {
  if (stop.stop_requested())
    throw alloc::AdmissionFailure(alloc::AdmissionCode::kCanceled);
}
}  // namespace

struct CapturedSurfaceMembers::CopyJob {
  std::size_t first = 0, last = 0;
  std::uint64_t copied = 0, checks = 0;
  bool canceled = false;
};

CapturedSurfaceMembers::CapturedSurfaceMembers(
    const SurfaceOutput& output, const alloc::AllocationContext& bridge,
    SurfaceDeliveryObservation& reached)
    : output_(output),
      bridge_(bridge),
      reached_(reached),
      members_(alloc::Allocator<SuppliedSurfaceMember>(bridge)) {
  if (!bridge.HasRequestLimit())
    throw std::invalid_argument("surface delivery requires a capped bridge");
  const alloc::AllocationContext source_root(
      output_.Original().Source().ShareExecutionContext(), {});
  if (!bridge.SharesExecutionBudget(source_root))
    throw std::invalid_argument("surface delivery requires the source ledger");
  // Validate every sum before allocating any member payload. No unbounded
  // value-initialization/memset precedes the checkpointed copy below.
  for (const auto& member : output_.Members()) {
    const auto bytes = Host(member.bytes.size());
    const auto remaining =
        static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) -
        payload_bytes_;
    if (bytes > remaining)
      throw std::overflow_error("surface delivery payload extent overflow");
    payload_bytes_ += bytes;
  }
  (void)alloc::CheckedProduct(output_.Members().size(),
                              sizeof(SuppliedSurfaceMember));
  members_.resize(output_.Members().size());
  // All fallible capacity operations precede raw allocation, so constructor
  // unwinding never loses a charged block.
  if (payload_bytes_) {
    const auto request =
        alloc::AllocationRequestBytes(payload_bytes_, alignof(std::byte));
    payload_ = static_cast<std::byte*>(
        bridge_.Allocate(payload_bytes_, alignof(std::byte)));
    reached_.allocated_payload_bytes = payload_bytes_;
    reached_.allocated_request_bytes = request;
    reached_.active_payload_bytes = payload_bytes_;
  }
  std::size_t offset = 0, ordinal = 0;
  for (const auto& member : output_.Members()) {
    members_[ordinal++] = {
        member.path, member.bytes.empty()
                         ? std::span<const std::byte>{}
                         : std::span<const std::byte>{payload_ + offset,
                                                      member.bytes.size()}};
    offset += member.bytes.size();
  }
}
CapturedSurfaceMembers::~CapturedSurfaceMembers() {
  if (payload_) {
    alloc::AllocationContext::Deallocate(payload_);
    reached_.active_payload_bytes = 0;
  }
}

void CapturedSurfaceMembers::Capture(
    const geometry::MeshMaterializationWorkPolicy& work,
    std::uint64_t cancellation_block_bytes, std::stop_token stop,
    const alloc::NativeExecutionOptions& execution) {
  if (started_)
    throw std::logic_error("surface delivery capture is single-use");
  started_ = true;
  const auto partition = Host(work.partition_bytes);
  const auto block = Host(cancellation_block_bytes);
  const auto maximum_jobs = Host(work.max_jobs);
  if (!partition || !block || !maximum_jobs || !work.parallel_min_bytes)
    throw std::invalid_argument("surface delivery requires positive work caps");
  CheckStop(stop);
  if (!payload_bytes_) {
    captured_ = true;
    return;
  }
  auto count = payload_bytes_ / partition +
               static_cast<std::size_t>(payload_bytes_ % partition != 0);
  count = std::min(count, maximum_jobs);
  const auto context = output_.Original().Source().ShareExecutionContext();
  auto workers =
      payload_bytes_ >= work.parallel_min_bytes && count >= kMinimumCopyWorkers
          ? geometry::TryReserveWorkers(context, count)
          : geometry::WorkerLease();
  if (!workers.Count()) count = 1;
  std::vector<CopyJob, alloc::Allocator<CopyJob>> jobs{
      alloc::Allocator<CopyJob>(bridge_)};
  jobs.resize(count);
  const auto quotient = payload_bytes_ / count,
             remainder = payload_bytes_ % count;
  for (std::size_t i = 0; i < count; ++i) {
    jobs[i].first = i * quotient + std::min(i, remainder);
    jobs[i].last = jobs[i].first + quotient + (i < remainder ? 1 : 0);
  }
  struct CopyState {
    std::span<const SurfaceMemberView> source;
    std::span<CopyJob> jobs;
    std::byte* target;
    std::size_t block;
    std::stop_token stop;
  } state{output_.Members(), jobs, payload_, block, stop};
  const auto copy = [](void* opaque, std::size_t ordinal) noexcept {
    auto& state = *static_cast<CopyState*>(opaque);
    auto& job = state.jobs[ordinal];
    std::size_t base = 0;
    for (const auto& member : state.source) {
      const auto end = base + member.bytes.size();
      auto first = std::max(base, job.first);
      const auto last = std::min(end, job.last);
      while (first < last) {
        ++job.checks;
        if (state.stop.stop_requested()) {
          job.canceled = true;
          return;
        }
        const auto bytes = std::min(last - first, state.block);
        std::memcpy(state.target + first, member.bytes.data() + (first - base),
                    bytes);
        job.copied += bytes;
        first += bytes;
      }
      base = end;
    }
  };
  reached_.planned_jobs = count;
  alloc::OperationWorkerStats stats;
  std::exception_ptr launch_failure;
  try {
    if (workers.Count())
      alloc::RunOperationJobs(bridge_, workers, count, copy, &state, execution,
                              stats);
    else
      copy(&state, 0);
  } catch (...) {
    launch_failure = std::current_exception();
  }
  reached_.launched_workers = stats.launched;
  reached_.joined_workers = stats.joined;
  reached_.caller_jobs = workers.Count() ? 0 : 1;
  for (const auto& job : jobs) {
    reached_.copied_bytes += job.copied;
    reached_.byte_checkpoints += job.checks;
  }
  if (launch_failure) std::rethrow_exception(launch_failure);
  for (const auto& job : jobs)
    if (job.canceled)
      throw alloc::AdmissionFailure(alloc::AdmissionCode::kCanceled);
  CheckStop(stop);
  captured_ = true;
}

std::span<const SuppliedSurfaceMember> CapturedSurfaceMembers::Members() const {
  if (!captured_)
    throw std::logic_error("surface delivery bytes are not captured");
  return members_;
}
}  // namespace meshvale::interchange::surface_python_detail
