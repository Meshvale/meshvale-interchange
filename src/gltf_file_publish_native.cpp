// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <utility>

#include "file_publication_native.h"
#include "gltf_allocation.h"
#include "gltf_file_publish_internal.h"
#include "gltf_files_internal.h"
#include "gltf_files_test_internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace meshvale::interchange::gltf_files_detail {
namespace {
#ifdef _WIN32
constexpr NativeChar kSeparator = L'\\';
constexpr NativeView kCurrentDirectory = L".";
constexpr NativeView kParentDirectory = L"..";
constexpr std::size_t kWildcardUnits = 2;
#else
constexpr NativeChar kSeparator = '/';
constexpr NativeView kCurrentDirectory = ".";
constexpr NativeView kParentDirectory = "..";
constexpr std::size_t kWildcardUnits = 0;
#endif
constexpr std::string_view kStagePrefix = ".meshvale-gltf-stage-";
constexpr std::size_t kDecimalDigits = 20;
constexpr std::size_t kInitialWalkDepth = 1;
std::atomic<std::uint64_t> g_stage_sequence{0};
std::error_code Error() noexcept {
#ifdef _WIN32
  return {static_cast<int>(GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}
[[noreturn]] void NativeFailure(GltfFileStage stage,
                                GltfFileDiagnosticCode code,
                                std::optional<std::uint64_t> file = {},
                                std::optional<std::uint64_t> offset = {}) {
  Fail(GltfFileStatus::kIoFailure, stage, code, file, offset, Error());
}
std::error_code Remove(NativeView path, bool directory) noexcept {
#ifdef _WIN32
  if ((directory ? RemoveDirectoryW(path.data()) : DeleteFileW(path.data())))
    return {};
#else
  if ((directory ? rmdir(path.data()) : unlink(path.data())) == 0) return {};
#endif
  return Error();
}
bool Missing(const std::error_code& error) noexcept {
#ifdef _WIN32
  return error.value() == ERROR_FILE_NOT_FOUND ||
         error.value() == ERROR_PATH_NOT_FOUND;
#else
  return error.value() == ENOENT;
#endif
}
bool Present(NativeView path) {
#ifdef _WIN32
  if (GetFileAttributesW(path.data()) != INVALID_FILE_ATTRIBUTES) return true;
#else
  struct stat info{};
  if (lstat(path.data(), &info) == 0) return true;
#endif
  const auto error = Error();
  if (Missing(error)) return false;
  Fail(GltfFileStatus::kIoFailure, GltfFileStage::kPathPlanning,
       GltfFileDiagnosticCode::kDestinationPath, {}, {}, error);
}
}  // namespace
struct WalkFrame {
  std::size_t length = 0;
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
  DIR* handle = nullptr;
#endif
  std::error_code Close() noexcept {
    std::error_code error;
#ifdef _WIN32
    if (handle != INVALID_HANDLE_VALUE && !FindClose(handle)) error = Error();
    handle = INVALID_HANDLE_VALUE;
#else
    if (handle && closedir(handle) != 0) error = Error();
    handle = nullptr;
#endif
    return error;
  }
};
struct WalkEntry {
#ifdef _WIN32
  WIN32_FIND_DATAW data{};
#endif
};
NativePublication::NativePublication(
    const gltf_detail::AllocationContext& allocation,
    const GltfFileLimits& limits, const GltfFilePublicationHooks& hooks,
    std::stop_token stop)
    : allocation_(allocation),
      limits_(limits),
      hooks_(hooks),
      destination_(gltf_detail::Allocator<NativeChar>(allocation)),
      parent_(gltf_detail::Allocator<NativeChar>(allocation)),
      stage_(gltf_detail::Allocator<NativeChar>(allocation)),
      scratch_(gltf_detail::Allocator<NativeChar>(allocation)),
      frames_(gltf_detail::Allocator<WalkFrame>(allocation)),
      entries_(gltf_detail::Allocator<WalkEntry>(allocation)),
      root_handles_(gltf_detail::Allocator<NativeFile>(allocation)),
      stop_(stop) {}
NativePublication::~NativePublication() noexcept {
  std::error_code ignored;
  if (owned_) (void)Cleanup(ignored);
}
void NativePublication::Plan(NativeView destination) {
  destination_ = AbsolutePath(destination, allocation_, limits_);
  const auto end = destination_.rfind(kSeparator);
#ifdef _WIN32
  const auto minimum = kDriveRootUnits;
  const auto parent_units = end == kDriveRootUnits - 1 ? kDriveRootUnits : end;
#else
  constexpr std::size_t kRootUnits = 1;
  const auto minimum = kRootUnits;
  const auto parent_units = end == 0 ? kRootUnits : end;
#endif
  if (destination_.size() <= minimum || end == destination_.npos)
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kDestinationPath);
  parent_.assign(NativeView(destination_).substr(0, parent_units));
  (void)RelativeEntry(parent_, destination_, allocation_, limits_);
  const std::size_t separator_units = parent_.back() == kSeparator ? 0 : 1;
  const auto stage_units =
      parent_.size() + separator_units + kStagePrefix.size() + kDecimalDigits;
  Count(stage_units + kWildcardUnits + 1, limits_.max_native_path_code_units);
  stage_.reserve(stage_units + 1);
  scratch_.reserve(
      static_cast<std::size_t>(limits_.max_native_path_code_units));
  // At least one path separator is required per nested directory. This admits
  // a conservative full depth without later growth, including injected extras.
  frames_.resize(static_cast<std::size_t>(limits_.max_native_path_code_units));
  entries_.resize(kInitialWalkDepth);
  root_handles_.resize(kInitialWalkDepth);
}
void NativePublication::AdmitParent() {
  const GltfFileHooks hooks;
  const auto canonical = [&] {
    try {
      RejectAliases(parent_, true, allocation_, limits_);
      root_handles_[0] = OpenFile(parent_, 0, hooks, true);
      root_handles_[0].fatal_close_error = false;
      auto path = CanonicalPath(root_handles_[0], allocation_, limits_);
      CloseWrittenFile(root_handles_[0]);
      return path;
    } catch (const Failure& failure) {
      auto diagnostic = failure.diagnostic;
      diagnostic.stage = GltfFileStage::kPathPlanning;
      diagnostic.code = GltfFileDiagnosticCode::kDestinationParent;
      throw Failure{diagnostic};
    }
  }();
  const auto tail = RelativeEntry(parent_, destination_, allocation_, limits_);
  destination_ = JoinPath(canonical, tail, allocation_, limits_);
  parent_ = canonical;
  if (Present(destination_))
    Fail(GltfFileStatus::kDestinationExists, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kDestinationExists);
  const std::size_t separator_units = parent_.back() == kSeparator ? 0 : 1;
  const auto stage_units =
      parent_.size() + separator_units + kStagePrefix.size() + kDecimalDigits;
  Count(stage_units + kWildcardUnits + 1, limits_.max_native_path_code_units);
  stage_.reserve(stage_units + 1);
  constexpr unsigned kAttempts = 100;
  std::array<char, kDecimalDigits> digits{};
  for (unsigned attempt = 0; attempt < kAttempts; ++attempt) {
    allocation_.Checkpoint();
    stage_.assign(parent_);
    if (stage_.back() != kSeparator) stage_.push_back(kSeparator);
    for (const char c : kStagePrefix) stage_.push_back(c);
    const auto value = g_stage_sequence.fetch_add(1, std::memory_order_relaxed);
    const auto encoded =
        std::to_chars(digits.data(), digits.data() + digits.size(), value);
    // Fixed width keeps path-growth requests stable across decimal boundaries.
    // The complete width was already admitted and reserved before staging.
    const auto used = static_cast<std::size_t>(encoded.ptr - digits.data());
    for (std::size_t i = used; i < kDecimalDigits; ++i) stage_.push_back('0');
    for (auto p = digits.data(); p != encoded.ptr; ++p) stage_.push_back(*p);
    if (!Present(stage_)) return;
  }
  Fail(GltfFileStatus::kIoFailure, GltfFileStage::kStaging,
       GltfFileDiagnosticCode::kDestinationPath);
}
void NativePublication::CreateStage() {
  allocation_.Checkpoint();
#ifdef _WIN32
  const bool created = CreateDirectoryW(stage_.c_str(), nullptr) != 0;
#else
  constexpr mode_t kPrivateDirectoryMode = 0700;
  const bool created = mkdir(stage_.c_str(), kPrivateDirectoryMode) == 0;
#endif
  if (created) {
    owned_ = true;
    const GltfFileHooks hooks;
    root_handles_[0] = OpenFile(stage_, 0, hooks, true);
    root_handles_[0].fatal_close_error = false;
    identity_ = Metadata(root_handles_[0]);
    CloseWrittenFile(root_handles_[0]);
    if (hooks_.stage_ready) hooks_.stage_ready(stage_, hooks_.state);
    return;
  }
  NativeFailure(GltfFileStage::kStaging,
                GltfFileDiagnosticCode::kDestinationPath);
}
void NativePublication::CreateSubdirectory(NativeView path) {
  allocation_.Checkpoint();
#ifdef _WIN32
  if (CreateDirectoryW(path.data(), nullptr)) return;
#else
  constexpr mode_t kPrivateDirectoryMode = 0700;
  if (mkdir(path.data(), kPrivateDirectoryMode) == 0) return;
#endif
  NativeFailure(GltfFileStage::kStaging, GltfFileDiagnosticCode::kFileWrite);
}
std::error_code NativePublication::Walk(bool remove, InventoryVisitor visitor,
                                        void* state) noexcept {
  scratch_.assign(stage_);
  std::size_t depth = kInitialWalkDepth;
  frames_[0].length = scratch_.size();
  while (depth) {
    if (!remove && stop_.stop_requested()) {
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return std::make_error_code(std::errc::operation_canceled);
    }
    auto& frame = frames_[depth - 1];
    scratch_.resize(frame.length);
    NativeView name;
    bool directory = false;
    bool alias = false;
    bool ordinary = false;
    bool exhausted = false;
    std::error_code error;
#ifdef _WIN32
    auto& entry = entries_[0].data;
    if (frame.handle == INVALID_HANDLE_VALUE) {
      if (scratch_.size() + kWildcardUnits + 1 >
          limits_.max_native_path_code_units)
        error = std::make_error_code(std::errc::filename_too_long);
      else {
        scratch_.push_back(kSeparator);
        scratch_.push_back(L'*');
        frame.handle = FindFirstFileW(scratch_.c_str(), &entry);
        scratch_.resize(frame.length);
        if (frame.handle == INVALID_HANDLE_VALUE) {
          error = Error();
          exhausted = error.value() == ERROR_FILE_NOT_FOUND;
          if (exhausted) error.clear();
        }
      }
    } else if (!FindNextFileW(frame.handle, &entry)) {
      error = Error();
      exhausted = error.value() == ERROR_NO_MORE_FILES;
      if (exhausted) error.clear();
    }
    if (!error && !exhausted) {
      name = entry.cFileName;
      directory = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      alias = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
      ordinary =
          !alias && (entry.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) == 0;
    }
#else
    if (!frame.handle) {
      frame.handle = opendir(scratch_.c_str());
      if (!frame.handle) error = Error();
    }
    if (!error) {
      errno = 0;
      const auto item = readdir(frame.handle);
      if (!item) {
        exhausted = errno == 0;
        if (!exhausted) error = Error();
      } else
        name = item->d_name;
    }
#endif
    if (error) {
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return error;
    }
    if (exhausted) {
      error = frame.Close();
      if (error) {
        for (std::size_t i = 0; i + 1 < depth; ++i) (void)frames_[i].Close();
        return error;
      }
      if (remove) {
        error = Remove(scratch_, true);
        if (error) {
          for (std::size_t i = 0; i + 1 < depth; ++i) frames_[i].Close();
          return error;
        }
      }
      --depth;
      continue;
    }
    if (name == kCurrentDirectory || name == kParentDirectory) continue;
    if (scratch_.size() + name.size() + 2 >
        limits_.max_native_path_code_units) {
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return std::make_error_code(std::errc::filename_too_long);
    }
    scratch_.push_back(kSeparator);
    scratch_.append(name);
#ifndef _WIN32
    struct stat metadata{};
    if (lstat(scratch_.c_str(), &metadata) != 0) {
      error = Error();
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return error;
    }
    directory = S_ISDIR(metadata.st_mode);
    alias = S_ISLNK(metadata.st_mode);
    ordinary = S_ISREG(metadata.st_mode) || directory;
#endif
    if (!remove &&
        (alias || !ordinary || !visitor(scratch_, directory, state))) {
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return std::make_error_code(std::errc::state_not_recoverable);
    }
    if (directory && !alias) {
      frames_[depth].length = scratch_.size();
      ++depth;
    } else if (remove) {
      error = Remove(scratch_, directory);
      if (error) {
        for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
        return error;
      }
    }
  }
  return {};
}
void NativePublication::Inventory(InventoryVisitor visitor, void* state) {
  allocation_.Checkpoint();
  if (hooks_.before_inventory) hooks_.before_inventory(stage_, hooks_.state);
  const auto error = Walk(false, visitor, state);
  if (error == std::errc::operation_canceled)
    Fail(GltfFileStatus::kCanceled, GltfFileStage::kInventory,
         GltfFileDiagnosticCode::kStopRequested);
  if (error)
    Fail(GltfFileStatus::kPreservationMismatch, GltfFileStage::kInventory,
         GltfFileDiagnosticCode::kStagedInventory, {}, {}, error);
}
GltfFileCleanupOutcome NativePublication::Cleanup(
    std::error_code& error) noexcept {
  if (!owned_) return GltfFileCleanupOutcome::kNotRequired;
  if (hooks_.cleanup_error) error = hooks_.cleanup_error(stage_, hooks_.state);
  if (!error) {
    // Native identity checks use only fixed records; no charged request follows
    // an allocation refusal or cancellation. No alias traversal is permitted.
#ifdef _WIN32
    const auto handle = CreateFileW(
        stage_.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
      error = Error();
    else {
      FILE_ATTRIBUTE_TAG_INFO tag{};
      BY_HANDLE_FILE_INFORMATION info{};
      constexpr unsigned kWordBits = 32;
      if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                        sizeof(tag)) ||
          !GetFileInformationByHandle(handle, &info))
        error = Error();
      else if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
               info.dwVolumeSerialNumber != identity_.volume ||
               ((static_cast<std::uint64_t>(info.nFileIndexHigh) << kWordBits) |
                info.nFileIndexLow) != identity_.identity)
        error = std::make_error_code(std::errc::state_not_recoverable);
      if (!CloseHandle(handle) && !error) error = Error();
    }
#else
    struct stat metadata{};
    if (lstat(stage_.c_str(), &metadata) != 0)
      error = Error();
    else if (!S_ISDIR(metadata.st_mode) ||
             static_cast<std::uint64_t>(metadata.st_dev) != identity_.volume ||
             static_cast<std::uint64_t>(metadata.st_ino) != identity_.identity)
      error = std::make_error_code(std::errc::state_not_recoverable);
#endif
  }
  if (!error) error = Walk(true, nullptr, nullptr);
  const auto outcome = error ? GltfFileCleanupOutcome::kFailed
                             : GltfFileCleanupOutcome::kRemoved;
  // One attempt per operation. A failed cleanup retains the actual sibling;
  // destructor retry must not falsify the separately reported failure.
  owned_ = false;
  if (hooks_.cleanup_finished)
    hooks_.cleanup_finished(outcome, error, hooks_.state);
  return outcome;
}
void NativePublication::Commit() {
  allocation_.Checkpoint();
  if (hooks_.before_commit) hooks_.before_commit(stage_, hooks_.state);
  allocation_.Checkpoint();
  const auto result = file_publication_detail::CommitNoReplace(
      stage_.c_str(), destination_.c_str());
  using file_publication_detail::CommitStatus;
  switch (result.status) {
    case CommitStatus::kCommitted:
      owned_ = false;
      return;
    case CommitStatus::kDestinationExists:
      Fail(GltfFileStatus::kDestinationExists, GltfFileStage::kPublication,
           GltfFileDiagnosticCode::kDestinationExists, {}, {}, result.error);
    case CommitStatus::kFailed:
      Fail(GltfFileStatus::kPublicationFailed, GltfFileStage::kPublication,
           GltfFileDiagnosticCode::kPublicationFailed, {}, {}, result.error);
    case CommitStatus::kUnsupported:
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPublication,
           GltfFileDiagnosticCode::kPublicationUnsupported, {}, {},
           result.error);
  }
}
NativeFile OpenWrite(NativeView path, std::uint64_t ordinal,
                     const GltfFileHooks& hooks, bool exclusive) {
  NativeFile result;
  result.ordinal = ordinal;
  result.hooks = &hooks;
  result.fatal_close_error = false;
  if (hooks.before_open) hooks.before_open(ordinal, hooks.state);
#ifdef _WIN32
  const auto handle =
      CreateFileW(path.data(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, exclusive ? CREATE_NEW : OPEN_EXISTING,
                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    NativeFailure(GltfFileStage::kStaging, GltfFileDiagnosticCode::kFileWrite,
                  ordinal);
  result.value = reinterpret_cast<std::intptr_t>(handle);
  if (hooks.opened) hooks.opened(ordinal, hooks.state);
  FILE_ATTRIBUTE_TAG_INFO tag{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                    sizeof(tag)))
    NativeFailure(GltfFileStage::kStaging, GltfFileDiagnosticCode::kFileWrite,
                  ordinal);
  if ((tag.FileAttributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
      GetFileType(handle) != FILE_TYPE_DISK)
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kStaging,
         GltfFileDiagnosticCode::kResourceNotRegularFile, ordinal);
#else
  constexpr mode_t kPrivateFileMode = 0600;
  result.value = open(path.data(),
                      O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                          (exclusive ? O_CREAT | O_EXCL : 0),
                      kPrivateFileMode);
  if (result.value == kInvalidFileHandle)
    NativeFailure(GltfFileStage::kStaging, GltfFileDiagnosticCode::kFileWrite,
                  ordinal);
  if (hooks.opened) hooks.opened(ordinal, hooks.state);
  struct stat info{};
  if (fstat(static_cast<int>(result.value), &info) != 0)
    NativeFailure(GltfFileStage::kStaging, GltfFileDiagnosticCode::kFileWrite,
                  ordinal);
  if (!S_ISREG(info.st_mode))
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kStaging,
         GltfFileDiagnosticCode::kResourceNotRegularFile, ordinal);
#endif
  return result;
}
void CloseWrittenFile(NativeFile& file) {
  if (file.value == kInvalidFileHandle) return;
  const auto value = std::exchange(file.value, kInvalidFileHandle);
#ifdef _WIN32
  const bool failed = !CloseHandle(reinterpret_cast<HANDLE>(value));
#else
  const bool failed = close(static_cast<int>(value)) != 0;
#endif
  const auto error = failed ? Error() : std::error_code{};
  if (file.hooks && file.hooks->closed)
    file.hooks->closed(file.ordinal, file.hooks->state);
  if (error)
    Fail(GltfFileStatus::kIoFailure, GltfFileStage::kStaging,
         GltfFileDiagnosticCode::kFileWrite, file.ordinal, {}, error);
}
void WriteBlock(const NativeFile& file, std::uint64_t offset,
                std::span<const std::byte> bytes) {
#ifdef _WIN32
  Count(offset, std::numeric_limits<LONGLONG>::max(), GltfFileStage::kStaging);
  Count(bytes.size(), std::numeric_limits<DWORD>::max(),
        GltfFileStage::kStaging);
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  const auto handle = reinterpret_cast<HANDLE>(file.value);
  if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
    NativeFailure(GltfFileStage::kStaging, GltfFileDiagnosticCode::kFileWrite,
                  file.ordinal, offset);
  DWORD written = 0;
  if (!WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()),
                 &written, nullptr))
    NativeFailure(GltfFileStage::kStaging, GltfFileDiagnosticCode::kFileWrite,
                  file.ordinal, offset);
  if (written != bytes.size())
    Fail(GltfFileStatus::kIoFailure, GltfFileStage::kStaging,
         GltfFileDiagnosticCode::kFileWrite, file.ordinal, offset + written);
#else
  Count(offset, std::numeric_limits<off_t>::max(), GltfFileStage::kStaging);
  Count(bytes.size(), std::numeric_limits<ssize_t>::max(),
        GltfFileStage::kStaging);
  std::size_t begin = 0;
  while (begin < bytes.size()) {
    ssize_t written;
    do {
      written =
          pwrite(static_cast<int>(file.value), bytes.data() + begin,
                 bytes.size() - begin, static_cast<off_t>(offset + begin));
    } while (written < 0 && errno == EINTR);
    if (written < 0)
      NativeFailure(GltfFileStage::kStaging, GltfFileDiagnosticCode::kFileWrite,
                    file.ordinal, offset + begin);
    if (written == 0)
      Fail(GltfFileStatus::kIoFailure, GltfFileStage::kStaging,
           GltfFileDiagnosticCode::kFileWrite, file.ordinal, offset + begin);
    begin += static_cast<std::size_t>(written);
  }
#endif
}
}  // namespace meshvale::interchange::gltf_files_detail
