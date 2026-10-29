// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_workflow_support.h>

#include <stop_token>

// Verify the actual installed symbol on every compiler profile. Numerical
// operation admission is covered separately by the qualified runtime fixture.
int main() {
  using LegacyInspector =
      meshvale::interchange::GltfTopologyOutputInspectionResult (*)(
          const meshvale::interchange::GltfTopologyOutputInspectionRequest&,
          const meshvale::interchange::GltfTopologyOutputInspectionLimits&,
          const meshvale::interchange::GltfTopologyOutputInspectionWorkPolicy&,
          std::stop_token);
  LegacyInspector volatile inspect =
      &meshvale::interchange::InspectGltfTopologyOutput;
  return inspect != nullptr ? 0 : 1;
}
