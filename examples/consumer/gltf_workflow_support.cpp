// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
constexpr std::size_t kWorkerBudget = 2;
constexpr std::size_t kMinimumParallelVertices = 1;
constexpr std::size_t kPayloadBudget = 16777216;
constexpr std::uint64_t kCountLimit = 4096;
constexpr std::uint64_t kByteLimit = 1048576;
constexpr std::uint64_t kRequestLimit = 4194304;
constexpr std::uint64_t kChannelLimit = 64;
constexpr std::uint64_t kJsonDepthLimit = 128;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kParallelMinimumComponents = 1;
constexpr std::uint64_t kChannelPartitionElements = 16;
constexpr std::uint64_t kPartitionBytes = 64;
constexpr std::uint64_t kCancellationBlockBytes = 16;
constexpr std::uint64_t kCancellationBlockScalars = 16;
constexpr std::uint64_t kMaximumJobs = 8;
constexpr std::string_view kDocument =
    R"({"asset":{"version":"2.0"},"extras":{"exact":9007199254740993},"accessors":[{"componentType":5126,"type":"VEC3","count":6,"min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
constexpr std::array<geo::index_t, 1> kKeptFaces{1};
constexpr std::array<geo::index_t, 3> kKeptCorners{3, 4, 5};
constexpr std::array<std::uint32_t, 3> kExpectedIndices{3, 4, 5};
constexpr std::array<std::byte, 12> kExpectedBytes{
    std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{4}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{5}, std::byte{0}, std::byte{0}, std::byte{0}};
constexpr std::string_view kExpectedUri =
    "data:application/octet-stream;base64,AwAAAAQAAAAFAAAA";
void Require(bool value) {
  if (!value)
    throw std::runtime_error(
        "installed independent topology inspection failed");
}
mv::GltfReadLimits ReadLimits() {
  return {kByteLimit,    kByteLimit,   kByteLimit,    kCountLimit,
          kChannelLimit, kByteLimit,   kChannelLimit, kChannelLimit,
          kChannelLimit, kCountLimit,  kCountLimit,   kCountLimit,
          kCountLimit,   kRequestLimit};
}
}  // namespace

int main() {
  try {
    std::optional<mv::GltfTopologyWriteBundle> retained;
    std::optional<mv::GltfMaterializedPrimitive> original;
    std::optional<geo::MeshInspection> candidate;
    {
      geo::ExecutionContext context(
          {kWorkerBudget, kMinimumParallelVertices, kPayloadBudget});
      const geo::MeshMaterializationWorkPolicy geometry_work{
          kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
          kMaximumJobs};
      const geo::MeshMaterializationLimits geometry_limits{
          kCountLimit,   kCountLimit, kCountLimit,  kCountLimit,
          kChannelLimit, kCountLimit, kCountLimit,  kCountLimit,
          kCountLimit,   kByteLimit,  kRequestLimit};
      const mv::GltfMaterializationLimits transfer_limits{
          kChannelLimit,
          kRequestLimit,
          geometry_limits,
          {kCountLimit, kRequestLimit}};
      auto read = mv::ReadGltfAssetCapped(
          std::as_bytes(std::span(kDocument.data(), kDocument.size())), {},
          {ReadLimits(), kRequestLimit},
          {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumComponents,
           kChannelPartitionElements},
          context, {});
      Require(read.asset.has_value());
      auto extracted = mv::ExtractGltfPrimitiveCapped(
          *read.asset, *read.asset->Primitive(0),
          {mv::GltfTriangleRequest::kRequireTriangles,
           mv::GltfValueRequest::kRawAndNormalized},
          {kCountLimit, kCountLimit, kRequestLimit}, {});
      Require(extracted.extraction.has_value());
      auto transferred = mv::MaterializeGltfPrimitive(
          *extracted.extraction, transfer_limits, geometry_work);
      Require(transferred.primitive.has_value());
      auto selected =
          geo::SelectMeshFaces(transferred.primitive->Mesh(), kKeptFaces,
                               geometry_limits, geometry_work);
      Require(selected.selection.has_value());
      auto inspected = geo::InspectMaterializedMesh(
          selected.selection->Candidate(), {kCountLimit, kRequestLimit},
          geometry_work);
      Require(inspected.inspection && inspected.inspection->IsStorageValid());
      const mv::GltfTopologyWriteLimits writer_limits{
          kByteLimit,
          kCountLimit,
          kJsonDepthLimit,
          kCountLimit,
          kChannelLimit,
          kCountLimit,
          kByteLimit,
          kCountLimit,
          kCountLimit,
          kByteLimit,
          {kByteLimit, kChannelLimit, kByteLimit, kByteLimit, kRequestLimit}};
      const mv::GltfTopologyWriteWorkPolicy writer_work{
          kParallelMinimumBytes, kPartitionBytes, kCancellationBlockBytes,
          kMaximumJobs};
      auto written = mv::WriteGltfTopologySelection(
          {*transferred.primitive, *inspected.inspection,
           selected.selection->SourceFaces(),
           selected.selection->SourceCorners()},
          writer_limits, writer_work);
      Require(written.bundle.has_value() && written.bundle->ChangedBinding());
      retained = *written.bundle;
      original = *transferred.primitive;
      candidate = *inspected.inspection;
    }
    Require(retained->Resources().size() == 1 &&
            retained->Resources()[0].authored_uri == kExpectedUri &&
            std::ranges::equal(retained->Resources()[0].bytes, kExpectedBytes));
    Require(retained->SourceFaces().size() == 1 &&
            retained->SourceFaces()[0] == kKeptFaces[0] &&
            retained->Source().Mesh().View().positions.size() == 18 &&
            retained->OriginalAsset().OriginalJson().size() ==
                kDocument.size());
    const auto context = retained->Source().Mesh().ShareExecutionContext();
    auto reloaded = mv::ReadGltfAssetCapped(
        retained->Json(), {}, {ReadLimits(), kRequestLimit},
        {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumComponents,
         kChannelPartitionElements},
        context, {});
    Require(reloaded.asset.has_value());
    auto extracted = mv::ExtractGltfPrimitiveCapped(
        *reloaded.asset, *reloaded.asset->Primitive(0),
        {mv::GltfTriangleRequest::kRequireTriangles,
         mv::GltfValueRequest::kRawAndNormalized},
        {kCountLimit, kCountLimit, kRequestLimit}, {});
    Require(extracted.extraction &&
            std::ranges::equal(extracted.extraction->TriangleIndices(),
                               kExpectedIndices));
    const mv::GltfTopologyOutputInspectionLimits inspection_limits{
        kByteLimit,  kCountLimit,   kJsonDepthLimit, kChannelLimit, kByteLimit,
        kCountLimit, kChannelLimit, kCountLimit,     kByteLimit,    kCountLimit,
        kCountLimit, kByteLimit,    kRequestLimit};
    auto checked = mv::InspectGltfTopologyOutput(
        {*original, *candidate, kKeptFaces, kKeptCorners, *retained,
         *reloaded.asset, *extracted.extraction},
        inspection_limits,
        {kParallelMinimumBytes, kPartitionBytes, kCancellationBlockBytes,
         kMaximumJobs});
    Require(checked.inspection.has_value());
    const auto& coverage = checked.inspection->Coverage();
    using Coverage = mv::GltfTopologyOutputCheckCoverage;
    Require(coverage.source_candidate == Coverage::kComplete &&
            coverage.correspondence == Coverage::kComplete &&
            coverage.lexical == Coverage::kComplete &&
            coverage.resources == Coverage::kComplete &&
            coverage.reloaded_primitive == Coverage::kComplete &&
            coverage.provenance == Coverage::kComplete);
    retained.reset();
    original.reset();
    candidate.reset();
    reloaded.asset.reset();
    extracted.extraction.reset();
    Require(
        std::ranges::equal(checked.inspection->SourceFaces(), kKeptFaces) &&
        std::ranges::equal(checked.inspection->SourceCorners(), kKeptCorners) &&
        std::ranges::equal(
            checked.inspection->ReloadedExtraction().TriangleIndices(),
            kExpectedIndices));
    checked.inspection.reset();
    Require(context.ActiveTrackedPayload() == 0 &&
            context.ActiveWorkers() == 0);
    std::cout << "installed independent inspection, owned lifetimes and "
                 "cleanup passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
