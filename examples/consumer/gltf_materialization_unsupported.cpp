// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
void Require(bool value) {
  if (!value)
    throw std::runtime_error(
        "unsupported materialization profile assertion failed");
}
}  // namespace
int main() {
  try {
    // Finite implicit-zero position fixture, admitted by the real reader.
    constexpr std::string_view kDocument =
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":3,"min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
    geo::ExecutionContext context({2, 1, 10485760});
    const mv::GltfReadLimits read_limits{65536, 4096, 131072, 4096,  32,
                                         65536, 128,  128,    128,   1024,
                                         1024,  1024, 1024,   131072};
    auto read =
        mv::ReadGltfAsset({reinterpret_cast<const std::byte*>(kDocument.data()),
                           kDocument.size()},
                          {}, read_limits, {1, 4, 1, 1}, context, {});
    Require(read.asset && read.diagnostic.status == mv::GltfStatus::kAccepted);
    auto extraction =
        mv::ExtractGltfPrimitive(*read.asset, *read.asset->Primitive(0),
                                 {mv::GltfTriangleRequest::kRequireTriangles,
                                  mv::GltfValueRequest::kRaw},
                                 {4096, 4096, 1048576}, {});
    Require(extraction.extraction &&
            extraction.diagnostic.status == mv::GltfStatus::kAccepted);
    const mv::GltfMaterializationLimits limits{
        64,
        1048576,
        {1024, 1024, 4096, 1025, 64, 65536, 4096, 4096, 1024, 1048576, 1048576},
        {1024, 1048576}};
    const auto source_charge = context.ActiveTrackedPayload();
    const auto result = mv::MaterializeGltfPrimitive(*extraction.extraction,
                                                     limits, {1, 64, 16, 4});
    Require(!result.primitive &&
            result.status == mv::GltfMaterializationStatus::kGeometryFailure);
    Require(result.diagnostic.stage == mv::GltfMaterializationStage::kCapture &&
            result.diagnostic.code ==
                mv::GltfMaterializationDiagnosticCode::kGeometry &&
            result.diagnostic.geometry_status ==
                geo::MaterializationStatus::kUnsupportedAllocationProfile);
    Require(context.ActiveTrackedPayload() == source_charge &&
            context.ActiveWorkers() == 0);
    std::cout << "actual glTF transfer preserved unsupported Geometry "
                 "allocation profile\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
