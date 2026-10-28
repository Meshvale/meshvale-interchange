// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_INCLUDE_MESHVALE_INTERCHANGE_SURFACE_FILES_H_
#define MESHVALE_INTERCHANGE_INCLUDE_MESHVALE_INTERCHANGE_SURFACE_FILES_H_

#include <meshvale/interchange/surface_write.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>

namespace meshvale::interchange {
inline constexpr std::string_view kSurfacePublicationSchema =
    "meshvale.surface-publication/1";
// Positive inclusive file/work caps and distinct fresh inspection limits.
// All input handles, options, callback state and returned report backing remain
// alive and unchanged through return/all joins. See docs/surface-files.md.
struct SurfaceFileLimits {
  std::uint64_t max_payload_members = 0;
  std::uint64_t max_payload_member_bytes = 0;
  std::uint64_t max_total_payload_bytes = 0;
  std::uint64_t max_receipt_bytes = 0;
  std::uint64_t max_caller_report_bytes = 0;
  std::uint64_t max_path_bytes = 0;
  std::uint64_t max_total_path_bytes = 0;
  std::uint64_t max_native_path_code_units = 0;
  std::uint64_t max_inventory_entries = 0;
  std::uint64_t max_stage_name_attempts = 0;
  std::uint64_t max_new_requested_bytes = 0;
};
struct SurfaceFilePublicationLimits {
  SurfaceFileLimits files;
  SurfaceInspectLimits inspection;
  std::uint64_t max_total_requested_allowance = 0;
};
struct SurfaceFileWorkPolicy {
  std::uint64_t io_block_bytes = 0;
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct SurfaceFilePublicationWorkPolicy {
  SurfaceFileWorkPolicy files;
  SurfaceWorkPolicy inspection;
};

inline constexpr std::string_view kDefaultSurfacePublicationReceiptPath =
    "publication.json";
struct SurfaceFileVerifiedContent {
  const SurfaceOutputInspection& inspection;  // Fresh physical V only.
  std::string_view entry_path;
};
struct SurfaceReceiptProvider {
  std::span<const std::byte> (*provide)(const SurfaceFileVerifiedContent&,
                                        void* state) = nullptr;
  void* state = nullptr;
};
struct SurfaceFilePublicationOptions {
  std::string_view receipt_path = kDefaultSurfacePublicationReceiptPath;
  SurfaceReceiptProvider receipt{};
};

enum class SurfaceFilePublicationOutcome { kFailed, kCanceled, kPublished };
enum class SurfaceFileCleanupOutcome { kNotRequired, kRemoved, kFailed };
enum class SurfaceFileStatus {
  kSuccess,
  kInvalidConfiguration,
  kInvalidStorage,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled,
  kUnsupportedProfile,
  kIoFailure,
  kVerificationFailed,
  kDestinationExists,
  kPublicationFailed,
  kCallbackFailure
};
enum class SurfaceFileStage {
  kConfiguration,
  kPathPlanning,
  kStaging,
  kReread,
  kInspection,
  kReceipt,
  kInventory,
  kPublication,
  kComplete
};
enum class SurfaceFileCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kUnknownEnum,
  kEncodingLimitsMismatch,
  kAllowanceTooSmall,
  kProviderConfiguration,
  kDestinationPath,
  kReceiptPath,
  kPathCollision,
  kSourceContract,
  kCountLimit,
  kRequestBudget,
  kStopRequested,
  kUnsupportedProfile,
  kDestinationParent,
  kStageCreate,
  kStageAttemptsExhausted,
  kFileOpen,
  kFileRead,
  kFileWrite,
  kFileClose,
  kEnumeration,
  kMemberInventory,
  kFileChanged,
  kFileKind,
  kInspection,
  kCallerReportEncoding,
  kReceiptSize,
  kReceiptBytes,
  kProvider,
  kDestinationExists,
  kCommitFailed,
  kCommitUnsupported
};
struct SurfaceFileDiagnostic {
  SurfaceFileStatus status = SurfaceFileStatus::kInvalidConfiguration;
  SurfaceFileStage stage = SurfaceFileStage::kConfiguration;
  SurfaceFileCode code = SurfaceFileCode::kNone;
  std::optional<std::uint64_t> member_ordinal, byte_offset;
  std::optional<std::error_code> system_error;
  std::optional<SurfaceStatus> inspection_status;
  std::optional<SurfaceDiagnostic> inspection_diagnostic;
};
struct SurfaceFileCleanupObservation {
  SurfaceFileCleanupOutcome outcome = SurfaceFileCleanupOutcome::kNotRequired;
  std::optional<std::error_code> error;
};
enum class SurfaceFilePhaseOutcome { kCompleted, kFailed, kCanceled };
struct SurfaceFileExecutionStats {
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t caller_jobs = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t completed_jobs = 0;
};
struct SurfaceFileIoObservation {
  SurfaceFilePhaseOutcome outcome = SurfaceFilePhaseOutcome::kFailed;
  std::uint64_t bytes_read = 0, bytes_written = 0, bytes_copied = 0;
  std::uint64_t bytes_compared = 0, bytes_hashed = 0;
  std::uint64_t completed_files = 0, closed_files = 0;
  SurfaceFileExecutionStats execution;
};
struct SurfaceFileInventoryObservation {
  SurfaceFilePhaseOutcome outcome = SurfaceFilePhaseOutcome::kFailed;
  std::uint64_t observed_files = 0, observed_directories = 0;
};
struct SurfaceFileInspectionObservation {
  SurfaceStatus status = SurfaceStatus::kInvalidConfiguration;
  SurfaceDiagnostic diagnostic;
  std::optional<SurfaceFacts> facts;
};
struct SurfaceFileProviderObservation {
  SurfaceFilePhaseOutcome outcome = SurfaceFilePhaseOutcome::kFailed;
  std::optional<std::uint64_t> returned_bytes;
};
enum class SurfaceFileCommitOutcome {
  kCommitted,
  kDestinationExists,
  kFailed,
  kUnsupported
};
struct SurfaceFileCommitObservation {
  SurfaceFileCommitOutcome outcome = SurfaceFileCommitOutcome::kFailed;
  std::optional<std::error_code> error;
};
struct SurfaceFileFacts {
  std::optional<SurfaceFileIoObservation> payload_write;
  std::optional<SurfaceFileInventoryObservation> payload_inventory;
  std::optional<SurfaceFileIoObservation> payload_reread;
  std::optional<SurfaceFileInspectionObservation> inspection;
  std::optional<SurfaceFileProviderObservation> provider;
  std::optional<SurfaceFileIoObservation> report_import;
  std::optional<SurfaceFileIoObservation> receipt_write;
  std::optional<SurfaceFileIoObservation> receipt_reread;
  std::optional<SurfaceFileInventoryObservation> final_inventory;
  std::optional<SurfaceFileIoObservation> final_payload_verification;
  std::optional<SurfaceFileIoObservation> final_receipt_verification;
  std::optional<SurfaceFileCommitObservation> commit;
};
struct SurfaceFileReceiptView {
  std::string_view path;
  std::span<const std::byte> bytes;
  SurfaceDigest sha256;
};
namespace surface_files_detail {
struct PublishedState;
struct Access;
}  // namespace surface_files_detail
class SurfacePublishedBundle {
 public:
  SurfacePublishedBundle(const SurfacePublishedBundle&) noexcept;
  SurfacePublishedBundle& operator=(const SurfacePublishedBundle&) noexcept;
  ~SurfacePublishedBundle() noexcept;
  const SurfaceOutputInspection& Inspection() const noexcept;
  SurfaceFileReceiptView Receipt() const noexcept;
  std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct surface_files_detail::Access;
  explicit SurfacePublishedBundle(
      std::shared_ptr<const surface_files_detail::PublishedState>) noexcept;
  std::shared_ptr<const surface_files_detail::PublishedState> state_;
};
struct SurfaceFilePublicationResult {
  SurfaceFilePublicationOutcome outcome =
      SurfaceFilePublicationOutcome::kFailed;
  SurfaceFileDiagnostic diagnostic;
  SurfaceFileCleanupObservation cleanup;
  std::optional<SurfacePublishedBundle> bundle;
  SurfaceFileFacts facts;
};
[[nodiscard]] SurfaceFilePublicationResult PublishSurfaceBundle(
    const SurfaceOutputInspection& inspected,
    const std::filesystem::path& destination,
    const SurfaceFilePublicationOptions& options,
    const SurfaceFilePublicationLimits& limits,
    const SurfaceFilePublicationWorkPolicy& work, std::stop_token stop = {});
// Receipt/inspection views borrow a live unchanged strong owner. Copies are
// allocation-free; separate live handles permit concurrent reads. Assignment or
// destruction cannot race access to the same visible handle or borrowed views.
// NewRequestedBytes is P-only; inspection bytes have a separate owner/getter.
}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_INCLUDE_MESHVALE_INTERCHANGE_SURFACE_FILES_H_
