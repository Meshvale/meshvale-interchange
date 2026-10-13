// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_TEST_INTERNAL_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/interchange/gltf.h>

#include <cstddef>
#include <span>
#include <stop_token>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_detail {
// Private synchronous per-call qualification entrypoints. Backend state lives
// through all blocks it allocated, including returned immutable owners. Native
// backend and observer state live through all joins before the call returns.
[[nodiscard]] GltfReadResult TestReadGltfAssetCapped(
    std::span<const std::byte> json,
    std::span<const SuppliedGltfResource> supplied_resources,
    const GltfReadOperationLimits& limits, const GltfWorkPolicy& policy,
    const geometry::ExecutionContext& context, std::stop_token stop,
    AllocationBackend allocation_backend, NativeWorkerBackend worker_backend,
    const OperationWorkerObserver* observer = nullptr);
[[nodiscard]] GltfExtractionResult TestExtractGltfPrimitiveCapped(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
    const GltfExtractionRequest& request, const GltfExtractionLimits& limits,
    std::stop_token stop, AllocationBackend allocation_backend,
    NativeWorkerBackend worker_backend,
    const OperationWorkerObserver* observer = nullptr);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_TEST_INTERNAL_H_
