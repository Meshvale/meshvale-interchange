// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_GLTF_MATERIALIZATION_H_
#define MESHVALE_INTERCHANGE_GLTF_MATERIALIZATION_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

namespace meshvale::interchange {
namespace gltf_materialization_detail {
struct PrimitiveState;
struct Access;
}  // namespace gltf_materialization_detail

// Implementation candidate; native and installed qualification remain pending.
// Every limit is required and positive. Channels includes POSITION.
struct GltfMaterializationLimits {
  std::uint64_t max_channels = 0;
  std::uint64_t max_bridge_new_requested_bytes = 0;
  geometry::MeshMaterializationLimits materialization;
  geometry::MeshInspectionLimits inspection;
};

enum class GltfMaterializationDestination { kPositions, kVertexAttribute };
enum class GltfMaterializationEncoding {
  kExact,
  kFloatPositionWidened,
  kSignedIntegerWidened
};
struct GltfMaterializedChannel {
  std::uint64_t source_channel = 0;
  GltfMaterializationDestination destination =
      GltfMaterializationDestination::kPositions;
  std::optional<geometry::index_t> attribute;
  GltfMaterializationEncoding encoding = GltfMaterializationEncoding::kExact;
};

// Separate live post-success operation charges, not a combined cap or peak.
struct GltfMaterializationRequestedBytes {
  std::uint64_t bridge = 0;
  std::uint64_t materialization = 0;
  std::uint64_t inspection = 0;
};
enum class GltfMaterializationStage {
  kBridge,
  kCapture,
  kInspection,
  kPublication
};
enum class GltfMaterializationStatus {
  kSuccess,
  kInvalidConfiguration,
  kLimitExceeded,
  kUnsupportedRepresentation,
  kBudgetExceeded,
  kCanceled,
  kSourceContractFailure,
  kGeometryFailure,
  kInvalidStorage
};
enum class GltfMaterializationDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kChannelLimit,
  kBridgeRequestLimit,
  kSetIndexRepresentation,
  kSourceDescriptor,
  kRequestBudget,
  kStopRequested,
  kGeometry,
  kStorageIssues
};
struct GltfMaterializationDiagnostic {
  GltfMaterializationStage stage = GltfMaterializationStage::kBridge;
  GltfMaterializationDiagnosticCode code =
      GltfMaterializationDiagnosticCode::kNone;
  std::optional<std::uint64_t> source_channel;
  // Actual delegated outcome, including kSuccess for invalid inspected storage.
  std::optional<geometry::MaterializationStatus> geometry_status;
  std::optional<geometry::MaterializationDiagnostic> geometry_diagnostic;
  std::optional<geometry::StorageIssue> storage_issue;
};
struct GltfMaterializationExecution {
  geometry::MaterializationExecutionStats capture;
  geometry::MaterializationExecutionStats inspection;
};

// Immutable copy-only strong owner. Copies share storage/charges without
// allocation. Borrowed references/spans require this owner to remain alive and
// unreassigned; independent copies support concurrent reads.
class GltfMaterializedPrimitive {
 public:
  GltfMaterializedPrimitive(const GltfMaterializedPrimitive&) noexcept;
  GltfMaterializedPrimitive& operator=(
      const GltfMaterializedPrimitive&) noexcept;
  ~GltfMaterializedPrimitive() noexcept;
  [[nodiscard]] const geometry::MaterializedMesh& Mesh() const noexcept;
  [[nodiscard]] const geometry::MeshInspection& Inspection() const noexcept;
  [[nodiscard]] const GltfPrimitiveExtraction& SourceExtraction()
      const noexcept;
  [[nodiscard]] const GltfAsset& SourceAsset() const noexcept;
  [[nodiscard]] const GltfPrimitiveHandle& SourcePrimitive() const noexcept;
  [[nodiscard]] std::span<const GltfMaterializedChannel> ChannelMappings()
      const noexcept;
  [[nodiscard]] GltfMaterializationRequestedBytes NewRequestedBytes()
      const noexcept;

 private:
  friend struct gltf_materialization_detail::Access;
  explicit GltfMaterializedPrimitive(
      std::shared_ptr<
          const gltf_materialization_detail::PrimitiveState>) noexcept;
  std::shared_ptr<const gltf_materialization_detail::PrimitiveState> owner_;
};
struct GltfMaterializationResult {
  GltfMaterializationStatus status =
      GltfMaterializationStatus::kInvalidConfiguration;
  std::optional<GltfMaterializedPrimitive> primitive;
  GltfMaterializationDiagnostic diagnostic;
  GltfMaterializationExecution execution;
};

// Synchronous raw transfer using the extraction's retained context and a fresh
// token. Success publishes complete canonical storage plus valid inspection and
// retained provenance. All started workers join before return/exception.
[[nodiscard]] GltfMaterializationResult MaterializeGltfPrimitive(
    const GltfPrimitiveExtraction& extraction,
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work,
    std::stop_token stop = {});

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_GLTF_MATERIALIZATION_H_
