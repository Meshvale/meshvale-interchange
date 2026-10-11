// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
// Explicit finite example policies; these are not library defaults.
constexpr std::size_t kWorkerBudget = 2;
constexpr std::size_t kSharedPayloadBytes = 10485760;
constexpr std::uint64_t kDocumentBytes = 65536;
constexpr std::uint64_t kResourceBytes = 4096;
constexpr std::uint64_t kProjectionBytes = 131072;
constexpr std::uint64_t kDocumentNodes = 4096;
constexpr std::uint64_t kDocumentDepth = 32;
constexpr std::uint64_t kNumericTokenBytes = 128;
constexpr std::uint64_t kNumericDigits = 128;
constexpr std::uint64_t kTableRows = 1024;
constexpr std::uint64_t kChannelCount = 64;
constexpr std::uint64_t kOperationBytes = 1048576;
constexpr std::uint64_t kExtractionComponents = 4096;
constexpr std::uint64_t kTriangleCorners = 4096;
constexpr std::uint64_t kAttributeScalars = 65536;
constexpr std::uint64_t kAttributeOffsets = 4096;
constexpr std::uint64_t kPresenceBytes = 4096;
constexpr std::uint64_t kMetadataEntries = 1024;
constexpr std::uint64_t kMinimumParallelBytes = 1;
constexpr std::uint64_t kResourcePartitionBytes = 4;
constexpr std::uint64_t kCapturePartitionBytes = 64;
constexpr std::uint64_t kCancellationBlockScalars = 16;
constexpr std::uint64_t kMaximumJobs = 4;
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
    geo::ExecutionContext context(
        {kWorkerBudget, kMinimumParallelBytes, kSharedPayloadBytes});
    const mv::GltfReadLimits read_limits{
        kDocumentBytes, kResourceBytes,  kProjectionBytes,   kDocumentNodes,
        kDocumentDepth, kDocumentBytes,  kNumericTokenBytes, kNumericDigits,
        kNumericDigits, kTableRows,      kTableRows,         kTableRows,
        kTableRows,     kProjectionBytes};
    auto read =
        mv::ReadGltfAsset({reinterpret_cast<const std::byte*>(kDocument.data()),
                           kDocument.size()},
                          {}, read_limits,
                          {kMinimumParallelBytes, kResourcePartitionBytes,
                           kMinimumParallelBytes, kMinimumParallelBytes},
                          context, {});
    Require(read.asset && read.diagnostic.status == mv::GltfStatus::kAccepted);
    auto extraction = mv::ExtractGltfPrimitive(
        *read.asset, *read.asset->Primitive(0),
        {mv::GltfTriangleRequest::kRequireTriangles,
         mv::GltfValueRequest::kRaw},
        {kExtractionComponents, kTriangleCorners, kOperationBytes}, {});
    Require(extraction.extraction &&
            extraction.diagnostic.status == mv::GltfStatus::kAccepted);
    const mv::GltfMaterializationLimits limits{
        kChannelCount,
        kOperationBytes,
        {kTableRows, kTableRows, kTriangleCorners, kTableRows + 1,
         kChannelCount, kAttributeScalars, kAttributeOffsets, kPresenceBytes,
         kMetadataEntries, kOperationBytes, kOperationBytes},
        {kTableRows, kOperationBytes}};
    const auto source_charge = context.ActiveTrackedPayload();
    const auto result = mv::MaterializeGltfPrimitive(
        *extraction.extraction, limits,
        {kMinimumParallelBytes, kCapturePartitionBytes,
         kCancellationBlockScalars, kMaximumJobs});
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
