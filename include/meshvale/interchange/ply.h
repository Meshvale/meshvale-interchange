// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PLY_H_
#define MESHVALE_INTERCHANGE_PLY_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

namespace meshvale::interchange {
namespace ply_detail {
struct DocumentState;
struct OutputState;
struct Access;
}  // namespace ply_detail

enum class PlyEncoding {
  kInvalid,
  kAscii,
  kBinaryLittleEndian,
  kBinaryBigEndian
};
enum class PlyScalarType {
  kInvalid,
  kChar,
  kUchar,
  kShort,
  kUshort,
  kInt,
  kUint,
  kFloat,
  kDouble
};
enum class PlyElementKind { kVertex, kFace };
enum class PlyPropertyKind { kScalar, kList };
enum class PlyBinding { kX, kY, kZ, kVertexIndices, kAttribute };
enum class PlyStatus {
  kSuccess,
  kInvalidConfiguration,
  kMalformed,
  kUnsupportedVariant,
  kUnsupportedBinding,
  kUnsupportedNumericConversion,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled,
  kInvalidStorage,
  kGeometryFailure
};
enum class PlyStage {
  kConfiguration,
  kScan,
  kDecode,
  kMaterialization,
  kInspection,
  kValidation,
  kEncode,
  kPublication
};
enum class PlyDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kSourceLimit,
  kHeaderLimit,
  kHeaderLineLimit,
  kLineLimit,
  kTokenLimit,
  kCommentLimit,
  kNameLimit,
  kTotalNameLimit,
  kPropertyLimit,
  kVertexLimit,
  kFaceLimit,
  kCornerLimit,
  kPerFaceCornerLimit,
  kListRowLimit,
  kListItemLimit,
  kPropertyValueLimit,
  kOutputLimit,
  kSyntax,
  kIntegerSyntax,
  kIntegerRange,
  kNumericSyntax,
  kNumericRange,
  kNonfinite,
  kVariant,
  kDuplicateName,
  kMissingCoreProperty,
  kTrailingData,
  kVertexRange,
  kFaceTooShort,
  kSchemaBinding,
  kNumericConversion,
  kRewritePolicy,
  kEncoding,
  kRequestBudget,
  kStopRequested,
  kGeometry,
  kStorageIssues
};
struct PlyDiagnostic {
  PlyStage stage = PlyStage::kConfiguration;
  PlyDiagnosticCode code = PlyDiagnosticCode::kNone;
  std::optional<std::uint64_t> byte_offset;
  std::optional<std::uint64_t> element;
  std::optional<std::uint64_t> property;
  std::optional<std::uint64_t> row;
  std::optional<std::uint64_t> item;
  std::optional<std::uint64_t> attribute;
  std::optional<geometry::MaterializationStatus> geometry_status;
  std::optional<geometry::MaterializationDiagnostic> geometry_diagnostic;
  std::optional<geometry::StorageIssue> storage_issue;
};

// Required positive inclusive limits. Aggregate allowances cover checked sums
// of fresh request caps, not cumulative traffic or process memory.
struct PlyReadLimits {
  std::uint64_t max_source_bytes = 0;
  std::uint64_t max_header_bytes = 0;
  std::uint64_t max_header_lines = 0;
  std::uint64_t max_line_bytes = 0;
  std::uint64_t max_token_bytes = 0;
  std::uint64_t max_comment_bytes = 0;
  std::uint64_t max_name_bytes = 0;
  std::uint64_t max_total_name_bytes = 0;
  std::uint64_t max_properties = 0;
  std::uint64_t max_vertices = 0;
  std::uint64_t max_faces = 0;
  std::uint64_t max_corners = 0;
  std::uint64_t max_corners_per_face = 0;
  std::uint64_t max_list_rows = 0;
  std::uint64_t max_list_items_per_row = 0;
  std::uint64_t max_property_values = 0;
  std::uint64_t max_codec_new_requested_bytes = 0;
  std::uint64_t max_total_new_requested_bytes = 0;
  geometry::MeshMaterializationLimits materialization;
  geometry::MeshInspectionLimits inspection;
};
struct PlyWriteLimits {
  std::uint64_t max_header_bytes = 0;
  std::uint64_t max_header_lines = 0;
  std::uint64_t max_line_bytes = 0;
  std::uint64_t max_token_bytes = 0;
  std::uint64_t max_name_bytes = 0;
  std::uint64_t max_total_name_bytes = 0;
  std::uint64_t max_properties = 0;
  std::uint64_t max_vertices = 0;
  std::uint64_t max_faces = 0;
  std::uint64_t max_corners = 0;
  std::uint64_t max_corners_per_face = 0;
  std::uint64_t max_list_rows = 0;
  std::uint64_t max_list_items_per_row = 0;
  std::uint64_t max_property_values = 0;
  std::uint64_t max_codec_new_requested_bytes = 0;
  std::uint64_t max_total_new_requested_bytes = 0;
  std::uint64_t max_output_bytes = 0;
  geometry::MeshInspectionLimits inspection;
};
struct PlyWorkPolicy {
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t cancellation_block_values = 0;
  std::uint64_t cancellation_block_records = 0;
  geometry::MeshMaterializationWorkPolicy geometry;
};
struct PlyExecution {
  // Serial codec observations are zero; delegated observations are actual.
  geometry::MaterializationExecutionStats codec;
  geometry::MaterializationExecutionStats materialization;
  geometry::MaterializationExecutionStats inspection;
};
struct PlyRequestedBytes {
  std::uint64_t codec = 0;
  std::uint64_t materialization = 0;
  std::uint64_t inspection = 0;
  std::uint64_t total = 0;
};
struct PlyPropertyView {
  std::string_view name;
  PlyPropertyKind kind = PlyPropertyKind::kScalar;
  PlyScalarType value_type = PlyScalarType::kInvalid;
  std::optional<PlyScalarType> count_type;
  PlyBinding binding = PlyBinding::kAttribute;
  std::optional<geometry::index_t> attribute;
};
struct PlyElementView {
  PlyElementKind kind = PlyElementKind::kVertex;
  std::uint64_t original_count = 0;
  std::span<const PlyPropertyView> properties;
};

// Copies allocate nothing and share immutable charged storage. Views require a
// live unreassigned document; independent copies support concurrent reads.
// Source bytes, schema, mesh and inspection survive original caller/context
// death.
class PlyDocument {
 public:
  PlyDocument(const PlyDocument&) noexcept;
  PlyDocument& operator=(const PlyDocument&) noexcept;
  ~PlyDocument() noexcept;
  [[nodiscard]] PlyEncoding Encoding() const noexcept;
  [[nodiscard]] std::span<const std::byte> OriginalBytes() const noexcept;
  [[nodiscard]] std::span<const PlyElementView> Schema() const noexcept;
  [[nodiscard]] const geometry::MaterializedMesh& Mesh() const noexcept;
  [[nodiscard]] const geometry::MeshInspection& Inspection() const noexcept;
  [[nodiscard]] PlyRequestedBytes NewRequestedBytes() const noexcept;

 private:
  friend struct ply_detail::Access;
  explicit PlyDocument(
      std::shared_ptr<const ply_detail::DocumentState>) noexcept;
  std::shared_ptr<const ply_detail::DocumentState> state_;
};

enum class PlyRewritePolicy { kInvalid, kCanonical };
enum class PlyCorrespondence { kInvalid, kIdentity };
struct PlyWriteOptions {
  PlyEncoding encoding = PlyEncoding::kInvalid;
  PlyRewritePolicy rewrite_policy = PlyRewritePolicy::kInvalid;
};
struct PlyWriteFacts {
  PlyEncoding source_encoding = PlyEncoding::kInvalid;
  std::uint64_t source_vertex_count = 0;
  std::uint64_t source_face_count = 0;
  PlyEncoding encoding = PlyEncoding::kInvalid;
  PlyRewritePolicy rewrite_policy = PlyRewritePolicy::kInvalid;
  geometry::index_t vertex_count = 0;
  geometry::index_t face_count = 0;
  geometry::index_t corner_count = 0;
  std::uint64_t property_count = 0;
  // Ordinal i maps to i at this supplied-mesh serialization boundary only.
  PlyCorrespondence correspondence = PlyCorrespondence::kInvalid;
};
class PlyOutput {
 public:
  PlyOutput(const PlyOutput&) noexcept;
  PlyOutput& operator=(const PlyOutput&) noexcept;
  ~PlyOutput() noexcept;
  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
  [[nodiscard]] const PlyWriteFacts& Facts() const noexcept;
  [[nodiscard]] PlyRequestedBytes NewRequestedBytes() const noexcept;

 private:
  friend struct ply_detail::Access;
  explicit PlyOutput(std::shared_ptr<const ply_detail::OutputState>) noexcept;
  std::shared_ptr<const ply_detail::OutputState> state_;
};
struct PlyReadResult {
  PlyStatus status = PlyStatus::kInvalidConfiguration;
  std::optional<PlyDocument> document;
  PlyDiagnostic diagnostic;
  PlyExecution execution;
};
struct PlyWriteResult {
  PlyStatus status = PlyStatus::kInvalidConfiguration;
  std::optional<PlyOutput> output;
  PlyDiagnostic diagnostic;
  PlyExecution execution;
};

// Borrowed bytes/options remain unchanged/readable through return and joins.
// Expected failures return no owner; unexpected exceptions propagate after
// cleanup.
[[nodiscard]] PlyReadResult ReadPly(std::span<const std::byte> bytes,
                                    const PlyReadLimits& limits,
                                    const PlyWorkPolicy& work,
                                    const geometry::ExecutionContext& context,
                                    std::stop_token stop = {});
// Schema is borrowed through return/joins and may retain a different ledger.
// All fresh requests/output use the supplied mesh ledger. No filesystem effect;
// output retains no mesh, schema document, options or completed inspection
// owner.
[[nodiscard]] PlyWriteResult WritePly(const geometry::MaterializedMesh& mesh,
                                      const PlyDocument& schema_source,
                                      const PlyWriteOptions& options,
                                      const PlyWriteLimits& limits,
                                      const PlyWorkPolicy& work,
                                      std::stop_token stop = {});
}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_PLY_H_
