// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_STL_FILES_H_
#define MESHVALE_INTERCHANGE_STL_FILES_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <variant>

namespace meshvale::interchange {
// SHA-256 digest extent (FIPS 180-4) and opaque binary STL header extent.
inline constexpr std::size_t kStlFileSha256Bytes = 32;
inline constexpr std::size_t kStlFileBinaryHeaderBytes = 80;
namespace stl_files_detail {
struct CaptureState;
struct AssetState;
struct VerifiedState;
struct BundleState;
struct Access;
}  // namespace stl_files_detail

// All integer inputs are required positive inclusive bounds; zero never
// disables a cap. File requests include accounting and overlapping storage.
struct StlFileLimits {
  std::uint64_t max_entry_bytes = 0;
  std::uint64_t max_receipt_bytes = 0;
  std::uint64_t max_total_file_bytes = 0;
  std::uint64_t max_entry_name_bytes = 0;
  std::uint64_t max_native_path_code_units = 0;
  std::uint64_t max_unit_label_bytes = 0;
  std::uint64_t max_caller_report_bytes = 0;
  std::uint64_t max_file_new_requested_bytes = 0;
};
struct StlFileWorkPolicy {
  std::uint64_t io_block_bytes = 0;
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct StlFileReadLimits {
  StlFileLimits files;
  StlReadLimits read;
};
struct StlFileReadWorkPolicy {
  StlFileWorkPolicy files;
  StlWorkPolicy read;
};
struct StlFileRewriteLimits {
  StlFileLimits files;
  StlWriteLimits write;
  StlReadLimits fresh_read;
  std::uint64_t max_total_new_requested_bytes = 0;
};
struct StlFileRewriteWorkPolicy {
  StlFileWorkPolicy files;
  StlWorkPolicy write;
  StlWorkPolicy fresh_read;
};
enum class StlFileStatus {
  kSuccess,
  kInvalidConfiguration,
  kInvalidRequest,
  kUnsupportedProfile,
  kLimitExceeded,
  kBudgetExceeded,
  kIoFailure,
  kFileChanged,
  kStlFailure,
  kPreservationMismatch,
  kProviderFailure,
  kDestinationExists,
  kPublicationFailed,
  kCanceled
};
enum class StlFilePhase {
  kConfiguration,
  kPathPlanning,
  kCapture,
  kRead,
  kWrite,
  kStaging,
  kStagedReread,
  kFreshRead,
  kVerification,
  kProvider,
  kReceiptVerification,
  kInventory,
  kPublication,
  kComplete
};
enum class StlFileCleanupOutcome { kNotRequired, kRemoved, kFailed };
enum class StlFileObservationState {
  kNotAttempted,
  kNotApplicable,
  kFailed,
  kCanceled,
  kSucceeded
};
enum class StlFileDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kEncoding,
  kMetadata,
  kUnitDeclaration,
  kCountLimit,
  kPathUnsupported,
  kNameUnsupported,
  kNameCollision,
  kSourceDestinationOverlap,
  kNotRegular,
  kOpen,
  kRead,
  kWrite,
  kClose,
  kChanged,
  kRequestBudget,
  kSharedBudget,
  kStopRequested,
  kDelegatedStl,
  kByteMismatch,
  kValueMismatch,
  kNormalMismatch,
  kOrderMismatch,
  kMetadataMismatch,
  kOutputFacts,
  kProvider,
  kReportUtf8,
  kStagedInventory,
  kDestinationExists,
  kCommit,
  kCommitUnsupported
};
struct StlFileDiagnostic {
  StlFileStatus status = StlFileStatus::kSuccess;
  StlFilePhase phase = StlFilePhase::kConfiguration;
  StlFileDiagnosticCode code = StlFileDiagnosticCode::kNone;
  std::optional<std::uint64_t> file_index;
  std::optional<std::uint64_t> byte_offset;
  std::optional<std::uint64_t> facet;
  std::optional<std::error_code> system_error;
  std::optional<StlStatus> stl_status;
  std::optional<StlDiagnostic> stl_diagnostic;
};

// Immutable copy-only owners. Copies/assignment/destruction allocate nothing.
// Views borrow a live unreassigned owner; independent copies permit concurrent
// reads. The execution/backend ledger survives its last retained block. No
// native handles or publicly visible absolute source locator are retained.
class StlCapturedFile {
 public:
  StlCapturedFile(const StlCapturedFile&) noexcept;
  StlCapturedFile& operator=(const StlCapturedFile&) noexcept;
  ~StlCapturedFile() noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  [[nodiscard]] std::array<std::byte, kStlFileSha256Bytes> Sha256()
      const noexcept;
  [[nodiscard]] geometry::ExecutionContext ShareExecutionContext()
      const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct stl_files_detail::Access;
  explicit StlCapturedFile(
      std::shared_ptr<const stl_files_detail::CaptureState>) noexcept;
  std::shared_ptr<const stl_files_detail::CaptureState> state_;
};
class StlFileAsset {
 public:
  StlFileAsset(const StlFileAsset&) noexcept;
  StlFileAsset& operator=(const StlFileAsset&) noexcept;
  ~StlFileAsset() noexcept;
  [[nodiscard]] const StlDocument& Document() const noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  [[nodiscard]] std::array<std::byte, kStlFileSha256Bytes> Sha256()
      const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct stl_files_detail::Access;
  explicit StlFileAsset(
      std::shared_ptr<const stl_files_detail::AssetState>) noexcept;
  std::shared_ptr<const stl_files_detail::AssetState> state_;
};
class StlVerifiedRewrite {
 public:
  StlVerifiedRewrite(const StlVerifiedRewrite&) noexcept;
  StlVerifiedRewrite& operator=(const StlVerifiedRewrite&) noexcept;
  ~StlVerifiedRewrite() noexcept;
  [[nodiscard]] const StlFileAsset& Source() const noexcept;
  [[nodiscard]] const StlOutput& Output() const noexcept;
  [[nodiscard]] const StlDocument& Document() const noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  [[nodiscard]] std::array<std::byte, kStlFileSha256Bytes> Sha256()
      const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct stl_files_detail::Access;
  explicit StlVerifiedRewrite(
      std::shared_ptr<const stl_files_detail::VerifiedState>) noexcept;
  std::shared_ptr<const stl_files_detail::VerifiedState> state_;
};
class StlPublishedBundle {
 public:
  StlPublishedBundle(const StlPublishedBundle&) noexcept;
  StlPublishedBundle& operator=(const StlPublishedBundle&) noexcept;
  ~StlPublishedBundle() noexcept;
  [[nodiscard]] const StlVerifiedRewrite& Verified() const noexcept;
  [[nodiscard]] std::string_view ReceiptPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> ReceiptBytes() const noexcept;
  [[nodiscard]] std::array<std::byte, kStlFileSha256Bytes> ReceiptSha256()
      const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct stl_files_detail::Access;
  explicit StlPublishedBundle(
      std::shared_ptr<const stl_files_detail::BundleState>) noexcept;
  std::shared_ptr<const stl_files_detail::BundleState> state_;
};
enum class StlFileMetadataMode { kRetain, kReplace };
enum class StlFileUnitKind { kUnknown, kLabel };
struct StlFileVerifiedContent {
  const StlFileAsset& source;
  const StlVerifiedRewrite& verified;
};
struct StlFileReportProvider {
  std::span<const std::byte> (*provide)(const StlFileVerifiedContent&,
                                        void*) = nullptr;
  void* state = nullptr;
};
struct StlFileRewriteOptions {
  StlEncoding encoding = StlEncoding::kInvalid;
  StlFileMetadataMode metadata_mode = StlFileMetadataMode::kRetain;
  std::string_view ascii_name;
  std::span<const std::byte> binary_header;
  bool acknowledge_corner_expansion = false;
  StlFileUnitKind unit_kind = StlFileUnitKind::kUnknown;
  std::string_view unit_label;
  std::optional<StlFileReportProvider> report;
};

struct StlFileCaptureFacts {
  std::string_view entry;
  std::uint64_t bytes = 0;
  std::array<std::byte, kStlFileSha256Bytes> sha256{};
};
struct StlFileReadFacts {
  StlEncoding encoding = StlEncoding::kInvalid;
  std::uint64_t facets = 0, vertices = 0, corners = 0;
  std::optional<std::string_view> ascii_name;
  std::optional<std::array<std::byte, kStlFileBinaryHeaderBytes>> binary_header;
};
struct StlFileWriteFacts {
  StlWriteFacts codec;
  std::uint64_t bytes = 0;
  std::array<std::byte, kStlFileSha256Bytes> sha256{};
};
struct StlFilePayloadFacts {
  std::uint64_t bytes = 0;
  std::array<std::byte, kStlFileSha256Bytes> sha256{};
};
struct StlFileVerificationFacts {
  bool position_bits = false, raw_normal_bits = false, facet_order = false,
       corner_order = false, soup_identity = false, metadata = false,
       exact_bytes = false;
};
struct StlFileInventoryFacts {
  std::array<StlFileCaptureFacts, 2> entries;
};
struct StlFilePublicationFacts {
  bool published = true;
};
using StlFileFacts =
    std::variant<std::monostate, StlFileCaptureFacts, StlFileReadFacts,
                 StlFileWriteFacts, StlFilePayloadFacts,
                 StlFileVerificationFacts, StlFileInventoryFacts,
                 StlFilePublicationFacts>;
struct StlFileObservation {
  StlFileObservationState state = StlFileObservationState::kNotAttempted;
  std::optional<StlFileDiagnostic> diagnostic;
  StlFileFacts facts;
};
struct StlFileObservations {
  StlFileObservation capture, read, write, staged_reread, fresh_read,
      verification, provider, receipt_verification, inventory, publication;
};
struct StlFileExecution {
  geometry::MaterializationExecutionStats capture, writing_files, staged_reread,
      byte_verification, value_verification, receipt_verification, inventory,
      hashing, provider_import;
  StlExecution read, write, fresh_read;
};
struct StlFileRequestedBytes {
  std::uint64_t file_capture = 0, file_publication = 0, application = 0;
  std::optional<StlRequestedBytes> read, write, fresh_read;
};
struct StlFileResult {
  StlFileStatus status = StlFileStatus::kInvalidConfiguration;
  StlFilePhase phase = StlFilePhase::kConfiguration;
  StlFileDiagnostic diagnostic;
  StlFileObservations observations;
  StlFileExecution execution;
  StlFileRequestedBytes requested_bytes;
  StlFileCleanupOutcome cleanup = StlFileCleanupOutcome::kNotRequired;
  std::optional<std::error_code> cleanup_error;
};
struct StlFileCaptureResult : StlFileResult {
  std::optional<StlCapturedFile> captured;
};
struct StlFileReadResult : StlFileResult {
  std::optional<StlCapturedFile> captured;
  std::optional<StlFileAsset> asset;
};
struct StlFileRewriteResult : StlFileResult {
  explicit StlFileRewriteResult(const StlFileAsset& value) noexcept
      : source(value) {}
  StlFileAsset source;
  std::optional<StlOutput> output;
  std::optional<StlDocument> fresh_document;
  std::optional<StlVerifiedRewrite> verified;
  std::optional<StlPublishedBundle> bundle;
};

// Borrowed arguments/provider backing remain readable and unchanged through
// synchronous return and every worker join. Expected failures retain completed
// owners/facts. Unexpected exceptions propagate after joins and owned cleanup.
// Rewrite uses the retained source ledger and never reopens the source path.
// No callback, allocation or required physical action follows native commit.
[[nodiscard]] StlFileCaptureResult CaptureStlFile(
    const std::filesystem::path&, const StlFileLimits&,
    const StlFileWorkPolicy&, const geometry::ExecutionContext&,
    std::stop_token stop = {});
[[nodiscard]] StlFileReadResult DecodeStlFile(const StlCapturedFile&,
                                              StlEncoding, const StlReadLimits&,
                                              const StlWorkPolicy&,
                                              std::stop_token stop = {});
[[nodiscard]] StlFileReadResult ReadStlFileAsset(
    const std::filesystem::path&, StlEncoding, const StlFileReadLimits&,
    const StlFileReadWorkPolicy&, const geometry::ExecutionContext&,
    std::stop_token stop = {});
[[nodiscard]] StlFileRewriteResult RewriteStlFile(
    const StlFileAsset&, const std::filesystem::path&,
    const StlFileRewriteOptions&, const StlFileRewriteLimits&,
    const StlFileRewriteWorkPolicy&, std::stop_token stop = {});
}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_STL_FILES_H_
