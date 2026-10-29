// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_FILE_READ_PROVIDER_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_FILE_READ_PROVIDER_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>

#include <cstddef>
#include <span>
#include <stop_token>

#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_detail {
struct AssetState;
// Invoked after canonical JSON/metadata/graph admission, before resources.
// Provider storage survives the synchronous reader call. Exceptions propagate;
// source file discovery does not replace the reader's semantic admission.
using FileResourceProvider =
    std::span<const SuppliedGltfResource> (*)(const AssetState&, void*);
void ValidateFileReadConfiguration(const GltfReadOperationLimits& limits,
                                   const GltfWorkPolicy& policy);
[[nodiscard]] GltfReadResult ReadGltfAssetFromFiles(
    std::span<const std::byte> json, const GltfReadOperationLimits& limits,
    const GltfWorkPolicy& policy, const geometry::ExecutionContext& context,
    std::stop_token stop, FileResourceProvider provider, void* provider_state,
    const GltfReadRequest* request = nullptr);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_FILE_READ_PROVIDER_H_
