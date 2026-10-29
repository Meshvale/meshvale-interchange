// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE_H_
#define MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_deformation.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_write.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

namespace meshvale::interchange {
namespace gltf_topology_write_detail {
struct BundleState;
struct Access;
}  // namespace gltf_topology_write_detail

// Development native interface; link the optional meshvale::gltf_topology_write
// target. See the owning contract for qualification and build boundaries.
// Inputs remain alive and unmodified until the synchronous operation returns.
struct GltfTopologyWriteRequest {
  const GltfMaterializedPrimitive& source;
  const geometry::MeshInspection& candidate;
  // Output triangle/corner -> canonical source triangle/corner, respectively.
  std::span<const geometry::index_t> source_faces;
  std::span<const geometry::index_t> source_corners;
};
// All fields, including nested output fields, are required and positive.
// Inclusive physical counts; source and candidate scalar/descriptor totals are
// checked separately. Channels includes POSITION. No delegated inspection cap.
struct GltfTopologyWriteLimits {
  std::uint64_t max_source_json_bytes = 0;
  std::uint64_t max_source_json_nodes = 0;
  std::uint64_t max_source_json_depth = 0;
  std::uint64_t max_vertices = 0;
  std::uint64_t max_channels = 0;
  std::uint64_t max_attribute_scalars = 0;
  std::uint64_t max_descriptor_bytes = 0;
  std::uint64_t max_candidate_faces = 0;
  std::uint64_t max_candidate_corners = 0;
  std::uint64_t max_index_bytes = 0;
  // max_output_requested_bytes covers this entire fresh writer operation.
  GltfWriteLimits output;
  GltfDirectTranslationLimits direct_translation = {};
};
struct GltfTopologyWriteWorkPolicy {
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
  GltfDirectTranslationWorkPolicy direct_translation = {};
};
struct GltfTopologyWriteExecutionStats {
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t caller_jobs = 0;
};
struct GltfTopologyWriteExecution {
  GltfTopologyWriteExecutionStats verification;
  GltfTopologyWriteExecutionStats index_encoding;
  GltfTopologyWriteExecutionStats base64_encoding;
  GltfTopologyWriteExecutionStats output_copy;
};
enum class GltfTopologyWriteStage {
  kAdmission,
  kVerification,
  kLexicalPlanning,
  kIndexEncoding,
  kBase64Encoding,
  kOutputCopy,
  kPublication
};
enum class GltfTopologyWriteStatus {
  kSuccess,
  kInvalidConfiguration,
  kLimitExceeded,
  kForeignBudget,
  kInvalidCandidateStorage,
  kInvalidCorrespondence,
  kPreservationMismatch,
  kUnsupportedProfile,
  kUnsupportedSelection,
  kSourceContractFailure,
  kBudgetExceeded,
  kCanceled
};
enum class GltfTopologyWriteDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kCountLimit,
  kWriterRequestLimit,
  kForeignBudget,
  kCandidateStorage,
  kSourceCapability,
  kEmptyChangedSelection,
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
  kSourceDescriptor,
  kSourceProvenance,
  kLexicalSource,
  kIndexRepresentation,
  kOutputExtent,
  kRequestBudget,
  kStopRequested,
  kDeformationPolicyRequired = 27,
  kDeformation = 28
};
struct GltfTopologyWriteDiagnostic {
  GltfTopologyWriteStage stage = GltfTopologyWriteStage::kAdmission;
  GltfTopologyWriteDiagnosticCode code = GltfTopologyWriteDiagnosticCode::kNone;
  std::optional<geometry::index_t> attribute;
  std::optional<geometry::index_t> element;
  std::optional<GltfSourceSpan> json_span;
  // First actual issue of the supplied invalid candidate inspection. No new
  // Geometry call or fabricated delegated status accompanies this issue.
  std::optional<geometry::StorageIssue> storage_issue;
};
// Present only when the selected primitive binding changed. Old tables remain
// intact. Modes are the existing public glTF source-mode representation.
struct GltfTopologyBindingChange {
  std::uint64_t source_mode = 0;
  std::optional<std::uint64_t> source_indices_accessor;
  std::uint64_t output_mode = 0;
  std::uint64_t output_indices_accessor = 0;
  std::uint64_t appended_buffer = 0;
  std::uint64_t appended_buffer_view = 0;
};

// Copy-only immutable owner. Copies share storage/charges without allocation.
// Borrowed views/references require an alive, unreassigned visible bundle copy.
// Original handles remain source-bound; public reload creates output handles.
class GltfTopologyWriteBundle {
 public:
  GltfTopologyWriteBundle(const GltfTopologyWriteBundle&) noexcept;
  GltfTopologyWriteBundle& operator=(const GltfTopologyWriteBundle&) noexcept;
  ~GltfTopologyWriteBundle() noexcept;
  [[nodiscard]] std::span<const std::byte> Json() const noexcept;
  [[nodiscard]] std::span<const GltfResourceView> Resources() const noexcept;
  [[nodiscard]] const GltfMaterializedPrimitive& Source() const noexcept;
  [[nodiscard]] const GltfAsset& OriginalAsset() const noexcept;
  [[nodiscard]] const GltfPrimitiveHandle& OriginalPrimitive() const noexcept;
  [[nodiscard]] std::span<const geometry::index_t> SourceFaces() const noexcept;
  [[nodiscard]] std::span<const geometry::index_t> SourceCorners()
      const noexcept;
  // Canonical corner correspondence composed with extraction provenance.
  [[nodiscard]] std::span<const GltfSourceCorner> OriginalCorners()
      const noexcept;
  [[nodiscard]] std::optional<GltfTopologyBindingChange> ChangedBinding()
      const noexcept;
  // Current live fresh requests, including control/header/padding. Neither
  // cumulative allocation traffic, peak observation, nor shared input charges.
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;
  // Actual called validation facts; null for the static selection. Borrowing
  // follows this immutable owner's lifetime, with no allocation/checkpoint.
  [[nodiscard]] const GltfDirectTranslationObservation* DirectTranslationFacts()
      const noexcept;

  [[nodiscard]] const GltfDeformationObservation* DeformationFacts()
      const noexcept;

 private:
  friend struct gltf_topology_write_detail::Access;
  explicit GltfTopologyWriteBundle(
      std::shared_ptr<const gltf_topology_write_detail::BundleState>) noexcept;
  std::shared_ptr<const gltf_topology_write_detail::BundleState> owner_;
};
struct GltfTopologyWriteResult {
  GltfTopologyWriteStatus status =
      GltfTopologyWriteStatus::kInvalidConfiguration;
  std::optional<GltfTopologyWriteBundle> bundle;
  GltfTopologyWriteDiagnostic diagnostic;
  GltfTopologyWriteExecution execution;
  std::optional<GltfDirectTranslationObservation> direct_translation = {};
  std::optional<GltfDeformationObservation> deformation{};
};

// Checked ordered triangle subset, exact unchanged canonical vertices/channels.
// Uses the source ledger and a fresh operation token, with one complete new
// request cap. Candidate inspection/maps are borrowed only until return. All
// started workers join before return/exception. Expected failure has no bundle;
// actual runtime/allocation exceptions propagate after rollback and joins.
[[nodiscard]] GltfTopologyWriteResult WriteGltfTopologySelection(
    const GltfTopologyWriteRequest& request,
    const GltfTopologyWriteLimits& limits,
    const GltfTopologyWriteWorkPolicy& work, std::stop_token stop = {});

// Retained deformation policy overload. All five arguments are required so
// existing three/four-argument brace calls retain their original overload.
// Policies/borrows remain valid through return and all joins. Expected typed
// failures publish no owner; unexpected exceptions propagate after joins.
[[nodiscard]] GltfTopologyWriteResult WriteGltfTopologySelection(
    const GltfTopologyWriteRequest& request,
    const GltfDeformationPolicy& deformation,
    const GltfTopologyWriteLimits& limits,
    const GltfTopologyWriteWorkPolicy& work, std::stop_token stop);

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE_H_
