// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/glb.h"

#include <meshvale/geometry/execution.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

#include "glb_detail.h"

namespace {
using namespace meshvale::interchange;
using meshvale::geometry::ExecutionContext;
constexpr std::size_t kTestBudget = 16 * 1024 * 1024;
constexpr std::uint32_t kOpaqueType = 0x12345678;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}
bool Same(std::span<const std::byte> first, std::span<const std::byte> second) {
  return std::equal(first.begin(), first.end(), second.begin(), second.end());
}
GlbLimits Limits() { return {kTestBudget, 65536, kTestBudget}; }
GlbWorkPolicy Serial() { return {kTestBudget, 64}; }
GlbWorkPolicy Parallel() { return {1, 31}; }
// Independent literal wire expectations, not derived from production constants.
std::vector<std::byte> Fixture() {
  constexpr unsigned char kWire[] = {
      0x67, 0x6c, 0x54, 0x46, 2,    0,    0,    0,    48,   0,   0,   0,
      4,    0,    0,    0,    0x4a, 0x53, 0x4f, 0x4e, '{',  '}', ' ', ' ',
      4,    0,    0,    0,    0x42, 0x49, 0x4e, 0,    0xff, 0,   0,   0,
      4,    0,    0,    0,    0x78, 0x56, 0x34, 0x12, 1,    2,   3,   4};
  std::vector<std::byte> bytes;
  for (auto value : kWire) bytes.push_back(std::byte(value));
  return bytes;
}
void RoundTrip() {
  ExecutionContext context({4, 1, kTestBudget});
  auto source = Fixture();
  auto read = ReadGlbContainer(source, Limits(), Serial(), context);
  Require(
      read.status == GlbStatus::kSuccess && read.container.ChunkCount() == 3,
      "literal container rejected");
  Require(Same(read.container.Bytes(), source), "capture changed wire bytes");
  Require(read.container.Chunk(2).type == kOpaqueType,
          "opaque chunk identity lost");
  const std::array chunks{read.container.Chunk(0), read.container.Chunk(1),
                          read.container.Chunk(2)};
  auto written = WriteGlbContainer(chunks, Limits(), Parallel(), context);
  Require(written.status == GlbStatus::kSuccess && written.workers_used >= 2,
          "real parallel assembly missing");
  Require(Same(written.container.Bytes(), source), "round trip changed bytes");
  const auto json = Bytes("{}");
  const std::array<std::byte, 1> bin{std::byte{0xff}};
  const std::array<std::byte, 4> opaque{std::byte{1}, std::byte{2},
                                        std::byte{3}, std::byte{4}};
  const std::array unpadded{GlbChunk{0x4e4f534a, json},
                            GlbChunk{0x004e4942, bin},
                            GlbChunk{kOpaqueType, opaque}};
  auto assembled = WriteGlbContainer(unpadded, Limits(), Serial(), context);
  Require(Same(assembled.container.Bytes(), source),
          "JSON/BIN canonical padding differs from literal wire");
  auto moved = std::move(assembled.container);
  Require(!assembled.container && assembled.container.Bytes().empty(),
          "move did not detach owner");
  source.assign(source.size(), std::byte{0});
  Require(Same(moved.Bytes(), Fixture()), "result borrowed caller source");
  try {
    (void)moved.Chunk(3);
    throw std::runtime_error("invalid index accepted");
  } catch (const std::out_of_range&) {
  }
  GlbContainer survivor;
  {
    ExecutionContext temporary({2, 1, kTestBudget});
    auto captured = ReadGlbContainer(Fixture(), Limits(), Serial(), temporary);
    survivor = std::move(captured.container);
  }
  Require(Same(survivor.Bytes(), Fixture()), "caller context lifetime leaked");
}
void StructureAndOptions() {
  ExecutionContext context({4, 1, kTestBudget});
  const auto fixture = Fixture();
  for (std::size_t size = 0; size < fixture.size(); ++size) {
    auto result = ReadGlbContainer(std::span(fixture).first(size), Limits(),
                                   Parallel(), context);
    Require(result.status == GlbStatus::kMalformed && !result.container,
            "truncated container accepted");
    Require(context.ActiveTrackedPayload() == 0, "truncation allocated");
  }
  for (const auto offset : {0, 8, 12, 16, 36}) {
    auto corrupt = fixture;
    corrupt[static_cast<std::size_t>(offset)] = std::byte{0xff};
    Require(ReadGlbContainer(corrupt, Limits(), Serial(), context).status ==
                GlbStatus::kMalformed,
            "bad header/order/extent accepted");
  }
  auto future = fixture;
  future[4] = std::byte{3};
  Require(ReadGlbContainer(future, Limits(), Serial(), context).status ==
              GlbStatus::kUnsupportedVersion,
          "future version is not unsupported");
  for (std::size_t field = 0; field < 5; ++field) {
    auto limits = Limits();
    auto work = Parallel();
    switch (field) {
      case 0:
        limits.max_container_bytes = 0;
        break;
      case 1:
        limits.max_chunks = 0;
        break;
      case 2:
        limits.max_requested_bytes = 0;
        break;
      case 3:
        work.parallel_min_bytes = 0;
        break;
      case 4:
        work.partition_bytes = 0;
        break;
    }
    Require(ReadGlbContainer(fixture, limits, work, context).status ==
                GlbStatus::kInvalidOptions,
            "zero options functional default");
  }
  auto excessive = Limits();
  excessive.max_requested_bytes = std::numeric_limits<std::size_t>::max();
  Require(ReadGlbContainer(fixture, excessive, Parallel(), context).status ==
              GlbStatus::kInvalidOptions,
          "non-host limit accepted");
  auto limits = Limits();
  limits.max_container_bytes = fixture.size() - 1;
  Require(ReadGlbContainer(fixture, limits, Serial(), context).status ==
              GlbStatus::kLimitExceeded,
          "container ceiling lost");
  limits = Limits();
  limits.max_chunks = 2;
  Require(ReadGlbContainer(fixture, limits, Serial(), context).status ==
              GlbStatus::kLimitExceeded,
          "chunk ceiling lost");
  const std::array opaque_bad{GlbChunk{0x4e4f534a, Bytes("{}")},
                              GlbChunk{kOpaqueType, Bytes("x")}};
  Require(WriteGlbContainer(opaque_bad, Limits(), Serial(), context).status ==
              GlbStatus::kMalformed,
          "opaque alignment altered implicitly");
  const std::array bin_after_unknown{GlbChunk{0x4e4f534a, Bytes("{}")},
                                     GlbChunk{kOpaqueType, Bytes("abcd")},
                                     GlbChunk{0x004e4942, Bytes("abcd")}};
  Require(WriteGlbContainer(bin_after_unknown, Limits(), Serial(), context)
                  .status == GlbStatus::kMalformed,
          "BIN after unknown accepted");
  const std::array no_bin{GlbChunk{0x4e4f534a, Bytes("{}")},
                          GlbChunk{kOpaqueType, Bytes("abcd")}};
  Require(WriteGlbContainer(no_bin, Limits(), Serial(), context).status ==
              GlbStatus::kSuccess,
          "unknown chunk without BIN rejected");
  Require(context.ActiveTrackedPayload() == 0, "failure leaked storage");
}
struct Counter {
  const ExecutionContext& context;
  std::size_t allocations = 0;
  std::size_t frees = 0;
  std::size_t live = 0;
  std::size_t fail_request = 0;
  std::size_t launches = 0;
  bool launch_fault = false;
  bool ordering_failure = false;
  std::stop_source* stop_on_allocation = nullptr;
  std::stop_source* stop_on_launch = nullptr;
};
void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
  auto& counter = *static_cast<Counter*>(state);
  if (counter.context.ActiveTrackedPayload() < bytes)
    counter.ordering_failure = true;
  if (++counter.allocations == counter.fail_request) throw std::bad_alloc();
  auto* pointer = ::operator new(bytes, std::align_val_t(alignment));
  counter.live += bytes;
  if (counter.stop_on_allocation) counter.stop_on_allocation->request_stop();
  return pointer;
}
void Free(void* pointer, std::size_t bytes, std::size_t alignment,
          void* state) noexcept {
  auto& counter = *static_cast<Counter*>(state);
  if (counter.context.ActiveTrackedPayload() < counter.live)
    counter.ordering_failure = true;
  ::operator delete(pointer, std::align_val_t(alignment));
  counter.live -= bytes;
  ++counter.frees;
}
void BeforeLaunch(std::size_t index, void* state) {
  auto& counter = *static_cast<Counter*>(state);
  if (index == 1 && counter.launch_fault)
    throw std::runtime_error("injected launch failure");
  ++counter.launches;
  if (index == 1 && counter.stop_on_launch)
    counter.stop_on_launch->request_stop();
}
glb_detail::Hooks Hooks(Counter& counter) {
  return {{Allocate, Free, &counter}, BeforeLaunch, &counter};
}
void BudgetsFaultsAndCancellation() {
  const auto fixture = Fixture();
  ExecutionContext context({4, 1, kTestBudget});
  Counter counter{context};
  std::size_t requested = 0;
  {
    auto result = glb_detail::Read(fixture, Limits(), Serial(), context, {},
                                   Hooks(counter));
    Require(result.status == GlbStatus::kSuccess, "budget fixture rejected");
    requested = context.ActiveTrackedPayload();
    Require(counter.live == requested && requested > fixture.size(),
            "state/descriptors/header not charged");
    auto limits = Limits();
    limits.max_requested_bytes = requested;
    auto exact = ReadGlbContainer(fixture, limits, Serial(), context);
    Require(exact.status == GlbStatus::kSuccess, "exact request cap failed");
    limits.max_requested_bytes = requested - 1;
    Require(ReadGlbContainer(fixture, limits, Serial(), context).status ==
                GlbStatus::kLimitExceeded,
            "minus-one operation cap accepted");
  }
  Require(context.ActiveTrackedPayload() == 0 && counter.live == 0,
          "owned result release leaked");
  ExecutionContext too_small({4, 1, requested - 1});
  Require(ReadGlbContainer(fixture, Limits(), Serial(), too_small).status ==
              GlbStatus::kBudgetExceeded,
          "shared cap translated incorrectly");
  {
    auto occupied = meshvale::geometry::TryReserveWorkers(context, 4);
    auto fallback = ReadGlbContainer(fixture, Limits(), Parallel(), context);
    Require(
        fallback.status == GlbStatus::kSuccess && fallback.workers_used == 0,
        "occupied shared workers did not fall back");
    Require(Same(fallback.container.Bytes(), fixture), "fallback bytes differ");
  }
  for (std::size_t fail = 1; fail <= 2; ++fail) {
    Counter fault{context};
    fault.fail_request = fail;
    try {
      (void)glb_detail::Read(fixture, Limits(), Parallel(), context, {},
                             Hooks(fault));
      throw std::runtime_error("allocation failure swallowed");
    } catch (const std::bad_alloc&) {
    }
    Require(fault.live == 0 && context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == 0 && !fault.ordering_failure,
            "fault did not release after joins/storage free");
  }
  Counter launch{context};
  launch.launch_fault = true;
  try {
    (void)glb_detail::Read(fixture, Limits(), Parallel(), context, {},
                           Hooks(launch));
    throw std::logic_error("launch fault swallowed");
  } catch (const std::runtime_error&) {
  }
  Require(launch.launches == 1 && launch.live == 0 &&
              context.ActiveWorkers() == 0 &&
              context.ActiveTrackedPayload() == 0,
          "partial launch escaped worker lifetime");
  for (const auto timing : {0, 1, 2}) {
    std::stop_source stop;
    Counter cancellation{context};
    if (timing == 0) stop.request_stop();
    if (timing == 1) cancellation.stop_on_allocation = &stop;
    if (timing == 2) cancellation.stop_on_launch = &stop;
    auto canceled = glb_detail::Read(fixture, Limits(), Parallel(), context,
                                     stop.get_token(), Hooks(cancellation));
    Require(canceled.status == GlbStatus::kCanceled && !canceled.container &&
                cancellation.live == 0 && context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == 0,
            "cancellation cleanup/translation failed");
    auto malformed = fixture;
    malformed[0] = std::byte{0};
    Require(ReadGlbContainer(malformed, Limits(), Parallel(), context,
                             stop.get_token())
                    .status == GlbStatus::kMalformed,
            "cancellation concealed established malformed input");
  }
  Require(!counter.ordering_failure, "storage freed after its lease");
}
void LargePartitions() {
  ExecutionContext context({4, 1, kTestBudget});
  std::vector<std::byte> input(100003);
  for (std::size_t index = 0; index < input.size(); ++index)
    input[index] = std::byte(index % 251);
  const std::array chunks{GlbChunk{0x4e4f534a, Bytes("{}")},
                          GlbChunk{0x004e4942, input}};
  const GlbWorkPolicy work{1, 1021};
  auto parallel = WriteGlbContainer(chunks, Limits(), work, context);
  auto serial = WriteGlbContainer(chunks, Limits(), Serial(), context);
  Require(parallel.status == GlbStatus::kSuccess &&
              parallel.workers_used == 4 &&
              Same(parallel.container.Bytes(), serial.container.Bytes()),
          "large uneven parallel output differs");
  Require(Same(parallel.container.Chunk(1).bytes.first(input.size()), input),
          "large raw payload mismatch");
  auto read =
      ReadGlbContainer(parallel.container.Bytes(), Limits(), work, context);
  Require(read.status == GlbStatus::kSuccess && read.workers_used == 4 &&
              Same(read.container.Bytes(), parallel.container.Bytes()),
          "large parallel capture mismatch");
  Require(context.ActiveWorkers() == 0 && context.PeakWorkers() == 4,
          "workers were not released/accounted");
}
}  // namespace
int main() {
  try {
    RoundTrip();
    StructureAndOptions();
    BudgetsFaultsAndCancellation();
    LargePartitions();
    std::cout << "GLB wire/owner/limits/workers/faults passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
