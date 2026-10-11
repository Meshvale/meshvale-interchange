// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLB_DETAIL_H_
#define MESHVALE_INTERCHANGE_SRC_GLB_DETAIL_H_

#include <cstddef>
#include <span>
#include <stop_token>

#include "gltf_allocation.h"
#include "meshvale/interchange/glb.h"

namespace meshvale::interchange::glb_detail {
// Private qualification only; state outlives every allocation. Callback runs
// before each launch, may throw, and must not retain operation state.
struct Hooks {
  gltf_detail::AllocationBackend allocation =
      gltf_detail::DefaultAllocationBackend();
  void (*before_launch)(std::size_t index, void* state) = nullptr;
  void* state = nullptr;
};
[[nodiscard]] GlbResult Read(std::span<const std::byte> bytes,
                             const GlbLimits& limits, const GlbWorkPolicy& work,
                             const geometry::ExecutionContext& context,
                             std::stop_token stop, Hooks hooks);
[[nodiscard]] GlbResult Write(std::span<const GlbChunk> chunks,
                              const GlbLimits& limits,
                              const GlbWorkPolicy& work,
                              const geometry::ExecutionContext& context,
                              std::stop_token stop, Hooks hooks);
}  // namespace meshvale::interchange::glb_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLB_DETAIL_H_
