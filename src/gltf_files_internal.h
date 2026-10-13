// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_FILES_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_FILES_INTERNAL_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_files_test_internal.h"

namespace meshvale::interchange::gltf_files_detail {
template <class T>
using Vector = std::vector<T, gltf_detail::Allocator<T>>;
using String = std::basic_string<char, std::char_traits<char>,
                                 gltf_detail::Allocator<char>>;
#ifdef _WIN32
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
using NativeString = std::basic_string<NativeChar, std::char_traits<NativeChar>,
                                       gltf_detail::Allocator<NativeChar>>;
using NativeView = std::basic_string_view<NativeChar>;
inline constexpr std::intptr_t kInvalidFileHandle = -1;
#ifdef _WIN32
inline constexpr std::size_t kDriveRootUnits = 3;
#endif
struct Failure {
  GltfFileDiagnostic diagnostic;
};
[[noreturn]] void Fail(GltfFileStatus status, GltfFileStage stage,
                       GltfFileDiagnosticCode code,
                       std::optional<std::uint64_t> file = {},
                       std::optional<std::uint64_t> offset = {},
                       std::optional<std::error_code> error = {});
void Count(std::uint64_t value, std::uint64_t maximum,
           GltfFileStage stage = GltfFileStage::kPathPlanning);
struct FileRecord {
  String path;
  Vector<std::byte> bytes;
  explicit FileRecord(const gltf_detail::AllocationContext& allocation)
      : path(gltf_detail::Allocator<char>(allocation)),
        bytes(gltf_detail::Allocator<std::byte>(allocation)) {}
};
struct FileAssetState {
  gltf_detail::AllocationContext allocation;
  std::optional<GlbContainer> container;
  std::optional<GltfAsset> asset;
  GltfExtensionFilePolicy extensions;
  Vector<FileRecord> records;
  Vector<GltfFileView> views;
  std::size_t entry = 0;
  FileAssetState(const gltf_detail::AllocationContext& allocation,
                 GltfExtensionFilePolicy extensions);
};
struct PublishedBundleState {
  GltfFileAsset source;
  GltfTopologyOutputInspection inspection;
  FileAssetState files;
  PublishedBundleState(const GltfFileAsset& source,
                       const GltfTopologyOutputInspection& inspection,
                       FileAssetState&& files);
};
struct Access {
  static GltfFileAsset Make(std::shared_ptr<const FileAssetState> state);
  static const FileAssetState& State(const GltfFileAsset& asset) noexcept;
  static GltfPublishedBundle MakePublished(
      std::shared_ptr<const PublishedBundleState> state) noexcept;
};
void ValidatePublicationConfiguration(
    const GltfFilePublicationLimits& limits,
    const GltfFilePublicationWorkPolicy& work);
// Reuses the caller's existing file request cap. Canonical reader requests
// retain their distinct fresh cap on the same ledger. Native root is already
// admitted; relative entry and opaque paths remain borrowed through all joins.
GltfFileDiagnostic CaptureFiles(
    FileAssetState& state, NativeView root, std::string_view entry,
    std::span<const std::string_view> opaque_files,
    const GltfFileReadLimits& limits, const GltfFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const GltfFileTestOptions& testing, GltfFileExecution& execution);
void CapturePhysicalFiles(
    FileAssetState& state, NativeView root, std::string_view entry,
    std::span<const std::string_view> paths, const GltfFileReadLimits& limits,
    const GltfFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const GltfFileTestOptions& testing, GltfFileExecution& execution);
struct NativeMetadata {
  std::uint64_t volume = 0;
  std::uint64_t identity = 0;
  std::uint64_t size = 0;
  std::int64_t modified_seconds = 0;
  std::int64_t modified_fraction = 0;
  bool operator==(const NativeMetadata&) const = default;
};
struct NativeFile {
  std::intptr_t value = kInvalidFileHandle;
  std::uint64_t ordinal = 0;
  const GltfFileHooks* hooks = nullptr;
  bool fatal_close_error = true;
  NativeFile() noexcept = default;
  NativeFile(const NativeFile&) = delete;
  NativeFile& operator=(const NativeFile&) = delete;
  NativeFile(NativeFile&& other) noexcept;
  NativeFile& operator=(NativeFile&& other) noexcept;
  ~NativeFile() noexcept;
  void Close() noexcept;
};
// Every returned string uses the operation allocator. Input native paths are
// borrowed; no internal filesystem::path or stream construction is permitted.
#ifdef _WIN32
void RequireDriveAbsolutePath(NativeView path);
#endif
NativeString AbsolutePath(NativeView path,
                          const gltf_detail::AllocationContext& allocation,
                          const GltfFileLimits& limits);
NativeString JoinPath(NativeView root, std::string_view relative,
                      const gltf_detail::AllocationContext& allocation,
                      const GltfFileLimits& limits);
String RelativeEntry(NativeView root, NativeView input,
                     const gltf_detail::AllocationContext& allocation,
                     const GltfFileLimits& limits);
String ResourcePath(std::string_view entry, std::string_view uri,
                    const gltf_detail::AllocationContext& allocation,
                    const GltfFileLimits& limits);
String PortablePath(std::string_view path,
                    const gltf_detail::AllocationContext& allocation,
                    const GltfFileLimits& limits);
void RejectAliases(NativeView path, bool directory,
                   const gltf_detail::AllocationContext& allocation,
                   const GltfFileLimits& limits);
NativeFile OpenFile(NativeView path, std::uint64_t ordinal,
                    const GltfFileHooks& hooks, bool directory = false);
NativeString CanonicalPath(const NativeFile& file,
                           const gltf_detail::AllocationContext& allocation,
                           const GltfFileLimits& limits);
NativeMetadata Metadata(const NativeFile& file);
std::uint64_t MaximumReadBlockBytes() noexcept;
void ReadBlock(const NativeFile& file, std::uint64_t offset,
               std::span<std::byte> bytes);
}  // namespace meshvale::interchange::gltf_files_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_FILES_INTERNAL_H_
