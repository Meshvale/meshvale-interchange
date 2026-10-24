// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_OFF_H_
#define MESHVALE_INTERCHANGE_OFF_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

namespace meshvale::interchange {
namespace off_detail {
struct DocumentState;
struct OutputState;
struct Access;
}  // namespace off_detail

enum class OffProfile { kInvalid, kAsciiRecords };
enum class OffStatus {
  kSuccess,
  kInvalidConfiguration,
  kMalformed,
  kUnsupportedVariant,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled,
  kInvalidStorage,
  kGeometryFailure
};
enum class OffStage {
  kConfiguration,
  kScan,
  kDecode,
  kMaterialization,
  kInspection,
  kValidation,
  kEncode,
  kPublication
};
enum class OffDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kSourceLimit,
  kVertexLimit,
  kFaceLimit,
  kCornerLimit,
  kPerFaceCornerLimit,
  kLineLimit,
  kTokenLimit,
  kCommentLimit,
  kOutputLimit,
  kSyntax,
  kIntegerSyntax,
  kIntegerRange,
  kNumericSyntax,
  kNumericRange,
  kNonfinite,
  kVariant,
  kUnsupportedPayload,
  kTrailingData,
  kVertexRange,
  kFaceTooShort,
  kUnsupportedAttribute,
  kTextPolicy,
  kEdgeCountPolicy,
  kRequestBudget,
  kStopRequested,
  kGeometry,
  kStorageIssues
};
struct OffDiagnostic {
  OffStage stage = OffStage::kConfiguration;
  OffDiagnosticCode code = OffDiagnosticCode::kNone;
  std::optional<std::uint64_t> byte_offset;
  std::optional<std::uint64_t> vertex;
  std::optional<std::uint64_t> face;
  std::optional<geometry::MaterializationStatus> geometry_status;
  std::optional<geometry::MaterializationDiagnostic> geometry_diagnostic;
  std::optional<geometry::StorageIssue> storage_issue;
};

// Required positive inclusive limits. Total allowance covers the sum of the
// codec/materialization/inspection caps, not process memory or caller ingress.
struct OffReadLimits {
  std::uint64_t max_source_bytes = 0;
  std::uint64_t max_vertices = 0;
  std::uint64_t max_faces = 0;
  std::uint64_t max_corners = 0;
  std::uint64_t max_corners_per_face = 0;
  std::uint64_t max_line_bytes = 0;
  std::uint64_t max_token_bytes = 0;
  std::uint64_t max_comment_bytes = 0;
  std::uint64_t max_codec_new_requested_bytes = 0;
  std::uint64_t max_total_new_requested_bytes = 0;
  geometry::MeshMaterializationLimits materialization;
  geometry::MeshInspectionLimits inspection;
};
struct OffWriteLimits {
  std::uint64_t max_output_bytes = 0;
  std::uint64_t max_vertices = 0;
  std::uint64_t max_faces = 0;
  std::uint64_t max_corners = 0;
  std::uint64_t max_corners_per_face = 0;
  std::uint64_t max_line_bytes = 0;
  std::uint64_t max_token_bytes = 0;
  std::uint64_t max_codec_new_requested_bytes = 0;
  std::uint64_t max_total_new_requested_bytes = 0;
  geometry::MeshInspectionLimits inspection;
};
struct OffWorkPolicy {
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t cancellation_block_tokens = 0;
  std::uint64_t cancellation_block_records = 0;
  geometry::MeshMaterializationWorkPolicy geometry;
};
struct OffExecution {
  // Serial codec work reports zero workers; Geometry observations are actual.
  geometry::MaterializationExecutionStats codec;
  geometry::MaterializationExecutionStats materialization;
  geometry::MaterializationExecutionStats inspection;
};
struct OffRequestedBytes {
  std::uint64_t codec = 0;
  std::uint64_t materialization = 0;
  std::uint64_t inspection = 0;
  std::uint64_t total = 0;
};

// Immutable sharing copies allocate nothing. Views require a live unreassigned
// owner; independent copies support concurrent reads. Original bytes and the
// canonical mesh outlive the caller's source and original execution context.
class OffDocument {
 public:
  OffDocument(const OffDocument&) noexcept;
  OffDocument& operator=(const OffDocument&) noexcept;
  ~OffDocument() noexcept;
  [[nodiscard]] OffProfile Profile() const noexcept;
  [[nodiscard]] std::span<const std::byte> OriginalBytes() const noexcept;
  [[nodiscard]] std::uint64_t DeclaredEdgeCount() const noexcept;
  [[nodiscard]] const geometry::MaterializedMesh& Mesh() const noexcept;
  [[nodiscard]] const geometry::MeshInspection& Inspection() const noexcept;
  [[nodiscard]] OffRequestedBytes NewRequestedBytes() const noexcept;

 private:
  friend struct off_detail::Access;
  explicit OffDocument(
      std::shared_ptr<const off_detail::DocumentState>) noexcept;
  std::shared_ptr<const off_detail::DocumentState> state_;
};

enum class OffTextPolicy { kInvalid, kCanonicalAscii };
enum class OffEdgeCountPolicy { kInvalid, kUnknownZero };
enum class OffCorrespondence { kInvalid, kIdentity };
struct OffWriteOptions {
  OffTextPolicy text_policy = OffTextPolicy::kInvalid;
  OffEdgeCountPolicy edge_count_policy = OffEdgeCountPolicy::kInvalid;
  // Optional borrowed metadata provenance only, not source/edit equivalence.
  // Remains readable through synchronous return and all worker joins.
  const OffDocument* source_document = nullptr;
};
struct OffWriteFacts {
  OffProfile profile = OffProfile::kInvalid;
  geometry::index_t vertex_count = 0;
  geometry::index_t face_count = 0;
  geometry::index_t corner_count = 0;
  OffTextPolicy text_policy = OffTextPolicy::kInvalid;
  OffEdgeCountPolicy edge_count_policy = OffEdgeCountPolicy::kInvalid;
  std::uint64_t emitted_edge_count = 0;
  // At this supplied-mesh serialization boundary, ordinal i maps to i in
  // every domain. These facts do not replace earlier Repair correspondence.
  OffCorrespondence correspondence = OffCorrespondence::kInvalid;
  std::optional<std::uint64_t> source_declared_edge_count;
};
class OffOutput {
 public:
  OffOutput(const OffOutput&) noexcept;
  OffOutput& operator=(const OffOutput&) noexcept;
  ~OffOutput() noexcept;
  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
  [[nodiscard]] const OffWriteFacts& Facts() const noexcept;
  [[nodiscard]] OffRequestedBytes NewRequestedBytes() const noexcept;

 private:
  friend struct off_detail::Access;
  explicit OffOutput(std::shared_ptr<const off_detail::OutputState>) noexcept;
  std::shared_ptr<const off_detail::OutputState> state_;
};
struct OffReadResult {
  OffStatus status = OffStatus::kInvalidConfiguration;
  std::optional<OffDocument> document;
  OffDiagnostic diagnostic;
  OffExecution execution;
};
struct OffWriteResult {
  OffStatus status = OffStatus::kInvalidConfiguration;
  std::optional<OffOutput> output;
  OffDiagnostic diagnostic;
  OffExecution execution;
};

// Bounded supplied-byte base ASCII records; no sniffing, welding or implicit
// triangulation. Borrowed bytes remain unchanged/readable until return.
// Expected failures return no owner. Unexpected exceptions propagate after
// cleanup/joins.
[[nodiscard]] OffReadResult ReadOff(std::span<const std::byte> bytes,
                                    const OffReadLimits& limits,
                                    const OffWorkPolicy& work,
                                    const geometry::ExecutionContext& context,
                                    std::stop_token stop = {});
// Uses the strong source ledger and a fresh stop token. No filesystem effect;
// returned bytes/facts retain no source/options/document/inspection owner.
[[nodiscard]] OffWriteResult WriteOff(const geometry::MaterializedMesh& mesh,
                                      const OffWriteOptions& options,
                                      const OffWriteLimits& limits,
                                      const OffWorkPolicy& work,
                                      std::stop_token stop = {});

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_OFF_H_
