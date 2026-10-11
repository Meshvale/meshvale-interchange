// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_write.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gltf_write_detail.h"

namespace {
namespace mv = meshvale::interchange;
namespace detail = mv::gltf_detail;
namespace writer = mv::gltf_write_detail;
void Require(bool value,
             std::source_location source = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("retained writer assertion failed at line " +
                             std::to_string(source.line()));
}
constexpr std::string_view kJson =
    " \n"
    R"({"asset":{"version":"2.0"},"extras":{"integer":9007199254740993,"negativeZero":-0.0,"huge":1e400,"escaped":"\u0394\ud83d\ude00","decimal":1.00e+01},"extensionsUsed":["VENDOR_keep"],"extensions":{"VENDOR_keep":{"reference":9007199254740995}},"buffers":[{"uri":"same.bin","byteLength":16},{"uri":"data:application/gltf-buffer;base64,AQIDBA==","byteLength":4}],"bufferViews":[{"buffer":0,"byteOffset":8,"byteLength":8},{"buffer":0,"byteLength":8}],"images":[{"uri":"same.bin","mimeType":"image/jpeg"},{"uri":"data:image/png;base64,iVBORw0KGgo="},{"bufferView":1,"mimeType":"image/png"}],"samplers":[{}],"textures":[{"source":0,"sampler":0},{"source":1}],"materials":[{"name":"retained","normalTexture":{"index":0}}],"accessors":[{"componentType":5126,"type":"VEC3","count":3,"min":[0,0,0],"max":[0,0,0]},{"componentType":5126,"type":"SCALAR","count":2,"bufferView":0,"min":[0],"max":[1]},{"componentType":5126,"type":"VEC3","count":2},{"componentType":5126,"type":"VEC3","count":3}],"meshes":[{"weights":[0],"primitives":[{"attributes":{"POSITION":0},"targets":[{"POSITION":3}],"material":0}]}],"nodes":[{"mesh":0,"weights":[0]},{"mesh":0,"weights":[0],"scale":[0,2,3]},{"name":"unused","mesh":0},{"children":[0,1]}],"skins":[{"joints":[0,1],"skeleton":3}],"animations":[{"samplers":[{"input":1,"output":2}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}],"scenes":[{"nodes":[3]},{"nodes":[3]}],"scene":0})"
    "\r\n";
constexpr std::array<std::byte, 20> kBuffer{
    std::byte{137}, std::byte{80}, std::byte{78}, std::byte{71}, std::byte{13},
    std::byte{10},  std::byte{26}, std::byte{10}, std::byte{0},  std::byte{0},
    std::byte{0},   std::byte{0},  std::byte{0},  std::byte{0},  std::byte{128},
    std::byte{63},  std::byte{42}, std::byte{43}, std::byte{44}, std::byte{45}};
constexpr std::array<std::byte, 4> kImage{std::byte{255}, std::byte{216},
                                          std::byte{255}, std::byte{217}};
mv::GltfReadLimits ReadLimits() {
  return {100000, 100000, 100000, 10000, 100,  100000, 100,
          100,    100,    1000,   1000,  1000, 100000, 100000};
}
mv::GltfWriteLimits WriteLimits() {
  return {100000, 100, 100000, 1000000, 1000000};
}
mv::GltfAsset Capture(const meshvale::geometry::ExecutionContext& context,
                      std::stop_token stop = {}) {
  const std::array<mv::SuppliedGltfResource, 2> bindings{
      mv::SuppliedGltfResource{mv::GltfResourceKind::kBuffer, 0, kBuffer},
      mv::SuppliedGltfResource{mv::GltfResourceKind::kImage, 0, kImage}};
  auto read = mv::ReadGltfAsset(
      {reinterpret_cast<const std::byte*>(kJson.data()), kJson.size()},
      bindings, ReadLimits(), {100000, 4096, 100000, 4096}, context, stop);
  if (!read.asset)
    std::cerr << "fixture read status="
              << static_cast<int>(read.diagnostic.status)
              << " code=" << static_cast<int>(read.diagnostic.code)
              << " rule=" << read.diagnostic.rule_id << '\n';
  Require(read.asset.has_value());
  return *read.asset;
}
void Inspect(const mv::GltfWriteBundle& output) {
  const auto json = output.Json();
  Require(std::string_view(reinterpret_cast<const char*>(json.data()),
                           json.size()) == kJson);
  const auto resources = output.Resources();
  Require(resources.size() == 5);
  Require(resources[0].kind == mv::GltfResourceKind::kBuffer &&
          resources[0].table_index == 0 && resources[0].logical_bytes == 16 &&
          resources[0].authored_uri == "same.bin");
  Require(std::ranges::equal(resources[0].bytes, kBuffer));
  Require(resources[1].origin == mv::GltfResourceOrigin::kDecodedDataUri &&
          resources[1].bytes.size() == 4 &&
          resources[1].bytes[3] == std::byte{4});
  Require(resources[2].kind == mv::GltfResourceKind::kImage &&
          resources[2].table_index == 0 &&
          std::ranges::equal(resources[2].bytes, kImage));
  Require(resources[4].origin == mv::GltfResourceOrigin::kBufferView &&
          resources[4].bytes.data() == resources[0].bytes.data() &&
          resources[4].bytes.size() == 8 && resources[4].buffer_index == 0 &&
          resources[4].buffer_view_index == 1);
  Require(output.NewRequestedBytes() > json.size());
}
void PreservationAndLifetime() {
  std::optional<mv::GltfWriteBundle> retained;
  {
    meshvale::geometry::ExecutionContext context({4, 1, 10000000});
    std::stop_source capture_stop;
    auto asset = Capture(context, capture_stop.get_token());
    capture_stop.request_stop();
    const auto source_charge = context.ActiveTrackedPayload();
    auto result =
        mv::WriteRetainedGltfAsset(asset, WriteLimits(), {1, 128}, {});
    Require(result.bundle.has_value());
    Require(result.bundle->Json().data() != asset.OriginalJson().data());
    Require(result.bundle->Resources().data() == asset.Resources().data());
    Require(context.ActiveTrackedPayload() ==
            source_charge + result.bundle->NewRequestedBytes());
    Inspect(*result.bundle);
    auto occupied = meshvale::geometry::TryReserveWorkers(context, 4);
    writer::ExecutionStats stats;
    auto serial = writer::WriteWithHooks(asset, WriteLimits(), {1, 128}, {},
                                         detail::DefaultAllocationBackend(),
                                         &stats, nullptr);
    Require(serial.bundle && stats.launched == 0 && stats.joined == 0);
    Require(std::ranges::equal(serial.bundle->Json(), result.bundle->Json()));
    retained = *result.bundle;
  }
  Inspect(*retained);
  meshvale::geometry::ExecutionContext reload_context({4, 1, 10000000});
  std::vector<mv::SuppliedGltfResource> bindings;
  for (const auto& resource : retained->Resources())
    if (resource.origin == mv::GltfResourceOrigin::kCallerBinding)
      bindings.push_back({resource.kind, resource.table_index, resource.bytes});
  auto reloaded = mv::ReadGltfAsset(retained->Json(), bindings, ReadLimits(),
                                    {1, 128, 1, 1}, reload_context, {});
  Require(reloaded.asset && reloaded.asset->PrimitiveCount() == 1 &&
          reloaded.asset->InstanceCount() == 4 &&
          reloaded.asset->Nodes().size() == 4 &&
          reloaded.asset->Scenes().size() == 2 &&
          reloaded.asset->RawNumbers().size() > 30);
  Require(std::ranges::equal(reloaded.asset->OriginalJson(), retained->Json()));
  std::atomic<bool> okay{true};
  const auto copied = *retained;
  std::jthread first([copied, &okay] {
    try {
      Inspect(copied);
    } catch (...) {
      okay = false;
    }
  });
  std::jthread second([copied, &okay] {
    try {
      Inspect(copied);
    } catch (...) {
      okay = false;
    }
  });
  first.join();
  second.join();
  Require(okay.load());
}
void LimitsAndBackendFailures() {
  struct BackendState {
    detail::AllocationBackend normal = detail::DefaultAllocationBackend();
    std::size_t remaining = 100;
    std::size_t active = 0;
    std::size_t peak = 0;
  };
  BackendState backend_state;
  const detail::AllocationBackend backend{
      [](std::size_t bytes, std::size_t alignment, void* opaque) -> void* {
        auto& state = *static_cast<BackendState*>(opaque);
        if (!state.remaining) throw std::bad_alloc();
        --state.remaining;
        auto* block =
            state.normal.allocate(bytes, alignment, state.normal.state);
        state.active += bytes;
        state.peak = std::max(state.peak, state.active);
        return block;
      },
      [](void* block, std::size_t bytes, std::size_t alignment,
         void* opaque) noexcept {
        auto& state = *static_cast<BackendState*>(opaque);
        state.active -= bytes;
        state.normal.deallocate(block, bytes, alignment, state.normal.state);
      },
      &backend_state};
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  auto asset = Capture(context);
  const auto source_charge = context.ActiveTrackedPayload();
  writer::ExecutionStats stats;
  const mv::GltfWriteWorkPolicy serial_policy{100000, 128};
  {
    auto first = writer::WriteWithHooks(asset, WriteLimits(), serial_policy, {},
                                        backend, &stats, nullptr);
    Require(first.bundle && stats.launched == 0);
    Require(first.bundle->NewRequestedBytes() == backend_state.active &&
            stats.maximum_observed_requests == backend_state.peak);
  }
  Require(backend_state.active == 0 &&
          context.ActiveTrackedPayload() == source_charge);
  auto limits = WriteLimits();
  limits.max_output_requested_bytes = backend_state.peak;
  {
    auto exact = mv::WriteRetainedGltfAsset(asset, limits, serial_policy, {});
    Require(exact.bundle.has_value());
  }
  --limits.max_output_requested_bytes;
  Require(mv::WriteRetainedGltfAsset(asset, limits, serial_policy, {})
              .diagnostic.status == mv::GltfStatus::kUnsupportedProfile);
  backend_state.peak = 0;
  const mv::GltfWriteWorkPolicy parallel_policy{1, 128};
  {
    auto parallel = writer::WriteWithHooks(
        asset, WriteLimits(), parallel_policy, {}, backend, &stats, nullptr);
    Require(parallel.bundle && stats.launched == 4 && stats.joined == 4);
    Require(stats.maximum_observed_requests == backend_state.peak &&
            parallel.bundle->NewRequestedBytes() < backend_state.peak);
  }
  limits.max_output_requested_bytes = backend_state.peak;
  {
    auto exact = mv::WriteRetainedGltfAsset(asset, limits, parallel_policy, {});
    Require(exact.bundle.has_value());
  }
  --limits.max_output_requested_bytes;
  Require(mv::WriteRetainedGltfAsset(asset, limits, parallel_policy, {})
              .diagnostic.status == mv::GltfStatus::kUnsupportedProfile);
  Require(backend_state.active == 0 &&
          context.ActiveTrackedPayload() == source_charge &&
          context.ActiveWorkers() == 0);
  std::size_t rejected = 0;
  for (std::size_t allowance = 0; allowance < 16; ++allowance) {
    backend_state.remaining = allowance;
    bool accepted = false;
    try {
      auto output = writer::WriteWithHooks(asset, WriteLimits(), {1, 128}, {},
                                           backend, &stats, nullptr);
      Require(output.bundle.has_value());
      Require(stats.launched == 4 && stats.joined == 4);
      accepted = true;
    } catch (const std::bad_alloc&) {
      ++rejected;
    }
    Require(backend_state.active == 0 &&
            context.ActiveTrackedPayload() == source_charge &&
            context.ActiveWorkers() == 0);
    if (accepted) break;
  }
  Require(rejected >= 5);
  std::stop_source canceled;
  canceled.request_stop();
  auto zero = WriteLimits();
  zero.max_resources = 0;
  Require(mv::WriteRetainedGltfAsset(asset, zero, serial_policy,
                                     canceled.get_token())
              .diagnostic.status == mv::GltfStatus::kInvalidLimits);
  Require(mv::WriteRetainedGltfAsset(asset, WriteLimits(), serial_policy,
                                     canceled.get_token())
              .diagnostic.status == mv::GltfStatus::kCanceled);
  for (const auto index : {0, 1, 2, 3}) {
    auto tiny = WriteLimits();
    if (index == 0) tiny.max_json_bytes = 1;
    if (index == 1) tiny.max_resources = 1;
    if (index == 2) tiny.max_resource_bytes = 1;
    if (index == 3) tiny.max_total_resource_bytes = 1;
    Require(mv::WriteRetainedGltfAsset(asset, tiny, serial_policy, {})
                .diagnostic.status == mv::GltfStatus::kUnsupportedProfile);
  }
  auto held =
      meshvale::geometry::TryReservePayload(context, 10000000 - source_charge);
  Require(held.status == meshvale::geometry::ReservationStatus::kAccepted);
  Require(mv::WriteRetainedGltfAsset(asset, WriteLimits(), serial_policy, {})
              .diagnostic.status == mv::GltfStatus::kBudgetExceeded);
  Require(context.ActiveWorkers() == 0);
}
struct RuntimeFault {
  std::size_t job;
};
void WorkerFailuresAndCancellation() {
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  auto asset = Capture(context);
  const auto source_charge = context.ActiveTrackedPayload();
  writer::ExecutionStats stats;
  writer::ExecutionHooks launch;
  launch.before_launch = [](std::size_t worker, void*) {
    if (worker == 1) throw RuntimeFault{worker};
  };
  bool caught = false;
  try {
    (void)writer::WriteWithHooks(asset, WriteLimits(), {1, 128}, {},
                                 detail::DefaultAllocationBackend(), &stats,
                                 &launch);
  } catch (const RuntimeFault&) {
    caught = true;
  }
  Require(caught && stats.launched == 1 && stats.joined == 1);
  Require(context.ActiveWorkers() == 0 &&
          context.ActiveTrackedPayload() == source_charge);
  struct State {
    std::atomic<bool> second{false};
    std::stop_source stop;
  } state;
  writer::ExecutionHooks jobs;
  jobs.state = &state;
  jobs.before_job = [](std::size_t job, void* opaque) {
    auto& value = *static_cast<State*>(opaque);
    if (job == 0) {
      while (!value.second.load()) std::this_thread::yield();
      throw RuntimeFault{job};
    }
    if (job == 1) {
      value.stop.request_stop();
      value.second = true;
      throw RuntimeFault{job};
    }
  };
  caught = false;
  try {
    (void)writer::WriteWithHooks(
        asset, WriteLimits(), {1, 128}, state.stop.get_token(),
        detail::DefaultAllocationBackend(), &stats, &jobs);
  } catch (const RuntimeFault& failure) {
    caught = failure.job == 0;
  }
  Require(caught && stats.launched == 4 && stats.joined == 4);
  Require(context.ActiveWorkers() == 0 &&
          context.ActiveTrackedPayload() == source_charge);
  std::stop_source stopped;
  jobs.state = &stopped;
  jobs.before_job = [](std::size_t, void* opaque) {
    static_cast<std::stop_source*>(opaque)->request_stop();
  };
  auto canceled = writer::WriteWithHooks(
      asset, WriteLimits(), {1, 128}, stopped.get_token(),
      detail::DefaultAllocationBackend(), &stats, &jobs);
  Require(!canceled.bundle &&
          canceled.diagnostic.status == mv::GltfStatus::kCanceled);
  Require(stats.launched == 4 && stats.joined == 4);
  Require(context.ActiveWorkers() == 0 &&
          context.ActiveTrackedPayload() == source_charge);
  Require(std::string_view(
              reinterpret_cast<const char*>(asset.OriginalJson().data()),
              asset.OriginalJson().size()) == kJson);
}
}  // namespace
int main() {
  try {
    PreservationAndLifetime();
    LimitsAndBackendFailures();
    WorkerFailuresAndCancellation();
    std::cout
        << "retained writer bytes, identities, reload, lifetime, caps and "
           "joined failures passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
