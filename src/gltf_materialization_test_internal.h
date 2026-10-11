// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_MATERIALIZATION_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_MATERIALIZATION_TEST_INTERNAL_H_

#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <cstdint>
#include <stop_token>

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_materialization_detail {
// Per-call, nonallocating observation/fault seam. The allocation backend state
// outlives all returned blocks and obeys original matched allocate/free rules.
// Descriptor observation is noexcept, runs outside ledger locks, and its state
// remains alive until the synchronous call returns. Production uses defaults.
struct TestHooks {
  gltf_detail::AllocationBackend allocation =
      gltf_detail::DefaultAllocationBackend();
  void (*after_descriptor)(std::uint64_t channel,
                           void* state) noexcept = nullptr;
  void* descriptor_state = nullptr;
};
[[nodiscard]] GltfMaterializationResult TestMaterializeGltfPrimitive(
    const GltfPrimitiveExtraction& extraction,
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop,
    const TestHooks& hooks);
}  // namespace meshvale::interchange::gltf_materialization_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_MATERIALIZATION_TEST_INTERNAL_H_
