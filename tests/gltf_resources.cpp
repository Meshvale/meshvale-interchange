// SPDX-License-Identifier: Apache-2.0
#include "gltf_resources.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>

namespace {
namespace mv = meshvale::interchange;
namespace detail = mv::gltf_detail;
using meshvale::geometry::ExecutionContext;
constexpr std::size_t kPayloadBudget = 10000000;
constexpr std::size_t kWorkerCount = 4;
constexpr std::size_t kCopyPartitionBytes = 16;
constexpr std::size_t kSourceBytes = kWorkerCount * kCopyPartitionBytes;
constexpr std::string_view kExternalBuffer =
    R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":64,"uri":"b.bin"}]})";
void Require(bool condition) {
  if (!condition) throw std::runtime_error("resource expectation failed");
}
detail::AssetState State(
    std::string_view source, const ExecutionContext& context,
    std::stop_token stop = {},
    detail::AllocationBackend backend = detail::DefaultAllocationBackend()) {
  constexpr detail::JsonCaptureLimits kCaptureLimits{65536, 8192, 65536, 64};
  constexpr mv::GltfReadLimits kReadLimits{65536, 65536, 131072, 8192,  64,
                                           65536, 256,   256,    32,    8192,
                                           8192,  8192,  8192,   131072};
  constexpr mv::GltfWorkPolicy kWorkPolicy{1, kCopyPartitionBytes, 1, 1};
  auto capture = detail::CaptureJsonSnapshot(
      source, kCaptureLimits, detail::AllocationContext(context, {}));
  Require(capture.snapshot.has_value());
  detail::AssetState state(context, stop, kReadLimits, kWorkPolicy,
                           std::move(*capture.snapshot));
  // Only the resource stage uses the fault backend. Capture is already owned.
  state.allocation = detail::AllocationContext(context, stop, backend);
  state.resource_storage = detail::OwnedVector<detail::OwnedVector<std::byte>>(
      detail::Allocator<detail::OwnedVector<std::byte>>(state.allocation));
  state.resources = detail::OwnedVector<mv::GltfResourceView>(
      detail::Allocator<mv::GltfResourceView>(state.allocation));
  return state;
}
void OwnedInventoryAndAliases() {
  ExecutionContext context({kWorkerCount, 1, kPayloadBudget});
  {
    constexpr std::string_view kInventory =
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":8,"uri":"png.bin"},{"byteLength":1,"uri":"unused.bin"}],"bufferViews":[{"buffer":0,"byteLength":8}],"images":[{"bufferView":0,"mimeType":"image/png"}]})";
    constexpr std::array<std::byte, 8> kPng = {
        std::byte{137}, std::byte{80}, std::byte{78}, std::byte{71},
        std::byte{13},  std::byte{10}, std::byte{26}, std::byte{10}};
    std::array<std::byte, 12> input{};
    std::copy(kPng.begin(), kPng.end(), input.begin());
    input.back() = std::byte{42};
    const std::array unused{std::byte{7}, std::byte{8}};
    const std::array bindings = {
        mv::SuppliedGltfResource{mv::GltfResourceKind::kBuffer, 1, unused},
        mv::SuppliedGltfResource{mv::GltfResourceKind::kBuffer, 0, input}};
    auto state = State(kInventory, context);
    detail::AdmitResources(state, bindings);
    input.fill(std::byte{0});
    Require(state.resources.size() == 3);
    Require(state.resources[0].bytes.size() == 12 &&
            state.resources[0].logical_bytes == 8 &&
            state.resources[0].bytes.back() == std::byte{42});
    Require(state.resources[1].bytes.size() == 2 &&
            state.resources[1].logical_bytes == 1 &&
            state.resources[1].bytes[0] == std::byte{7});
    Require(state.resources[2].origin == mv::GltfResourceOrigin::kBufferView &&
            state.resources[2].bytes.data() ==
                state.resources[0].bytes.data() &&
            state.resources[2].bytes.size() == kPng.size());
    Require(
        std::equal(kPng.begin(), kPng.end(), state.resources[2].bytes.begin()));
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0);
}
void BindingAndExtentFailures() {
  ExecutionContext context({kWorkerCount, 1, kPayloadBudget});
  const std::array<std::byte, kSourceBytes> input{};
  const mv::SuppliedGltfResource binding{mv::GltfResourceKind::kBuffer, 0,
                                         input};
  auto rejected = [&](std::span<const mv::SuppliedGltfResource> bindings,
                      mv::GltfDiagnosticCode expected, bool bound = false) {
    bool failed = false;
    {
      auto state = State(kExternalBuffer, context);
      if (bound) state.limits.max_resource_bytes = input.size() - 1;
      try {
        detail::AdmitResources(state, bindings);
      } catch (const detail::GltfFailure& failure) {
        Require(failure.Diagnostic().code == expected);
        failed = true;
      }
    }
    Require(failed && context.ActiveTrackedPayload() == 0 &&
            context.ActiveWorkers() == 0);
  };
  rejected({}, mv::GltfDiagnosticCode::kMissingResource);
  const std::array duplicates = {binding, binding};
  rejected(duplicates, mv::GltfDiagnosticCode::kDuplicateResource);
  const std::array extra = {
      mv::SuppliedGltfResource{mv::GltfResourceKind::kImage, 0, input}};
  rejected(extra, mv::GltfDiagnosticCode::kExtraResource);
  const std::array short_binding = {
      mv::SuppliedGltfResource{mv::GltfResourceKind::kBuffer, 0,
                               std::span(input).first(input.size() - 1)}};
  rejected(short_binding, mv::GltfDiagnosticCode::kResourceExtent);
  rejected(std::span(&binding, 1), mv::GltfDiagnosticCode::kLimitExceeded,
           true);
}
void WorkerJoinsAndFallback() {
  ExecutionContext context({kWorkerCount, 1, kPayloadBudget});
  std::array<std::byte, kSourceBytes> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<std::byte>(i);
  const mv::SuppliedGltfResource binding{mv::GltfResourceKind::kBuffer, 0,
                                         input};
  detail::ResourceExecutionStats stats;
  {
    auto state = State(kExternalBuffer, context);
    detail::AdmitResources(state, std::span(&binding, 1), &stats);
    Require(stats.jobs == kWorkerCount && stats.launched == kWorkerCount &&
            stats.joined == kWorkerCount && stats.caller_copies == 0);
    Require(std::equal(input.begin(), input.end(),
                       state.resources[0].bytes.begin()));
  }
  {
    auto occupied =
        meshvale::geometry::TryReserveWorkers(context, kWorkerCount);
    Require(occupied.Count() == kWorkerCount);
    auto state = State(kExternalBuffer, context);
    detail::AdmitResources(state, std::span(&binding, 1), &stats);
    Require(stats.launched == 0 && stats.joined == 0 &&
            stats.caller_copies == 1);
    Require(context.ActiveWorkers() == kWorkerCount);
    Require(std::equal(input.begin(), input.end(),
                       state.resources[0].bytes.begin()));
  }
  detail::ResourceExecutionHooks hooks;
  hooks.before_launch = [](std::size_t worker, void*) {
    if (worker == 1) throw std::runtime_error("partial launch failure");
  };
  bool threw = false;
  try {
    auto state = State(kExternalBuffer, context);
    detail::AdmitResources(state, std::span(&binding, 1), &stats, &hooks);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  Require(threw && stats.launched == 1 && stats.joined == 1);
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0);
  struct Schedule {
    std::stop_source stop;
    std::atomic<bool> first_done{false};
    bool runtime_failure = false;
  } schedule;
  hooks = {};
  hooks.state = &schedule;
  hooks.before_job = [](std::size_t job, void* opaque) {
    auto& schedule = *static_cast<Schedule*>(opaque);
    if (job != 0)
      while (!schedule.first_done.load(std::memory_order_acquire))
        std::this_thread::yield();
    if (job == 2 && schedule.runtime_failure)
      throw std::runtime_error("resource worker exception");
  };
  hooks.after_job = [](std::size_t job, void* opaque) noexcept {
    auto& schedule = *static_cast<Schedule*>(opaque);
    if (job == 0) {
      schedule.stop.request_stop();
      schedule.first_done.store(true, std::memory_order_release);
    }
  };
  bool canceled = false;
  try {
    auto state = State(kExternalBuffer, context, schedule.stop.get_token());
    detail::AdmitResources(state, std::span(&binding, 1), &stats, &hooks);
  } catch (const detail::AdmissionFailure& failure) {
    Require(failure.Code() == detail::AdmissionCode::kCanceled);
    canceled = true;
  }
  Require(canceled && stats.launched == kWorkerCount &&
          stats.joined == kWorkerCount);
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0);
  schedule.stop = std::stop_source{};
  schedule.first_done.store(false);
  schedule.runtime_failure = true;
  threw = false;
  try {
    auto state = State(kExternalBuffer, context, schedule.stop.get_token());
    detail::AdmitResources(state, std::span(&binding, 1), &stats, &hooks);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  Require(threw && stats.launched == kWorkerCount &&
          stats.joined == kWorkerCount);
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0);
}
void ResourceAllocationFailures() {
  struct Fault {
    detail::AllocationBackend original = detail::DefaultAllocationBackend();
    std::size_t remaining;
  };
  detail::AllocationBackend backend{
      [](std::size_t bytes, std::size_t alignment, void* opaque) -> void* {
        auto& fault = *static_cast<Fault*>(opaque);
        if (fault.remaining == 0) throw std::bad_alloc();
        --fault.remaining;
        return fault.original.allocate(bytes, alignment, fault.original.state);
      },
      [](void* block, std::size_t bytes, std::size_t alignment,
         void* opaque) noexcept {
        auto& fault = *static_cast<Fault*>(opaque);
        fault.original.deallocate(block, bytes, alignment,
                                  fault.original.state);
      },
      nullptr};
  const std::array<std::byte, kSourceBytes> input{};
  const mv::SuppliedGltfResource binding{mv::GltfResourceKind::kBuffer, 0,
                                         input};
  bool accepted = false;
  std::size_t rejected = 0;
  constexpr std::size_t kMaximumFaultBoundaries = 16;
  for (std::size_t allowance = 0; allowance < kMaximumFaultBoundaries;
       ++allowance) {
    ExecutionContext context({kWorkerCount, 1, kPayloadBudget});
    Fault fault{detail::DefaultAllocationBackend(), allowance};
    backend.state = &fault;
    detail::ResourceExecutionStats stats;
    try {
      auto state = State(kExternalBuffer, context, {}, backend);
      detail::AdmitResources(state, std::span(&binding, 1), &stats);
      Require(stats.launched == kWorkerCount && stats.joined == kWorkerCount);
      accepted = true;
    } catch (const std::bad_alloc&) {
      ++rejected;
    }
    Require(context.ActiveWorkers() == 0 &&
            context.ActiveTrackedPayload() == 0);
    Require(stats.launched == stats.joined);
    if (accepted) break;
  }
  Require(accepted && rejected >= 4);
}
}  // namespace
int main() {
  try {
    OwnedInventoryAndAliases();
    BindingAndExtentFailures();
    WorkerJoinsAndFallback();
    ResourceAllocationFailures();
    std::cout << "owned inventory, aliases, copy workers and failure cleanup "
                 "passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
