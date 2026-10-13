// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "gltf_files_internal.h"

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

namespace meshvale::interchange::gltf_files_detail {
namespace {
std::error_code NativeError() {
#ifdef _WIN32
  return {static_cast<int>(GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}
[[noreturn]] void IoFailure(const NativeFile& file,
                            GltfFileDiagnosticCode code) {
  Fail(GltfFileStatus::kIoFailure, GltfFileStage::kCapture, code, file.ordinal,
       {}, NativeError());
}
}  // namespace
NativeFile::NativeFile(NativeFile&& other) noexcept
    : value(std::exchange(other.value, kInvalidFileHandle)),
      ordinal(other.ordinal),
      hooks(other.hooks) {}
NativeFile& NativeFile::operator=(NativeFile&& other) noexcept {
  if (this != &other) {
    Close();
    value = std::exchange(other.value, kInvalidFileHandle);
    ordinal = other.ordinal;
    hooks = other.hooks;
  }
  return *this;
}
NativeFile::~NativeFile() noexcept { Close(); }
void NativeFile::Close() noexcept {
  if (value == kInvalidFileHandle) return;
#ifdef _WIN32
  if (!CloseHandle(reinterpret_cast<HANDLE>(value))) std::terminate();
#else
  // Do not retry EINTR: descriptor ownership may already have been released.
  if (close(static_cast<int>(value)) != 0 && errno != EINTR) std::terminate();
#endif
  value = kInvalidFileHandle;
  if (hooks && hooks->closed) hooks->closed(ordinal, hooks->state);
}
void RejectAliases(NativeView path, bool directory,
                   const gltf_detail::AllocationContext& allocation,
                   const GltfFileLimits& limits) {
  Count(path.size() + 1, limits.max_native_path_code_units);
  NativeString prefix{gltf_detail::Allocator<NativeChar>(allocation)};
#ifdef _WIN32
  constexpr NativeChar kSeparator = L'\\';
  const auto start = kDriveRootUnits;
#else
  constexpr NativeChar kSeparator = '/';
  const std::size_t start = 1;
#endif
  for (std::size_t end = start; end <= path.size(); ++end) {
    if (end != path.size() && path[end] != kSeparator) continue;
    allocation.Checkpoint();
    prefix.assign(path.substr(0, end));
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(prefix.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
      Fail(GltfFileStatus::kIoFailure, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kResourceMissing, {}, {}, NativeError());
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kPathUnsupported);
    const auto is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat metadata{};
    if (lstat(prefix.c_str(), &metadata) != 0)
      Fail(GltfFileStatus::kIoFailure, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kResourceMissing, {}, {}, NativeError());
    if (S_ISLNK(metadata.st_mode))
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kPathUnsupported);
    const auto is_directory = S_ISDIR(metadata.st_mode);
    if (end == path.size() && !directory && !S_ISREG(metadata.st_mode))
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kResourceNotRegularFile);
#endif
    if ((end < path.size() || directory) && !is_directory)
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kResourceNotRegularFile);
  }
}
NativeFile OpenFile(NativeView path, std::uint64_t ordinal,
                    const GltfFileHooks& hooks, bool directory) {
  NativeFile result;
  result.ordinal = ordinal;
  result.hooks = &hooks;
  if (hooks.before_open) hooks.before_open(ordinal, hooks.state);
#ifdef _WIN32
  const auto handle = CreateFileW(
      path.data(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT |
          (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_RANDOM_ACCESS),
      nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    IoFailure(result, GltfFileDiagnosticCode::kResourceMissing);
  result.value = reinterpret_cast<std::intptr_t>(handle);
  if (hooks.opened) hooks.opened(ordinal, hooks.state);
  FILE_ATTRIBUTE_TAG_INFO tag{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                    sizeof(tag)))
    IoFailure(result, GltfFileDiagnosticCode::kFileRead);
  if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported, ordinal);
  if (((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
      GetFileType(handle) != FILE_TYPE_DISK)
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kCapture,
         GltfFileDiagnosticCode::kResourceNotRegularFile, ordinal);
#else
  result.value =
      open(path.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                            (directory ? O_DIRECTORY : 0));
  if (result.value == kInvalidFileHandle)
    IoFailure(result, GltfFileDiagnosticCode::kResourceMissing);
  if (hooks.opened) hooks.opened(ordinal, hooks.state);
#endif
  return result;
}
NativeMetadata Metadata(const NativeFile& file) {
#ifdef _WIN32
  constexpr unsigned kWordBits = 32;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(file.value), &info))
    IoFailure(file, GltfFileDiagnosticCode::kFileRead);
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
    IoFailure(file, GltfFileDiagnosticCode::kFileRead);
  if (!S_ISREG(info.st_mode) || info.st_size < 0)
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kCapture,
         GltfFileDiagnosticCode::kResourceNotRegularFile, file.ordinal);
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
                           const gltf_detail::AllocationContext& allocation,
                           const GltfFileLimits& limits) {
  NativeString result{gltf_detail::Allocator<NativeChar>(allocation)};
#ifdef _WIN32
  constexpr std::wstring_view kDosPrefix = L"\\\\?\\";
  const auto needed =
      GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(file.value), nullptr,
                                0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!needed) IoFailure(file, GltfFileDiagnosticCode::kFileRead);
  Count(needed, limits.max_native_path_code_units);
  result.resize(needed);
  const auto length = GetFinalPathNameByHandleW(
      reinterpret_cast<HANDLE>(file.value), result.data(), needed,
      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!length || length >= needed)
    IoFailure(file, GltfFileDiagnosticCode::kFileRead);
  result.resize(length);
  if (result.starts_with(kDosPrefix)) result.erase(0, kDosPrefix.size());
  RequireDriveAbsolutePath(result);
#elif defined(__APPLE__)
  result.resize(MAXPATHLEN);
  if (fcntl(static_cast<int>(file.value), F_GETPATH, result.data()) != 0)
    IoFailure(file, GltfFileDiagnosticCode::kFileRead);
  result.resize(std::char_traits<char>::length(result.c_str()));
#else
  constexpr std::string_view kDescriptorPrefix = "/proc/self/fd/";
  constexpr std::size_t kDescriptorDigits =
      std::numeric_limits<int>::digits10 + 1;
  std::array<char, kDescriptorDigits> digits{};
  const auto converted =
      std::to_chars(digits.data(), digits.data() + digits.size(), file.value);
  NativeString descriptor{gltf_detail::Allocator<char>(allocation)};
  descriptor.assign(kDescriptorPrefix);
  descriptor.append(digits.data(), converted.ptr);
  Count(descriptor.size() + 1, limits.max_native_path_code_units);
  result.resize(static_cast<std::size_t>(limits.max_native_path_code_units));
  const auto length =
      readlink(descriptor.c_str(), result.data(), result.size());
  if (length < 0) IoFailure(file, GltfFileDiagnosticCode::kFileRead);
  if (static_cast<std::size_t>(length) >= result.size())
    Fail(GltfFileStatus::kLimitExceeded, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kCountLimit, file.ordinal);
  result.resize(static_cast<std::size_t>(length));
#endif
  Count(result.size() + 1, limits.max_native_path_code_units);
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
  if (file.hooks && file.hooks->before_read)
    file.hooks->before_read(file.ordinal, offset, file.hooks->state);
#ifdef _WIN32
  Count(bytes.size(), std::numeric_limits<DWORD>::max(),
        GltfFileStage::kCapture);
  Count(offset, std::numeric_limits<LONGLONG>::max(), GltfFileStage::kCapture);
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  const auto handle = reinterpret_cast<HANDLE>(file.value);
  if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
    IoFailure(file, GltfFileDiagnosticCode::kFileRead);
  DWORD read = 0;
  if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read,
                nullptr))
    IoFailure(file, GltfFileDiagnosticCode::kFileRead);
  if (read != bytes.size())
    Fail(GltfFileStatus::kIoFailure, GltfFileStage::kCapture,
         GltfFileDiagnosticCode::kFileChanged, file.ordinal, offset + read);
#else
  Count(offset, std::numeric_limits<off_t>::max(), GltfFileStage::kCapture);
  Count(bytes.size(), std::numeric_limits<ssize_t>::max(),
        GltfFileStage::kCapture);
  ssize_t read;
  do {
    read = pread(static_cast<int>(file.value), bytes.data(), bytes.size(),
                 static_cast<off_t>(offset));
  } while (read < 0 && errno == EINTR);
  if (read < 0) IoFailure(file, GltfFileDiagnosticCode::kFileRead);
  if (static_cast<std::size_t>(read) != bytes.size())
    Fail(GltfFileStatus::kIoFailure, GltfFileStage::kCapture,
         GltfFileDiagnosticCode::kFileChanged, file.ordinal, offset + read);
#endif
}
}  // namespace meshvale::interchange::gltf_files_detail
