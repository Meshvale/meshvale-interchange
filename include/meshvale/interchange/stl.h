// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_STL_H_
#define MESHVALE_INTERCHANGE_STL_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

namespace meshvale::interchange {
namespace stl_detail {
struct DocumentState;
struct OutputState;
struct Access;
}  // namespace stl_detail

enum class StlEncoding { kInvalid, kAscii, kBinary };
enum class StlStatus {
  kSuccess,
  kInvalidConfiguration,
  kMalformed,
  kUnsupportedVariant,
  kUnsupportedNumericConversion,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled,
  kInvalidStorage,
  kGeometryFailure
};
enum class StlStage {
  kConfiguration,
  kScan,
  kDecode,
  kMaterialization,
  kInspection,
  kValidation,
  kEncode,
  kPublication
};
enum class StlDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kEncoding,
  kSourceLimit,
  kFacetLimit,
  kNameLimit,
  kLineLimit,
  kTokenLimit,
  kOutputLimit,
  kSyntax,
  kNumericSyntax,
  kNumericRange,
  kNonfinite,
  kBinaryExtent,
  kTrailingBytes,
  kAttributeWord,
  kRequestBudget,
  kStopRequested,
  kGeometry,
  kStorageIssues,
  kNotTriangles,
  kNormalSelection,
  kUnsupportedAttribute,
  kMetadata,
  kExpansionAcknowledgment,
  kNumericConversion
};
struct StlDiagnostic {
  StlStage stage = StlStage::kConfiguration;
  StlDiagnosticCode code = StlDiagnosticCode::kNone;
  std::optional<std::uint64_t> byte_offset;
  std::optional<std::uint64_t> facet;
  std::optional<std::uint64_t> attribute;
  std::optional<geometry::MaterializationStatus> geometry_status;
  std::optional<geometry::MaterializationDiagnostic> geometry_diagnostic;
  std::optional<geometry::StorageIssue> storage_issue;
};

// Required positive inclusive bounds. The total allowance must cover the sum
// of codec/materialization/inspection caps; it is not a process-memory bound.
struct StlReadLimits {
  std::uint64_t max_source_bytes = 0;
  std::uint64_t max_facets = 0;
  std::uint64_t max_name_bytes = 0;
  std::uint64_t max_line_bytes = 0;
  std::uint64_t max_token_bytes = 0;
  std::uint64_t max_codec_new_requested_bytes = 0;
  std::uint64_t max_total_new_requested_bytes = 0;
  geometry::MeshMaterializationLimits materialization;
  geometry::MeshInspectionLimits inspection;
};
struct StlWriteLimits {
  std::uint64_t max_output_bytes = 0;
  std::uint64_t max_facets = 0;
  std::uint64_t max_name_bytes = 0;
  std::uint64_t max_line_bytes = 0;
  std::uint64_t max_token_bytes = 0;
  std::uint64_t max_codec_new_requested_bytes = 0;
  std::uint64_t max_total_new_requested_bytes = 0;
  geometry::MeshInspectionLimits inspection;
};
struct StlWorkPolicy {
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t cancellation_block_tokens = 0;
  std::uint64_t cancellation_block_facets = 0;
  geometry::MeshMaterializationWorkPolicy geometry;
};
struct StlExecution {
  // Serial codec loops report zero launched/joined workers.
  geometry::MaterializationExecutionStats codec;
  geometry::MaterializationExecutionStats materialization;
  geometry::MaterializationExecutionStats inspection;
};
struct StlRequestedBytes {
  std::uint64_t codec = 0;
  std::uint64_t materialization = 0;
  std::uint64_t inspection = 0;
  std::uint64_t total = 0;
};

// Immutable sharing copies allocate nothing. Borrowed views require a live,
// unreassigned owner; independent copies support concurrent reads.
class StlDocument {
 public:
  StlDocument(const StlDocument&) noexcept;
  StlDocument& operator=(const StlDocument&) noexcept;
  ~StlDocument() noexcept;
  [[nodiscard]] StlEncoding Encoding() const noexcept;
  [[nodiscard]] std::span<const std::byte> OriginalBytes() const noexcept;
  [[nodiscard]] std::span<const std::byte> BinaryHeader() const noexcept;
  [[nodiscard]] std::string_view AsciiName() const noexcept;
  [[nodiscard]] const geometry::MaterializedMesh& Mesh() const noexcept;
  [[nodiscard]] const geometry::MeshInspection& Inspection() const noexcept;
  [[nodiscard]] StlRequestedBytes NewRequestedBytes() const noexcept;

 private:
  friend struct stl_detail::Access;
  explicit StlDocument(
      std::shared_ptr<const stl_detail::DocumentState>) noexcept;
  std::shared_ptr<const stl_detail::DocumentState> state_;
};

enum class StlMetadataPolicy {
  kExplicitWithoutSource,
  kRetainedSourceMetadata,
  kReplacedSourceMetadata
};
struct StlWriteOptions {
  StlEncoding encoding = StlEncoding::kInvalid;
  std::span<const std::byte> binary_header;
  std::string_view ascii_name;
  geometry::index_t face_normal_attribute =
      std::numeric_limits<geometry::index_t>::max();
  bool acknowledge_corner_expansion = false;
  // Optional metadata provenance, not a claim of source/candidate equivalence.
  // Borrowed with header/name through synchronous return and all joins.
  const StlDocument* source_document = nullptr;
};
struct StlWriteFacts {
  StlEncoding encoding = StlEncoding::kInvalid;
  geometry::index_t face_normal_attribute = 0;
  geometry::index_t source_vertex_count = 0;
  geometry::index_t source_face_count = 0;
  bool corner_expansion_acknowledged = false;
  std::optional<StlEncoding> source_encoding;
  StlMetadataPolicy metadata_policy = StlMetadataPolicy::kExplicitWithoutSource;
};
class StlOutput {
 public:
  StlOutput(const StlOutput&) noexcept;
  StlOutput& operator=(const StlOutput&) noexcept;
  ~StlOutput() noexcept;
  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
  [[nodiscard]] const StlWriteFacts& Facts() const noexcept;
  [[nodiscard]] std::span<const geometry::index_t> CornerSourceVertices()
      const noexcept;
  [[nodiscard]] StlRequestedBytes NewRequestedBytes() const noexcept;

 private:
  friend struct stl_detail::Access;
  explicit StlOutput(std::shared_ptr<const stl_detail::OutputState>) noexcept;
  std::shared_ptr<const stl_detail::OutputState> state_;
};
struct StlReadResult {
  StlStatus status = StlStatus::kInvalidConfiguration;
  std::optional<StlDocument> document;
  StlDiagnostic diagnostic;
  StlExecution execution;
};
struct StlWriteResult {
  StlStatus status = StlStatus::kInvalidConfiguration;
  std::optional<StlOutput> output;
  StlDiagnostic diagnostic;
  StlExecution execution;
};

// No implicit welding/encoding detection. Expected failures return no partial
// owner. Unexpected exceptions propagate after cleanup and all worker joins.
[[nodiscard]] StlReadResult ReadStl(std::span<const std::byte> bytes,
                                    StlEncoding encoding,
                                    const StlReadLimits& limits,
                                    const StlWorkPolicy& work,
                                    const geometry::ExecutionContext& context,
                                    std::stop_token stop = {});
// Uses the strong mesh's retained execution ledger with a fresh stop token.
[[nodiscard]] StlWriteResult WriteStl(const geometry::MaterializedMesh& mesh,
                                      const StlWriteOptions& options,
                                      const StlWriteLimits& limits,
                                      const StlWorkPolicy& work,
                                      std::stop_token stop = {});

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_STL_H_
