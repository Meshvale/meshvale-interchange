// SPDX-License-Identifier: Apache-2.0
#include "fixture_support.h"

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gltf_topology_write_semantics {
namespace {
constexpr std::uint64_t kMebibyte = 1024 * 1024;
constexpr std::uint64_t kSmallPayloadBytes = 64 * kMebibyte;
constexpr std::uint64_t kLargePayloadBytes = 2 * 1024 * kMebibyte;
constexpr std::uint64_t kSmallElementLimit = 4096;
constexpr std::uint64_t kLargeElementLimit = 64 * 1024 * 1024;
constexpr std::uint64_t kChannelLimit = 32;
constexpr std::uint64_t kDocumentDepthLimit = 64;
constexpr std::uint64_t kNumericTokenBytes = 128;
constexpr std::uint64_t kWorkerBudget = 4;
constexpr std::uint64_t kPartitionBytes = 1024;
constexpr std::uint64_t kCheckpointScalars = 64;
constexpr std::uint64_t kJobLimit = 32;
constexpr std::uint64_t kReaderPartitionElements = 64;
constexpr std::uint64_t kMinimumParallelBytes = 1;
constexpr std::uint64_t kMinimumParallelComponents = 1;
constexpr std::size_t kMinimumParallelVertices = 1;
constexpr std::uint64_t kSerialThresholdBytes =
    std::numeric_limits<std::uint64_t>::max();
}  // namespace

void Require(bool value, std::string_view message,
             std::source_location source) {
  if (!value)
    throw std::runtime_error(std::string(message) + " at " +
                             source.file_name() + ":" +
                             std::to_string(source.line()));
}
geo::ExecutionContext Context(bool large) {
  return geo::ExecutionContext(
      {kWorkerBudget, kMinimumParallelVertices,
       large ? kLargePayloadBytes : kSmallPayloadBytes});
}
geo::MeshMaterializationWorkPolicy GeometryWork(bool serial) {
  return {serial ? kSerialThresholdBytes : kMinimumParallelBytes,
          kPartitionBytes, kCheckpointScalars, kJobLimit};
}
mv::GltfReadLimits ReadLimits(bool large) {
  mv::GltfReadLimits limits;
  limits.max_json_bytes = kMebibyte;
  limits.max_resource_bytes = kMebibyte;
  limits.max_owned_source_resource_bytes = kMebibyte;
  limits.max_document_nodes = kSmallElementLimit;
  limits.max_document_depth = kDocumentDepthLimit;
  limits.max_decoded_string_bytes = kMebibyte;
  limits.max_numeric_token_bytes = kNumericTokenBytes;
  limits.max_decimal_coefficient_digits = kNumericTokenBytes;
  limits.max_decimal_exponent_digits = kNumericTokenBytes;
  limits.max_table_rows = kSmallElementLimit;
  limits.max_graph_edges = kSmallElementLimit;
  limits.max_instances = kSmallElementLimit;
  limits.max_accessor_elements =
      large ? kLargeElementLimit : kSmallElementLimit;
  limits.max_projection_bytes = kMebibyte;
  return limits;
}
mv::GltfWorkPolicy ReaderWork() {
  return {kMinimumParallelBytes, kPartitionBytes, kMinimumParallelComponents,
          kReaderPartitionElements};
}
mv::GltfExtractionLimits ExtractionLimits(bool large) {
  return {large ? kLargeElementLimit : kSmallElementLimit, kSmallElementLimit,
          large ? kLargePayloadBytes : kSmallPayloadBytes};
}
mv::GltfMaterializationLimits TransferLimits(bool large) {
  const auto count = large ? kLargeElementLimit : kSmallElementLimit;
  const auto bytes = large ? kLargePayloadBytes : kSmallPayloadBytes;
  mv::GltfMaterializationLimits limits;
  limits.max_channels = kChannelLimit;
  limits.max_bridge_new_requested_bytes = kMebibyte;
  limits.materialization = {count,         count,     count, count,
                            kChannelLimit, count,     count, count,
                            count,         kMebibyte, bytes};
  limits.inspection = {count, bytes};
  return limits;
}
mv::GltfAsset ReadFixture(const Fixture& fixture,
                          const geo::ExecutionContext& context) {
  std::vector<mv::SuppliedGltfResource> bindings;
  if (!fixture.external_buffer.empty())
    bindings.push_back(
        {mv::GltfResourceKind::kBuffer, 0, fixture.external_buffer});
  const auto result = mv::ReadGltfAsset(
      {reinterpret_cast<const std::byte*>(fixture.json.data()),
       fixture.json.size()},
      bindings, ReadLimits(fixture.kind == FixtureKind::kLargeIndex),
      ReaderWork(), context, {});
  if (!result.asset)
    std::cerr << "semantic fixture admission status="
              << static_cast<int>(result.diagnostic.status)
              << " code=" << static_cast<int>(result.diagnostic.code)
              << " rule=" << result.diagnostic.rule_id << '\n';
  Require(result.asset && result.diagnostic.status == mv::GltfStatus::kAccepted,
          "Independent fixture must pass public reader admission");
  return *result.asset;
}
mv::GltfPrimitiveExtraction Extract(const mv::GltfAsset& asset,
                                    const Fixture& fixture,
                                    std::uint64_t ordinal) {
  const auto primitive = asset.Primitive(ordinal);
  Require(primitive.has_value(), "Independent primitive ordinal is absent");
  const bool converted =
      fixture.kind == FixtureKind::kStrip || fixture.kind == FixtureKind::kFan;
  const mv::GltfExtractionRequest request{
      converted ? mv::GltfTriangleRequest::kConvertStripOrFan
                : mv::GltfTriangleRequest::kRequireTriangles,
      mv::GltfValueRequest::kRawAndNormalized};
  const auto result = mv::ExtractGltfPrimitive(
      asset, *primitive, request,
      ExtractionLimits(fixture.kind == FixtureKind::kLargeIndex), {});
  Require(result.extraction &&
              result.diagnostic.status == mv::GltfStatus::kAccepted,
          "Independent fixture must pass public typed extraction");
  return *result.extraction;
}
mv::GltfMaterializedPrimitive Transfer(const Fixture& fixture,
                                       const geo::ExecutionContext& context) {
  const auto asset = ReadFixture(fixture, context);
  const auto extraction = Extract(asset, fixture, fixture.primitive_ordinal);
  CheckSourceExtraction(fixture, extraction);
  const auto result = mv::MaterializeGltfPrimitive(
      extraction, TransferLimits(fixture.kind == FixtureKind::kLargeIndex),
      GeometryWork(), {});
  Require(result.primitive &&
              result.status == mv::GltfMaterializationStatus::kSuccess &&
              result.primitive->Inspection().IsStorageValid(),
          "Independent source must transfer into valid canonical storage");
  return *result.primitive;
}
geo::MeshInspection Inspect(const geo::MaterializedMesh& mesh, bool large) {
  const auto result = geo::InspectMaterializedMesh(
      mesh, TransferLimits(large).inspection, GeometryWork(), {});
  Require(result.inspection &&
              result.status == geo::MaterializationStatus::kSuccess &&
              result.inspection->IsStorageValid(),
          "Candidate must have a valid public inspection");
  return *result.inspection;
}
}  // namespace gltf_topology_write_semantics
