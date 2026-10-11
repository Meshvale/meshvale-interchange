// SPDX-License-Identifier: Apache-2.0
#include "fixture_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace gltf_materialization_semantics {
namespace {
constexpr std::uint64_t kMebibyte = 1024 * 1024;
constexpr std::uint64_t kSmallPayloadBytes = 64 * kMebibyte;
// The optional >binary32-precision index fixture owns >16 million XYZ rows.
constexpr std::uint64_t kLargePayloadBytes = 2 * 1024 * kMebibyte;
constexpr std::uint64_t kSmallElements = 4096;
constexpr std::uint64_t kLargeElements = 64 * 1024 * 1024;
constexpr std::uint64_t kChannelCap = 64;
constexpr std::uint64_t kDocumentDepth = 64;
constexpr std::uint64_t kNumberTokenBytes = 128;
constexpr std::uint64_t kWorkerCount = 4;
constexpr std::uint64_t kCopyPartitionBytes = 65536;
constexpr std::uint64_t kCheckpointScalars = 256;
constexpr std::uint64_t kJobCap = 32;
constexpr std::uint64_t kReadPartitionElements = 1024;
constexpr std::uint64_t kMinimumParallelBytes = 1;
constexpr std::uint64_t kMinimumParallelComponents = 1;
constexpr std::size_t kMinimumParallelVertices = 1;

mv::GltfReadLimits ReadLimits(bool large) {
  mv::GltfReadLimits limits;
  limits.max_json_bytes = kMebibyte;
  limits.max_resource_bytes = kMebibyte;
  limits.max_owned_source_resource_bytes = 2 * kMebibyte;
  limits.max_document_nodes = kSmallElements;
  limits.max_document_depth = kDocumentDepth;
  limits.max_decoded_string_bytes = kMebibyte;
  limits.max_numeric_token_bytes = kNumberTokenBytes;
  limits.max_decimal_coefficient_digits = kNumberTokenBytes;
  limits.max_decimal_exponent_digits = kNumberTokenBytes;
  limits.max_table_rows = kSmallElements;
  limits.max_graph_edges = kSmallElements;
  limits.max_instances = kSmallElements;
  limits.max_accessor_elements = large ? kLargeElements : kSmallElements;
  limits.max_projection_bytes = kMebibyte;
  return limits;
}
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
      {kWorkerCount, kMinimumParallelVertices,
       large ? kLargePayloadBytes : kSmallPayloadBytes});
}
geo::MeshMaterializationWorkPolicy Work() {
  return {kMinimumParallelBytes, kCopyPartitionBytes, kCheckpointScalars,
          kJobCap};
}
mv::GltfMaterializationLimits TransferLimits(bool large) {
  const auto count = large ? kLargeElements : kSmallElements;
  const auto bytes = large ? kLargePayloadBytes : kSmallPayloadBytes;
  mv::GltfMaterializationLimits limits;
  limits.max_channels = kChannelCap;
  limits.max_bridge_new_requested_bytes = kMebibyte;
  auto& capture = limits.materialization;
  capture.max_vertices = count;
  capture.max_faces = count;
  capture.max_corners = count;
  capture.max_face_offsets = count;
  capture.max_attributes = kChannelCap;
  capture.max_attribute_scalars = count;
  capture.max_attribute_offsets = count;
  capture.max_presence_bytes = count;
  capture.max_metadata_entries = count;
  capture.max_descriptor_bytes = kMebibyte;
  capture.max_new_requested_bytes = bytes;
  limits.inspection = {count, bytes};
  return limits;
}
mv::GltfPrimitiveExtraction ReadAndExtract(
    const Fixture& fixture, mv::GltfValueRequest values,
    const geo::ExecutionContext& context) {
  const std::array<mv::SuppliedGltfResource, 1> resources{
      mv::SuppliedGltfResource{mv::GltfResourceKind::kBuffer, 0,
                               fixture.bytes}};
  const mv::GltfWorkPolicy read_work{kMinimumParallelBytes, kCopyPartitionBytes,
                                     kMinimumParallelComponents,
                                     kReadPartitionElements};
  std::stop_source old_read_token;
  auto read = mv::ReadGltfAsset(
      {reinterpret_cast<const std::byte*>(fixture.json.data()),
       fixture.json.size()},
      resources, ReadLimits(fixture.large), read_work, context,
      old_read_token.get_token());
  Require(read.asset && read.diagnostic.status == mv::GltfStatus::kAccepted,
          "Independent fixture must pass public reader admission");
  old_read_token.request_stop();
  const auto primitive = read.asset->Primitive(fixture.primitive_ordinal);
  Require(primitive.has_value(), "Selected primitive must exist");
  const mv::GltfExtractionRequest request{
      fixture.convert_strip_or_fan ? mv::GltfTriangleRequest::kConvertStripOrFan
                                   : mv::GltfTriangleRequest::kRequireTriangles,
      values};
  const mv::GltfExtractionLimits limits{
      fixture.large ? kLargeElements : kSmallElements, kSmallElements,
      fixture.large ? kLargePayloadBytes : kSmallPayloadBytes};
  std::stop_source old_extraction_token;
  auto extraction =
      mv::ExtractGltfPrimitive(*read.asset, *primitive, request, limits,
                               old_extraction_token.get_token());
  Require(extraction.extraction &&
              extraction.diagnostic.status == mv::GltfStatus::kAccepted,
          "Independent fixture must pass public typed extraction");
  old_extraction_token.request_stop();
  return *extraction.extraction;
}
mv::GltfMaterializedPrimitive Transfer(const Fixture& fixture,
                                       mv::GltfValueRequest values) {
  const auto context = Context(fixture.large);
  const auto extraction = ReadAndExtract(fixture, values, context);
  auto result = mv::MaterializeGltfPrimitive(
      extraction, TransferLimits(fixture.large), Work(), {});
  Require(result.status == mv::GltfMaterializationStatus::kSuccess &&
              result.primitive,
          "Public bridge must transfer this admitted extraction");
  Require(result.primitive->Inspection().IsStorageValid(),
          "Successful bridge must expose a valid inspection");
  Require(result.execution.capture.launched_workers ==
                  result.execution.capture.joined_workers &&
              result.execution.inspection.launched_workers ==
                  result.execution.inspection.joined_workers &&
              context.ActiveWorkers() == 0,
          "Delegated jobs must be joined before publication");
  return *result.primitive;
}
const geo::AttributeReadView& Attribute(const geo::MeshReadView& mesh,
                                        std::string_view name) {
  const auto found =
      std::find_if(mesh.attributes.begin(), mesh.attributes.end(),
                   [name](const auto& a) { return a.name == name; });
  Require(found != mesh.attributes.end(),
          "Expected canonical channel is absent");
  return *found;
}
const mv::GltfChannelView& Channel(const mv::GltfPrimitiveExtraction& source,
                                   std::string_view semantic) {
  const auto channels = source.Channels();
  const auto found = std::find_if(
      channels.begin(), channels.end(),
      [semantic](const auto& c) { return c.semantic == semantic; });
  Require(found != channels.end(), "Expected retained channel is absent");
  return *found;
}
}  // namespace gltf_materialization_semantics
