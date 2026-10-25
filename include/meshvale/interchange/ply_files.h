// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PLY_FILES_H_
#define MESHVALE_INTERCHANGE_PLY_FILES_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>

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
namespace ply_files_detail {
struct AssetState;
struct BundleState;
struct Access;
}  // namespace ply_files_detail

// Required, positive, inclusive and host-representable. Fresh requests are
// file-module requests only, including accounting/control and overlapping IO.
struct PlyFileLimits {
  std::uint64_t max_entry_bytes = 0;
  std::uint64_t max_receipt_bytes = 0;
  std::uint64_t max_total_file_bytes = 0;
  std::uint64_t max_entry_name_bytes = 0;
  std::uint64_t max_receipt_name_bytes = 0;
  std::uint64_t max_native_path_code_units = 0;
  std::uint64_t max_file_new_requested_bytes = 0;
};
struct PlyFileWorkPolicy {
  std::uint64_t io_block_bytes = 0;
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct PlyFileReadLimits {
  PlyFileLimits files;
  PlyReadLimits ply;
};
struct PlyFileReadWorkPolicy {
  PlyFileWorkPolicy files;
  PlyWorkPolicy ply;
};
struct PlyFilePublicationLimits {
  PlyFileLimits files;
  geometry::MeshInspectionLimits expected_inspection;
  PlyReadLimits reload;
};
struct PlyFilePublicationWorkPolicy {
  PlyFileWorkPolicy files;
  geometry::MeshMaterializationWorkPolicy expected_inspection;
  PlyWorkPolicy reload;
};
enum class PlyFileStatus {
  kSuccess,
  kInvalidConfiguration,
  kInvalidRequest,
  kUnsupportedProfile,
  kLimitExceeded,
  kBudgetExceeded,
  kForeignBudget,
  kIoFailure,
  kFileChanged,
  kPlyFailure,
  kGeometryFailure,
  kPreservationMismatch,
  kProviderFailure,
  kDestinationExists,
  kPublicationFailed,
  kCanceled
};
enum class PlyFileStage {
  kConfiguration,
  kPathPlanning,
  kCapture,
  kPlyRead,
  kExpectedInspection,
  kStaging,
  kReload,
  kVerification,
  kProvider,
  kInventory,
  kPublication,
  kComplete
};
enum class PlyFileDiagnosticCode {
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
  kDelegatedPly,
  kDelegatedInspection,
  kAttributeMismatch,
  kOutputFacts,
  kByteMismatch,
  kMeshMismatch,
  kProvider,
  kStagedInventory,
  kDestinationExists,
  kCommit,
  kCommitUnsupported,
  kSchemaMismatch
};
struct PlyFileDiagnostic {
  PlyFileStatus status = PlyFileStatus::kSuccess;
  PlyFileStage stage = PlyFileStage::kConfiguration;
  PlyFileDiagnosticCode code = PlyFileDiagnosticCode::kNone;
  // Entry=0, receipt=1. No allocating machine-path message is retained.
  std::optional<std::uint64_t> file_index;
  std::optional<std::uint64_t> byte_offset;
  std::optional<std::error_code> system_error;
  std::optional<PlyStatus> ply_status;
  std::optional<PlyDiagnostic> ply_diagnostic;
  std::optional<geometry::MaterializationStatus> geometry_status;
  std::optional<geometry::MaterializationDiagnostic> geometry_diagnostic;
};
struct PlyFileExecution {
  geometry::MaterializationExecutionStats capture;
  geometry::MaterializationExecutionStats copying;
  geometry::MaterializationExecutionStats byte_verification;
  geometry::MaterializationExecutionStats mesh_verification;
  geometry::MaterializationExecutionStats expected_inspection;
  PlyExecution ply;
};

// Copy-only sharing owners. Copies allocate nothing. Original backend state
// must outlive the last free. Views require a live unreassigned owner; distinct
// immutable copies support concurrent reads after synchronous return.
class PlyFileAsset {
 public:
  PlyFileAsset(const PlyFileAsset&) noexcept;
  PlyFileAsset& operator=(const PlyFileAsset&) noexcept;
  ~PlyFileAsset() noexcept;
  [[nodiscard]] const PlyDocument& Document() const noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  // Live attributed file requests, excluding delegated PLY/Geometry owners.
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct ply_files_detail::Access;
  explicit PlyFileAsset(
      std::shared_ptr<const ply_files_detail::AssetState>) noexcept;
  std::shared_ptr<const ply_files_detail::AssetState> state_;
};
struct PlyFileReadResult {
  PlyFileStatus status = PlyFileStatus::kInvalidConfiguration;
  std::optional<PlyFileAsset> asset;
  PlyFileDiagnostic diagnostic;
  PlyFileExecution execution;
};
struct PlyFileVerifiedContent {
  const PlyDocument& document;
  std::string_view entry_path;
  std::span<const std::byte> entry_bytes;
};
struct PlyFileReceiptProvider {
  std::string_view basename;
  // Invoked exactly once after staged verification. State and returned backing
  // survive the entire publisher return, including import joins and cleanup.
  // bad_alloc propagates; other external exceptions are typed provider failure.
  std::span<const std::byte> (*provide)(const PlyFileVerifiedContent&,
                                        void* state) = nullptr;
  void* state = nullptr;
};
struct PlyFilePublicationRequest {
  const PlyFileAsset& source;
  const geometry::MaterializedMesh& expected;
  const PlyOutput& output;
};
struct PlyFilePublicationOptions {
  std::optional<PlyFileReceiptProvider> receipt;
};
class PlyPublishedBundle {
 public:
  PlyPublishedBundle(const PlyPublishedBundle&) noexcept;
  PlyPublishedBundle& operator=(const PlyPublishedBundle&) noexcept;
  ~PlyPublishedBundle() noexcept;
  [[nodiscard]] const PlyFileAsset& Source() const noexcept;
  [[nodiscard]] const geometry::MaterializedMesh& ExpectedMesh() const noexcept;
  [[nodiscard]] const PlyDocument& Document() const noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  [[nodiscard]] std::optional<std::string_view> ReceiptPath() const noexcept;
  [[nodiscard]] std::optional<std::span<const std::byte>> ReceiptBytes()
      const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct ply_files_detail::Access;
  explicit PlyPublishedBundle(
      std::shared_ptr<const ply_files_detail::BundleState>) noexcept;
  std::shared_ptr<const ply_files_detail::BundleState> state_;
};
enum class PlyFilePublicationOutcome { kFailed, kCanceled, kPublished };
enum class PlyFileCleanupOutcome { kNotRequired, kRemoved, kFailed };
struct PlyFilePublicationResult {
  PlyFilePublicationOutcome outcome = PlyFilePublicationOutcome::kFailed;
  PlyFileStage phase = PlyFileStage::kConfiguration;
  std::optional<PlyPublishedBundle> bundle;
  PlyFileDiagnostic diagnostic;
  PlyFileCleanupOutcome cleanup = PlyFileCleanupOutcome::kNotRequired;
  std::optional<std::error_code> cleanup_error;
  PlyFileExecution execution;
};

// Input, configuration and all borrowed owners stay unchanged/readable through
// synchronous return and joins. Native handles/absolute paths are not retained.
// Expected failure exposes no asset/bundle. Unexpected exceptions propagate
// after joins and best-effort owned-stage cleanup. See PLY-FILES-001.
[[nodiscard]] PlyFileReadResult ReadPlyFileAsset(
    const std::filesystem::path& input, const PlyFileReadLimits& limits,
    const PlyFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop = {});
[[nodiscard]] PlyFilePublicationResult PublishPlyBundle(
    const PlyFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const PlyFilePublicationOptions& options,
    const PlyFilePublicationLimits& limits,
    const PlyFilePublicationWorkPolicy& work, std::stop_token stop = {});
}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_PLY_FILES_H_
