// SPDX-License-Identifier: Apache-2.0
#include "gltf_allocation.h"

#include <meshvale/geometry/triangulation.h>

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "gltf_parser.h"

namespace {
using namespace meshvale::interchange::gltf_detail;
using meshvale::geometry::ExecutionContext;

void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class F>
void Reject(AdmissionCode code, F operation) {
  try {
    operation();
  } catch (const AdmissionFailure& failure) {
    Require(failure.Code() == code, "wrong admission classification");
    return;
  }
  throw std::runtime_error("missing admission rejection");
}
struct Counter {
  const ExecutionContext& context;
  std::atomic<std::size_t> requests{0};
  std::atomic<std::size_t> live{0};
  std::atomic<std::size_t> releases{0};
  std::atomic<bool> failure{false};
  std::atomic<bool> ordering_failure{false};
  std::size_t fail_request = 0;
  std::size_t cancel_request = 0;
  std::stop_source* cancel_after_allocate = nullptr;
};
void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
  auto& counter = *static_cast<Counter*>(state);
  const auto request = ++counter.requests;
  if (counter.context.ActiveTrackedPayload() < bytes)
    counter.ordering_failure = true;
  if (counter.failure || request == counter.fail_request)
    throw std::bad_alloc();
  auto* pointer = ::operator new(bytes, std::align_val_t(alignment));
  counter.live += bytes;
  if (counter.cancel_after_allocate &&
      (counter.cancel_request == 0 || request == counter.cancel_request))
    counter.cancel_after_allocate->request_stop();
  return pointer;
}
void Free(void* pointer, std::size_t bytes, std::size_t alignment,
          void* state) noexcept {
  auto& counter = *static_cast<Counter*>(state);
  if (counter.context.ActiveTrackedPayload() < bytes)
    counter.ordering_failure = true;
  ::operator delete(pointer, std::align_val_t(alignment));
  counter.live -= bytes;
  ++counter.releases;
}
AllocationBackend Backend(Counter& counter) {
  return {Allocate, Free, &counter};
}
void Boundaries() {
  const auto request = AllocationRequestBytes(12, alignof(std::uint32_t));
  Require(request > 12, "header/alignment excluded");
  ExecutionContext context({2, 1, request});
  Counter counter{context};
  AllocationContext allocation(context, {}, Backend(counter));
  Allocator<std::uint32_t> allocator(allocation);
  auto* data = allocator.allocate(3);
  Require(context.ActiveTrackedPayload() == request && counter.live == request,
          "exact requested cap mismatch");
  Reject(AdmissionCode::kBudgetExceeded, [&] { (void)allocator.allocate(1); });
  Require(counter.requests == 1, "budget rejection reached backend");
  allocator.deallocate(data, 3);
  Require(counter.live == 0 && context.ActiveTrackedPayload() == 0,
          "release did not unwind");
  Require(!counter.ordering_failure, "charge released before storage");
  Reject(AdmissionCode::kSizeLimit, [&] {
    (void)allocator.allocate(std::numeric_limits<std::size_t>::max());
  });
  Reject(AdmissionCode::kSizeLimit, [&] {
    (void)AllocationRequestBytes(std::numeric_limits<std::size_t>::max(), 1);
  });
  Reject(AdmissionCode::kInvalidObject,
         [&] { (void)allocation.Allocate(1, 3); });
  Require(counter.requests == 1, "size rejection reached backend");
  Allocator<char> detached;
  Reject(AdmissionCode::kInvalidObject, [&] { (void)detached.allocate(1); });
  auto* zero = Allocator<char>(allocation).allocate(0);
  Require(context.ActiveTrackedPayload() == AllocationRequestBytes(0, 1),
          "zero allocation must still charge its owned header");
  AllocationContext::Deallocate(zero);
}
void FaultAndStop() {
  ExecutionContext context({2, 1, 65536});
  Counter counter{context};
  std::stop_source stop;
  AllocationContext allocation(context, stop.get_token(), Backend(counter));
  counter.failure = true;
  try {
    (void)allocation.Allocate(32, 8);
    throw std::runtime_error("backend fault lost");
  } catch (const std::bad_alloc&) {
  }
  Require(context.ActiveTrackedPayload() == 0 && counter.live == 0,
          "backend exception reservation rollback");
  counter.failure = false;
  auto* pointer = allocation.Allocate(32, 8);
  stop.request_stop();
  const auto requests = counter.requests.load();
  Reject(AdmissionCode::kCanceled, [&] { (void)allocation.Allocate(32, 8); });
  Require(counter.requests == requests, "canceled request reached backend");
  AllocationContext::Deallocate(pointer);
  Require(context.ActiveTrackedPayload() == 0, "stopped cleanup blocked");
  std::stop_source during;
  counter.cancel_after_allocate = &during;
  AllocationContext canceled(context, during.get_token(), Backend(counter));
  Reject(AdmissionCode::kCanceled, [&] { (void)canceled.Allocate(32, 8); });
  Require(context.ActiveTrackedPayload() == 0 && counter.live == 0,
          "post-backend cancellation leaked");
}
struct alignas(256) Wide {
  std::uint64_t value = 7;
  std::byte padding[248]{};
};
void Ownership() {
  ExecutionContext ledger({2, 1, 65536});
  Counter counter{ledger};
  std::shared_ptr<Wide> owned;
  {
    auto caller = ledger;
    std::stop_source source;
    AllocationContext context(caller, source.get_token(), Backend(counter));
    Allocator<Wide> allocator(context);
    auto* pointer = allocator.allocate(2);
    Require(reinterpret_cast<std::uintptr_t>(pointer) % alignof(Wide) == 0,
            "over-aligned payload");
    allocator.deallocate(pointer, 2);
    owned = std::allocate_shared<Wide>(allocator);
    source.request_stop();
  }
  Require(owned->value == 7 && ledger.ActiveTrackedPayload() == counter.live &&
              counter.live > sizeof(Wide),
          "owned rebound/control block lifetime");
  auto copied = owned;
  const auto charged = counter.live.load();
  owned.reset();
  Require(counter.live == charged, "shared copy double/released charge");
  copied.reset();
  Require(counter.live == 0 && ledger.ActiveTrackedPayload() == 0,
          "final owner did not release");
  ExecutionContext other({2, 1, 65536});
  AllocationContext first(ledger, {}), second(other, {});
  Require(Allocator<int>(first) == Allocator<char>(first), "rebind identity");
  Require(!(Allocator<int>(first) == Allocator<int>(second)),
          "foreign identity");
  std::vector<int, Allocator<int>> a{Allocator<int>(first)};
  std::vector<int, Allocator<int>> b{Allocator<int>(second)};
  a.assign(8, 2);
  b.assign(9, 3);
  a.swap(b);
  Require(a[0] == 3 && a.get_allocator() == Allocator<int>(second),
          "unequal allocator swap propagation");
  b = std::move(a);
  Require(b[0] == 3 && b.get_allocator() == Allocator<int>(second),
          "unequal allocator move propagation");
}
void Overlap() {
  const auto old_charge = AllocationRequestBytes(8, 1);
  const auto new_charge = AllocationRequestBytes(16, 1);
  ExecutionContext context({2, 1, old_charge + new_charge - 1});
  Counter counter{context};
  std::vector<char, Allocator<char>> values{
      Allocator<char>(AllocationContext(context, {}, Backend(counter)))};
  values.reserve(8);
  values.assign(8, 'a');
  Reject(AdmissionCode::kBudgetExceeded, [&] { values.reserve(16); });
  Require(values.size() == 8 && values[0] == 'a' && counter.requests == 1 &&
              context.ActiveTrackedPayload() == old_charge,
          "overlap denial lost old allocation");
}
void Parsers() {
  ExecutionContext context({2, 1, 65536});
  Counter counter{context};
  AllocationContext allocation(context, {}, Backend(counter));
  const auto summary = InspectJsonSyntax(
      R"({"asset":{"version":"2.0"},"extras":[18446744073709551616,1e400]})",
      16, allocation);
  Require(summary.accepted && summary.events == 12,
          "configured lossless JSON syntax integration");
  Require(!InspectJsonSyntax("{/*comment*/}", 16, allocation).accepted &&
              !InspectJsonSyntax("[1,]", 16, allocation).accepted,
          "comments/trailing comma accepted");
  const auto core = InspectCoreProjection(
      R"({"asset":{"version":"2.0"},"nodes":[{}]})", allocation);
  Require(core.accepted && core.nodes == 1 && core.meshes == 0,
          "actual cgltf private callback integration");
  Require(!InspectCoreProjection("{bad", allocation).accepted,
          "invalid core syntax accepted");
  Require(counter.live == 0 && context.ActiveTrackedPayload() == 0 &&
              counter.requests > 0 && !counter.ordering_failure,
          "parser temporary cleanup/accounting");
  counter.failure = true;
  try {
    (void)InspectCoreProjection(R"({"asset":{"version":"2.0"}})", allocation);
    throw std::runtime_error("cgltf lost backend fault");
  } catch (const std::bad_alloc&) {
  }
  Require(context.ActiveTrackedPayload() == 0, "cgltf fault cleanup");
  std::stop_source stop;
  stop.request_stop();
  AllocationContext canceled(context, stop.get_token());
  Reject(AdmissionCode::kCanceled, [&] {
    (void)InspectCoreProjection(R"({"asset":{"version":"2.0"}})", canceled);
  });
}
void Concurrent() {
  const auto charge = AllocationRequestBytes(48, 8);
  ExecutionContext context({2, 1, charge});
  Counter counter{context};
  AllocationContext allocation(context, {}, Backend(counter));
  std::barrier phases(2);
  std::atomic<int> accepted{0}, rejected{0};
  const auto operation = [&] {
    void* pointer = nullptr;
    phases.arrive_and_wait();
    try {
      pointer = allocation.Allocate(48, 8);
      ++accepted;
    } catch (const AdmissionFailure& failure) {
      if (failure.Code() == AdmissionCode::kBudgetExceeded) ++rejected;
    }
    phases.arrive_and_wait();
    AllocationContext::Deallocate(pointer);
  };
  std::jthread a(operation), b(operation);
  a.join();
  b.join();
  Require(accepted == 1 && rejected == 1 && counter.requests == 1 &&
              counter.live == 0 && context.ActiveTrackedPayload() == 0,
          "shared concurrent allocator cap");
}
template <class Operation>
void ParserFailures(const char* name, Operation operation) {
  ExecutionContext baseline({2, 1, 131072});
  Counter observed{baseline};
  Require(operation(AllocationContext(baseline, {}, Backend(observed))), name);
  const auto requests = observed.requests.load();
  Require(requests > 1 && observed.live == 0 &&
              baseline.ActiveTrackedPayload() == 0,
          "parser fixture did not exercise partial construction");
  for (std::size_t target = 1; target <= requests; ++target) {
    ExecutionContext context({2, 1, 131072});
    Counter counter{context};
    counter.fail_request = target;
    try {
      (void)operation(AllocationContext(context, {}, Backend(counter)));
      throw std::runtime_error("partial parser backend fault lost");
    } catch (const std::bad_alloc&) {
    }
    Require(counter.live == 0 && context.ActiveTrackedPayload() == 0 &&
                !counter.ordering_failure,
            "partial parser backend fault leaked/released early");
    counter.fail_request = 0;
    counter.requests = 0;
    std::stop_source stop;
    counter.cancel_request = target;
    counter.cancel_after_allocate = &stop;
    Reject(AdmissionCode::kCanceled, [&] {
      (void)operation(
          AllocationContext(context, stop.get_token(), Backend(counter)));
    });
    Require(counter.live == 0 && context.ActiveTrackedPayload() == 0 &&
                !counter.ordering_failure,
            "partial parser cancellation leaked/released early");
  }
  ExecutionContext limited({2, 1, AllocationRequestBytes(0, 1) - 1});
  Counter rejected{limited};
  Reject(AdmissionCode::kBudgetExceeded, [&] {
    (void)operation(AllocationContext(limited, {}, Backend(rejected)));
  });
  Require(rejected.requests == 0 && limited.ActiveTrackedPayload() == 0,
          "parser cap rejection reached backend");
  std::cout << name << ": " << requests << " allocation boundaries\n";
}
void PartialParsers() {
  const auto json = [](const AllocationContext& allocation) {
    return InspectJsonSyntax(
               R"({"asset":{"version":"2.0"},"nodes":[{"name":"\u0061 long escaped name for parser-owned decoded string storage"},{}]})",
               16, allocation)
        .accepted;
  };
  ParserFailures("jsoncons", json);
  const auto core = [](const AllocationContext& allocation) {
    const auto result = InspectCoreProjection(
        R"({"asset":{"version":"2.0"},"nodes":[{"name":"a"},{}]})", allocation);
    return result.accepted && result.nodes == 2;
  };
  ParserFailures("cgltf", core);
  ExecutionContext context({2, 1, 131072});
  Counter counter{context};
  AllocationContext allocation(context, {}, Backend(counter));
  Require(!InspectJsonSyntax(R"({"nodes":[{"name":"unfinished)", 16, allocation)
                  .accepted &&
              !InspectCoreProjection(
                   R"({"asset":{"version":"2.0"},"nodes":[{},)", allocation)
                   .accepted &&
              counter.requests > 1 && counter.live == 0 &&
              context.ActiveTrackedPayload() == 0,
          "malformed partial parser cleanup");
  auto moved = context;
  auto owner = std::move(moved);
  try {
    AllocationContext invalid(moved, {});
    throw std::runtime_error("moved context accepted");
  } catch (const meshvale::geometry::EditorError& error) {
    Require(error.Code() == meshvale::geometry::EditorErrorCode::kInvalidObject,
            "moved context exception changed");
  }
  Require(owner.ActiveTrackedPayload() == 0, "moved owner charge changed");
}
void MixedGeometry() {
  using namespace meshvale::geometry;
  Mesh mesh;
  mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  mesh.corner_vertices = {0, 1, 2};
  mesh.face_offsets = {0, 3};
  ExecutionContext context({2, 1, 1048576});
  const auto baseline = Triangulate(mesh, {}, context);
  Require(baseline.status == TriangulationStatus::kAccepted &&
              context.ActiveTrackedPayload() == 0,
          "existing Geometry behavior changed");
  AllocationContext allocation(context, {});
  const auto wanted = context.TrackedPayloadBudget();
  const auto overhead = AllocationRequestBytes(0, 1);
  auto* retained = allocation.Allocate(wanted - overhead, 1);
  const auto blocked = Triangulate(mesh, {}, context);
  Require(blocked.status == TriangulationStatus::kBlocked &&
              context.ActiveTrackedPayload() == wanted,
          "Geometry ignored persistent allocator charge");
  AllocationContext::Deallocate(retained);
  Require(context.ActiveTrackedPayload() == 0 &&
              Triangulate(mesh, {}, context).status ==
                  TriangulationStatus::kAccepted,
          "mixed cap release changed Geometry");
  auto workers = TryReserveWorkers(context, 2);
  std::jthread a([] {}), b([] {});
  a.join();
  b.join();
  Require(workers.Count() == 2 && context.ActiveWorkers() == 2 &&
              TryReserveWorkers(context, 2).Count() == 0,
          "shared worker lease unavailable fallback");
  workers.Reset();
  Require(context.ActiveWorkers() == 0, "joined workers leaked slots");
}
void OperationCaps() {
  ExecutionContext context({2, 1, 1048576});
  AllocationContext root(context, {});
  {
    auto probe = root.WithRequestLimit(65536);
    const auto seed = probe.OperationActiveBytes();
    Require(seed != 0 && context.ActiveTrackedPayload() == seed,
            "operation accounting state was not charged");
    const auto charge = AllocationRequestBytes(256, 64);
    auto capped = root.WithRequestLimit(seed + charge);
    auto* block = capped.Allocate(256, 64);
    Require(reinterpret_cast<std::uintptr_t>(block) % 64 == 0 &&
                capped.OperationActiveBytes() == seed + charge,
            "operation cap omitted alignment/header");
    Reject(AdmissionCode::kSizeLimit, [&] { (void)capped.Allocate(1, 1); });
    Require(capped.OperationActiveBytes() == seed + charge,
            "rejected operation request changed charge");
    auto copy = capped;
    Require(copy.SharesBudget(capped) && !copy.SharesBudget(root),
            "allocator equality lost operation identity");
    AllocationContext::Deallocate(block);
    Require(capped.OperationActiveBytes() == seed,
            "operation free retained payload");
    Reject(AdmissionCode::kInvalidObject,
           [&] { (void)capped.WithRequestLimit(65536); });
  }
  Require(context.ActiveTrackedPayload() == 0,
          "operation accounting state leaked");
  // Containers and their operation accounting outlive the creating wrappers.
  std::vector<std::byte, Allocator<std::byte>> retained;
  {
    auto capped = root.WithRequestLimit(65536);
    retained = decltype(retained)(Allocator<std::byte>(capped));
    retained.resize(32);
  }
  Require(context.ActiveTrackedPayload() != 0,
          "retained capped container lost owner");
  retained = decltype(retained)();
  Require(context.ActiveTrackedPayload() == 0,
          "capped owner survived final free");
  Counter counter{context};
  AllocationContext injected(context, {}, {Allocate, Free, &counter});
  {
    auto capped = injected.WithRequestLimit(65536);
    const auto seed = capped.OperationActiveBytes();
    counter.failure = true;
    try {
      (void)capped.Allocate(64, 16);
      throw std::runtime_error("backend fault not observed");
    } catch (const std::bad_alloc&) {
      Require(capped.OperationActiveBytes() == seed &&
                  context.ActiveTrackedPayload() == seed,
              "backend fault did not roll back both counters");
    }
    counter.failure = false;
  }
  Require(context.ActiveTrackedPayload() == 0 && counter.live == 0,
          "injected operation budget leaked");
  {
    auto capped = root.WithRequestLimit(65536);
    std::barrier start(3);
    std::atomic<bool> bad{false};
    auto work = [&] {
      start.arrive_and_wait();
      for (int i = 0; i < 100; ++i) {
        auto* block = capped.Allocate(64, 16);
        if (capped.OperationActiveBytes() > 65536) bad = true;
        AllocationContext::Deallocate(block);
      }
    };
    std::jthread first(work), second(work);
    start.arrive_and_wait();
    first.join();
    second.join();
    Require(!bad, "concurrent operation cap overflowed");
  }
  Require(context.ActiveTrackedPayload() == 0,
          "concurrent operation budget leaked");
}
void AdditionalOperationCaps() {
  constexpr std::size_t kCompleteBytes = 1048576;
  constexpr std::size_t kPayloadBytes = 256;
  constexpr std::size_t kPayloadAlignment = 64;
  ExecutionContext context({2, 1, kCompleteBytes});
  Counter counter{context};
  AllocationContext root(context, {}, Backend(counter));
  Reject(AdmissionCode::kInvalidObject,
         [&] { (void)root.WithAdditionalRequestLimit(kCompleteBytes); });
  const auto charge = AllocationRequestBytes(kPayloadBytes, kPayloadAlignment);
  std::size_t owner_seed = 0;
  {
    const auto parent = root.WithRequestLimit(kCompleteBytes);
    owner_seed = parent.OperationActiveBytes();
    const auto requests = counter.requests.load();
    Reject(AdmissionCode::kSizeLimit,
           [&] { (void)parent.WithAdditionalRequestLimit(owner_seed - 1); });
    Require(counter.requests == requests, "child seed refusal reached backend");
    const auto child = parent.WithAdditionalRequestLimit(owner_seed);
    Require(child.OperationActiveBytes() == owner_seed &&
                parent.OperationActiveBytes() == 2 * owner_seed,
            "child control seed not inside both actual caps");
    Reject(AdmissionCode::kSizeLimit, [&] { (void)child.Allocate(1, 1); });
    Require(counter.requests == requests + 1,
            "seed-only child allowed payload backend");
  }
  Require(context.ActiveTrackedPayload() == 0 && counter.live == 0,
          "child seed scopes leaked");
  for (const bool child_tight : {false, true}) {
    const auto parent = root.WithRequestLimit(
        child_tight ? kCompleteBytes : 2 * owner_seed + charge);
    const auto child = parent.WithAdditionalRequestLimit(
        child_tight ? owner_seed + charge : kCompleteBytes);
    auto* payload = child.Allocate(kPayloadBytes, kPayloadAlignment);
    const auto requests = counter.requests.load();
    const auto parent_active = parent.OperationActiveBytes();
    const auto child_active = child.OperationActiveBytes();
    Reject(AdmissionCode::kSizeLimit, [&] { (void)child.Allocate(1, 1); });
    Require(counter.requests == requests &&
                parent.OperationActiveBytes() == parent_active &&
                child.OperationActiveBytes() == child_active,
            "child/parent refusal changed charges or reached backend");
    counter.failure = true;
    AllocationContext::Deallocate(payload);
    try {
      (void)child.Allocate(kPayloadBytes, kPayloadAlignment);
      throw std::runtime_error("child backend failure lost");
    } catch (const std::bad_alloc&) {
    }
    counter.failure = false;
    Require(parent.OperationActiveBytes() == 2 * owner_seed &&
                child.OperationActiveBytes() == owner_seed &&
                context.ActiveTrackedPayload() == 2 * owner_seed,
            "child backend exception failed coupled rollback");
    Reject(AdmissionCode::kSizeLimit, [&] {
      (void)child.Allocate(std::numeric_limits<std::size_t>::max(), 1);
    });
  }
  Require(context.ActiveTrackedPayload() == 0 && counter.live == 0,
          "coupled owner scopes leaked");
  std::shared_ptr<Wide> retained;
  {
    const auto parent = root.WithRequestLimit(kCompleteBytes);
    const auto child = parent.WithAdditionalRequestLimit(kCompleteBytes);
    const auto descriptors = child.WithAdditionalRequestLimit(kCompleteBytes);
    retained = std::allocate_shared<Wide>(Allocator<Wide>(descriptors));
  }
  Require(
      retained->value == 7 && context.ActiveTrackedPayload() == counter.live,
      "returned child allocation lost real parent lifetime");
  auto copy = retained;
  retained.reset();
  Require(context.ActiveTrackedPayload() != 0,
          "copied child owner released parent early");
  copy.reset();
  Require(context.ActiveTrackedPayload() == 0 && counter.live == 0,
          "final child copy did not release all real owners");
  {
    std::stop_source stop;
    const AllocationContext stopped_root(context, stop.get_token(),
                                         Backend(counter));
    const auto parent = stopped_root.WithRequestLimit(kCompleteBytes);
    const auto child = parent.WithAdditionalRequestLimit(kCompleteBytes);
    const auto active = context.ActiveTrackedPayload();
    const auto requests = counter.requests.load();
    stop.request_stop();
    Reject(AdmissionCode::kCanceled, [&] { (void)child.Allocate(32, 8); });
    Reject(AdmissionCode::kCanceled,
           [&] { (void)child.WithAdditionalRequestLimit(kCompleteBytes); });
    Require(context.ActiveTrackedPayload() == active &&
                counter.requests == requests &&
                parent.OperationActiveBytes() == active,
            "child canceled admission reached backend or leaked counters");
  }
  Require(context.ActiveTrackedPayload() == 0 && counter.live == 0 &&
              !counter.ordering_failure,
          "child cleanup lost original ledger/backend ordering");
  std::cout
      << "additional-budget actual-owner-seed=" << owner_seed
      << " child/parent-beforealloc/rollback/overflow/cancel/lifetime PASS\n";
}
void SiblingOperationCaps() {
  constexpr std::size_t kWorkers = 2;
  constexpr std::size_t kBudgetBytes = 1048576;
  constexpr std::size_t kPayloadBytes = 256;
  constexpr std::size_t kPayloadAlignment = 64;
  ExecutionContext context({kWorkers, 1, kBudgetBytes});
  Counter counter{context};
  AllocationContext root(context, {}, Backend(counter));
  std::size_t seed = 0;
  {
    const auto probe = root.WithRequestLimit(kBudgetBytes);
    seed = probe.OperationActiveBytes();
  }
  const auto charge = AllocationRequestBytes(kPayloadBytes, kPayloadAlignment);
  {
    const auto parent = root.WithRequestLimit((1 + kWorkers) * seed + charge);
    const std::array<AllocationContext, kWorkers> children{
        parent.WithAdditionalRequestLimit(seed + charge),
        parent.WithAdditionalRequestLimit(seed + charge)};
    auto lease = meshvale::geometry::TryReserveWorkers(context, kWorkers);
    Require(lease.Count() == kWorkers, "real sibling worker lease");
    std::barrier phases(static_cast<std::ptrdiff_t>(1 + kWorkers));
    std::array<void*, kWorkers> payloads{};
    std::array<bool, kWorkers> first_admitted{};
    std::array<std::exception_ptr, kWorkers> errors{};
    std::array<std::jthread, kWorkers> workers;
    const auto before = counter.requests.load();
    for (std::size_t worker = 0; worker < kWorkers; ++worker) {
      workers[worker] = std::jthread([&, worker] {
        phases.arrive_and_wait();
        try {
          payloads[worker] =
              children[worker].Allocate(kPayloadBytes, kPayloadAlignment);
          first_admitted[worker] = true;
        } catch (const AdmissionFailure& failure) {
          if (failure.Code() != AdmissionCode::kSizeLimit)
            errors[worker] = std::current_exception();
        } catch (...) {
          errors[worker] = std::current_exception();
        }
        phases.arrive_and_wait();
        phases.arrive_and_wait();
        AllocationContext::Deallocate(payloads[worker]);
        payloads[worker] = nullptr;
        phases.arrive_and_wait();
        if (!first_admitted[worker]) {
          try {
            payloads[worker] =
                children[worker].Allocate(kPayloadBytes, kPayloadAlignment);
          } catch (...) {
            errors[worker] = std::current_exception();
          }
        }
        phases.arrive_and_wait();
        phases.arrive_and_wait();
        AllocationContext::Deallocate(payloads[worker]);
      });
    }
    phases.arrive_and_wait();
    phases.arrive_and_wait();
    const auto admitted = static_cast<unsigned>(first_admitted[0]) +
                          static_cast<unsigned>(first_admitted[1]);
    bool correct =
        admitted == 1 && counter.requests == before + 1 &&
        parent.OperationActiveBytes() == (1 + kWorkers) * seed + charge &&
        context.ActiveTrackedPayload() == (1 + kWorkers) * seed + charge;
    for (std::size_t worker = 0; worker < kWorkers; ++worker)
      correct = correct && children[worker].OperationActiveBytes() ==
                               seed + (first_admitted[worker] ? charge : 0);
    phases.arrive_and_wait();
    phases.arrive_and_wait();
    // The next barrier lets the losing sibling retry. Inspect only after all
    // threads reach it; no success assertion may strand a worker at a barrier.
    phases.arrive_and_wait();
    correct = correct && counter.requests == before + 2 &&
              parent.OperationActiveBytes() == (1 + kWorkers) * seed + charge;
    for (std::size_t worker = 0; worker < kWorkers; ++worker)
      correct = correct && children[worker].OperationActiveBytes() ==
                               seed + (first_admitted[worker] ? 0 : charge);
    phases.arrive_and_wait();
    for (auto& worker : workers) worker.join();
    const bool held_through_join = context.ActiveWorkers() == kWorkers;
    lease.Reset();
    for (const auto& error : errors)
      if (error) std::rethrow_exception(error);
    Require(correct && held_through_join &&
                parent.OperationActiveBytes() == (1 + kWorkers) * seed &&
                children[0].OperationActiveBytes() == seed &&
                children[1].OperationActiveBytes() == seed,
            "sibling child rollback/shared-parent retry or worker lifetime");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0 &&
              counter.live == 0 && !counter.ordering_failure,
          "sibling child/parent/root final counters did not release");
  std::cout << "sibling-child coordinated shared-parent refusal/retry PASS; "
               "payload=0 workers=0\n";
}
}  // namespace
int main() {
  try {
    Boundaries();
    FaultAndStop();
    Ownership();
    Overlap();
    Parsers();
    PartialParsers();
    Concurrent();
    MixedGeometry();
    OperationCaps();
    AdditionalOperationCaps();
    SiblingOperationCaps();
    std::cout << "allocation/parser foundation passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
