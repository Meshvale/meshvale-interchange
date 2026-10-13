// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace {
namespace mv = meshvale::interchange;
constexpr std::uint64_t kSourceByteLimit = 65536;
constexpr std::uint64_t kSourceCountLimit = 1024;
constexpr std::uint64_t kDocumentDepthLimit = 32;
constexpr std::uint64_t kNumericTokenByteLimit = 128;
constexpr std::uint64_t kDecimalDigitLimit = 128;
constexpr std::uint64_t kRequestedByteLimit = 1048576;
constexpr std::uint64_t kResourcePartitionBytes = 4096;
constexpr std::uint64_t kParallelResourceThresholdBytes = 4096;
constexpr std::uint64_t kParallelChannelThresholdComponents = 512;
constexpr std::uint64_t kChannelPartitionElements = 512;
constexpr std::uint64_t kTriangleCorners = 3;
constexpr std::uint64_t kPositionComponents = 9;
constexpr std::string_view kDocument =
    R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
mv::GltfReadLimits SourceLimits() {
  mv::GltfReadLimits limits;
  limits.max_json_bytes = kSourceByteLimit;
  limits.max_resource_bytes = kSourceByteLimit;
  limits.max_owned_source_resource_bytes = kSourceByteLimit;
  limits.max_document_nodes = kSourceCountLimit;
  limits.max_document_depth = kDocumentDepthLimit;
  limits.max_decoded_string_bytes = kSourceByteLimit;
  limits.max_numeric_token_bytes = kNumericTokenByteLimit;
  limits.max_decimal_coefficient_digits = kDecimalDigitLimit;
  limits.max_decimal_exponent_digits = kDecimalDigitLimit;
  limits.max_table_rows = kSourceCountLimit;
  limits.max_graph_edges = kSourceCountLimit;
  limits.max_instances = kSourceCountLimit;
  limits.max_accessor_elements = kSourceCountLimit;
  limits.max_projection_bytes = kSourceByteLimit;
  return limits;
}
}  // namespace
int main() {
  meshvale::geometry::ExecutionContext context;
  auto read = mv::ReadGltfAssetCapped(
      std::as_bytes(std::span(kDocument.data(), kDocument.size())), {},
      {SourceLimits(), kRequestedByteLimit},
      {kParallelResourceThresholdBytes, kResourcePartitionBytes,
       kParallelChannelThresholdComponents, kChannelPartitionElements},
      context, {});
  if (!read.asset || !read.asset->NewRequestedBytes() ||
      *read.asset->NewRequestedBytes() == 0 ||
      *read.asset->NewRequestedBytes() > kRequestedByteLimit)
    return 1;
  const auto source_requests = read.asset->NewRequestedBytes();
  const auto source_charge = context.ActiveTrackedPayload();
  meshvale::geometry::ExecutionContext separate_context;
  if (!read.asset->SharesExecutionBudget(context) ||
      read.asset->SharesExecutionBudget(separate_context) ||
      read.asset->NewRequestedBytes() != source_requests ||
      context.ActiveTrackedPayload() != source_charge)
    return 1;
  auto extraction = mv::ExtractGltfPrimitiveCapped(
      *read.asset, *read.asset->Primitive(0),
      {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw},
      {kPositionComponents, kTriangleCorners, kRequestedByteLimit}, {});
  constexpr std::array<std::uint32_t, 3> kExpectedIndices{0, 1, 2};
  if (!extraction.extraction ||
      extraction.extraction->NewRequestedBytes() == 0 ||
      extraction.extraction->NewRequestedBytes() > kRequestedByteLimit ||
      !std::ranges::equal(extraction.extraction->TriangleIndices(),
                          kExpectedIndices))
    return 1;
  read.asset.reset();
  if (extraction.extraction->NewRequestedBytes() == 0) return 1;
  extraction.extraction.reset();
  return context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0
             ? 0
             : 1;
}
