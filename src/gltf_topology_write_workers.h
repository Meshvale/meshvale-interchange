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
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_topology_write_detail {
// The writer and its translation validator use the identical per-call backend.
using NativeWorkerRecord = gltf_detail::NativeWorkerRecord;
using NativeWorkerBackend = gltf_detail::NativeWorkerBackend;
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
