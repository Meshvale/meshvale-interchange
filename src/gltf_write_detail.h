// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_WRITE_DETAIL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_WRITE_DETAIL_H_

#include <meshvale/interchange/gltf_write.h>

#include <cstddef>
#include <stop_token>

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_write_detail {
struct ExecutionStats {
  std::size_t jobs = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::size_t maximum_observed_requests = 0;
};
// Private synchronous fault/observation seam. No callback runs under a ledger
// mutex; state outlives this call and after_job must not throw or allocate.
struct ExecutionHooks {
  void (*before_launch)(std::size_t worker, void* state) = nullptr;
  void (*before_job)(std::size_t job, void* state) = nullptr;
  void (*after_job)(std::size_t job, void* state) noexcept = nullptr;
  void* state = nullptr;
};
[[nodiscard]] GltfWriteResult WriteWithHooks(
    const GltfAsset& asset, const GltfWriteLimits& limits,
    const GltfWriteWorkPolicy& work_policy, std::stop_token stop,
    const gltf_detail::AllocationBackend& backend, ExecutionStats* stats,
    const ExecutionHooks* hooks);
}  // namespace meshvale::interchange::gltf_write_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_WRITE_DETAIL_H_
