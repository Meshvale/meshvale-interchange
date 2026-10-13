// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_RESOURCES_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_RESOURCES_H_

#include <cstddef>
#include <span>

#include "gltf_asset_state.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_detail {
struct ResourceExecutionStats {
  std::size_t jobs = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::size_t caller_copies = 0;
};
// Private synchronous qualification seam. Production passes null. Hook state
// lives through all joins, and callbacks never execute under a ledger lock.
struct ResourceExecutionHooks {
  void (*before_launch)(std::size_t worker, void* state) = nullptr;
  void (*before_job)(std::size_t job, void* state) = nullptr;
  void (*after_job)(std::size_t job, void* state) noexcept = nullptr;
  void* state = nullptr;
};
void AdmitResources(AssetState& state,
                    std::span<const SuppliedGltfResource> supplied,
                    ResourceExecutionStats* stats = nullptr,
                    const ResourceExecutionHooks* hooks = nullptr,
                    const NativeExecutionOptions* execution = nullptr);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_RESOURCES_H_
