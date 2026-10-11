// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_WRITE_WORKERS_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_WRITE_WORKERS_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_topology_write.h>

#include <cstddef>
#include <cstdint>
#include <stop_token>

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_topology_write_detail {
struct NativeWorkerRecord {
  void (*entry)(NativeWorkerRecord&) noexcept = nullptr;
  void* state = nullptr;
  std::size_t ordinal = 0;
};
// Per-call private fault seam. State and callbacks outlive all launched/joined
// workers. No process-global hook, ownership transfer or default heap callable.
struct NativeWorkerBackend {
  std::uintptr_t (*launch)(NativeWorkerRecord*, void* state);
  void (*join)(std::uintptr_t, void* state) noexcept;
  void* state;
};
[[nodiscard]] NativeWorkerBackend DefaultNativeWorkerBackend() noexcept;
using RangeOperation = void (*)(void*, std::size_t begin, std::size_t end);
// Positive width/work and valid callbacks are already admitted by
// orchestration.
void RunRange(const geometry::ExecutionContext& context,
              const gltf_detail::AllocationContext& allocation,
              const GltfTopologyWriteWorkPolicy& work, std::size_t count,
              std::size_t element_bytes, RangeOperation operation, void* state,
              std::stop_token stop, GltfTopologyWriteExecutionStats& stats,
              const NativeWorkerBackend& backend);
}  // namespace meshvale::interchange::gltf_topology_write_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_WRITE_WORKERS_H_
