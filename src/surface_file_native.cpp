// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "surface_files_internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/param.h>
#endif
#endif

namespace meshvale::interchange::surface_files_detail {
namespace {
#ifndef _WIN32
constexpr mode_t kPrivateFileMode = 0600;
#endif
std::error_code NativeError() {
#ifdef _WIN32
  return {static_cast<int>(GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}
[[noreturn]] void IoFailure(const NativeFile& file, SurfaceFileCode code) {
  Fail(SurfaceFileStatus::kIoFailure, file.stage, code, file.ordinal, {},
       NativeError());
}
}  // namespace
NativeGate::~NativeGate() noexcept { Close(); }
void NativeGate::Initialize() {
#ifdef _WIN32
  const auto handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!handle) throw std::system_error(NativeError());
  event = reinterpret_cast<std::intptr_t>(handle);
#else
  auto error = pthread_mutex_init(&mutex, nullptr);
  if (error) throw std::system_error(error, std::generic_category());
  error = pthread_cond_init(&condition, nullptr);
  if (error) {
    if (pthread_mutex_destroy(&mutex) != 0) std::terminate();
    throw std::system_error(error, std::generic_category());
  }
#endif
  initialized = true;
}
void NativeGate::Release(bool abort) noexcept {
  if (!initialized) return;
#ifdef _WIN32
  if (abort) aborted.store(true, std::memory_order_release);
  if (!SetEvent(reinterpret_cast<HANDLE>(event))) std::terminate();
#else
  if (pthread_mutex_lock(&mutex) != 0) std::terminate();
  if (abort) aborted.store(true, std::memory_order_release);
  released = true;
  if (pthread_cond_broadcast(&condition) != 0) std::terminate();
  if (pthread_mutex_unlock(&mutex) != 0) std::terminate();
#endif
}
bool NativeGate::Wait() noexcept {
#ifdef _WIN32
  if (WaitForSingleObject(reinterpret_cast<HANDLE>(event), INFINITE) !=
      WAIT_OBJECT_0)
    std::terminate();
#else
  if (pthread_mutex_lock(&mutex) != 0) std::terminate();
  while (!released)
    if (pthread_cond_wait(&condition, &mutex) != 0) std::terminate();
  if (pthread_mutex_unlock(&mutex) != 0) std::terminate();
#endif
  return !aborted.load(std::memory_order_acquire);
}
void NativeGate::Close() noexcept {
  if (!initialized) return;
#ifdef _WIN32
  if (!CloseHandle(reinterpret_cast<HANDLE>(event))) std::terminate();
  event = 0;
#else
  if (pthread_cond_destroy(&condition) != 0 ||
      pthread_mutex_destroy(&mutex) != 0)
    std::terminate();
#endif
  initialized = false;
}
void NativeYield() noexcept {
#ifdef _WIN32
  (void)SwitchToThread();
#else
  (void)sched_yield();
#endif
}
bool ValidNativeEncoding(NativeView text) noexcept {
#ifdef _WIN32
  constexpr wchar_t kHighBegin = 0xd800, kHighEnd = 0xdbff;
  constexpr wchar_t kLowBegin = 0xdc00, kLowEnd = 0xdfff;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto value = text[i];
    if (value >= kHighBegin && value <= kHighEnd) {
      if (++i == text.size() || text[i] < kLowBegin || text[i] > kLowEnd)
        return false;
    } else if (value >= kLowBegin && value <= kLowEnd)
      return false;
  }
#else
  (void)text;
#endif
  return true;
}
namespace {
[[noreturn]] void OpenFailure(NativeView path, const NativeFile& file,
                              bool directory = false) {
  const auto error = NativeError();
#ifndef _WIN32
  // O_NONBLOCK prevents FIFO opens from waiting. Diagnose an actually observed
  // unsafe path even when O_NOFOLLOW/nonblocking refused before returning fd.
  struct stat metadata{};
  if (lstat(path.data(), &metadata) == 0 &&
      !(directory ? S_ISDIR(metadata.st_mode) : S_ISREG(metadata.st_mode)))
    Fail(SurfaceFileStatus::kVerificationFailed, file.stage,
         SurfaceFileCode::kFileKind, file.ordinal);
#else
  const auto attributes = GetFileAttributesW(path.data());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
       (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) !=
           0))
    Fail(SurfaceFileStatus::kVerificationFailed, file.stage,
         SurfaceFileCode::kFileKind, file.ordinal);
#endif
  Fail(SurfaceFileStatus::kIoFailure, file.stage, SurfaceFileCode::kFileOpen,
       file.ordinal, {}, error);
}
void AdmitHandleKind(const NativeFile& file, bool directory) {
#ifdef _WIN32
  const auto handle = reinterpret_cast<HANDLE>(file.value);
  FILE_ATTRIBUTE_TAG_INFO tag{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                    sizeof(tag)))
    IoFailure(file, SurfaceFileCode::kFileRead);
  if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) ||
      GetFileType(handle) != FILE_TYPE_DISK)
    Fail(SurfaceFileStatus::kVerificationFailed, file.stage,
         SurfaceFileCode::kFileKind, file.ordinal);
#else
  struct stat info{};
  if (fstat(static_cast<int>(file.value), &info) != 0)
    IoFailure(file, SurfaceFileCode::kFileRead);
  if (!(directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)))
    Fail(SurfaceFileStatus::kVerificationFailed, file.stage,
         SurfaceFileCode::kFileKind, file.ordinal);
#endif
}
}  // namespace
NativeFile::NativeFile(NativeFile&& other) noexcept
    : value(std::exchange(other.value, kInvalidFileHandle)),
      ordinal(other.ordinal),
      stage(other.stage),
      hooks(other.hooks) {}
NativeFile& NativeFile::operator=(NativeFile&& other) noexcept {
  if (this != &other) {
    Close();
    value = std::exchange(other.value, kInvalidFileHandle);
    ordinal = other.ordinal;
    stage = other.stage;
    hooks = other.hooks;
  }
  return *this;
}
NativeFile::~NativeFile() noexcept { Close(); }
void NativeFile::Close() noexcept {
  if (value == kInvalidFileHandle) return;
#ifdef _WIN32
  (void)CloseHandle(reinterpret_cast<HANDLE>(value));
#else
  // Do not retry EINTR: descriptor ownership may already have been released.
  (void)close(static_cast<int>(value));
#endif
  value = kInvalidFileHandle;
  if (hooks.closed) hooks.closed(ordinal, hooks.state);
}
void RejectAliases(NativeView path, bool directory,
                   const allocation::AllocationContext& allocation,
                   const SurfaceFileLimits& limits, SurfaceFileStage stage) {
  Count(path.size() + 1, limits.max_native_path_code_units, stage);
  NativeString prefix{allocation::Allocator<NativeChar>(allocation)};
#ifdef _WIN32
  const auto start = kDriveRootUnits;
#else
  const std::size_t start = 1;
#endif
  for (std::size_t end = start; end <= path.size(); ++end) {
    allocation.Checkpoint();
    if (end != path.size() && path[end] != kSeparator) continue;
    allocation.Checkpoint();
    Assign(prefix, path.substr(0, end), allocation);
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(prefix.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
      Fail(SurfaceFileStatus::kIoFailure, stage, SurfaceFileCode::kFileOpen, {},
           {}, NativeError());
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      Fail(SurfaceFileStatus::kUnsupportedProfile, stage,
           SurfaceFileCode::kUnsupportedProfile);
    const auto is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat metadata{};
    if (lstat(prefix.c_str(), &metadata) != 0)
      Fail(SurfaceFileStatus::kIoFailure, stage, SurfaceFileCode::kFileOpen, {},
           {}, NativeError());
    if (S_ISLNK(metadata.st_mode))
      Fail(SurfaceFileStatus::kUnsupportedProfile, stage,
           SurfaceFileCode::kUnsupportedProfile);
    const auto is_directory = S_ISDIR(metadata.st_mode);
    if (end == path.size() && !directory && !S_ISREG(metadata.st_mode))
      Fail(SurfaceFileStatus::kUnsupportedProfile, stage,
           SurfaceFileCode::kFileKind);
#endif
    if ((end < path.size() || directory) && !is_directory)
      Fail(SurfaceFileStatus::kUnsupportedProfile, stage,
           SurfaceFileCode::kFileKind);
  }
}
NativeFile OpenFile(NativeView path, std::uint64_t ordinal,
                    const FileHooks& hooks, bool directory,
                    SurfaceFileStage stage) {
  NativeFile result;
  result.ordinal = ordinal;
  result.stage = stage;
  result.hooks = hooks;
  if (result.hooks.before_open)
    result.hooks.before_open(ordinal, result.hooks.state);
  if (result.hooks.io_error) {
    auto error = result.hooks.io_error(IoOperation::kOpen, stage, ordinal, 0,
                                       result.hooks.state);
    if (error)
      Fail(SurfaceFileStatus::kIoFailure, stage, SurfaceFileCode::kFileOpen,
           ordinal, {}, error);
  }
#ifdef _WIN32
  const auto handle = CreateFileW(
      path.data(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT |
          (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_RANDOM_ACCESS),
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) OpenFailure(path, result, directory);
  result.value = reinterpret_cast<std::intptr_t>(handle);
  if (result.hooks.opened) result.hooks.opened(ordinal, result.hooks.state);
#else
  result.value =
      open(path.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                            (directory ? O_DIRECTORY : 0));
  if (result.value == kInvalidFileHandle) OpenFailure(path, result, directory);
  if (result.hooks.opened) result.hooks.opened(ordinal, result.hooks.state);
#endif
  AdmitHandleKind(result, directory);
  return result;
}
NativeMetadata Metadata(const NativeFile& file, bool directory) {
#ifdef _WIN32
  constexpr unsigned kWordBits = 32;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(file.value), &info))
    IoFailure(file, SurfaceFileCode::kFileRead);
  if (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
      GetFileType(reinterpret_cast<HANDLE>(file.value)) != FILE_TYPE_DISK)
    Fail(SurfaceFileStatus::kVerificationFailed, file.stage,
         SurfaceFileCode::kFileKind, file.ordinal);
  return {info.dwVolumeSerialNumber,
          (static_cast<std::uint64_t>(info.nFileIndexHigh) << kWordBits) |
              info.nFileIndexLow,
          (static_cast<std::uint64_t>(info.nFileSizeHigh) << kWordBits) |
              info.nFileSizeLow,
          static_cast<std::int64_t>(
              (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime)
               << kWordBits) |
              info.ftLastWriteTime.dwLowDateTime),
          0};
#else
  struct stat info{};
  if (fstat(static_cast<int>(file.value), &info) != 0)
    IoFailure(file, SurfaceFileCode::kFileRead);
  if (!(directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) ||
      info.st_size < 0)
    Fail(SurfaceFileStatus::kVerificationFailed, file.stage,
         SurfaceFileCode::kFileKind, file.ordinal);
#ifdef __APPLE__
  const auto modified = info.st_mtimespec;
#else
  const auto modified = info.st_mtim;
#endif
  return {static_cast<std::uint64_t>(info.st_dev),
          static_cast<std::uint64_t>(info.st_ino),
          static_cast<std::uint64_t>(info.st_size),
          static_cast<std::int64_t>(modified.tv_sec),
          static_cast<std::int64_t>(modified.tv_nsec)};
#endif
}
NativeString CanonicalPath(const NativeFile& file,
                           const allocation::AllocationContext& allocation,
                           const SurfaceFileLimits& limits) {
  NativeString result{allocation::Allocator<NativeChar>(allocation)};
#ifdef _WIN32
  constexpr std::wstring_view kDosPrefix = L"\\\\?\\";
  const auto needed =
      GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(file.value), nullptr,
                                0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!needed) IoFailure(file, SurfaceFileCode::kFileRead);
  Count(needed, limits.max_native_path_code_units, file.stage);
  result.reserve(needed);
  while (result.size() < needed) {
    allocation.Checkpoint();
    result.push_back(0);
  }
  const auto length = GetFinalPathNameByHandleW(
      reinterpret_cast<HANDLE>(file.value), result.data(), needed,
      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!length || length >= needed) IoFailure(file, SurfaceFileCode::kFileRead);
  result.resize(length);
  if (result.starts_with(kDosPrefix)) {
    for (std::size_t i = kDosPrefix.size(); i < result.size(); ++i) {
      allocation.Checkpoint();
      result[i - kDosPrefix.size()] = result[i];
    }
    result.resize(result.size() - kDosPrefix.size());
  }
  if (result.size() < kDriveRootUnits || result[1] != L':' ||
      result[2] != kSeparator)
    Fail(SurfaceFileStatus::kUnsupportedProfile, file.stage,
         SurfaceFileCode::kUnsupportedProfile);
#elif defined(__APPLE__)
  result.reserve(MAXPATHLEN);
  while (result.size() < MAXPATHLEN) {
    allocation.Checkpoint();
    result.push_back(0);
  }
  if (fcntl(static_cast<int>(file.value), F_GETPATH, result.data()) != 0)
    IoFailure(file, SurfaceFileCode::kFileRead);
  result.resize(TerminatedLength(
      std::span<const NativeChar>(result.data(), result.size()), allocation,
      file.stage));
#elif defined(__linux__)
  constexpr std::string_view kDescriptorPrefix = "/proc/self/fd/";
  constexpr std::size_t kDescriptorDigits =
      std::numeric_limits<int>::digits10 + 1;
  std::array<char, kDescriptorDigits> digits{};
  const auto converted =
      std::to_chars(digits.data(), digits.data() + digits.size(), file.value);
  NativeString descriptor{allocation::Allocator<char>(allocation)};
  descriptor.reserve(kDescriptorPrefix.size() + kDescriptorDigits);
  Extend(descriptor, kDescriptorPrefix, allocation);
  Extend(descriptor,
         std::string_view(digits.data(), static_cast<std::size_t>(
                                             converted.ptr - digits.data())),
         allocation);
  Count(descriptor.size() + 1, limits.max_native_path_code_units, file.stage);
  result.reserve(static_cast<std::size_t>(limits.max_native_path_code_units));
  while (result.size() < limits.max_native_path_code_units) {
    allocation.Checkpoint();
    result.push_back(0);
  }
  const auto length =
      readlink(descriptor.c_str(), result.data(), result.size());
  if (length < 0) IoFailure(file, SurfaceFileCode::kFileRead);
  if (static_cast<std::size_t>(length) >= result.size())
    Fail(SurfaceFileStatus::kLimitExceeded, file.stage,
         SurfaceFileCode::kCountLimit, file.ordinal);
  result.resize(static_cast<std::size_t>(length));
#else
  Fail(SurfaceFileStatus::kUnsupportedProfile, file.stage,
       SurfaceFileCode::kUnsupportedProfile, file.ordinal);
#endif
  if (!ValidNativeEncoding(result))
    Fail(SurfaceFileStatus::kUnsupportedProfile, file.stage,
         SurfaceFileCode::kUnsupportedProfile);
  Count(result.size() + 1, limits.max_native_path_code_units, file.stage);
  return result;
}
std::uint64_t MaximumReadBlockBytes() noexcept {
#ifdef _WIN32
  return std::numeric_limits<DWORD>::max();
#else
  return static_cast<std::uint64_t>(std::numeric_limits<ssize_t>::max());
#endif
}
[[noreturn]] void TransferFailure(const NativeFile& file, SurfaceFileCode code,
                                  std::uint64_t offset, std::uint64_t read,
                                  std::uint64_t written,
                                  std::optional<std::error_code> error = {}) {
  SurfaceFileDiagnostic d;
  d.status = SurfaceFileStatus::kIoFailure;
  d.stage = file.stage;
  d.code = code;
  d.member_ordinal = file.ordinal;
  d.byte_offset = offset;
  d.system_error = error;
  throw Failure{d, read, written};
}
void ReadBlock(const NativeFile& file, std::uint64_t offset,
               std::span<std::byte> bytes) {
  if (file.hooks.io_error) {
    auto error = file.hooks.io_error(IoOperation::kRead, file.stage,
                                     file.ordinal, offset, file.hooks.state);
    if (error)
      Fail(SurfaceFileStatus::kIoFailure, file.stage,
           SurfaceFileCode::kFileRead, file.ordinal, offset, error);
  }
#ifdef _WIN32
  Count(bytes.size(), std::numeric_limits<DWORD>::max(), file.stage);
  Count(offset, std::numeric_limits<LONGLONG>::max(), file.stage);
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  const auto handle = reinterpret_cast<HANDLE>(file.value);
  if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
    IoFailure(file, SurfaceFileCode::kFileRead);
  DWORD read = 0;
  if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read,
                nullptr))
    TransferFailure(file, SurfaceFileCode::kFileRead, offset + read, read, 0,
                    NativeError());
  if (read != bytes.size())
    TransferFailure(file, SurfaceFileCode::kFileRead,
                    offset + static_cast<std::uint64_t>(read),
                    static_cast<std::uint64_t>(read), 0);
#else
  Count(offset, std::numeric_limits<off_t>::max(), file.stage);
  Count(bytes.size(), std::numeric_limits<ssize_t>::max(), file.stage);
  ssize_t read;
  do {
    read = pread(static_cast<int>(file.value), bytes.data(), bytes.size(),
                 static_cast<off_t>(offset));
  } while (read < 0 && errno == EINTR);
  if (read < 0) IoFailure(file, SurfaceFileCode::kFileRead);
  if (static_cast<std::size_t>(read) != bytes.size())
    TransferFailure(file, SurfaceFileCode::kFileRead,
                    offset + static_cast<std::uint64_t>(read),
                    static_cast<std::uint64_t>(read), 0);
#endif
}
void RequireEnd(const NativeFile& file, std::uint64_t offset) {
  std::byte byte{};
#ifdef _WIN32
  Count(offset, std::numeric_limits<LONGLONG>::max(), file.stage);
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  const auto handle = reinterpret_cast<HANDLE>(file.value);
  if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
    IoFailure(file, SurfaceFileCode::kFileRead);
  DWORD count = 0;
  if (!ReadFile(handle, &byte, 1, &count, nullptr))
    IoFailure(file, SurfaceFileCode::kFileRead);
#else
  Count(offset, std::numeric_limits<off_t>::max(), file.stage);
  ssize_t count;
  do {
    count = pread(static_cast<int>(file.value), &byte, 1,
                  static_cast<off_t>(offset));
  } while (count < 0 && errno == EINTR);
  if (count < 0) IoFailure(file, SurfaceFileCode::kFileRead);
#endif
  if (count != 0) {
    SurfaceFileDiagnostic diagnostic;
    diagnostic.status = SurfaceFileStatus::kVerificationFailed;
    diagnostic.stage = file.stage;
    diagnostic.code = SurfaceFileCode::kFileChanged;
    diagnostic.member_ordinal = file.ordinal;
    diagnostic.byte_offset = offset;
    throw Failure{diagnostic, static_cast<std::uint64_t>(count), 0};
  }
}
}  // namespace meshvale::interchange::surface_files_detail

namespace meshvale::interchange::surface_files_detail {
namespace {
char Fold(char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
#ifdef _WIN32
[[noreturn]] void Unsupported() {
  Fail(SurfaceFileStatus::kUnsupportedProfile, SurfaceFileStage::kPathPlanning,
       SurfaceFileCode::kReceiptPath);
}
#endif
template <class Char>
void Zeros(std::basic_string<Char, std::char_traits<Char>,
                             allocation::Allocator<Char>>& text,
           std::size_t size, const allocation::AllocationContext& allocator) {
  text.reserve(size);
  while (text.size() < size) {
    allocator.Checkpoint();
    text.push_back(0);
  }
}
}  // namespace
bool EqualFold(std::string_view a, std::string_view b,
               const allocation::AllocationContext* allocator) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (allocator) allocator->Checkpoint();
    if (Fold(a[i]) != Fold(b[i])) return false;
  }
  return true;
}
std::size_t LastSeparator(NativeView path,
                          const allocation::AllocationContext& allocator) {
  for (std::size_t end = path.size(); end != 0; --end) {
    allocator.Checkpoint();
    if (path[end - 1] == kSeparator) return end - 1;
  }
  return NativeView::npos;
}
std::size_t TerminatedLength(std::span<const NativeChar> storage,
                             const allocation::AllocationContext& allocator,
                             SurfaceFileStage stage) {
  for (std::size_t index = 0; index < storage.size(); ++index) {
    allocator.Checkpoint();
    if (storage[index] == NativeChar{}) return index;
  }
  Fail(SurfaceFileStatus::kLimitExceeded, stage, SurfaceFileCode::kCountLimit);
}
void PortableName(std::string_view name, std::uint64_t cap,
                  const allocation::AllocationContext& a) {
  a.Checkpoint();
  PortablePath(name, cap);
}
NativeString AbsolutePath(NativeView input,
                          const allocation::AllocationContext& allocator,
                          const SurfaceFileLimits& limits) {
  Count(input.size(), limits.max_native_path_code_units - 1);
  if (!ValidNativeEncoding(input))
    Fail(SurfaceFileStatus::kUnsupportedProfile,
         SurfaceFileStage::kPathPlanning, SurfaceFileCode::kUnsupportedProfile);
  if (input.empty())
    Fail(SurfaceFileStatus::kUnsupportedProfile,
         SurfaceFileStage::kPathPlanning, SurfaceFileCode::kUnsupportedProfile);
  for (auto value : input) {
    allocator.Checkpoint();
    if (value == NativeChar{})
      Fail(SurfaceFileStatus::kUnsupportedProfile,
           SurfaceFileStage::kPathPlanning,
           SurfaceFileCode::kUnsupportedProfile);
  }
  NativeString output{allocation::Allocator<NativeChar>(allocator)};
  output.reserve(static_cast<std::size_t>(limits.max_native_path_code_units));
#ifdef _WIN32
  const bool absolute = input.size() >= kDriveRootUnits && input[1] == L':' &&
                        (input[2] == L'\\' || input[2] == L'/') &&
                        ((input[0] >= L'A' && input[0] <= L'Z') ||
                         (input[0] >= L'a' && input[0] <= L'z'));
  if (!absolute) {
    bool colon = false;
    for (auto value : input) {
      allocator.Checkpoint();
      if (value == L':') colon = true;
    }
    if (input.front() == L'\\' || input.front() == L'/' || colon)
      Fail(SurfaceFileStatus::kUnsupportedProfile,
           SurfaceFileStage::kPathPlanning,
           SurfaceFileCode::kUnsupportedProfile);
    Count(limits.max_native_path_code_units, std::numeric_limits<DWORD>::max());
    Zeros(output, static_cast<std::size_t>(limits.max_native_path_code_units),
          allocator);
    const auto count =
        GetCurrentDirectoryW(static_cast<DWORD>(output.size()), output.data());
    if (!count || count >= output.size())
      Fail(SurfaceFileStatus::kIoFailure, SurfaceFileStage::kPathPlanning,
           SurfaceFileCode::kUnsupportedProfile, {}, {},
           std::error_code(static_cast<int>(GetLastError()),
                           std::system_category()));
    output.resize(count);
    output.push_back(kSeparator);
  }
  Count(output.size() + input.size() + 1, limits.max_native_path_code_units);
  for (auto value : input) {
    allocator.Checkpoint();
    output.push_back(value == L'/' ? kSeparator : value);
  }
#else
  if (input.front() != kSeparator) {
    Zeros(output, static_cast<std::size_t>(limits.max_native_path_code_units),
          allocator);
    if (!getcwd(output.data(), output.size()))
      Fail(SurfaceFileStatus::kIoFailure, SurfaceFileStage::kPathPlanning,
           SurfaceFileCode::kUnsupportedProfile, {}, {},
           std::error_code(errno, std::generic_category()));
    output.resize(TerminatedLength(
        std::span<const NativeChar>(output.data(), output.size()), allocator,
        SurfaceFileStage::kPathPlanning));
    if (output.back() != kSeparator) output.push_back(kSeparator);
  }
  Count(output.size() + input.size() + 1, limits.max_native_path_code_units);
  Extend(output, input, allocator);
#endif
  // Preserve dot/parent components for alias admission; never resolve an
  // unsupported alias away before RejectAliases traverses the logical path.
  return output;
}
String EntryName(NativeView path,
                 const allocation::AllocationContext& allocator,
                 const SurfaceFileLimits& limits) {
  const auto end = LastSeparator(path, allocator);
  const auto tail = path.substr(end == path.npos ? 0 : end + 1);
  String result{allocation::Allocator<char>(allocator)};
#ifdef _WIN32
  Count(tail.size(), std::numeric_limits<int>::max());
  const int bytes = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, tail.data(), static_cast<int>(tail.size()),
      nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) Unsupported();
  Count(static_cast<std::uint64_t>(bytes), limits.max_path_bytes);
  Zeros(result, static_cast<std::size_t>(bytes), allocator);
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, tail.data(),
                           static_cast<int>(tail.size()), result.data(), bytes,
                           nullptr, nullptr))
    Unsupported();
#else
  Count(tail.size(), limits.max_path_bytes);
  PortableName(tail, limits.max_path_bytes, allocator);
  Assign(result, tail, allocator);
#endif
  PortableName(result, limits.max_path_bytes, allocator);
  return result;
}
NativeString JoinPath(NativeView root, std::string_view name,
                      const allocation::AllocationContext& allocator,
                      const SurfaceFileLimits& limits) {
  NativeString tail{allocation::Allocator<NativeChar>(allocator)};
#ifdef _WIN32
  Count(name.size(), std::numeric_limits<int>::max());
  const int units =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                          static_cast<int>(name.size()), nullptr, 0);
  if (units <= 0) Unsupported();
  Count(static_cast<std::uint64_t>(units) + 1,
        limits.max_native_path_code_units);
  Zeros(tail, static_cast<std::size_t>(units), allocator);
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                           static_cast<int>(name.size()), tail.data(), units))
    Unsupported();
#else
  Assign(tail, name, allocator);
#endif
  for (auto& c : tail)
    if (c == static_cast<NativeChar>('/')) c = kSeparator;
  const std::size_t separator = root.back() == kSeparator ? 0 : 1;
  Count(tail.size() + 1, limits.max_native_path_code_units);
  if (root.size() + separator >
      limits.max_native_path_code_units - 1 - tail.size())
    Fail(SurfaceFileStatus::kLimitExceeded, SurfaceFileStage::kPathPlanning,
         SurfaceFileCode::kCountLimit);
  NativeString result{allocation::Allocator<NativeChar>(allocator)};
  result.reserve(root.size() + separator + tail.size());
  Extend(result, root, allocator);
  if (separator) result.push_back(kSeparator);
  Extend(result, NativeView(tail), allocator);
  return result;
}
}  // namespace meshvale::interchange::surface_files_detail

namespace meshvale::interchange::surface_files_detail {
NativeFile OpenWrite(NativeView path, std::uint64_t ordinal,
                     const FileHooks& hooks, SurfaceFileStage stage,
                     bool exclusive) {
  NativeFile result;
  result.ordinal = ordinal;
  result.stage = stage;
  result.hooks = hooks;
  if (result.hooks.before_open)
    result.hooks.before_open(ordinal, result.hooks.state);
  if (result.hooks.io_error) {
    auto e = result.hooks.io_error(IoOperation::kOpen, stage, ordinal, 0,
                                   result.hooks.state);
    if (e)
      Fail(SurfaceFileStatus::kIoFailure, stage, SurfaceFileCode::kFileOpen,
           ordinal, {}, e);
  }
#ifdef _WIN32
  auto handle = CreateFileW(
      path.data(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      exclusive ? CREATE_NEW : OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_RANDOM_ACCESS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) OpenFailure(path, result);
  result.value = reinterpret_cast<std::intptr_t>(handle);
#else
  result.value = open(path.data(),
                      O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                          (exclusive ? (O_CREAT | O_EXCL) : 0),
                      kPrivateFileMode);
  if (result.value == kInvalidFileHandle) OpenFailure(path, result);
#endif
  if (result.hooks.opened) result.hooks.opened(ordinal, result.hooks.state);
  AdmitHandleKind(result, false);
  return result;
}
void WriteBlock(const NativeFile& file, std::uint64_t offset,
                std::span<const std::byte> bytes) {
  if (file.hooks.io_error) {
    auto e = file.hooks.io_error(IoOperation::kWrite, file.stage, file.ordinal,
                                 offset, file.hooks.state);
    if (e)
      Fail(SurfaceFileStatus::kIoFailure, file.stage,
           SurfaceFileCode::kFileWrite, file.ordinal, offset, e);
  }
#ifdef _WIN32
  Count(offset, std::numeric_limits<LONGLONG>::max(), file.stage);
  Count(bytes.size(), std::numeric_limits<DWORD>::max(), file.stage);
  LARGE_INTEGER pos{};
  pos.QuadPart = static_cast<LONGLONG>(offset);
  auto h = reinterpret_cast<HANDLE>(file.value);
  if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN))
    IoFailure(file, SurfaceFileCode::kFileWrite);
  DWORD done = 0;
  if (!WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &done,
                 nullptr))
    TransferFailure(file, SurfaceFileCode::kFileWrite, offset + done, 0, done,
                    NativeError());
  if (done != bytes.size())
    TransferFailure(file, SurfaceFileCode::kFileWrite, offset + done, 0, done);
#else
  Count(offset, std::numeric_limits<off_t>::max(), file.stage);
  Count(bytes.size(), std::numeric_limits<ssize_t>::max(), file.stage);
  ssize_t done;
  do {
    done = pwrite(static_cast<int>(file.value), bytes.data(), bytes.size(),
                  static_cast<off_t>(offset));
  } while (done < 0 && errno == EINTR);
  if (done < 0) IoFailure(file, SurfaceFileCode::kFileWrite);
  if (static_cast<std::size_t>(done) != bytes.size())
    TransferFailure(file, SurfaceFileCode::kFileWrite,
                    offset + static_cast<std::uint64_t>(done), 0,
                    static_cast<std::uint64_t>(done));
#endif
}
void CloseChecked(NativeFile& file) {
  std::error_code injected;
  if (file.hooks.io_error)
    injected = file.hooks.io_error(IoOperation::kClose, file.stage,
                                   file.ordinal, 0, file.hooks.state);
  std::error_code error;
#ifdef _WIN32
  if (!CloseHandle(reinterpret_cast<HANDLE>(file.value))) error = NativeError();
#else
  if (close(static_cast<int>(file.value)) != 0) error = NativeError();
#endif
  file.value = kInvalidFileHandle;
  if (file.hooks.closed) file.hooks.closed(file.ordinal, file.hooks.state);
  if (injected || error)
    Fail(SurfaceFileStatus::kIoFailure, file.stage, SurfaceFileCode::kFileClose,
         file.ordinal, {}, injected ? injected : error);
}
}  // namespace meshvale::interchange::surface_files_detail
