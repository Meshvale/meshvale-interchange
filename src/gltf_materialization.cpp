// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/gltf_materialization.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_accessor.h"
#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_materialization_test_internal.h"

namespace meshvale::interchange::gltf_materialization_detail {
namespace {
template <class T>
using Vector = std::vector<T, gltf_detail::Allocator<T>>;
constexpr std::string_view kPositionSemantic = "POSITION";
constexpr char kRejectionMessage[] = "glTF materialization rejected";
constexpr std::size_t kPositionComponentCount = 3;
constexpr std::size_t kTriangleCornerCount = 3;

class Failure : public std::exception {
 public:
  Failure(GltfMaterializationStatus status,
          GltfMaterializationDiagnostic diagnostic) noexcept
      : status_(status), diagnostic_(diagnostic) {}
  [[nodiscard]] GltfMaterializationStatus Status() const noexcept {
    return status_;
  }
  [[nodiscard]] const GltfMaterializationDiagnostic& Diagnostic()
      const noexcept {
    return diagnostic_;
  }
  [[nodiscard]] const char* what() const noexcept override {
    return kRejectionMessage;
  }

 private:
  GltfMaterializationStatus status_;
  GltfMaterializationDiagnostic diagnostic_;
};
[[noreturn]] void Reject(GltfMaterializationStatus status,
                         GltfMaterializationDiagnosticCode code,
                         std::optional<std::uint64_t> channel = std::nullopt) {
  GltfMaterializationDiagnostic diagnostic;
  diagnostic.code = code;
  diagnostic.source_channel = channel;
  throw Failure(status, diagnostic);
}
void ValidateConfiguration(
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work) {
  for (const auto value :
       {limits.max_channels, limits.max_bridge_new_requested_bytes,
        limits.materialization.max_vertices, limits.materialization.max_faces,
        limits.materialization.max_corners,
        limits.materialization.max_face_offsets,
        limits.materialization.max_attributes,
        limits.materialization.max_attribute_scalars,
        limits.materialization.max_attribute_offsets,
        limits.materialization.max_presence_bytes,
        limits.materialization.max_metadata_entries,
        limits.materialization.max_descriptor_bytes,
        limits.materialization.max_new_requested_bytes,
        limits.inspection.max_issues, limits.inspection.max_new_requested_bytes,
        work.parallel_min_bytes, work.partition_bytes,
        work.cancellation_block_scalars, work.max_jobs}) {
    if (value == 0)
      Reject(GltfMaterializationStatus::kInvalidConfiguration,
             GltfMaterializationDiagnosticCode::kRequiredFieldZero);
    if (value > std::numeric_limits<std::size_t>::max())
      Reject(GltfMaterializationStatus::kInvalidConfiguration,
             GltfMaterializationDiagnosticCode::kConfigurationExtent);
  }
}
template <class T>
constexpr GltfComponentType ComponentType() {
  if constexpr (std::is_same_v<T, std::int8_t>)
    return GltfComponentType::kInt8;
  else if constexpr (std::is_same_v<T, std::uint8_t>)
    return GltfComponentType::kUint8;
  else if constexpr (std::is_same_v<T, std::int16_t>)
    return GltfComponentType::kInt16;
  else if constexpr (std::is_same_v<T, std::uint16_t>)
    return GltfComponentType::kUint16;
  else if constexpr (std::is_same_v<T, std::uint32_t>)
    return GltfComponentType::kUint32;
  else {
    static_assert(std::is_same_v<T, float>);
    return GltfComponentType::kFloat32;
  }
}
std::uint32_t Components(const GltfChannelView& channel,
                         std::uint64_t ordinal) {
  switch (channel.shape) {
    case GltfAccessorShape::kScalar:
    case GltfAccessorShape::kVec2:
    case GltfAccessorShape::kVec3:
    case GltfAccessorShape::kVec4:
      break;
    default:
      Reject(GltfMaterializationStatus::kSourceContractFailure,
             GltfMaterializationDiagnosticCode::kSourceDescriptor, ordinal);
  }
  const auto layout = gltf_detail::DescribeAccessorLayout(
      channel.component_type, channel.shape);
  return static_cast<std::uint32_t>(layout.rows * layout.columns);
}
geometry::ScalarIngress Ingress(const GltfChannelView& channel,
                                std::uint32_t components,
                                std::uint64_t ordinal) {
  return std::visit(
      [&](auto values) -> geometry::ScalarIngress {
        using Value = typename decltype(values)::value_type;
        if (channel.component_type != ComponentType<Value>() ||
            channel.element_count >
                std::numeric_limits<std::size_t>::max() / components ||
            values.size() != channel.element_count * components)
          Reject(GltfMaterializationStatus::kSourceContractFailure,
                 GltfMaterializationDiagnosticCode::kSourceDescriptor, ordinal);
        return values;
      },
      channel.raw);
}
GltfMaterializationEncoding Encoding(GltfComponentType type) {
  return type == GltfComponentType::kInt8 || type == GltfComponentType::kInt16
             ? GltfMaterializationEncoding::kSignedIntegerWidened
             : GltfMaterializationEncoding::kExact;
}
GltfMaterializationResult DelegatedFailure(
    GltfMaterializationStage stage, geometry::MaterializationStatus status,
    const geometry::MaterializationDiagnostic& diagnostic,
    const GltfMaterializationExecution& execution) {
  GltfMaterializationResult result;
  result.status = GltfMaterializationStatus::kGeometryFailure;
  result.diagnostic.stage = stage;
  result.diagnostic.code = GltfMaterializationDiagnosticCode::kGeometry;
  result.diagnostic.geometry_status = status;
  result.diagnostic.geometry_diagnostic = diagnostic;
  result.execution = execution;
  return result;
}
}  // namespace

struct PrimitiveState {
  PrimitiveState(const gltf_detail::AllocationContext& operation,
                 const GltfPrimitiveExtraction& source,
                 const geometry::MeshInspection& inspected,
                 Vector<GltfMaterializedChannel> channel_mappings)
      : allocation(operation),
        extraction(source),
        asset(gltf_detail::Access::ExtractionAsset(source)),
        primitive(gltf_detail::Access::ExtractionPrimitive(source)),
        inspection(inspected),
        mappings(std::move(channel_mappings)) {}
  gltf_detail::AllocationContext allocation;
  GltfPrimitiveExtraction extraction;
  GltfAsset asset;
  GltfPrimitiveHandle primitive;
  geometry::MeshInspection inspection;
  Vector<GltfMaterializedChannel> mappings;
};
struct Access {
  static GltfMaterializedPrimitive Make(
      std::shared_ptr<const PrimitiveState> state) noexcept {
    return GltfMaterializedPrimitive(std::move(state));
  }
};

GltfMaterializationResult TestMaterializeGltfPrimitive(
    const GltfPrimitiveExtraction& extraction,
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop,
    const TestHooks& hooks) {
  GltfMaterializationExecution execution;
  auto stage = GltfMaterializationStage::kBridge;
  try {
    ValidateConfiguration(limits, work);
    const auto channels = extraction.Channels();
    if (channels.size() > limits.max_channels)
      Reject(GltfMaterializationStatus::kLimitExceeded,
             GltfMaterializationDiagnosticCode::kChannelLimit);
    const auto triangles = extraction.TriangleIndices();
    if (channels.empty() || triangles.size() % kTriangleCornerCount != 0 ||
        triangles.size() != extraction.SourceCorners().size())
      Reject(GltfMaterializationStatus::kSourceContractFailure,
             GltfMaterializationDiagnosticCode::kSourceDescriptor);
    const auto& context = gltf_detail::Access::ExtractionContext(extraction);
    gltf_detail::AllocationContext allocation(context, stop, hooks.allocation);
    allocation.Checkpoint();
    allocation = allocation.WithRequestLimit(
        static_cast<std::size_t>(limits.max_bridge_new_requested_bytes));
    Vector<geometry::AttributeMaterializationInput> attributes{
        gltf_detail::Allocator<geometry::AttributeMaterializationInput>(
            allocation)};
    Vector<GltfMaterializedChannel> mappings{
        gltf_detail::Allocator<GltfMaterializedChannel>(allocation)};
    if (channels.size() > mappings.max_size() ||
        channels.size() - 1 > attributes.max_size())
      Reject(GltfMaterializationStatus::kLimitExceeded,
             GltfMaterializationDiagnosticCode::kBridgeRequestLimit);
    mappings.reserve(channels.size());
    attributes.reserve(channels.size() - 1);
    geometry::MeshMaterializationInput input;
    input.topology = geometry::TriangleTopologyInput{triangles};
    const auto vertex_count = channels.front().element_count;
    bool position_seen = false;
    for (std::size_t ordinal = 0; ordinal < channels.size(); ++ordinal) {
      if (ordinal % work.cancellation_block_scalars == 0)
        allocation.Checkpoint();
      const auto& channel = channels[ordinal];
      if (channel.element_count != vertex_count || channel.semantic.empty())
        Reject(GltfMaterializationStatus::kSourceContractFailure,
               GltfMaterializationDiagnosticCode::kSourceDescriptor, ordinal);
      const auto components = Components(channel, ordinal);
      auto values = Ingress(channel, components, ordinal);
      GltfMaterializedChannel mapping;
      mapping.source_channel = ordinal;
      if (channel.semantic == kPositionSemantic) {
        if (position_seen || components != kPositionComponentCount ||
            channel.component_type != GltfComponentType::kFloat32 ||
            channel.normalized)
          Reject(GltfMaterializationStatus::kSourceContractFailure,
                 GltfMaterializationDiagnosticCode::kSourceDescriptor, ordinal);
        input.positions = std::get<std::span<const float>>(values);
        position_seen = true;
        mapping.destination = GltfMaterializationDestination::kPositions;
        mapping.encoding = GltfMaterializationEncoding::kFloatPositionWidened;
      } else {
        if (channel.set_index &&
            *channel.set_index > std::numeric_limits<std::uint32_t>::max())
          Reject(GltfMaterializationStatus::kUnsupportedRepresentation,
                 GltfMaterializationDiagnosticCode::kSetIndexRepresentation,
                 ordinal);
        geometry::AttributeMaterializationInput attribute;
        attribute.domain = geometry::AttributeDomain::vertex;
        attribute.name = channel.semantic;
        attribute.semantic = channel.semantic;
        if (channel.set_index)
          attribute.set_index = static_cast<std::uint32_t>(*channel.set_index);
        attribute.components = components;
        attribute.values = values;
        mapping.destination = GltfMaterializationDestination::kVertexAttribute;
        mapping.attribute = attributes.size();
        mapping.encoding = Encoding(channel.component_type);
        attributes.push_back(attribute);
      }
      mappings.push_back(mapping);
      if (hooks.after_descriptor)
        hooks.after_descriptor(ordinal, hooks.descriptor_state);
    }
    if (!position_seen)
      Reject(GltfMaterializationStatus::kSourceContractFailure,
             GltfMaterializationDiagnosticCode::kSourceDescriptor);
    input.attributes = attributes;
    allocation.Checkpoint();
    stage = GltfMaterializationStage::kCapture;
    auto captured = geometry::MaterializeMesh(input, limits.materialization,
                                              work, context, stop);
    execution.capture = captured.execution;
    if (captured.status != geometry::MaterializationStatus::kSuccess)
      return DelegatedFailure(stage, captured.status, captured.diagnostic,
                              execution);
    stage = GltfMaterializationStage::kInspection;
    auto inspected = geometry::InspectMaterializedMesh(
        *captured.mesh, limits.inspection, work, stop);
    execution.inspection = inspected.execution;
    if (inspected.status != geometry::MaterializationStatus::kSuccess)
      return DelegatedFailure(stage, inspected.status, inspected.diagnostic,
                              execution);
    if (!inspected.inspection->IsStorageValid()) {
      auto result = DelegatedFailure(stage, inspected.status,
                                     inspected.diagnostic, execution);
      result.status = GltfMaterializationStatus::kInvalidStorage;
      result.diagnostic.code =
          GltfMaterializationDiagnosticCode::kStorageIssues;
      result.diagnostic.storage_issue = inspected.inspection->Issues().front();
      return result;
    }
    stage = GltfMaterializationStage::kPublication;
    auto state = std::allocate_shared<PrimitiveState>(
        gltf_detail::Allocator<PrimitiveState>(allocation), allocation,
        extraction, *inspected.inspection, std::move(mappings));
    allocation.Checkpoint();
    GltfMaterializationResult result;
    result.status = GltfMaterializationStatus::kSuccess;
    result.primitive = Access::Make(std::move(state));
    result.diagnostic.stage = stage;
    result.execution = execution;
    return result;
  } catch (const Failure& failure) {
    GltfMaterializationResult result;
    result.status = failure.Status();
    result.diagnostic = failure.Diagnostic();
    result.execution = execution;
    return result;
  } catch (const gltf_detail::AdmissionFailure& failure) {
    GltfMaterializationResult result;
    result.diagnostic.stage = stage;
    result.execution = execution;
    switch (failure.Code()) {
      case gltf_detail::AdmissionCode::kSizeLimit:
        result.status = GltfMaterializationStatus::kLimitExceeded;
        result.diagnostic.code =
            GltfMaterializationDiagnosticCode::kBridgeRequestLimit;
        break;
      case gltf_detail::AdmissionCode::kBudgetExceeded:
        result.status = GltfMaterializationStatus::kBudgetExceeded;
        result.diagnostic.code =
            GltfMaterializationDiagnosticCode::kRequestBudget;
        break;
      case gltf_detail::AdmissionCode::kCanceled:
        result.status = GltfMaterializationStatus::kCanceled;
        result.diagnostic.code =
            GltfMaterializationDiagnosticCode::kStopRequested;
        break;
      case gltf_detail::AdmissionCode::kInvalidObject:
        throw;
    }
    return result;
  }
}
}  // namespace meshvale::interchange::gltf_materialization_detail

namespace meshvale::interchange {
GltfMaterializedPrimitive::GltfMaterializedPrimitive(
    std::shared_ptr<const gltf_materialization_detail::PrimitiveState>
        state) noexcept
    : owner_(std::move(state)) {}
GltfMaterializedPrimitive::GltfMaterializedPrimitive(
    const GltfMaterializedPrimitive&) noexcept = default;
GltfMaterializedPrimitive& GltfMaterializedPrimitive::operator=(
    const GltfMaterializedPrimitive&) noexcept = default;
GltfMaterializedPrimitive::~GltfMaterializedPrimitive() noexcept = default;
const geometry::MaterializedMesh& GltfMaterializedPrimitive::Mesh()
    const noexcept {
  return owner_->inspection.Source();
}
const geometry::MeshInspection& GltfMaterializedPrimitive::Inspection()
    const noexcept {
  return owner_->inspection;
}
const GltfPrimitiveExtraction& GltfMaterializedPrimitive::SourceExtraction()
    const noexcept {
  return owner_->extraction;
}
const GltfAsset& GltfMaterializedPrimitive::SourceAsset() const noexcept {
  return owner_->asset;
}
const GltfPrimitiveHandle& GltfMaterializedPrimitive::SourcePrimitive()
    const noexcept {
  return owner_->primitive;
}
std::span<const GltfMaterializedChannel>
GltfMaterializedPrimitive::ChannelMappings() const noexcept {
  return owner_->mappings;
}
GltfMaterializationRequestedBytes GltfMaterializedPrimitive::NewRequestedBytes()
    const noexcept {
  return {owner_->allocation.OperationActiveBytes(),
          owner_->inspection.Source().NewRequestedBytes(),
          owner_->inspection.NewRequestedBytes()};
}
GltfMaterializationResult MaterializeGltfPrimitive(
    const GltfPrimitiveExtraction& extraction,
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop) {
  return gltf_materialization_detail::TestMaterializeGltfPrimitive(
      extraction, limits, work, stop, {});
}
}  // namespace meshvale::interchange
