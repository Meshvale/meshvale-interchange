// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_topology_write.h>

// Link the actual installed symbol on every compiler profile. Unsupported
// numerical capture is exercised separately by the materialization consumer;
// this dependency check does not claim to execute a topology writer operation.
int main() {
  auto* volatile writer = &meshvale::interchange::WriteGltfTopologySelection;
  return writer != nullptr ? 0 : 1;
}
