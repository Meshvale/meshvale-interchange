// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_GLTF_WORKFLOW_INSPECTION_SUPPORT_H_
#define MESHVALE_INTERCHANGE_TESTS_GLTF_WORKFLOW_INSPECTION_SUPPORT_H_

#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <span>
#include <stop_token>
#include <string_view>

#include "gltf_topology_write_semantics/fixture_support.h"

namespace gltf_workflow_inspection {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace authored = gltf_topology_write_semantics;
using Status = mv::GltfTopologyOutputInspectionStatus;
using Code = mv::GltfTopologyOutputInspectionDiagnosticCode;
using Stage = mv::GltfTopologyOutputInspectionStage;
constexpr std::uint64_t kRequestBytes = 64 * 1024 * 1024;
constexpr std::uint64_t kByteLimit = 4 * 1024 * 1024;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kChannelLimit = 64;
constexpr std::uint64_t kDepthLimit = 128;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCheckpointBytes = 64;
constexpr std::uint64_t kMaxJobs = 32;
constexpr std::size_t kWorkerBudget = 4;
constexpr std::array<geo::index_t, 1> kSelectedFaces{1};
constexpr std::array<geo::index_t, 3> kSelectedCorners{3, 4, 5};
constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};

void Require(bool value, std::string_view message,
             std::source_location location = std::source_location::current());
geo::ExecutionContext Context();
mv::GltfReadOperationLimits ReadLimits();
mv::GltfExtractionLimits ExtractionLimits();
mv::GltfMaterializationLimits TransferLimits();
geo::MeshMaterializationWorkPolicy GeometryWork();
mv::GltfTopologyOutputInspectionLimits Limits();
mv::GltfTopologyOutputInspectionWorkPolicy Work(bool serial = false);
mv::GltfAsset Capture(std::span<const std::byte> json,
                      std::span<const mv::SuppliedGltfResource> resources,
                      const geo::ExecutionContext& context);
mv::GltfMaterializedPrimitive Source(const authored::Fixture& fixture,
                                     const geo::ExecutionContext& context);
mv::GltfPrimitiveExtraction Extract(const mv::GltfAsset& asset,
                                    std::uint64_t ordinal, bool convert,
                                    bool normalized = true);
geo::MeshInspection Inspect(const geo::MaterializedMesh& mesh);
mv::GltfTopologyWriteBundle Write(const mv::GltfMaterializedPrimitive& source,
                                  const geo::MeshInspection& candidate,
                                  std::span<const geo::index_t> faces,
                                  std::span<const geo::index_t> corners);
mv::GltfAsset Reload(const mv::GltfTopologyWriteBundle& bundle,
                     const geo::ExecutionContext& context);
void CheckComplete(const mv::GltfTopologyOutputInspectionResult& result,
                   bool serial);
mv::GltfTopologyOutputInspectionResult CheckRejected(
    const mv::GltfTopologyOutputInspectionRequest& request, Status status,
    Code code, Stage stage,
    const mv::GltfTopologyOutputInspectionLimits& limits = Limits(),
    std::stop_token stop = {});
void CheckRequests();
}  // namespace gltf_workflow_inspection
#endif  // MESHVALE_INTERCHANGE_TESTS_GLTF_WORKFLOW_INSPECTION_SUPPORT_H_
