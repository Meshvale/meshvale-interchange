// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
constexpr std::size_t kWorkerBudget = 2;
constexpr std::size_t kPayloadBudget = 10485760;
constexpr std::uint64_t kCountLimit = 4096;
constexpr std::uint64_t kByteLimit = 1048576;
constexpr std::uint64_t kChannelLimit = 64;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kPartitionBytes = 64;
constexpr std::uint64_t kCancellationBlockScalars = 16;
constexpr std::uint64_t kMaximumJobs = 4;
constexpr std::string_view kDocument =
    R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":3,"min":[0,0,0],"max":[0,0,0]},{"componentType":5122,"type":"VEC2","count":3,"normalized":true}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"_SIGNED":1}}]}]})";
void Require(bool value) {
  if (!value) throw std::runtime_error("installed glTF materialization failed");
}
}  // namespace
int main() {
  try {
    const mv::GltfMaterializationLimits limits{
        kChannelLimit,
        kByteLimit,
        {kCountLimit, kCountLimit, kCountLimit, kCountLimit, kChannelLimit,
         kCountLimit, kCountLimit, kCountLimit, kCountLimit, kByteLimit,
         kByteLimit},
        {kCountLimit, kByteLimit}};
    const geo::MeshMaterializationWorkPolicy work{
        kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
        kMaximumJobs};
    std::optional<mv::GltfMaterializedPrimitive> retained;
    {
      geo::ExecutionContext context(
          {kWorkerBudget, kParallelMinimumBytes, kPayloadBudget});
      const mv::GltfReadLimits read_limits{
          kByteLimit,    kByteLimit,  kByteLimit,    kCountLimit,
          kChannelLimit, kByteLimit,  kChannelLimit, kChannelLimit,
          kChannelLimit, kCountLimit, kCountLimit,   kCountLimit,
          kCountLimit,   kByteLimit};
      auto read = mv::ReadGltfAsset(
          std::as_bytes(std::span(kDocument.data(), kDocument.size())), {},
          read_limits,
          {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumBytes,
           kCancellationBlockScalars},
          context, {});
      Require(read.asset.has_value());
      auto extraction =
          mv::ExtractGltfPrimitive(*read.asset, *read.asset->Primitive(0),
                                   {mv::GltfTriangleRequest::kRequireTriangles,
                                    mv::GltfValueRequest::kRawAndNormalized},
                                   {kCountLimit, kCountLimit, kByteLimit}, {});
      Require(extraction.extraction.has_value());
      auto materialized =
          mv::MaterializeGltfPrimitive(*extraction.extraction, limits, work);
      Require(materialized.primitive.has_value());
      retained = *materialized.primitive;
    }
    const auto view = retained->Mesh().View();
    Require(retained->Inspection().IsStorageValid() &&
            retained->SourceAsset().OriginalJson().size() == kDocument.size() &&
            retained->ChannelMappings().size() == 2 &&
            view.positions.size() == 9 && view.attributes.size() == 1);
    Require(std::get<std::span<const std::int32_t>>(view.attributes[0].values)
                .size() == 6);
    Require(
        retained->SourceExtraction().Channels()[1].normalized_values.size() ==
        6);
    constexpr std::array<geo::index_t, 1> kKeptFaces{0};
    auto selected = geo::SelectMeshFaces(retained->Mesh(), kKeptFaces,
                                         limits.materialization, work);
    Require(selected.selection.has_value() &&
            selected.selection->Candidate().View().positions.data() ==
                view.positions.data() &&
            selected.selection->SourceFaces()[0] == 0);
    std::cout << "installed raw glTF materialization, retained provenance and "
                 "face selection passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
