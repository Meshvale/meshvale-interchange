// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_GLTF_FILES_H_
#define MESHVALE_INTERCHANGE_GLTF_FILES_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace meshvale::interchange {
namespace gltf_files_detail {
struct FileAssetState;
struct PublishedBundleState;
struct Access;
}  // namespace gltf_files_detail

// Draft declaration only; the optional meshvale::gltf_files target requires
// gltf_workflow_support. See docs/gltf-files.md for the unimplemented contract.
enum class GltfFileFormat { kGltfJson, kGlb };
enum class GltfExtensionFilePolicy {
  kRejectOpaqueExtensions,
  // The caller explicitly enumerates every opaque external dependency. This
  // is a caller declaration, not automatic extension-resource discovery.
  kExplicitOpaqueInventory
};
struct GltfFileView {
  // Portable UTF-8 path relative to the captured/published root.
  std::string_view path;
  std::span<const std::byte> bytes;
};
struct GltfFileReadOptions {
  // Empty selects the input entry's parent. This caller-owned path is borrowed.
  std::filesystem::path resource_root;
  GltfExtensionFilePolicy extensions =
      GltfExtensionFilePolicy::kRejectOpaqueExtensions;
  // Portable relative paths, distinct from URI spellings. Borrow through
  // return.
  std::span<const std::string_view> opaque_source_files;
};
// Required positive, inclusive and host-representable. Unique physical files
// include the entry and explicit opaque/supplemental files. Resource bindings
// count separately, including multiple bindings to one physical file.
struct GltfFileLimits {
  std::uint64_t max_files = 0;
  std::uint64_t max_resource_bindings = 0;
  std::uint64_t max_file_bytes = 0;
  std::uint64_t max_total_file_bytes = 0;
  std::uint64_t max_path_bytes = 0;
  std::uint64_t max_total_path_bytes = 0;
  // Per native absolute path, including terminator: wchar_t units on Windows,
  // char units on POSIX. Fresh conversions/paths remain charged requests.
  std::uint64_t max_native_path_code_units = 0;
  // Complete fresh explicit file-module requests; delegated operation owners
  // have their own caps on the same ledger. This is not a combined process cap.
  std::uint64_t max_new_requested_bytes = 0;
};
struct GltfFileWorkPolicy {
  std::uint64_t io_block_bytes = 0;
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct GltfFileReadLimits {
  GltfFileLimits files;
  GltfReadOperationLimits asset;
  GlbLimits container;
};
struct GltfFileReadWorkPolicy {
  GltfFileWorkPolicy files;
  GltfWorkPolicy asset;
  GlbWorkPolicy container;
};
struct GltfFileExecutionStats {
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t caller_jobs = 0;
};
// Actual file-module jobs only; delegated operations retain their own scope.
struct GltfFileExecution {
  GltfFileExecutionStats capture;
  GltfFileExecutionStats byte_verification;
};
enum class GltfFileStage {
  kConfiguration,
  kPathPlanning,
  kCapture,
  kContainer,
  kAssetRead,
  kStaging,
  kExtraction,
  kInspection,
  kSupplemental,
  kInventory,
  kPublication,
  kComplete
};
enum class GltfFileStatus {
  kSuccess,
  kInvalidConfiguration,
  kInvalidRequest,
  kMalformed,
  kUnsupportedProfile,
  kLimitExceeded,
  kForeignBudget,
  kSourceContractFailure,
  kBudgetExceeded,
  kIoFailure,
  kPreservationMismatch,
  kCallbackFailure,
  kDestinationExists,
  kPublicationFailed,
  kCanceled
};
enum class GltfFileDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kInvalidRequest,
  kCountLimit,
  kFileRequestLimit,
  kRequestBudget,
  kStopRequested,
  kSourceIdentity,
  kForeignBudget,
  kUriMalformed,
  kUriUnsupported,
  kPathUnsupported,
  kPathOutsideRoot,
  kPathCollision,
  kResourceMissing,
  kResourceNotRegularFile,
  kFileChanged,
  kFileRead,
  kFileWrite,
  kOpaqueInventoryRequired,
  kContainerBinding,
  kContainerPadding,
  kContainerPreservation,
  kEntryBytes,
  kResourceBytes,
  kStagedInventory,
  kDelegatedContainer,
  kDelegatedRead,
  kDelegatedExtraction,
  kDelegatedInspection,
  kProgressCallback,
  kVerifiedCallback,
  kDestinationPath,
  kDestinationParent,
  kPublicationUnsupported,
  kPublicationFailed,
  kDestinationExists
};
// Fixed inline first failure; no allocating diagnostic path or message. File
// ordinal belongs to the operation's ordered plan, not an absolute source path.
struct GltfFileDiagnostic {
  GltfFileStatus status = GltfFileStatus::kInvalidRequest;
  GltfFileStage stage = GltfFileStage::kConfiguration;
  GltfFileDiagnosticCode code = GltfFileDiagnosticCode::kNone;
  std::optional<std::uint64_t> file_index;
  std::optional<std::uint64_t> byte_offset;
  std::optional<GltfResourceKind> resource_kind;
  std::optional<std::uint64_t> resource_index;
  std::optional<std::error_code> system_error;
  std::optional<GltfDiagnostic> gltf_diagnostic;
  std::optional<GlbStatus> container_status;
  std::optional<std::uint64_t> container_byte_offset;
  std::optional<GltfTopologyOutputInspectionStatus> inspection_status;
  std::optional<GltfTopologyOutputInspectionDiagnostic> inspection_diagnostic;
};

// Immutable copy-only capture. Files() is UTF-8 path-sorted and includes the
// exact entry bytes once. Container() is null for JSON; its GLB owner retains
// all wire bytes/chunks. Initial GLB Asset() captures the complete JSON chunk.
class GltfFileAsset {
 public:
  GltfFileAsset(const GltfFileAsset&) noexcept;
  GltfFileAsset& operator=(const GltfFileAsset&) noexcept;
  ~GltfFileAsset() noexcept;
  [[nodiscard]] const GltfAsset& Asset() const noexcept;
  [[nodiscard]] GltfFileFormat Format() const noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  [[nodiscard]] std::span<const GltfFileView> Files() const noexcept;
  [[nodiscard]] const GlbContainer* Container() const noexcept;
  [[nodiscard]] GltfExtensionFilePolicy ExtensionFilesPolicy() const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct gltf_files_detail::Access;
  explicit GltfFileAsset(
      std::shared_ptr<const gltf_files_detail::FileAssetState>) noexcept;
  std::shared_ptr<const gltf_files_detail::FileAssetState> owner_;
};
struct GltfFileReadResult {
  std::optional<GltfFileAsset> asset;
  GltfFileDiagnostic diagnostic;
  GltfFileExecution execution;
};

struct GltfFilePublicationRequest {
  const GltfFileAsset& source;
  const GltfTopologyOutputInspection& inspected_output;
};
struct GltfFilePublicationLimits {
  GltfFileLimits files;
  GltfReadOperationLimits reload;
  GltfExtractionLimits extraction;
  GltfTopologyOutputInspectionLimits inspection;
  GlbLimits container;
};
struct GltfFilePublicationWorkPolicy {
  GltfFileWorkPolicy files;
  GltfWorkPolicy reload;
  GltfTopologyOutputInspectionWorkPolicy inspection;
  GlbWorkPolicy container;
};
enum class GltfFilePublicationPhase {
  kPreflight,
  kStaging,
  kVerification,
  kPublication
};
enum class GltfFilePublicationOutcome { kFailed, kCanceled, kPublished };
enum class GltfFileCleanupOutcome { kNotRequired, kRemoved, kFailed };
// Callback-produced storage is caller-owned input. The module admits and
// imports it into its charged storage before any staged write.
struct GltfFileSupplement {
  std::string path;
  std::vector<std::byte> bytes;
};
struct GltfFileVerifiedContent {
  // This actual staged-byte reload/inspection does not certify publication.
  const GltfTopologyOutputInspection& inspection;
  GltfFileFormat format;
  std::string_view entry_path;
  const GlbContainer* container;
  std::span<const GltfFileView> files;
};
struct GltfFilePublicationOptions {
  // Views/functions are borrowed until return; the module never copies a
  // std::function or retains an unowned callback-produced view.
  std::span<const GltfFileView> supplemental_files;
  std::function<void(GltfFilePublicationPhase)> on_phase;
  std::function<std::vector<GltfFileSupplement>(const GltfFileVerifiedContent&)>
      on_verified;
};
// Published result only. Its inspection retains the actual staged reload;
// GLB reload JSON is the proven writer-length prefix, excluding only newly
// derived alignment padding. Files()/Container() retain full physical bytes.
class GltfPublishedBundle {
 public:
  GltfPublishedBundle(const GltfPublishedBundle&) noexcept;
  GltfPublishedBundle& operator=(const GltfPublishedBundle&) noexcept;
  ~GltfPublishedBundle() noexcept;
  [[nodiscard]] const GltfFileAsset& Source() const noexcept;
  [[nodiscard]] const GltfTopologyOutputInspection& Inspection() const noexcept;
  [[nodiscard]] GltfFileFormat Format() const noexcept;
  [[nodiscard]] std::string_view EntryPath() const noexcept;
  [[nodiscard]] std::span<const std::byte> EntryBytes() const noexcept;
  [[nodiscard]] std::span<const GltfFileView> Files() const noexcept;
  [[nodiscard]] const GlbContainer* Container() const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct gltf_files_detail::Access;
  explicit GltfPublishedBundle(
      std::shared_ptr<const gltf_files_detail::PublishedBundleState>) noexcept;
  std::shared_ptr<const gltf_files_detail::PublishedBundleState> owner_;
};
struct GltfFilePublicationResult {
  GltfFilePublicationOutcome outcome = GltfFilePublicationOutcome::kFailed;
  GltfFilePublicationPhase phase = GltfFilePublicationPhase::kPreflight;
  std::optional<GltfPublishedBundle> bundle;
  GltfFileDiagnostic diagnostic;
  GltfFileCleanupOutcome cleanup = GltfFileCleanupOutcome::kNotRequired;
  std::optional<std::error_code> cleanup_error;
  GltfFileExecution execution;
};

// Magic/content detection; suffixes do not choose the format. Inputs/context
// wrappers remain alive and unchanged through return and all native joins.
[[nodiscard]] GltfFileReadResult ReadGltfFileAsset(
    const std::filesystem::path& input, const GltfFileReadOptions& options,
    const GltfFileReadLimits& limits, const GltfFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop = {});
// Source.Asset() must be the exact original asset owner retained by the
// successful inspection. No source filesystem reread; no format conversion.
// Destination is a new directory with an existing parent. No owner is
// published until native no-replace finalization succeeds. Expected failures
// return typed outcomes; actual allocation/native runtime exceptions propagate
// after joins and best-effort staging cleanup. No callback follows commit.
[[nodiscard]] GltfFilePublicationResult PublishGltfTopologyBundle(
    const GltfFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const GltfFilePublicationOptions& options,
    const GltfFilePublicationLimits& limits,
    const GltfFilePublicationWorkPolicy& work, std::stop_token stop = {});

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_GLTF_FILES_H_
