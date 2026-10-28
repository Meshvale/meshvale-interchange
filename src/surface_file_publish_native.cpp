// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/surface_files.h>

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
#include "surface_files_internal.h"

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

namespace meshvale::interchange::surface_files_detail {
namespace {
#ifdef _WIN32
constexpr NativeView kCurrentDirectory = L".";
constexpr NativeView kParentDirectory = L"..";
constexpr std::size_t kWildcardUnits = 2;
#else
constexpr NativeView kCurrentDirectory = ".";
constexpr NativeView kParentDirectory = "..";
constexpr std::size_t kWildcardUnits = 0;
#endif
#ifndef _WIN32
constexpr mode_t kPrivateDirectoryMode = 0700;
#endif
constexpr std::string_view kStagePrefix = ".meshvale-surface-stage-";
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
[[noreturn]] void NativeFailure(SurfaceFileStage stage, SurfaceFileCode code,
                                std::optional<std::uint64_t> file = {},
                                std::optional<std::uint64_t> offset = {}) {
  Fail(SurfaceFileStatus::kIoFailure, stage, code, file, offset, Error());
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
  Fail(SurfaceFileStatus::kIoFailure, SurfaceFileStage::kPathPlanning,
       SurfaceFileCode::kDestinationPath, {}, {}, error);
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
    const SurfaceFileLimits& limits, TestOptions& hooks, std::stop_token stop)
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
    Fail(SurfaceFileStatus::kUnsupportedProfile,
         SurfaceFileStage::kPathPlanning, SurfaceFileCode::kDestinationPath);
  parent_.assign(NativeView(destination_).substr(0, parent_units));
  if (end + 1 == destination_.size())
    Fail(SurfaceFileStatus::kInvalidConfiguration,
         SurfaceFileStage::kPathPlanning, SurfaceFileCode::kDestinationPath);
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
  const FileHooks hooks;
  const auto canonical = [&] {
    try {
      RejectAliases(parent_, true, allocation_, limits_);
      root_handles_[0] = OpenFile(parent_, 0, hooks, true);
      auto path = CanonicalPath(root_handles_[0], allocation_, limits_);
      CloseChecked(root_handles_[0]);
      return path;
    } catch (const Failure& failure) {
      auto diagnostic = failure.diagnostic;
      diagnostic.stage = SurfaceFileStage::kPathPlanning;
      diagnostic.code = SurfaceFileCode::kDestinationParent;
      throw Failure{diagnostic};
    }
  }();
  const auto split = LastSeparator(destination_, allocation_);
  NativeString tail{gltf_detail::Allocator<NativeChar>(allocation_)};
  Assign(tail, NativeView(destination_).substr(split + 1), allocation_);
  const std::size_t separator = canonical.back() == kSeparator ? 0 : 1;
  const auto cap = limits_.max_native_path_code_units;
  Count(canonical.size() + separator + 1, cap);
  if (tail.size() > cap - canonical.size() - separator - 1)
    Fail(SurfaceFileStatus::kLimitExceeded, SurfaceFileStage::kPathPlanning,
         SurfaceFileCode::kCountLimit);
  destination_.reserve(canonical.size() + separator + tail.size());
  destination_.assign(canonical);
  if (destination_.back() != kSeparator) destination_.push_back(kSeparator);
  destination_.append(tail);
  parent_ = canonical;
  if (Present(destination_))
    Fail(SurfaceFileStatus::kDestinationExists, SurfaceFileStage::kPathPlanning,
         SurfaceFileCode::kDestinationExists);
  const std::size_t separator_units = parent_.back() == kSeparator ? 0 : 1;
  const auto stage_units =
      parent_.size() + separator_units + kStagePrefix.size() + kDecimalDigits;
  Count(stage_units + kWildcardUnits + 1, limits_.max_native_path_code_units);
  stage_.reserve(stage_units + 1);
}
void NativePublication::CheckChildExtent(std::size_t child) {
  Count(
      Sum(Sum(parent_.size(), kStagePrefix.size() + kDecimalDigits + 3), child),
      limits_.max_native_path_code_units);
}
void NativePublication::CreateStage() {
  constexpr auto kStage = SurfaceFileStage::kStaging;
  std::array<char, kDecimalDigits> digits{};
  for (std::uint64_t attempt = 0; attempt < limits_.max_stage_name_attempts;
       ++attempt) {
    allocation_.Checkpoint();
    stage_.assign(parent_);
    if (stage_.back() != kSeparator) stage_.push_back(kSeparator);
    for (char c : kStagePrefix) stage_.push_back(static_cast<NativeChar>(c));
    auto value = g_stage_sequence.fetch_add(1, std::memory_order_relaxed);
    auto result =
        std::to_chars(digits.data(), digits.data() + digits.size(), value);
    auto used = static_cast<std::size_t>(result.ptr - digits.data());
    for (std::size_t i = used; i < kDecimalDigits; ++i)
      stage_.push_back(static_cast<NativeChar>('0'));
    for (auto p = digits.data(); p != result.ptr; ++p)
      stage_.push_back(static_cast<NativeChar>(*p));
    if (hooks_.before_stage_create)
      hooks_.before_stage_create(stage_, attempt, hooks_.state);
#ifdef _WIN32
    bool created = CreateDirectoryW(stage_.c_str(), nullptr) != 0;
#else
    bool created = mkdir(stage_.c_str(), kPrivateDirectoryMode) == 0;
#endif
    if (!created) {
      auto error = Error();
#ifdef _WIN32
      bool collision = error.value() == ERROR_ALREADY_EXISTS ||
                       error.value() == ERROR_FILE_EXISTS;
#else
      bool collision = error.value() == EEXIST;
#endif
      if (collision) continue;
      Fail(SurfaceFileStatus::kIoFailure, kStage, SurfaceFileCode::kStageCreate,
           {}, {}, error);
    }
    owned_ = true;
    auto root = OpenFile(stage_, 0, {}, true, kStage);
    identity_ = Metadata(root, true);
    CloseChecked(root);
    if (hooks_.stage_ready) hooks_.stage_ready(stage_, hooks_.state);
    return;
  }
  Fail(SurfaceFileStatus::kIoFailure, kStage,
       SurfaceFileCode::kStageAttemptsExhausted);
}
void NativePublication::CreateSubdirectory(NativeView path) {
  allocation_.Checkpoint();
#ifdef _WIN32
  if (CreateDirectoryW(path.data(), nullptr)) return;
#else
  if (mkdir(path.data(), kPrivateDirectoryMode) == 0) return;
#endif
  NativeFailure(SurfaceFileStage::kStaging, SurfaceFileCode::kFileWrite);
}
WalkResult NativePublication::Walk(bool remove, InventoryVisitor visitor,
                                   void* state) noexcept {
  scratch_.assign(stage_);
  auto result = [](std::error_code error,
                   SurfaceFileCode code = SurfaceFileCode::kEnumeration,
                   SurfaceFileStatus status = SurfaceFileStatus::kIoFailure) {
    return WalkResult{error, status, code};
  };
  std::size_t depth = kInitialWalkDepth;
  std::uint64_t visited = 0;
  frames_[0].length = scratch_.size();
  while (depth) {
    if (!remove && stop_.stop_requested()) {
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return result(std::make_error_code(std::errc::operation_canceled),
                    SurfaceFileCode::kStopRequested,
                    SurfaceFileStatus::kCanceled);
    }
    auto& frame = frames_[depth - 1];
    scratch_.resize(frame.length);
    NativeView name;
    bool directory = false;
    bool alias = false;
    bool ordinary = false;
    bool exhausted = false;
    std::error_code error;
    auto operation_code = SurfaceFileCode::kEnumeration;
#ifdef _WIN32
    auto& entry = entries_[0].data;
    if (frame.handle == INVALID_HANDLE_VALUE) {
      if (scratch_.size() + kWildcardUnits + 1 >
          limits_.max_native_path_code_units) {
        for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
        return result(std::make_error_code(std::errc::filename_too_long),
                      SurfaceFileCode::kCountLimit,
                      SurfaceFileStatus::kLimitExceeded);
      } else {
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
      const int fd = open(scratch_.c_str(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
      frame.handle = fd < 0 ? nullptr : fdopendir(fd);
      if (!frame.handle) {
        error = Error();
        if (fd < 0) operation_code = SurfaceFileCode::kFileOpen;
      }
      if (fd >= 0 && !frame.handle) (void)close(fd);
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
      return result(error, operation_code);
    }
    if (exhausted) {
      error = frame.Close();
      if (error) {
        for (std::size_t i = 0; i + 1 < depth; ++i) (void)frames_[i].Close();
        return result(error, SurfaceFileCode::kFileClose);
      }
      if (remove) {
        error = Remove(scratch_, true);
        if (error) {
          for (std::size_t i = 0; i + 1 < depth; ++i) frames_[i].Close();
          return result(error);
        }
      }
      --depth;
      continue;
    }
    if (name == kCurrentDirectory || name == kParentDirectory) continue;
    if (visited >= limits_.max_inventory_entries) {
      for (std::size_t i = 0; i < depth; ++i) (void)frames_[i].Close();
      return result(std::make_error_code(std::errc::value_too_large),
                    SurfaceFileCode::kCountLimit,
                    SurfaceFileStatus::kLimitExceeded);
    }
    ++visited;
    if (scratch_.size() + name.size() + 2 >
        limits_.max_native_path_code_units) {
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return result(std::make_error_code(std::errc::filename_too_long),
                    SurfaceFileCode::kCountLimit,
                    SurfaceFileStatus::kLimitExceeded);
    }
    scratch_.push_back(kSeparator);
    scratch_.append(name);
#ifndef _WIN32
    struct stat metadata{};
    if (lstat(scratch_.c_str(), &metadata) != 0) {
      error = Error();
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return result(error, SurfaceFileCode::kFileRead);
    }
    directory = S_ISDIR(metadata.st_mode);
    alias = S_ISLNK(metadata.st_mode);
    ordinary = S_ISREG(metadata.st_mode) || directory;
#endif
    if (!remove && (alias || !ordinary)) {
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return result(std::make_error_code(std::errc::state_not_recoverable),
                    SurfaceFileCode::kFileKind,
                    SurfaceFileStatus::kVerificationFailed);
    }
    if (!remove && !visitor(scratch_, directory, state)) {
      for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
      return result(std::make_error_code(std::errc::state_not_recoverable),
                    SurfaceFileCode::kMemberInventory,
                    SurfaceFileStatus::kVerificationFailed);
    }
    if (directory && !alias) {
      if (depth >= frames_.size()) {
        for (std::size_t i = 0; i < depth; ++i) (void)frames_[i].Close();
        return result(std::make_error_code(std::errc::value_too_large),
                      SurfaceFileCode::kCountLimit,
                      SurfaceFileStatus::kLimitExceeded);
      }
      frames_[depth].length = scratch_.size();
      ++depth;
    } else if (remove) {
      error = Remove(scratch_, directory);
      if (error) {
        for (std::size_t i = 0; i < depth; ++i) frames_[i].Close();
        return result(error);
      }
    }
  }
  return {};
}
void NativePublication::Inventory(InventoryVisitor visitor, void* state,
                                  bool receipt_present, bool final) {
  allocation_.Checkpoint();
  if (!final && hooks_.before_inventory)
    hooks_.before_inventory(stage_, receipt_present, hooks_.state);
  if (!final && hooks_.files.io_error) {
    auto error = hooks_.files.io_error(IoOperation::kEnumerate,
                                       SurfaceFileStage::kInventory, 0, 0,
                                       hooks_.files.state);
    if (error)
      Fail(SurfaceFileStatus::kIoFailure, SurfaceFileStage::kInventory,
           SurfaceFileCode::kEnumeration, {}, {}, error);
  }
  auto root = OpenFile(stage_, 0, {}, true, SurfaceFileStage::kInventory);
  auto actual = Metadata(root, true);
  CloseChecked(root);
  if (actual.volume != identity_.volume ||
      actual.identity != identity_.identity)
    Fail(SurfaceFileStatus::kVerificationFailed, SurfaceFileStage::kInventory,
         SurfaceFileCode::kFileChanged);
  const auto walk = Walk(false, visitor, state);
  if (walk.error)
    Fail(walk.status, SurfaceFileStage::kInventory, walk.code, {}, {},
         walk.error);
}
SurfaceFileCleanupOutcome NativePublication::Cleanup(
    std::error_code& error) noexcept {
  if (!owned_) return SurfaceFileCleanupOutcome::kNotRequired;
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
  if (!error) error = Walk(true, nullptr, nullptr).error;
  const auto outcome = error ? SurfaceFileCleanupOutcome::kFailed
                             : SurfaceFileCleanupOutcome::kRemoved;
  // One attempt per operation. A failed cleanup retains the actual sibling;
  // destructor retry must not falsify the separately reported failure.
  owned_ = false;
  if (hooks_.cleanup_finished)
    hooks_.cleanup_finished(outcome, error, hooks_.state);
  return outcome;
}
void NativePublication::Scrub() noexcept {
  for (auto* path : {&destination_, &parent_, &stage_, &scratch_}) {
    for (auto& unit : *path) unit = NativeChar{};
    path->clear();
  }
}
void NativePublication::Commit(SurfaceFileCommitObservation& observation,
                               std::error_code injected) {
  if (injected)
    Fail(SurfaceFileStatus::kPublicationFailed, SurfaceFileStage::kPublication,
         SurfaceFileCode::kCommitFailed, {}, {}, injected);
  const auto result = file_publication_detail::CommitNoReplace(
      stage_.c_str(), destination_.c_str());
  using C = file_publication_detail::CommitStatus;
  observation.error = result.error
                          ? std::optional<std::error_code>(result.error)
                          : std::nullopt;
  switch (result.status) {
    case C::kCommitted:
      owned_ = false;
      observation.outcome = SurfaceFileCommitOutcome::kCommitted;
      if (hooks_.native_commit_observed)
        hooks_.native_commit_observed->store(true, std::memory_order_release);
      return;
    case C::kDestinationExists:
      observation.outcome = SurfaceFileCommitOutcome::kDestinationExists;
      Fail(SurfaceFileStatus::kDestinationExists,
           SurfaceFileStage::kPublication, SurfaceFileCode::kDestinationExists,
           {}, {}, result.error);
    case C::kFailed:
      observation.outcome = SurfaceFileCommitOutcome::kFailed;
      Fail(SurfaceFileStatus::kPublicationFailed,
           SurfaceFileStage::kPublication, SurfaceFileCode::kCommitFailed, {},
           {}, result.error);
    case C::kUnsupported:
      observation.outcome = SurfaceFileCommitOutcome::kUnsupported;
      Fail(SurfaceFileStatus::kUnsupportedProfile,
           SurfaceFileStage::kPublication, SurfaceFileCode::kCommitUnsupported,
           {}, {}, result.error);
  }
}
}  // namespace meshvale::interchange::surface_files_detail
