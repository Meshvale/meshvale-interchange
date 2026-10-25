// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/ply_files.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "gltf_allocation.h"
#include "ply_file_internal.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/param.h>
#endif
#endif
namespace meshvale::interchange::ply_files_detail {
namespace {
std::error_code NativeError() {
#ifdef _WIN32
  return {static_cast<int>(GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}
[[noreturn]] void IoFailure(const NativeFile& file,
                            PlyFileDiagnosticCode code) {
  Fail(PlyFileStatus::kIoFailure, file.stage, code, file.ordinal, {},
       NativeError());
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
                   const PlyFileLimits& limits, PlyFileStage stage) {
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
      Fail(PlyFileStatus::kIoFailure, stage, PlyFileDiagnosticCode::kOpen, {},
           {}, NativeError());
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      Fail(PlyFileStatus::kUnsupportedProfile, stage,
           PlyFileDiagnosticCode::kPathUnsupported);
    const auto is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat metadata{};
    if (lstat(prefix.c_str(), &metadata) != 0)
      Fail(PlyFileStatus::kIoFailure, stage, PlyFileDiagnosticCode::kOpen, {},
           {}, NativeError());
    if (S_ISLNK(metadata.st_mode))
      Fail(PlyFileStatus::kUnsupportedProfile, stage,
           PlyFileDiagnosticCode::kPathUnsupported);
    const auto is_directory = S_ISDIR(metadata.st_mode);
    if (end == path.size() && !directory && !S_ISREG(metadata.st_mode))
      Fail(PlyFileStatus::kUnsupportedProfile, stage,
           PlyFileDiagnosticCode::kNotRegular);
#endif
    if ((end < path.size() || directory) && !is_directory)
      Fail(PlyFileStatus::kUnsupportedProfile, stage,
           PlyFileDiagnosticCode::kNotRegular);
  }
}
NativeFile OpenFile(NativeView path, std::uint64_t ordinal,
                    const FileHooks& hooks, bool directory,
                    PlyFileStage stage) {
  NativeFile result;
  result.ordinal = ordinal;
  result.stage = stage;
  result.hooks = hooks;
  if (result.hooks.before_open)
    result.hooks.before_open(ordinal, result.hooks.state);
#ifdef _WIN32
  const auto handle = CreateFileW(
      path.data(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT |
          (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_RANDOM_ACCESS),
      nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    IoFailure(result, PlyFileDiagnosticCode::kOpen);
  result.value = reinterpret_cast<std::intptr_t>(handle);
  if (result.hooks.opened) result.hooks.opened(ordinal, result.hooks.state);
  FILE_ATTRIBUTE_TAG_INFO tag{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                    sizeof(tag)))
    IoFailure(result, PlyFileDiagnosticCode::kRead);
  if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    Fail(PlyFileStatus::kUnsupportedProfile, stage,
         PlyFileDiagnosticCode::kPathUnsupported, ordinal);
  if (((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
      GetFileType(handle) != FILE_TYPE_DISK)
    Fail(PlyFileStatus::kUnsupportedProfile, stage,
         PlyFileDiagnosticCode::kNotRegular, ordinal);
#else
  result.value =
      open(path.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                            (directory ? O_DIRECTORY : 0));
  if (result.value == kInvalidFileHandle)
    IoFailure(result, PlyFileDiagnosticCode::kOpen);
  if (result.hooks.opened) result.hooks.opened(ordinal, result.hooks.state);
#endif
  return result;
}
NativeMetadata Metadata(const NativeFile& file, bool directory) {
#ifdef _WIN32
  constexpr unsigned kWordBits = 32;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(file.value), &info))
    IoFailure(file, PlyFileDiagnosticCode::kRead);
  if (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
      GetFileType(reinterpret_cast<HANDLE>(file.value)) != FILE_TYPE_DISK)
    Fail(PlyFileStatus::kUnsupportedProfile, file.stage,
         PlyFileDiagnosticCode::kNotRegular, file.ordinal);
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
    IoFailure(file, PlyFileDiagnosticCode::kRead);
  if (!(directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) ||
      info.st_size < 0)
    Fail(PlyFileStatus::kUnsupportedProfile, file.stage,
         PlyFileDiagnosticCode::kNotRegular, file.ordinal);
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
                           const PlyFileLimits& limits) {
  NativeString result{allocation::Allocator<NativeChar>(allocation)};
#ifdef _WIN32
  constexpr std::wstring_view kDosPrefix = L"\\\\?\\";
  const auto needed =
      GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(file.value), nullptr,
                                0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!needed) IoFailure(file, PlyFileDiagnosticCode::kRead);
  Count(needed, limits.max_native_path_code_units, file.stage);
  result.reserve(needed);
  while (result.size() < needed) {
    allocation.Checkpoint();
    result.push_back(0);
  }
  const auto length = GetFinalPathNameByHandleW(
      reinterpret_cast<HANDLE>(file.value), result.data(), needed,
      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!length || length >= needed)
    IoFailure(file, PlyFileDiagnosticCode::kRead);
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
    Fail(PlyFileStatus::kUnsupportedProfile, file.stage,
         PlyFileDiagnosticCode::kPathUnsupported);
#elif defined(__APPLE__)
  result.reserve(MAXPATHLEN);
  while (result.size() < MAXPATHLEN) {
    allocation.Checkpoint();
    result.push_back(0);
  }
  if (fcntl(static_cast<int>(file.value), F_GETPATH, result.data()) != 0)
    IoFailure(file, PlyFileDiagnosticCode::kRead);
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
  if (length < 0) IoFailure(file, PlyFileDiagnosticCode::kRead);
  if (static_cast<std::size_t>(length) >= result.size())
    Fail(PlyFileStatus::kLimitExceeded, file.stage,
         PlyFileDiagnosticCode::kCountLimit, file.ordinal);
  result.resize(static_cast<std::size_t>(length));
#else
  Fail(PlyFileStatus::kUnsupportedProfile, file.stage,
       PlyFileDiagnosticCode::kPathUnsupported, file.ordinal);
#endif
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
void ReadBlock(const NativeFile& file, std::uint64_t offset,
               std::span<std::byte> bytes) {
  if (file.hooks.before_read)
    file.hooks.before_read(file.ordinal, offset, file.hooks.state);
#ifdef _WIN32
  Count(bytes.size(), std::numeric_limits<DWORD>::max(), file.stage);
  Count(offset, std::numeric_limits<LONGLONG>::max(), file.stage);
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  const auto handle = reinterpret_cast<HANDLE>(file.value);
  if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
    IoFailure(file, PlyFileDiagnosticCode::kRead);
  DWORD read = 0;
  if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read,
                nullptr))
    IoFailure(file, PlyFileDiagnosticCode::kRead);
  if (read != bytes.size())
    Fail(PlyFileStatus::kFileChanged, file.stage,
         PlyFileDiagnosticCode::kChanged, file.ordinal, offset + read);
#else
  Count(offset, std::numeric_limits<off_t>::max(), file.stage);
  Count(bytes.size(), std::numeric_limits<ssize_t>::max(), file.stage);
  ssize_t read;
  do {
    read = pread(static_cast<int>(file.value), bytes.data(), bytes.size(),
                 static_cast<off_t>(offset));
  } while (read < 0 && errno == EINTR);
  if (read < 0) IoFailure(file, PlyFileDiagnosticCode::kRead);
  if (static_cast<std::size_t>(read) != bytes.size())
    Fail(PlyFileStatus::kFileChanged, file.stage,
         PlyFileDiagnosticCode::kChanged, file.ordinal, offset + read);
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
    IoFailure(file, PlyFileDiagnosticCode::kRead);
  DWORD count = 0;
  if (!ReadFile(handle, &byte, 1, &count, nullptr))
    IoFailure(file, PlyFileDiagnosticCode::kRead);
#else
  Count(offset, std::numeric_limits<off_t>::max(), file.stage);
  ssize_t count;
  do {
    count = pread(static_cast<int>(file.value), &byte, 1,
                  static_cast<off_t>(offset));
  } while (count < 0 && errno == EINTR);
  if (count < 0) IoFailure(file, PlyFileDiagnosticCode::kRead);
#endif
  if (count != 0)
    Fail(PlyFileStatus::kFileChanged, file.stage,
         PlyFileDiagnosticCode::kChanged, file.ordinal, offset);
}
}  // namespace meshvale::interchange::ply_files_detail
