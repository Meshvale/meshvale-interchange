// SPDX-License-Identifier: Apache-2.0
#include "gltf_workflow_inspection_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gltf_workflow_inspection {
void Require(bool value, std::string_view message,
             std::source_location location) {
  if (!value)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()) + ": " +
                             std::string(message));
}
geo::ExecutionContext Context() {
  return geo::ExecutionContext({kWorkerBudget, 1, kRequestBytes});
}
mv::GltfReadOperationLimits ReadLimits() {
  mv::GltfReadLimits limits;
  limits.max_json_bytes = kByteLimit;
  limits.max_resource_bytes = kByteLimit;
  limits.max_owned_source_resource_bytes = kByteLimit;
  limits.max_document_nodes = kCountLimit;
  limits.max_document_depth = kDepthLimit;
  limits.max_decoded_string_bytes = kByteLimit;
  limits.max_numeric_token_bytes = 128;
  limits.max_decimal_coefficient_digits = 128;
  limits.max_decimal_exponent_digits = 128;
  limits.max_table_rows = kCountLimit;
  limits.max_graph_edges = kCountLimit;
  limits.max_instances = kCountLimit;
  limits.max_accessor_elements = kCountLimit;
  limits.max_projection_bytes = kByteLimit;
  return {limits, kRequestBytes};
}
mv::GltfExtractionLimits ExtractionLimits() {
  return {kCountLimit, kCountLimit, kRequestBytes};
}
geo::MeshMaterializationWorkPolicy GeometryWork() {
  return {1, kPartitionBytes, kCheckpointBytes, kMaxJobs};
}
mv::GltfMaterializationLimits TransferLimits() {
  return {kChannelLimit,
          kByteLimit,
          {kCountLimit, kCountLimit, kCountLimit, kCountLimit, kChannelLimit,
           kCountLimit, kCountLimit, kCountLimit, kCountLimit, kByteLimit,
           kRequestBytes},
          {kCountLimit, kRequestBytes}};
}
mv::GltfTopologyOutputInspectionLimits Limits() {
  return {kByteLimit,  kCountLimit,   kDepthLimit,  kCountLimit, kByteLimit,
          kCountLimit, kChannelLimit, kCountLimit,  kByteLimit,  kCountLimit,
          kCountLimit, kByteLimit,    kRequestBytes};
}
mv::GltfTopologyOutputInspectionWorkPolicy Work(bool serial) {
  return {serial ? static_cast<std::uint64_t>(
                       std::numeric_limits<std::ptrdiff_t>::max())
                 : 1,
          kPartitionBytes, kCheckpointBytes, kMaxJobs};
}
mv::GltfAsset Capture(std::span<const std::byte> json,
                      std::span<const mv::SuppliedGltfResource> resources,
                      const geo::ExecutionContext& context) {
  const auto result = mv::ReadGltfAssetCapped(
      json, resources, ReadLimits(), {1, kPartitionBytes, 1, 64}, context);
  Require(result.asset.has_value(), "independent capped fixture capture");
  Require(result.asset->NewRequestedBytes().has_value(),
          "capped owner is attributable");
  return *result.asset;
}
mv::GltfPrimitiveExtraction Extract(const mv::GltfAsset& asset,
                                    std::uint64_t ordinal, bool convert,
                                    bool normalized) {
  const auto primitive = asset.Primitive(ordinal);
  Require(primitive.has_value(), "independent selected primitive ordinal");
  const auto result = mv::ExtractGltfPrimitiveCapped(
      asset, *primitive,
      {convert ? mv::GltfTriangleRequest::kConvertStripOrFan
               : mv::GltfTriangleRequest::kRequireTriangles,
       normalized ? mv::GltfValueRequest::kRawAndNormalized
                  : mv::GltfValueRequest::kRaw},
      ExtractionLimits());
  Require(
      result.extraction.has_value(),
      "independent additive extraction ordinal=" + std::to_string(ordinal) +
          " convert=" + std::to_string(convert) +
          " normalized=" + std::to_string(normalized) + " status=" +
          std::to_string(static_cast<int>(result.diagnostic.status)) +
          " code=" + std::to_string(static_cast<int>(result.diagnostic.code)) +
          " rule=" + std::to_string(result.diagnostic.rule_id));
  return *result.extraction;
}
mv::GltfMaterializedPrimitive Source(const authored::Fixture& fixture,
                                     const geo::ExecutionContext& context) {
  std::vector<mv::SuppliedGltfResource> resources;
  if (!fixture.external_buffer.empty())
    resources.push_back(
        {mv::GltfResourceKind::kBuffer, 0, fixture.external_buffer});
  const auto asset =
      Capture(std::as_bytes(std::span(fixture.json)), resources, context);
  const auto extraction =
      Extract(asset, fixture.primitive_ordinal,
              fixture.kind == authored::FixtureKind::kStrip ||
                  fixture.kind == authored::FixtureKind::kFan);
  Require(std::ranges::equal(extraction.TriangleIndices(),
                             fixture.source_triangles),
          "literal authored directed triangles precede writer invocation");
  const auto result = mv::MaterializeGltfPrimitive(extraction, TransferLimits(),
                                                   GeometryWork());
  Require(result.primitive.has_value(),
          "independent source canonical transfer");
  return *result.primitive;
}
geo::MeshInspection Inspect(const geo::MaterializedMesh& mesh) {
  const auto result = geo::InspectMaterializedMesh(
      mesh, TransferLimits().inspection, GeometryWork());
  Require(result.inspection.has_value(),
          "caller supplies actual Geometry inspection");
  return *result.inspection;
}
mv::GltfTopologyWriteBundle Write(const mv::GltfMaterializedPrimitive& source,
                                  const geo::MeshInspection& candidate,
                                  std::span<const geo::index_t> faces,
                                  std::span<const geo::index_t> corners) {
  const mv::GltfTopologyWriteLimits limits{
      kByteLimit,
      kCountLimit,
      kDepthLimit,
      kCountLimit,
      kChannelLimit,
      kCountLimit,
      kByteLimit,
      kCountLimit,
      kCountLimit,
      kByteLimit,
      {kByteLimit, kCountLimit, kByteLimit, kByteLimit, kRequestBytes}};
  const auto result = mv::WriteGltfTopologySelection(
      {source, candidate, faces, corners}, limits,
      {1, kPartitionBytes, kCheckpointBytes, kMaxJobs});
  Require(result.bundle.has_value(), "actual writer produces test input only");
  return *result.bundle;
}
mv::GltfAsset Reload(const mv::GltfTopologyWriteBundle& bundle,
                     const geo::ExecutionContext& context) {
  std::vector<mv::SuppliedGltfResource> resources;
  for (const auto& resource : bundle.Resources())
    if (resource.origin == mv::GltfResourceOrigin::kCallerBinding)
      resources.push_back(
          {resource.kind, resource.table_index, resource.bytes});
  return Capture(bundle.Json(), resources, context);
}
void CheckComplete(const mv::GltfTopologyOutputInspectionResult& result,
                   bool serial) {
  Require(result.status == Status::kSuccess && result.inspection.has_value(),
          "complete independent inspection: status=" +
              std::to_string(static_cast<int>(result.status)) + " code=" +
              std::to_string(static_cast<int>(result.diagnostic.code)) +
              " stage=" +
              std::to_string(static_cast<int>(result.diagnostic.stage)) +
              " resource=" +
              (result.diagnostic.resource
                   ? std::to_string(*result.diagnostic.resource)
                   : "absent") +
              " element=" +
              (result.diagnostic.element
                   ? std::to_string(*result.diagnostic.element)
                   : "absent"));
  const auto& coverage = result.coverage;
  for (const auto section :
       std::array{coverage.source_candidate, coverage.correspondence,
                  coverage.lexical, coverage.resources,
                  coverage.reloaded_primitive, coverage.provenance})
    Require(section == mv::GltfTopologyOutputCheckCoverage::kComplete,
            "whole named inventory coverage");
  const auto& execution = result.execution;
  for (const auto phase :
       std::array{execution.source_candidate, execution.correspondence,
                  execution.lexical, execution.resources,
                  execution.reloaded_primitive, execution.provenance}) {
    Require(phase.launched_workers == phase.joined_workers,
            "every real inspection worker joined");
    if (serial)
      Require(phase.launched_workers == 0,
              "serial policy has no native launches");
  }
}
mv::GltfTopologyOutputInspectionResult CheckRejected(
    const mv::GltfTopologyOutputInspectionRequest& request, Status status,
    Code code, Stage stage,
    const mv::GltfTopologyOutputInspectionLimits& limits,
    std::stop_token stop) {
  const auto context = request.source.Mesh().ShareExecutionContext();
  const auto baseline = context.ActiveTrackedPayload();
  const auto result =
      mv::InspectGltfTopologyOutput(request, limits, Work(), stop);
  Require(
      !result.inspection && result.status == status &&
          result.diagnostic.code == code && result.diagnostic.stage == stage,
      "exact first failure: status=" +
          std::to_string(static_cast<int>(result.status)) + " code=" +
          std::to_string(static_cast<int>(result.diagnostic.code)) + " stage=" +
          std::to_string(static_cast<int>(result.diagnostic.stage)));
  Require(
      context.ActiveTrackedPayload() == baseline &&
          context.ActiveWorkers() == 0,
      "failed inspection restores exact retained ledger and worker baseline");
  const auto& coverage = result.coverage;
  const std::array sections{coverage.source_candidate,
                            coverage.correspondence,
                            coverage.lexical,
                            coverage.resources,
                            coverage.reloaded_primitive,
                            coverage.provenance};
  const auto prefix =
      stage == Stage::kAdmission ? 0 : static_cast<std::size_t>(stage) - 1;
  if (status != Status::kCanceled && status != Status::kBudgetExceeded &&
      status != Status::kLimitExceeded)
    for (std::size_t i = 0; i < sections.size(); ++i)
      Require(
          sections[i] ==
              (i < prefix ? mv::GltfTopologyOutputCheckCoverage::kComplete
                          : mv::GltfTopologyOutputCheckCoverage::kNotPerformed),
          "only fully checked prefix survives failure");
  return result;
}
}  // namespace gltf_workflow_inspection
