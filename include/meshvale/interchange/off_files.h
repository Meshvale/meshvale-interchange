// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_OFF_FILES_H_
#define MESHVALE_INTERCHANGE_OFF_FILES_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>

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
namespace off_files_detail {
struct AssetState;
struct BundleState;
struct Access;
}  // namespace off_files_detail

// Required, positive, inclusive and host-representable. Fresh requests are
// file-module requests only, including accounting/control and overlapping IO.
struct OffFileLimits {
  std::uint64_t max_entry_bytes = 0;
  std::uint64_t max_receipt_bytes = 0;
  std::uint64_t max_total_file_bytes = 0;
  std::uint64_t max_entry_name_bytes = 0;
  std::uint64_t max_receipt_name_bytes = 0;
  std::uint64_t max_native_path_code_units = 0;
  std::uint64_t max_file_new_requested_bytes = 0;
};
struct OffFileWorkPolicy {
  std::uint64_t io_block_bytes = 0;
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct OffFileReadLimits {
  OffFileLimits files;
  OffReadLimits off;
};
struct OffFileReadWorkPolicy {
  OffFileWorkPolicy files;
  OffWorkPolicy off;
};
struct OffFilePublicationLimits {
  OffFileLimits files;
  geometry::MeshInspectionLimits expected_inspection;
  OffReadLimits reload;
};
struct OffFilePublicationWorkPolicy {
  OffFileWorkPolicy files;
  geometry::MeshMaterializationWorkPolicy expected_inspection;
  OffWorkPolicy reload;
};
enum class OffFileStatus {
  kSuccess,
  kInvalidConfiguration,
  kInvalidRequest,
  kUnsupportedProfile,
  kLimitExceeded,
  kBudgetExceeded,
  kForeignBudget,
  kIoFailure,
  kFileChanged,
  kOffFailure,
  kGeometryFailure,
  kPreservationMismatch,
  kProviderFailure,
  kDestinationExists,
  kPublicationFailed,
  kCanceled
};
enum class OffFileStage {
  kConfiguration,
  kPathPlanning,
  kCapture,
  kOffRead,
  kExpectedInspection,
  kStaging,
  kReload,
  kVerification,
  kProvider,
  kInventory,
  kPublication,
  kComplete
};
enum class OffFileDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kCountLimit,
  kPathUnsupported,
  kNameUnsupported,
  kNameCollision,
  kNotRegular,
  kOpen,
  kRead,
  kWrite,
  kClose,
  kChanged,
  kRequestBudget,
  kSharedBudget,
  kStopRequested,
  kLedgerMismatch,
  kDelegatedOff,
  kDelegatedInspection,
  kAttributeUnsupported,
  kOutputFacts,
  kByteMismatch,
  kMeshMismatch,
  kProvider,
  kStagedInventory,
  kDestinationExists,
  kCommit,
  kCommitUnsupported
};
struct OffFileDiagnostic {
  OffFileStatus status = OffFileStatus::kSuccess;
  OffFileStage stage = OffFileStage::kConfiguration;
  OffFileDiagnosticCode code = OffFileDiagnosticCode::kNone;
  // Entry=0, receipt=1. No allocating machine-path message is retained.
  std::optional<std::uint64_t> file_index;
  std::optional<std::uint64_t> byte_offset;
  std::optional<std::error_code> system_error;
  std::optional<OffStatus> off_status;
  std::optional<OffDiagnostic> off_diagnostic;
  std::optional<geometry::MaterializationStatus> geometry_status;
  std::optional<geometry::MaterializationDiagnostic> geometry_diagnostic;
};
struct OffFileExecution {
  geometry::MaterializationExecutionStats capture;
  geometry::MaterializationExecutionStats copying;
  geometry::MaterializationExecutionStats byte_verification;
  geometry::MaterializationExecutionStats mesh_verification;
  geometry::MaterializationExecutionStats expected_inspection;
  OffExecution off;
};

// Copy-only sharing owners. Copies allocate nothing. Original backend state
// must outlive the last free. Views require a live unreassigned owner; distinct
// immutable copies support concurrent reads after synchronous return.
class OffFileAsset {
 public:
  OffFileAsset(const OffFileAsset&) noexcept;
  OffFileAsset& operator=(const OffFileAsset&) noexcept;
  ~OffFileAsset() noexcept;
  [[nodiscard]] const OffDocument& Document() const noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  // Live attributed file requests, excluding delegated OFF/Geometry owners.
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct off_files_detail::Access;
  explicit OffFileAsset(
      std::shared_ptr<const off_files_detail::AssetState>) noexcept;
  std::shared_ptr<const off_files_detail::AssetState> state_;
};
struct OffFileReadResult {
  OffFileStatus status = OffFileStatus::kInvalidConfiguration;
  std::optional<OffFileAsset> asset;
  OffFileDiagnostic diagnostic;
  OffFileExecution execution;
};
struct OffFileVerifiedContent {
  const OffDocument& document;
  std::string_view entry_path;
  std::span<const std::byte> entry_bytes;
};
struct OffFileReceiptProvider {
  std::string_view basename;
  // Invoked exactly once after staged verification. State and returned backing
  // survive the entire publisher return, including import joins and cleanup.
  // bad_alloc propagates; other external exceptions are typed provider failure.
  std::span<const std::byte> (*provide)(const OffFileVerifiedContent&,
                                        void* state) = nullptr;
  void* state = nullptr;
};
struct OffFilePublicationRequest {
  const OffFileAsset& source;
  const geometry::MaterializedMesh& expected;
  const OffOutput& output;
};
struct OffFilePublicationOptions {
  std::optional<OffFileReceiptProvider> receipt;
};
class OffPublishedBundle {
 public:
  OffPublishedBundle(const OffPublishedBundle&) noexcept;
  OffPublishedBundle& operator=(const OffPublishedBundle&) noexcept;
  ~OffPublishedBundle() noexcept;
  [[nodiscard]] const OffFileAsset& Source() const noexcept;
  [[nodiscard]] const geometry::MaterializedMesh& ExpectedMesh() const noexcept;
  [[nodiscard]] const OffDocument& Document() const noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  [[nodiscard]] std::optional<std::string_view> ReceiptPath() const noexcept;
  [[nodiscard]] std::optional<std::span<const std::byte>> ReceiptBytes()
      const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct off_files_detail::Access;
  explicit OffPublishedBundle(
      std::shared_ptr<const off_files_detail::BundleState>) noexcept;
  std::shared_ptr<const off_files_detail::BundleState> state_;
};
enum class OffFilePublicationOutcome { kFailed, kCanceled, kPublished };
enum class OffFileCleanupOutcome { kNotRequired, kRemoved, kFailed };
struct OffFilePublicationResult {
  OffFilePublicationOutcome outcome = OffFilePublicationOutcome::kFailed;
  OffFileStage phase = OffFileStage::kConfiguration;
  std::optional<OffPublishedBundle> bundle;
  OffFileDiagnostic diagnostic;
  OffFileCleanupOutcome cleanup = OffFileCleanupOutcome::kNotRequired;
  std::optional<std::error_code> cleanup_error;
  OffFileExecution execution;
};

// Input, configuration and all borrowed owners stay unchanged/readable through
// synchronous return and joins. Native handles/absolute paths are not retained.
// Expected failure exposes no asset/bundle. Unexpected exceptions propagate
// after joins and best-effort owned-stage cleanup. See OFF-FILES-001.
[[nodiscard]] OffFileReadResult ReadOffFileAsset(
    const std::filesystem::path& input, const OffFileReadLimits& limits,
    const OffFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop = {});
[[nodiscard]] OffFilePublicationResult PublishOffBundle(
    const OffFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const OffFilePublicationOptions& options,
    const OffFilePublicationLimits& limits,
    const OffFilePublicationWorkPolicy& work, std::stop_token stop = {});
}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_OFF_FILES_H_
