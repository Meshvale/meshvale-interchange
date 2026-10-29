// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_topology_write.h>

#include <stop_token>

// Link the actual installed symbol on every compiler profile. Unsupported
// numerical capture is exercised separately by the materialization consumer;
// this dependency check does not claim to execute a topology writer operation.
int main() {
  using LegacyWriter = meshvale::interchange::GltfTopologyWriteResult (*)(
      const meshvale::interchange::GltfTopologyWriteRequest&,
      const meshvale::interchange::GltfTopologyWriteLimits&,
      const meshvale::interchange::GltfTopologyWriteWorkPolicy&,
      std::stop_token);
  LegacyWriter volatile writer =
      &meshvale::interchange::WriteGltfTopologySelection;
  return writer != nullptr ? 0 : 1;
}
