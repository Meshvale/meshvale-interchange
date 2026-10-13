// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT_H_
#define MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

namespace meshvale::interchange {
namespace gltf_workflow_support_detail {
struct InspectionState;
struct Access;
}  // namespace gltf_workflow_support_detail

// Draft native output-inspection implementation under qualification; link the
// optional meshvale::gltf_workflow_support target. Capped reader/extractor
// declarations belong to gltf.h.
// Expectations come from the original strong source and supplied inspected
// candidate/maps, independently of output writer annotations. Inputs remain
// alive and unchanged until this synchronous call returns.
// Output's original asset/primitive and retained Source() must bind the same
// original asset/primitive owners and indices as source. Reloaded extraction
// must belong to reloaded_asset at the corresponding output primitive indices.
struct GltfTopologyOutputInspectionRequest {
  const GltfMaterializedPrimitive& source;
  const geometry::MeshInspection& candidate;
  std::span<const geometry::index_t> source_faces;
  std::span<const geometry::index_t> source_corners;
  const GltfTopologyWriteBundle& output;
  const GltfAsset& reloaded_asset;
  const GltfPrimitiveExtraction& reloaded_extraction;
};
// Required positive, inclusive and host-representable. JSON, resource,
// scalar and descriptor totals bound each relevant input inventory separately.
struct GltfTopologyOutputInspectionLimits {
  std::uint64_t max_json_bytes = 0;
  std::uint64_t max_json_nodes = 0;
  std::uint64_t max_json_depth = 0;
  std::uint64_t max_resources = 0;
  std::uint64_t max_resource_bytes = 0;
  std::uint64_t max_vertices = 0;
  std::uint64_t max_channels = 0;
  std::uint64_t max_attribute_scalars = 0;
  std::uint64_t max_descriptor_bytes = 0;
  std::uint64_t max_candidate_faces = 0;
  std::uint64_t max_candidate_corners = 0;
  std::uint64_t max_index_bytes = 0;
  std::uint64_t max_new_requested_bytes = 0;
};
struct GltfTopologyOutputInspectionWorkPolicy {
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct GltfTopologyOutputInspectionExecutionStats {
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t caller_jobs = 0;
};
struct GltfTopologyOutputInspectionExecution {
  GltfTopologyOutputInspectionExecutionStats source_candidate;
  GltfTopologyOutputInspectionExecutionStats correspondence;
  GltfTopologyOutputInspectionExecutionStats lexical;
  GltfTopologyOutputInspectionExecutionStats resources;
  GltfTopologyOutputInspectionExecutionStats reloaded_primitive;
  GltfTopologyOutputInspectionExecutionStats provenance;
};
enum class GltfTopologyOutputCheckCoverage { kNotPerformed, kComplete };
// Complete means the entire named inventory passed, never a sampled prefix.
struct GltfTopologyOutputCoverage {
  GltfTopologyOutputCheckCoverage source_candidate =
      GltfTopologyOutputCheckCoverage::kNotPerformed;
  GltfTopologyOutputCheckCoverage correspondence =
      GltfTopologyOutputCheckCoverage::kNotPerformed;
  GltfTopologyOutputCheckCoverage lexical =
      GltfTopologyOutputCheckCoverage::kNotPerformed;
  GltfTopologyOutputCheckCoverage resources =
      GltfTopologyOutputCheckCoverage::kNotPerformed;
  GltfTopologyOutputCheckCoverage reloaded_primitive =
      GltfTopologyOutputCheckCoverage::kNotPerformed;
  GltfTopologyOutputCheckCoverage provenance =
      GltfTopologyOutputCheckCoverage::kNotPerformed;
};
enum class GltfTopologyOutputInspectionStage {
  kAdmission,
  kSourceCandidate,
  kCorrespondence,
  kLexical,
  kResources,
  kReloadedPrimitive,
  kProvenance,
  kPublication
};
enum class GltfTopologyOutputInspectionStatus {
  kSuccess,
  kInvalidConfiguration,
  kLimitExceeded,
  kForeignBudget,
  kInvalidCandidateStorage,
  kInvalidCorrespondence,
  kSourceContractFailure,
  kOutputMismatch,
  kUnsupportedSelection,
  kBudgetExceeded,
  kCanceled
};
enum class GltfTopologyOutputInspectionDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kCountLimit,
  kInspectionRequestLimit,
  kForeignBudget,
  kCandidateStorage,
  kSourceIdentity,
  kReloadIdentity,
  kFaceMapShape,
  kFaceMapOrder,
  kFaceMapRange,
  kCornerMapShape,
  kCornerMapValue,
  kTriangleShape,
  kTriangleLoop,
  kPositionBits,
  kChannelCount,
  kAttributeDescriptor,
  kAttributeBits,
  kEmptyChangedSelection,
  kOutputJson,
  kLexicalPreservation,
  kAppendedBinding,
  kResourceIdentity,
  kResourceBytes,
  kChannelDescriptor,
  kChannelBits,
  kSelectedTopology,
  kSourceProvenance,
  kRequestBudget,
  kStopRequested
};
struct GltfTopologyOutputInspectionDiagnostic {
  GltfTopologyOutputInspectionStage stage =
      GltfTopologyOutputInspectionStage::kAdmission;
  GltfTopologyOutputInspectionDiagnosticCode code =
      GltfTopologyOutputInspectionDiagnosticCode::kNone;
  std::optional<geometry::index_t> attribute;
  std::optional<geometry::index_t> element;
  std::optional<std::uint64_t> resource;
  std::optional<GltfSourceSpan> json_span;
  // First actual issue from the supplied inspection, with no delegated call.
  std::optional<geometry::StorageIssue> storage_issue;
};

// Copy-only immutable verification owner. Copies share storage and charges
// without allocation. Every borrowed view/reference needs an alive,
// unreassigned visible inspection copy. Strong inputs and charged
// map/provenance copies are retained; caller maps and context wrappers may be
// destroyed after return.
class GltfTopologyOutputInspection {
 public:
  GltfTopologyOutputInspection(const GltfTopologyOutputInspection&) noexcept;
  GltfTopologyOutputInspection& operator=(
      const GltfTopologyOutputInspection&) noexcept;
  ~GltfTopologyOutputInspection() noexcept;
  [[nodiscard]] const GltfMaterializedPrimitive& Source() const noexcept;
  [[nodiscard]] const geometry::MeshInspection& Candidate() const noexcept;
  [[nodiscard]] const GltfTopologyWriteBundle& Output() const noexcept;
  [[nodiscard]] const GltfAsset& ReloadedAsset() const noexcept;
  [[nodiscard]] const GltfPrimitiveExtraction& ReloadedExtraction()
      const noexcept;
  [[nodiscard]] std::span<const geometry::index_t> SourceFaces() const noexcept;
  [[nodiscard]] std::span<const geometry::index_t> SourceCorners()
      const noexcept;
  [[nodiscard]] std::span<const GltfSourceCorner> OriginalCorners()
      const noexcept;
  [[nodiscard]] const GltfTopologyOutputCoverage& Coverage() const noexcept;
  // Current live fresh inspection requests, excluding all retained input
  // operations. Includes seed/control/header/padding and replacement overlap.
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct gltf_workflow_support_detail::Access;
  explicit GltfTopologyOutputInspection(
      std::shared_ptr<
          const gltf_workflow_support_detail::InspectionState>) noexcept;
  std::shared_ptr<const gltf_workflow_support_detail::InspectionState> owner_;
};
struct GltfTopologyOutputInspectionResult {
  GltfTopologyOutputInspectionStatus status =
      GltfTopologyOutputInspectionStatus::kInvalidConfiguration;
  std::optional<GltfTopologyOutputInspection> inspection;
  GltfTopologyOutputInspectionDiagnostic diagnostic;
  GltfTopologyOutputCoverage coverage;
  GltfTopologyOutputInspectionExecution execution;
};
// Fresh token, source ledger, one complete inspection request cap. No read,
// extraction, Geometry inspection or filesystem publication occurs here.
// Failure publishes no owner. All started workers join before return/exception;
// actual runtime/allocation exceptions propagate after rollback and joins.
[[nodiscard]] GltfTopologyOutputInspectionResult InspectGltfTopologyOutput(
    const GltfTopologyOutputInspectionRequest& request,
    const GltfTopologyOutputInspectionLimits& limits,
    const GltfTopologyOutputInspectionWorkPolicy& work,
    std::stop_token stop = {});

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT_H_
