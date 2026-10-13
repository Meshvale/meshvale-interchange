// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_workflow_support.h>

// Verify the actual installed symbol on every compiler profile. Numerical
// operation admission is covered separately by the qualified runtime fixture.
int main() {
  auto* volatile inspect = &meshvale::interchange::InspectGltfTopologyOutput;
  return inspect != nullptr ? 0 : 1;
}
