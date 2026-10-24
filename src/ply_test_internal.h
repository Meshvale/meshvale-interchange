// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_PLY_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_PLY_TEST_INTERNAL_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

#include "finite_double_format.h"
#include "gltf_allocation.h"

namespace meshvale::interchange::ply_detail {
enum class RelocationKind {
  kProperties,
  kAttributeValues,
  kCorners,
  kOutputBytes
};
struct TestOptions {
  gltf_detail::AllocationBackend allocation_backend =
      gltf_detail::DefaultAllocationBackend();
  // Original backend state outlives every block; checkpoint state lives through
  // return. Observe real bounded checkpoints and optionally request caller
  // stop.
  void (*checkpoint)(PlyStage, std::uint64_t, void*) noexcept = nullptr;
  void* state = nullptr;
  // Real completed-copy count at bounded relocation checkpoints, then total.
  // The same state is borrowed through return; stop is checked after callback.
  void (*relocation_checkpoint)(RelocationKind, PlyStage, std::uint64_t,
                                std::uint64_t, void*) noexcept = nullptr;
  // Actual bounded integer scaling passes/digits, immediately before stop.
  void (*numeric_format_checkpoint)(PlyStage, numeric_format_detail::Phase,
                                    std::uint64_t, std::uint64_t,
                                    void*) noexcept = nullptr;
};
[[nodiscard]] PlyReadResult ReadPlyForTest(
    std::span<const std::byte> bytes, const PlyReadLimits& limits,
    const PlyWorkPolicy& work, const geometry::ExecutionContext& context,
    std::stop_token stop, const TestOptions& options);
[[nodiscard]] PlyWriteResult WritePlyForTest(
    const geometry::MaterializedMesh& mesh, const PlyDocument& schema_source,
    const PlyWriteOptions& options, const PlyWriteLimits& limits,
    const PlyWorkPolicy& work, std::stop_token stop, const TestOptions& test);
}  // namespace meshvale::interchange::ply_detail
#endif  // MESHVALE_INTERCHANGE_SRC_PLY_TEST_INTERNAL_H_
