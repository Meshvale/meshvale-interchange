// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/ply_files.h>

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
#include <string_view>
#include <system_error>
#include <utility>

#include "file_publication_native.h"
#include "gltf_allocation.h"
#include "ply_file_internal.h"
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

namespace meshvale::interchange::ply_files_detail {
namespace {
constexpr std::string_view kStagePrefix = ".meshvale-ply-stage-";
constexpr std::size_t kSequenceDigits = 20;
constexpr unsigned kStageAttempts = 100;
std::atomic<std::uint64_t> g_sequence{0};
std::error_code Error() noexcept {
#ifdef _WIN32
  return {static_cast<int>(GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}
bool Missing(std::error_code error) noexcept {
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
  struct stat value{};
  if (lstat(path.data(), &value) == 0) return true;
#endif
  const auto error = Error();
  if (Missing(error)) return false;
  Fail(PlyFileStatus::kIoFailure, PlyFileStage::kPathPlanning,
       PlyFileDiagnosticCode::kOpen, {}, {}, error);
}
std::error_code Remove(NativeView path, bool directory) noexcept {
#ifdef _WIN32
  if (directory ? RemoveDirectoryW(path.data()) : DeleteFileW(path.data()))
    return {};
#else
  if ((directory ? rmdir(path.data()) : unlink(path.data())) == 0) return {};
#endif
  return Error();
}
bool SameIdentity(NativeView path, const NativeMetadata& expected,
                  std::error_code& error) noexcept {
#ifdef _WIN32
  const auto handle = CreateFileW(
      path.data(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    error = Error();
    return false;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  FILE_ATTRIBUTE_TAG_INFO tag{};
  bool same = GetFileInformationByHandle(handle, &info) &&
              GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                           sizeof(tag));
  if (!same) error = Error();
  constexpr unsigned kWordBits = 32;
  same = same && (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
         info.dwVolumeSerialNumber == expected.volume &&
         ((static_cast<std::uint64_t>(info.nFileIndexHigh) << kWordBits) |
          info.nFileIndexLow) == expected.identity;
  if (!CloseHandle(handle) && !error) error = Error();
#else
  struct stat info{};
  if (lstat(path.data(), &info) != 0) {
    error = Error();
    return false;
  }
  const bool same =
      S_ISDIR(info.st_mode) &&
      static_cast<std::uint64_t>(info.st_dev) == expected.volume &&
      static_cast<std::uint64_t>(info.st_ino) == expected.identity;
#endif
  if (!same && !error)
    error = std::make_error_code(std::errc::state_not_recoverable);
  return same && !error;
}
}  // namespace
void CloseChecked(NativeFile& file, const TestOptions& testing,
                  PlyFileStage stage) {
  if (file.value == kInvalidFileHandle) return;
  const auto value = std::exchange(file.value, kInvalidFileHandle);
#ifdef _WIN32
  const bool failed = !CloseHandle(reinterpret_cast<HANDLE>(value));
#else
  const bool failed = close(static_cast<int>(value)) != 0;
#endif
  auto error = failed ? Error() : std::error_code{};
  if (file.hooks.closed) file.hooks.closed(file.ordinal, file.hooks.state);
  if (testing.close_error) {
    const auto injected = testing.close_error(file.ordinal, testing.state);
    if (!error) error = injected;
  }
  if (error)
    Fail(PlyFileStatus::kIoFailure, stage, PlyFileDiagnosticCode::kClose,
         file.ordinal, {}, error);
}
void WriteFile(NativeView path, std::span<const std::byte> bytes,
               std::uint64_t ordinal, const PlyFileWorkPolicy& work,
               const allocation::AllocationContext& allocator,
               const TestOptions& testing) {
  NativeFile file;
  file.ordinal = ordinal;
  file.stage = PlyFileStage::kStaging;
  file.hooks = testing.files;
  if (file.hooks.before_open) file.hooks.before_open(ordinal, file.hooks.state);
#ifdef _WIN32
  const auto handle =
      CreateFileW(path.data(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    Fail(PlyFileStatus::kIoFailure, PlyFileStage::kStaging,
         PlyFileDiagnosticCode::kOpen, ordinal, {}, Error());
  file.value = reinterpret_cast<std::intptr_t>(handle);
#else
  constexpr mode_t kPrivateFileMode = 0600;
  file.value =
      open(path.data(),
           O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_CREAT | O_EXCL,
           kPrivateFileMode);
  if (file.value == kInvalidFileHandle)
    Fail(PlyFileStatus::kIoFailure, PlyFileStage::kStaging,
         PlyFileDiagnosticCode::kOpen, ordinal, {}, Error());
#endif
  if (file.hooks.opened) file.hooks.opened(ordinal, file.hooks.state);
  (void)Metadata(file);
  const auto block = static_cast<std::size_t>(
      std::min({work.io_block_bytes, work.cancellation_block_bytes,
                MaximumReadBlockBytes()}));
  for (std::size_t offset = 0; offset < bytes.size();) {
    if (testing.checkpoint)
      testing.checkpoint(PlyFileStage::kStaging, offset, testing.state);
    allocator.Checkpoint();
    const auto size = std::min(block, bytes.size() - offset);
#ifdef _WIN32
    DWORD written = 0;
    if (!::WriteFile(reinterpret_cast<HANDLE>(file.value),
                     bytes.data() + offset, static_cast<DWORD>(size), &written,
                     nullptr))
      Fail(PlyFileStatus::kIoFailure, PlyFileStage::kStaging,
           PlyFileDiagnosticCode::kWrite, ordinal, offset, Error());
    const auto count = static_cast<std::size_t>(written);
#else
    ssize_t written;
    do {
      written =
          write(static_cast<int>(file.value), bytes.data() + offset, size);
    } while (written < 0 && errno == EINTR);
    if (written < 0)
      Fail(PlyFileStatus::kIoFailure, PlyFileStage::kStaging,
           PlyFileDiagnosticCode::kWrite, ordinal, offset, Error());
    const auto count = static_cast<std::size_t>(written);
#endif
    if (!count)
      Fail(PlyFileStatus::kIoFailure, PlyFileStage::kStaging,
           PlyFileDiagnosticCode::kWrite, ordinal, offset);
    offset += count;
  }
  CloseChecked(file, testing, PlyFileStage::kStaging);
  allocator.Checkpoint();
}
NativePublication::NativePublication(
    const allocation::AllocationContext& allocator, const PlyFileLimits& limits,
    const TestOptions& testing)
    : allocation_(allocator),
      limits_(limits),
      testing_(testing),
      destination_(allocation::Allocator<NativeChar>(allocator)),
      parent_(allocation::Allocator<NativeChar>(allocator)),
      stage_(allocation::Allocator<NativeChar>(allocator)),
      entry_(allocation::Allocator<NativeChar>(allocator)),
      receipt_(allocation::Allocator<NativeChar>(allocator)),
      wildcard_(allocation::Allocator<NativeChar>(allocator)) {}
NativePublication::~NativePublication() noexcept {
  if (owned_) {
    std::error_code ignored;
    (void)Cleanup(ignored);
  }
}
void NativePublication::Plan(NativeView destination, std::string_view entry,
                             std::optional<std::string_view> receipt) {
  destination_ = AbsolutePath(destination, allocation_, limits_);
  const auto separator = LastSeparator(destination_, allocation_);
#ifdef _WIN32
  const auto root = kDriveRootUnits;
#else
  constexpr std::size_t root = 1;
#endif
  if (separator == destination_.npos || destination_.size() <= root ||
      separator + 1 == destination_.size())
    Fail(PlyFileStatus::kUnsupportedProfile, PlyFileStage::kPathPlanning,
         PlyFileDiagnosticCode::kPathUnsupported);
  Assign(parent_, NativeView(destination_).substr(0, std::max(separator, root)),
         allocation_);
  RejectAliases(parent_, true, allocation_, limits_);
  auto parent =
      OpenFile(parent_, 0, testing_.files, true, PlyFileStage::kPathPlanning);
  auto canonical = CanonicalPath(parent, allocation_, limits_);
  CloseChecked(parent, testing_, PlyFileStage::kPathPlanning);
  // Admission precedes use of canonical parent. Logical alias traversal has
  // already been refused; canonicalization never makes an alias acceptable.
  parent_ = std::move(canonical);
  if (Present(destination_))
    Fail(PlyFileStatus::kDestinationExists, PlyFileStage::kPathPlanning,
         PlyFileDiagnosticCode::kDestinationExists);
  Count(parent_.size() + (parent_.back() == kSeparator ? 0 : 1) +
            kStagePrefix.size() + kSequenceDigits + 1,
        limits_.max_native_path_code_units);
  stage_.reserve(static_cast<std::size_t>(limits_.max_native_path_code_units));
  std::array<char, kSequenceDigits> digits{};
  for (unsigned attempt = 0; attempt < kStageAttempts; ++attempt) {
    Assign(stage_, NativeView(parent_), allocation_);
    if (stage_.back() != kSeparator) stage_.push_back(kSeparator);
    for (char value : kStagePrefix)
      stage_.push_back(static_cast<NativeChar>(value));
    const auto sequence = g_sequence.fetch_add(1, std::memory_order_relaxed);
    const auto converted =
        std::to_chars(digits.data(), digits.data() + digits.size(), sequence);
    Count(stage_.size() + kSequenceDigits + 1,
          limits_.max_native_path_code_units);
    const auto used = static_cast<std::size_t>(converted.ptr - digits.data());
    for (std::size_t i = used; i < kSequenceDigits; ++i)
      stage_.push_back(static_cast<NativeChar>('0'));
    for (auto p = digits.data(); p != converted.ptr; ++p)
      stage_.push_back(static_cast<NativeChar>(*p));
    if (!Present(stage_)) {
      entry_ = JoinPath(stage_, entry, allocation_, limits_);
      if (receipt) receipt_ = JoinPath(stage_, *receipt, allocation_, limits_);
      wildcard_.clear();
      wildcard_.reserve(stage_.size() + 2);
      Extend(wildcard_, NativeView(stage_), allocation_);
#ifdef _WIN32
      Count(wildcard_.size() + 3, limits_.max_native_path_code_units);
      wildcard_.push_back(kSeparator);
      wildcard_.push_back(L'*');
#endif
      return;
    }
  }
  Fail(PlyFileStatus::kIoFailure, PlyFileStage::kPathPlanning,
       PlyFileDiagnosticCode::kOpen);
}
void NativePublication::Create() {
  allocation_.Checkpoint();
#ifdef _WIN32
  const bool created = CreateDirectoryW(stage_.c_str(), nullptr) != 0;
#else
  constexpr mode_t kPrivateDirectoryMode = 0700;
  const bool created = mkdir(stage_.c_str(), kPrivateDirectoryMode) == 0;
#endif
  if (!created)
    Fail(PlyFileStatus::kIoFailure, PlyFileStage::kStaging,
         PlyFileDiagnosticCode::kOpen, {}, {}, Error());
  owned_ = true;
  auto directory =
      OpenFile(stage_, 0, testing_.files, true, PlyFileStage::kStaging);
  identity_ = Metadata(directory, true);
  CloseChecked(directory, testing_, PlyFileStage::kStaging);
  if (testing_.stage_ready) testing_.stage_ready(stage_, testing_.state);
}
void NativePublication::Inventory(bool receipt_present) {
  allocation_.Checkpoint();
  if (testing_.before_inventory)
    testing_.before_inventory(stage_, testing_.state);
  std::error_code error;
  if (!SameIdentity(stage_, identity_, error))
    Fail(PlyFileStatus::kPreservationMismatch, PlyFileStage::kInventory,
         PlyFileDiagnosticCode::kStagedInventory, {}, {}, error);
  bool entry = false, receipt = false;
  const auto same_name = [&](NativeView first, NativeView second) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
      allocation_.Checkpoint();
      if (first[index] != second[index]) return false;
    }
    return true;
  };
  const auto match = [&](NativeView name, bool regular) {
    allocation_.Checkpoint();
    if (!regular) return false;
    const auto entry_name =
        NativeView(entry_).substr(LastSeparator(entry_, allocation_) + 1);
    const auto receipt_name = NativeView(receipt_).substr(
        receipt_.empty() ? 0 : LastSeparator(receipt_, allocation_) + 1);
    if (same_name(name, entry_name) && !entry) {
      entry = true;
      return true;
    }
    if (receipt_present && same_name(name, receipt_name) && !receipt) {
      receipt = true;
      return true;
    }
    return false;
  };
#ifdef _WIN32
  struct Enumeration {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~Enumeration() noexcept {
      if (handle != INVALID_HANDLE_VALUE) FindClose(handle);
    }
  } enumeration;
  WIN32_FIND_DATAW data{};
  enumeration.handle = FindFirstFileW(wildcard_.c_str(), &data);
  if (enumeration.handle == INVALID_HANDLE_VALUE)
    Fail(PlyFileStatus::kIoFailure, PlyFileStage::kInventory,
         PlyFileDiagnosticCode::kStagedInventory, {}, {}, Error());
  do {
    const auto storage = std::span<const NativeChar>(data.cFileName);
    const NativeView name(
        data.cFileName,
        TerminatedLength(storage, allocation_, PlyFileStage::kInventory));
    if (name == L"." || name == L"..") continue;
    if (!match(name,
               (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                         FILE_ATTRIBUTE_REPARSE_POINT)) == 0))
      Fail(PlyFileStatus::kPreservationMismatch, PlyFileStage::kInventory,
           PlyFileDiagnosticCode::kStagedInventory);
  } while (FindNextFileW(enumeration.handle, &data));
  if (GetLastError() != ERROR_NO_MORE_FILES)
    Fail(PlyFileStatus::kIoFailure, PlyFileStage::kInventory,
         PlyFileDiagnosticCode::kStagedInventory, {}, {}, Error());
  const auto handle = std::exchange(enumeration.handle, INVALID_HANDLE_VALUE);
  if (!FindClose(handle))
    Fail(PlyFileStatus::kIoFailure, PlyFileStage::kInventory,
         PlyFileDiagnosticCode::kClose, {}, {}, Error());
#else
  struct Enumeration {
    DIR* handle = nullptr;
    ~Enumeration() noexcept {
      if (handle) closedir(handle);
    }
  } enumeration;
  enumeration.handle = opendir(stage_.c_str());
  if (!enumeration.handle)
    Fail(PlyFileStatus::kIoFailure, PlyFileStage::kInventory,
         PlyFileDiagnosticCode::kStagedInventory, {}, {}, Error());
  for (;;) {
    errno = 0;
    auto* item = readdir(enumeration.handle);
    if (!item) {
      if (errno)
        Fail(PlyFileStatus::kIoFailure, PlyFileStage::kInventory,
             PlyFileDiagnosticCode::kStagedInventory, {}, {}, Error());
      break;
    }
    const auto storage = std::span<const NativeChar>(item->d_name);
    const NativeView name(
        item->d_name,
        TerminatedLength(storage, allocation_, PlyFileStage::kInventory));
    if (name == "." || name == "..") continue;
    struct stat info{};
    if (fstatat(dirfd(enumeration.handle), item->d_name, &info,
                AT_SYMLINK_NOFOLLOW) != 0)
      Fail(PlyFileStatus::kIoFailure, PlyFileStage::kInventory,
           PlyFileDiagnosticCode::kStagedInventory, {}, {}, Error());
    if (!match(name, S_ISREG(info.st_mode)))
      Fail(PlyFileStatus::kPreservationMismatch, PlyFileStage::kInventory,
           PlyFileDiagnosticCode::kStagedInventory);
  }
  const auto handle = std::exchange(enumeration.handle, nullptr);
  if (closedir(handle) != 0)
    Fail(PlyFileStatus::kIoFailure, PlyFileStage::kInventory,
         PlyFileDiagnosticCode::kClose, {}, {}, Error());
#endif
  if (!entry || receipt != receipt_present)
    Fail(PlyFileStatus::kPreservationMismatch, PlyFileStage::kInventory,
         PlyFileDiagnosticCode::kStagedInventory);
}
PlyFileCleanupOutcome NativePublication::Cleanup(
    std::error_code& error) noexcept {
  if (!owned_) return PlyFileCleanupOutcome::kNotRequired;
  if (testing_.cleanup_error)
    error = testing_.cleanup_error(stage_, testing_.state);
  if (!error) (void)SameIdentity(stage_, identity_, error);
  if (!error) {
    for (const auto path : {NativeView(entry_), NativeView(receipt_)}) {
      if (path.empty()) continue;
      const auto removed = Remove(path, false);
      if (removed && !Missing(removed)) {
        error = removed;
        break;
      }
    }
    if (!error) error = Remove(stage_, true);
  }
  owned_ = false;
  const auto result =
      error ? PlyFileCleanupOutcome::kFailed : PlyFileCleanupOutcome::kRemoved;
  if (testing_.cleanup_finished)
    testing_.cleanup_finished(result, error, testing_.state);
  return result;
}
void NativePublication::Commit() {
  allocation_.Checkpoint();
  if (testing_.before_commit) testing_.before_commit(stage_, testing_.state);
  allocation_.Checkpoint();
  using file_publication_detail::CommitStatus;
  // The private hook can inject only failure; successful publication still
  // requires the actual native operation. Both failures use one classification.
  const auto injected_error = testing_.commit_error
                                  ? testing_.commit_error(testing_.state)
                                  : std::error_code{};
  const auto result =
      injected_error
          ? file_publication_detail::CommitResult{CommitStatus::kFailed,
                                                  injected_error}
          : file_publication_detail::CommitNoReplace(stage_.c_str(),
                                                     destination_.c_str());
  if (result.status == CommitStatus::kCommitted) {
    owned_ = false;
    if (testing_.native_committed) testing_.native_committed(testing_.state);
    return;
  }
  if (result.status == CommitStatus::kDestinationExists)
    Fail(PlyFileStatus::kDestinationExists, PlyFileStage::kPublication,
         PlyFileDiagnosticCode::kDestinationExists, {}, {}, result.error);
  // ENOSYS identifies missing renameat2 support. EINVAL is kept as a real
  // operation failure because it can also identify an invalid request.
  if (result.status == CommitStatus::kUnsupported ||
      result.error == std::errc::function_not_supported)
    Fail(PlyFileStatus::kUnsupportedProfile, PlyFileStage::kPublication,
         PlyFileDiagnosticCode::kCommitUnsupported, {}, {}, result.error);
  Fail(PlyFileStatus::kPublicationFailed, PlyFileStage::kPublication,
       PlyFileDiagnosticCode::kCommit, {}, {}, result.error);
}
}  // namespace meshvale::interchange::ply_files_detail
