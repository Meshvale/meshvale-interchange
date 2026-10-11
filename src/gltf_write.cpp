// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/gltf_write.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

#include "gltf_asset_state.h"
#include "gltf_write_detail.h"

namespace meshvale::interchange::gltf_write_detail {
namespace {
using gltf_detail::AdmissionCode;
using gltf_detail::AdmissionFailure;
using gltf_detail::AllocationContext;
using gltf_detail::Allocator;
using gltf_detail::OwnedVector;
constexpr std::uint32_t kWriterRule = 320;
constexpr std::size_t kCheckpointCopyBytes = 4096;
void Reject(GltfStatus status, GltfDiagnosticCode code) {
  gltf_detail::Fail(status, code, {}, kWriterRule);
}
void ValidateConfiguration(const GltfWriteLimits& limits,
                           const GltfWriteWorkPolicy& work_policy) {
  constexpr auto kHostMaximum = std::numeric_limits<std::size_t>::max();
  for (const auto value :
       {limits.max_json_bytes, limits.max_resources, limits.max_resource_bytes,
        limits.max_total_resource_bytes, limits.max_output_requested_bytes,
        work_policy.parallel_min_json_bytes,
        work_policy.json_partition_bytes}) {
    if (!value)
      Reject(GltfStatus::kInvalidLimits, GltfDiagnosticCode::kZeroLimit);
    if (value > kHostMaximum)
      Reject(GltfStatus::kInvalidLimits,
             GltfDiagnosticCode::kLimitNotRepresentable);
  }
}
void ValidateInventory(const gltf_detail::AssetState& source,
                       const GltfWriteLimits& limits,
                       const AllocationContext& allocation) {
  if (source.document.Original().size() > limits.max_json_bytes ||
      source.resources.size() > limits.max_resources)
    Reject(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded);
  std::size_t total = 0;
  for (const auto& resource : source.resources) {
    allocation.Checkpoint();
    const auto bytes = resource.bytes.size();
    if (bytes > limits.max_resource_bytes)
      Reject(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kLimitExceeded);
    if (bytes > std::numeric_limits<std::size_t>::max() - total)
      Reject(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kSizeOverflow);
    total += bytes;
    if (total > limits.max_total_resource_bytes)
      Reject(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kLimitExceeded);
  }
}
void ObserveRequests(const AllocationContext& allocation,
                     ExecutionStats* stats) {
  if (stats)
    stats->maximum_observed_requests = std::max(
        stats->maximum_observed_requests, allocation.OperationActiveBytes());
}
struct BlockDelete {
  void operator()(std::byte* block) const noexcept {
    AllocationContext::Deallocate(block);
  }
};
struct JobResult {
  std::exception_ptr failure;
};
void CopyJson(const gltf_detail::AssetState& source,
              std::span<std::byte> destination,
              const GltfWriteWorkPolicy& policy,
              const AllocationContext& allocation, ExecutionStats* stats,
              const ExecutionHooks* hooks) {
  const auto original = source.document.Original();
  const auto partition = static_cast<std::size_t>(policy.json_partition_bytes);
  const auto jobs =
      original.size() / partition + (original.size() % partition != 0);
  if (stats) stats->jobs = jobs;
  auto lease = original.size() >= policy.parallel_min_json_bytes && jobs >= 2
                   ? geometry::TryReserveWorkers(source.context, jobs)
                   : geometry::WorkerLease{};
  auto copy = [&](std::size_t first, std::size_t last) {
    while (first != last) {
      allocation.Checkpoint();
      const auto count = std::min(last - first, kCheckpointCopyBytes);
      std::memcpy(destination.data() + first, original.data() + first, count);
      first += count;
    }
  };
  if (lease.Count() < 2) {
    copy(0, original.size());
    return;
  }
  OwnedVector<JobResult> results(jobs, JobResult{},
                                 Allocator<JobResult>(allocation));
  OwnedVector<std::jthread> workers{Allocator<std::jthread>(allocation)};
  workers.reserve(lease.Count());
  ObserveRequests(allocation, stats);
  try {
    for (std::size_t worker = 0; worker < lease.Count(); ++worker) {
      if (hooks && hooks->before_launch)
        hooks->before_launch(worker, hooks->state);
      workers.emplace_back([&, worker] {
        for (auto job = worker; job < jobs;) {
          const auto first = job * partition;
          const auto last =
              first + std::min(partition, original.size() - first);
          try {
            if (hooks && hooks->before_job)
              hooks->before_job(job, hooks->state);
            copy(first, last);
          } catch (...) {
            results[job].failure = std::current_exception();
          }
          if (hooks && hooks->after_job) hooks->after_job(job, hooks->state);
          if (jobs - job <= lease.Count()) break;
          job += lease.Count();
        }
      });
      if (stats) ++stats->launched;
    }
  } catch (...) {
    for (auto& worker : workers) {
      worker.join();
      if (stats) ++stats->joined;
    }
    throw;
  }
  for (auto& worker : workers) {
    worker.join();
    if (stats) ++stats->joined;
  }
  // Actual failures precede cooperative cancellation. Both passes keep source
  // job order, independent of worker completion/arrival order.
  for (const auto& result : results) {
    if (!result.failure) continue;
    try {
      std::rethrow_exception(result.failure);
    } catch (const AdmissionFailure&) {
      continue;
    } catch (...) {
      throw;
    }
  }
  for (const auto& result : results)
    if (result.failure) std::rethrow_exception(result.failure);
}
}  // namespace

struct BundleState {
  BundleState(std::shared_ptr<const gltf_detail::AssetState> source,
              const AllocationContext& operation)
      : source(std::move(source)), allocation(operation) {}
  std::shared_ptr<const gltf_detail::AssetState> source;
  AllocationContext allocation;
  std::size_t json_size = 0;
  std::unique_ptr<std::byte, BlockDelete> json;
};
struct Access {
  static GltfWriteBundle Make(std::shared_ptr<const BundleState> owner) {
    return GltfWriteBundle(std::move(owner));
  }
};

GltfWriteResult WriteWithHooks(const GltfAsset& asset,
                               const GltfWriteLimits& limits,
                               const GltfWriteWorkPolicy& policy,
                               std::stop_token stop,
                               const gltf_detail::AllocationBackend& backend,
                               ExecutionStats* stats,
                               const ExecutionHooks* hooks) {
  if (stats) *stats = {};
  try {
    ValidateConfiguration(limits, policy);
    const auto& source = gltf_detail::Access::State(asset);
    AllocationContext root_allocation(source.context, stop, backend);
    root_allocation.Checkpoint();
    ValidateInventory(source, limits, root_allocation);
    auto allocation = root_allocation.WithRequestLimit(
        static_cast<std::size_t>(limits.max_output_requested_bytes));
    ObserveRequests(allocation, stats);
    auto output = std::allocate_shared<BundleState>(
        Allocator<BundleState>(allocation), gltf_detail::Access::Owner(asset),
        allocation);
    ObserveRequests(allocation, stats);
    output->json_size = source.document.Original().size();
    output->json.reset(static_cast<std::byte*>(
        allocation.Allocate(output->json_size, alignof(std::byte))));
    ObserveRequests(allocation, stats);
    CopyJson(source, {output->json.get(), output->json_size}, policy,
             allocation, stats, hooks);
    allocation.Checkpoint();
    return {Access::Make(std::move(output)), gltf_detail::AcceptedDiagnostic()};
  } catch (const gltf_detail::GltfFailure& failure) {
    return {{}, failure.Diagnostic()};
  } catch (const AdmissionFailure& failure) {
    auto status = GltfStatus::kUnsupportedProfile;
    auto code = GltfDiagnosticCode::kLimitExceeded;
    switch (failure.Code()) {
      case AdmissionCode::kSizeLimit:
        break;
      case AdmissionCode::kBudgetExceeded:
        status = GltfStatus::kBudgetExceeded;
        code = GltfDiagnosticCode::kPayloadBudget;
        break;
      case AdmissionCode::kCanceled:
        status = GltfStatus::kCanceled;
        code = GltfDiagnosticCode::kCanceled;
        break;
      case AdmissionCode::kInvalidObject:
        throw;
    }
    return {{}, {status, code, {}, {}, {}, {}, kWriterRule}};
  }
}
}  // namespace meshvale::interchange::gltf_write_detail

namespace meshvale::interchange {
GltfWriteBundle::GltfWriteBundle(
    std::shared_ptr<const gltf_write_detail::BundleState> owner)
    : owner_(std::move(owner)) {}
GltfWriteBundle::GltfWriteBundle(const GltfWriteBundle&) = default;
GltfWriteBundle& GltfWriteBundle::operator=(const GltfWriteBundle&) = default;
GltfWriteBundle::~GltfWriteBundle() = default;
std::span<const std::byte> GltfWriteBundle::Json() const noexcept {
  return {owner_->json.get(), owner_->json_size};
}
std::span<const GltfResourceView> GltfWriteBundle::Resources() const noexcept {
  return owner_->source->resources;
}
std::uint64_t GltfWriteBundle::NewRequestedBytes() const noexcept {
  return owner_->allocation.OperationActiveBytes();
}
GltfWriteResult WriteRetainedGltfAsset(const GltfAsset& asset,
                                       const GltfWriteLimits& limits,
                                       const GltfWriteWorkPolicy& work_policy,
                                       std::stop_token stop) {
  return gltf_write_detail::WriteWithHooks(
      asset, limits, work_policy, stop, gltf_detail::DefaultAllocationBackend(),
      nullptr, nullptr);
}
}  // namespace meshvale::interchange
