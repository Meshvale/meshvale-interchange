// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_SUPPORT_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_SUPPORT_TEST_INTERNAL_H_

#include <meshvale/interchange/gltf_workflow_support.h>

#include <stop_token>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_workflow_support_detail {
struct InspectionTestOptions {
  gltf_detail::AllocationBackend allocation_backend =
      gltf_detail::DefaultAllocationBackend();
  gltf_detail::NativeWorkerBackend worker_backend =
      gltf_detail::DefaultNativeWorkerBackend();
  const gltf_detail::OperationWorkerObserver* observer = nullptr;
};
// Synchronous per-call seam. Allocation backend state outlives returned owners;
// native backend/observer state outlives all joined handles before return.
[[nodiscard]] GltfTopologyOutputInspectionResult
InspectGltfTopologyOutputForTest(
    const GltfTopologyOutputInspectionRequest& request,
    const GltfTopologyOutputInspectionLimits& limits,
    const GltfTopologyOutputInspectionWorkPolicy& work, std::stop_token stop,
    const InspectionTestOptions& options);
}  // namespace meshvale::interchange::gltf_workflow_support_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_SUPPORT_TEST_INTERNAL_H_
