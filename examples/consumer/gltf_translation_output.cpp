// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <exception>
#include <initializer_list>
#include <iostream>
#include <optional>

#include "../../tests/gltf_translation_output_fixture.h"

int main() {
  namespace moving = gltf_translation_output_fixture;
  namespace mv = meshvale::interchange;
  try {
    meshvale::geometry::ExecutionContext context({4, 1, 512 * moving::kMiB});
    std::optional<mv::GltfTopologyOutputInspection> retained;
    for (bool step : {false, true})
      for (bool identity : {false, true}) {
        const auto source = moving::Source(context, step);
        const auto candidate = moving::Candidate(source, identity);
        const auto written = mv::WriteGltfTopologySelection(
            {source, candidate, moving::Faces(identity),
             moving::Corners(identity)},
            moving::WriteLimits(), moving::WriteWork());
        moving::Require(written.bundle && written.direct_translation &&
                            written.direct_translation->complete,
                        "installed actual moving checked writer");
        const auto reloaded = moving::Reload(*written.bundle, context);
        const auto extraction = moving::Extract(reloaded);
        const auto inspected = mv::InspectGltfTopologyOutput(
            {source, candidate, moving::Faces(identity),
             moving::Corners(identity), *written.bundle, reloaded, extraction},
            moving::InspectionLimits(), moving::InspectionWork());
        moving::Require(inspected.inspection && inspected.direct_translation &&
                            inspected.direct_translation->complete,
                        "installed independent actual output proof");
        moving::CheckLiteral(inspected.inspection->Output(), identity, step);
        retained = inspected.inspection;
      }
    moving::Require(retained->DirectTranslationFacts() &&
                        retained->ReloadedExtraction().DirectTranslation() &&
                        context.ActiveWorkers() == 0,
                    "installed strong proof lifetime/joins");
    retained.reset();
    moving::Require(context.ActiveTrackedPayload() == 0,
                    "installed all retained owners released");
    std::cout
        << "installed direct translation memory output and inspection passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
