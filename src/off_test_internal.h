// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_OFF_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_OFF_TEST_INTERNAL_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

#include "gltf_allocation.h"

namespace meshvale::interchange::off_detail {
struct TestOptions {
  gltf_detail::AllocationBackend allocation_backend =
      gltf_detail::DefaultAllocationBackend();
  // Observe real bounded checkpoints; the caller can request its stop source.
  // Backend state must outlive every block; checkpoint state lives until
  // return.
  void (*checkpoint)(OffStage, std::uint64_t, void*) noexcept = nullptr;
  void* state = nullptr;
  // Real completed-copy count at bounded relocation checkpoints, then total.
  // The same state is borrowed through return; stop is checked after callback.
  void (*relocation_checkpoint)(OffStage, std::uint64_t, std::uint64_t,
                                void*) noexcept = nullptr;
};
[[nodiscard]] OffReadResult ReadOffForTest(
    std::span<const std::byte> bytes, const OffReadLimits& limits,
    const OffWorkPolicy& work, const geometry::ExecutionContext& context,
    std::stop_token stop, const TestOptions& options);
[[nodiscard]] OffWriteResult WriteOffForTest(
    const geometry::MaterializedMesh& mesh, const OffWriteOptions& options,
    const OffWriteLimits& limits, const OffWorkPolicy& work,
    std::stop_token stop, const TestOptions& test);
}  // namespace meshvale::interchange::off_detail
#endif  // MESHVALE_INTERCHANGE_SRC_OFF_TEST_INTERNAL_H_
