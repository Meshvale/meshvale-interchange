// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "../src/gltf_allocation.h"
#include "../src/gltf_operation_workers.h"
#include "../src/gltf_workflow_test_internal.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::gltf_detail;
constexpr std::uint64_t kBytes = 1024 * 1024;
constexpr std::uint64_t kBudget = 64 * kBytes;
constexpr std::uint64_t kCount = 4096;
constexpr std::uint64_t kDepth = 64;
constexpr std::size_t kThreads = 4;
constexpr std::size_t kQueries = 1024;
constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"raw.bin","byteLength":4}]})";
constexpr std::array<std::byte, 8> kResource{
    std::byte{0},    std::byte{1},    std::byte{2},    std::byte{3},
    std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};
void Require(bool condition, std::string_view message,
             std::source_location where = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
ic::GltfReadLimits Limits() {
  return {kBytes, kBytes, kBytes, kCount, kDepth, kBytes, kCount,
          kCount, kCount, kCount, kCount, kCount, kCount, kBudget};
}
struct Recorder {
  std::atomic<std::size_t> requests{0};
  std::atomic<std::size_t> releases{0};
  std::atomic<std::size_t> live{0};
  std::atomic<bool> armed{false};
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Recorder*>(state);
    ++self.requests;
    if (self.armed.load()) throw std::bad_alloc();
    auto* result = ::operator new(bytes, std::align_val_t{alignment});
    ++self.live;
    return result;
  }
  static void Deallocate(void* pointer, std::size_t, std::size_t alignment,
                         void* state) noexcept {
    auto& self = *static_cast<Recorder*>(state);
    ++self.releases;
    --self.live;
    ::operator delete(pointer, std::align_val_t{alignment});
  }
  detail::AllocationBackend Backend() { return {Allocate, Deallocate, this}; }
};
struct Snapshot {
  std::size_t requests;
  std::size_t releases;
  std::size_t live;
  std::size_t active_bytes;
  std::size_t peak_bytes;
  std::size_t active_workers;
  std::size_t peak_workers;
  bool operator==(const Snapshot&) const = default;
};
Snapshot Observe(const Recorder& recorder,
                 const geo::ExecutionContext& context) {
  return {recorder.requests.load(),     recorder.releases.load(),
          recorder.live.load(),         context.ActiveTrackedPayload(),
          context.PeakTrackedPayload(), context.ActiveWorkers(),
          context.PeakWorkers()};
}
void QueriesAndOwners() {
  Recorder recorder;
  geo::ExecutionContext context({kThreads, 1, kBudget});
  const auto copied = context;
  const geo::ExecutionContext foreign({kThreads, 1, kBudget});
  auto moving = context;
  const auto moved = std::move(moving);
  const std::array bindings{
      ic::SuppliedGltfResource{ic::GltfResourceKind::kBuffer, 0, kResource}};
  const ic::GltfWorkPolicy work{kBytes, kCount, kBytes, kCount};
  {
    auto legacy = ic::ReadGltfAsset(std::as_bytes(std::span(kJson)), bindings,
                                    Limits(), work, context, {});
    Require(legacy.asset.has_value(), "real legacy retained asset");
    Require(!legacy.asset->NewRequestedBytes(), "legacy has no fresh counter");
    auto capped = detail::TestReadGltfAssetCapped(
        std::as_bytes(std::span(kJson)), bindings, {Limits(), kBudget}, work,
        context, {}, recorder.Backend(), detail::DefaultNativeWorkerBackend());
    Require(capped.asset.has_value(), "real capped retained asset");
    Require(capped.asset->NewRequestedBytes().value_or(0) > 0,
            "capped asset retains positive fresh accounting");
    auto second =
        ic::ReadGltfAssetCapped(std::as_bytes(std::span(kJson)), bindings,
                                {Limits(), kBudget}, work, context);
    Require(second.asset.has_value(), "distinct fresh asset on same ledger");
    const auto fresh_bytes = capped.asset->NewRequestedBytes();
    const auto before = Observe(recorder, context);
    recorder.armed = true;
    for (const auto* asset : {&*legacy.asset, &*capped.asset, &*second.asset}) {
      Require(asset->SharesExecutionBudget(context) &&
                  asset->SharesExecutionBudget(copied) &&
                  asset->SharesExecutionBudget(moved),
              "copied ledger identity survives distinct fresh owners");
      Require(!asset->SharesExecutionBudget(foreign) &&
                  !asset->SharesExecutionBudget(moving),
              "equal settings and moved-from context are not identities");
    }
    const std::array owners{*capped.asset, *capped.asset, *legacy.asset,
                            *second.asset};
    std::array<bool, kThreads> accepted{};
    std::barrier gate(static_cast<std::ptrdiff_t>(kThreads + 1));
    std::array<std::jthread, kThreads> threads;
    for (std::size_t i = 0; i < threads.size(); ++i)
      threads[i] = std::jthread([&, i] {
        gate.arrive_and_wait();
        bool valid = true;
        for (std::size_t query = 0; query < kQueries; ++query)
          valid = valid && owners[i].SharesExecutionBudget(copied) &&
                  !owners[i].SharesExecutionBudget(foreign) &&
                  !owners[i].SharesExecutionBudget(moving);
        accepted[i] = valid;
      });
    gate.arrive_and_wait();
    for (auto& thread : threads) thread.join();
    for (const auto accepted_query : accepted)
      Require(accepted_query, "real concurrent immutable ledger queries");
    Require(before == Observe(recorder, context) &&
                capped.asset->NewRequestedBytes() == fresh_bytes,
            "queries create no original requests, leases or counter changes");
    recorder.armed = false;
  }
  Require(recorder.live == 0 && recorder.requests == recorder.releases &&
              context.ActiveTrackedPayload() == 0,
          "all immutable retained owners release original blocks");
}
void AllocatorEqualityControl() {
  Recorder recorder;
  geo::ExecutionContext context({kThreads, 1, kBudget});
  {
    const detail::AllocationContext seed(context, {}, recorder.Backend());
    const auto first = seed.WithRequestLimit(kBytes);
    const auto copy = first;
    const auto second = seed.WithRequestLimit(kBytes);
    const detail::Allocator<std::byte> first_allocator(first);
    const detail::Allocator<std::uint32_t> rebound(first_allocator);
    const detail::Allocator<std::byte> second_allocator(second);
    const auto before = Observe(recorder, context);
    const auto first_bytes = first.OperationActiveBytes();
    const auto second_bytes = second.OperationActiveBytes();
    recorder.armed = true;
    Require(first.SharesExecutionBudget(second) && first.SharesBudget(copy) &&
                first_allocator == rebound &&
                !(first_allocator == second_allocator) &&
                !first.SharesBudget(second),
            "shared ledger does not make distinct fresh allocators equal");
    const detail::AllocationContext detached;
    Require(detached.SharesBudget(detail::AllocationContext{}) &&
                detached.SharesExecutionBudget(detail::AllocationContext{}) &&
                !detached.SharesBudget(first) &&
                !detached.SharesExecutionBudget(first),
            "detached allocator controls retain their existing identity rule");
    Require(before == Observe(recorder, context) &&
                first.OperationActiveBytes() == first_bytes &&
                second.OperationActiveBytes() == second_bytes,
            "allocator identity queries do not reserve or allocate");
    recorder.armed = false;
  }
  Require(recorder.live == 0 && recorder.requests == recorder.releases &&
              context.ActiveTrackedPayload() == 0,
          "distinct operation controls release completely");
}
}  // namespace
int main() {
  try {
    QueriesAndOwners();
    AllocatorEqualityControl();
    std::cout << "legacy/capped ledger queries and allocator controls passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
