// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_STL_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_STL_TEST_INTERNAL_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

#include "gltf_allocation.h"

namespace meshvale::interchange::stl_detail {
struct TestOptions {
  gltf_detail::AllocationBackend allocation_backend =
      gltf_detail::DefaultAllocationBackend();
  // Observe actual bounded checkpoints; caller can request its own stop source.
  void (*checkpoint)(StlStage, std::uint64_t, void*) noexcept = nullptr;
  void* state = nullptr;
  // Real completed-copy count at bounded relocation checkpoints, then total.
  // The same state is borrowed through return; stop is checked after callback.
  void (*relocation_checkpoint)(StlStage, std::uint64_t, std::uint64_t,
                                void*) noexcept = nullptr;
};
[[nodiscard]] StlReadResult ReadStlForTest(
    std::span<const std::byte> bytes, StlEncoding encoding,
    const StlReadLimits& limits, const StlWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const TestOptions& options);
[[nodiscard]] StlWriteResult WriteStlForTest(
    const geometry::MaterializedMesh& mesh, const StlWriteOptions& options,
    const StlWriteLimits& limits, const StlWorkPolicy& work,
    std::stop_token stop, const TestOptions& test);
}  // namespace meshvale::interchange::stl_detail
#endif  // MESHVALE_INTERCHANGE_SRC_STL_TEST_INTERNAL_H_
