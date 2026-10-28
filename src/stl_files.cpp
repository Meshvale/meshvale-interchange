// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/stl_files.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "file_publication_native.h"
#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "stl_files_internal.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/param.h>
#endif
#endif
namespace meshvale::interchange::stl_files_detail {
namespace {
constexpr unsigned char kAsciiEnd = 0x80;
constexpr unsigned char kControlEnd = 0x20;
constexpr unsigned char kDelete = 0x7f;
constexpr std::uint32_t kLatinControlEnd = 0xa0;
constexpr std::uint32_t kUnicodeEnd = 0x10ffff;
constexpr std::uint32_t kSurrogateFirst = 0xd800;
constexpr std::uint32_t kSurrogateLast = 0xdfff;
constexpr std::string_view kProhibited = "/\\:<>\"|?*";
// Unicode scalar UTF-8 lead ranges, payload masks and minimal scalar values.
struct Utf8Lead {
  unsigned char first, last, mask;
  std::size_t tails;
  std::uint32_t minimum;
};
constexpr std::array kUtf8Leads{Utf8Lead{0xc2, 0xdf, 0x1f, 1, 0x80},
                                Utf8Lead{0xe0, 0xef, 0x0f, 2, 0x800},
                                Utf8Lead{0xf0, 0xf4, 0x07, 3, 0x10000}};
char Fold(char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
[[noreturn]] void Unsupported() {
  Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
       StlFileDiagnosticCode::kNameUnsupported);
}
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
NativeString Normalize(NativeView p, const allocation::AllocationContext& a) {
  NativeString out{allocation::Allocator<NativeChar>(a)};
  out.reserve(p.size());
#ifdef _WIN32
  constexpr auto root = kDriveRootUnits;
#else
  constexpr std::size_t root = 1;
#endif
  Extend(out, p.substr(0, root), a);
  for (std::size_t begin = root; begin < p.size();) {
    a.Checkpoint();
    while (begin < p.size() && p[begin] == kSeparator) ++begin;
    auto end = begin;
    while (end < p.size() && p[end] != kSeparator) {
      a.Checkpoint();
      ++end;
    }
    const auto part = p.substr(begin, end - begin);
    if (part.size() == 1 && part[0] == static_cast<NativeChar>('.')) {
    } else if (part.size() == 2 && part[0] == static_cast<NativeChar>('.') &&
               part[1] == static_cast<NativeChar>('.')) {
      if (out.size() > root) {
        auto last = out.size();
        while (last > root && out[last - 1] != kSeparator) {
          a.Checkpoint();
          --last;
        }
        out.resize(last > root ? last - 1 : root);
      }
    } else if (!part.empty()) {
      if (out.back() != kSeparator) out.push_back(kSeparator);
      Extend(out, part, a);
    }
    begin = end;
  }
  return out;
}
NativeChar NativeFold(NativeChar c) noexcept {
#ifdef _WIN32
  if (c >= L'A' && c <= L'Z') return static_cast<NativeChar>(c + (L'a' - L'A'));
#endif
  return c;
}
bool EqualNative(NativeView a, NativeView b,
                 const allocation::AllocationContext& x) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    x.Checkpoint();
    if (NativeFold(a[i]) != NativeFold(b[i])) return false;
  }
  return true;
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
                             StlFilePhase stage) {
  for (std::size_t index = 0; index < storage.size(); ++index) {
    allocator.Checkpoint();
    if (storage[index] == NativeChar{}) return index;
  }
  Fail(StlFileStatus::kLimitExceeded, stage,
       StlFileDiagnosticCode::kCountLimit);
}
void PortableName(std::string_view name, std::uint64_t cap,
                  const allocation::AllocationContext& allocator) {
  Count(name.size(), cap);
  if (name.empty() || name == "." || name == ".." || name.back() == '.' ||
      name.back() == ' ')
    Unsupported();
  for (std::size_t i = 0; i < name.size();) {
    allocator.Checkpoint();
    const auto lead = static_cast<unsigned char>(name[i++]);
    if (lead < kAsciiEnd) {
      if (lead < kControlEnd || lead == kDelete ||
          kProhibited.find(static_cast<char>(lead)) != kProhibited.npos)
        Unsupported();
      continue;
    }
    constexpr unsigned char kTailMask = 0xc0, kTailTag = 0x80, kPayload = 0x3f;
    constexpr unsigned kTailBits = 6;
    const Utf8Lead* selected = nullptr;
    for (const auto& entry : kUtf8Leads)
      if (lead >= entry.first && lead <= entry.last) {
        selected = &entry;
        break;
      }
    if (!selected) Unsupported();
    const auto tails = selected->tails;
    auto scalar = static_cast<std::uint32_t>(lead & selected->mask);
    if (tails > name.size() - i) Unsupported();
    for (std::size_t t = 0; t < tails; ++t) {
      const auto value = static_cast<unsigned char>(name[i++]);
      if ((value & kTailMask) != kTailTag) Unsupported();
      scalar = (scalar << kTailBits) | (value & kPayload);
    }
    if (scalar < selected->minimum || scalar > kUnicodeEnd ||
        (scalar >= kSurrogateFirst && scalar <= kSurrogateLast) ||
        scalar < kLatinControlEnd)
      Unsupported();
  }
  std::size_t stem_size = 0;
  while (stem_size < name.size() && name[stem_size] != '.') {
    allocator.Checkpoint();
    ++stem_size;
  }
  const auto stem = name.substr(0, stem_size);
  constexpr std::array<std::string_view, 4> kDevices{"con", "prn", "aux",
                                                     "nul"};
  for (auto device : kDevices)
    if (EqualFold(stem, device)) Unsupported();
  constexpr std::string_view kComDevicePrefix = "com";
  constexpr std::string_view kLptDevicePrefix = "lpt";
  constexpr std::size_t kNumberedDeviceLength = 4;
  constexpr std::size_t kDeviceStemLength = 3;
  constexpr char kFirstDeviceNumber = '1', kLastDeviceNumber = '9';
  if (stem.size() == kNumberedDeviceLength &&
      (EqualFold(stem.substr(0, kDeviceStemLength), kComDevicePrefix) ||
       EqualFold(stem.substr(0, kDeviceStemLength), kLptDevicePrefix)) &&
      stem.back() >= kFirstDeviceNumber && stem.back() <= kLastDeviceNumber)
    Unsupported();
}
NativeString AbsolutePath(NativeView input,
                          const allocation::AllocationContext& allocator,
                          const StlFileLimits& limits) {
  Count(input.size(), limits.max_native_path_code_units - 1);
  if (input.empty())
    Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
         StlFileDiagnosticCode::kPathUnsupported);
  for (auto value : input) {
    allocator.Checkpoint();
    if (value == NativeChar{})
      Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
           StlFileDiagnosticCode::kPathUnsupported);
  }
#ifdef _WIN32
  constexpr std::uint32_t kUtf16HighBegin = 0xd800, kUtf16HighEnd = 0xdbff,
                          kUtf16LowBegin = 0xdc00, kUtf16LowEnd = 0xdfff;
  for (std::size_t i = 0; i < input.size(); ++i) {
    allocator.Checkpoint();
    const auto unit = static_cast<std::uint32_t>(input[i]);
    if ((input[i] == L':' && i != 1) ||
        (unit >= kUtf16LowBegin && unit <= kUtf16LowEnd))
      Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
           StlFileDiagnosticCode::kPathUnsupported);
    if (unit >= kUtf16HighBegin && unit <= kUtf16HighEnd) {
      if (++i == input.size() ||
          static_cast<std::uint32_t>(input[i]) < kUtf16LowBegin ||
          static_cast<std::uint32_t>(input[i]) > kUtf16LowEnd)
        Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
             StlFileDiagnosticCode::kPathUnsupported);
    }
  }
#endif
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
      Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
           StlFileDiagnosticCode::kPathUnsupported);
    Count(limits.max_native_path_code_units, std::numeric_limits<DWORD>::max());
    Zeros(output, static_cast<std::size_t>(limits.max_native_path_code_units),
          allocator);
    const auto count =
        GetCurrentDirectoryW(static_cast<DWORD>(output.size()), output.data());
    if (!count || count >= output.size())
      Fail(StlFileStatus::kIoFailure, StlFilePhase::kPathPlanning,
           StlFileDiagnosticCode::kPathUnsupported, {}, {},
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
      Fail(StlFileStatus::kIoFailure, StlFilePhase::kPathPlanning,
           StlFileDiagnosticCode::kPathUnsupported, {}, {},
           std::error_code(errno, std::generic_category()));
    output.resize(TerminatedLength(
        std::span<const NativeChar>(output.data(), output.size()), allocator,
        StlFilePhase::kPathPlanning));
    if (output.back() != kSeparator) output.push_back(kSeparator);
  }
  Count(output.size() + input.size() + 1, limits.max_native_path_code_units);
  Extend(output, input, allocator);
#endif
#ifdef _WIN32
  std::array<NativeChar, kDriveRootUnits + 1> drive{output[0], L':', kSeparator,
                                                    NativeChar{}};
  const auto kind = GetDriveTypeW(drive.data());
  if (kind == DRIVE_REMOTE || kind == DRIVE_UNKNOWN ||
      kind == DRIVE_NO_ROOT_DIR)
    Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
         StlFileDiagnosticCode::kPathUnsupported);
#endif
  // Preserve dot/parent components for alias admission; never resolve an
  // unsupported alias away before RejectAliases traverses the logical path.
  return output;
}
String EntryName(NativeView path,
                 const allocation::AllocationContext& allocator,
                 const StlFileLimits& limits) {
  const auto end = LastSeparator(path, allocator);
  const auto tail = path.substr(end == path.npos ? 0 : end + 1);
  String result{allocation::Allocator<char>(allocator)};
#ifdef _WIN32
  Count(tail.size(), std::numeric_limits<int>::max());
  const int bytes = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, tail.data(), static_cast<int>(tail.size()),
      nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) Unsupported();
  Count(static_cast<std::uint64_t>(bytes), limits.max_entry_name_bytes);
  Zeros(result, static_cast<std::size_t>(bytes), allocator);
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, tail.data(),
                           static_cast<int>(tail.size()), result.data(), bytes,
                           nullptr, nullptr))
    Unsupported();
#else
  Count(tail.size(), limits.max_entry_name_bytes);
  PortableName(tail, limits.max_entry_name_bytes, allocator);
  Assign(result, tail, allocator);
#endif
  PortableName(result, limits.max_entry_name_bytes, allocator);
  return result;
}
NativeString JoinPath(NativeView root, std::string_view name,
                      const allocation::AllocationContext& allocator,
                      const StlFileLimits& limits) {
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
  const std::size_t separator = root.back() == kSeparator ? 0 : 1;
  Count(tail.size() + 1, limits.max_native_path_code_units);
  if (root.size() + separator >
      limits.max_native_path_code_units - 1 - tail.size())
    Fail(StlFileStatus::kLimitExceeded, StlFilePhase::kPathPlanning,
         StlFileDiagnosticCode::kCountLimit);
  NativeString result{allocation::Allocator<NativeChar>(allocator)};
  result.reserve(root.size() + separator + tail.size());
  Extend(result, root, allocator);
  if (separator) result.push_back(kSeparator);
  Extend(result, NativeView(tail), allocator);
  return result;
}
}  // namespace meshvale::interchange::stl_files_detail

namespace meshvale::interchange::stl_files_detail {
namespace {
std::error_code NativeError() {
#ifdef _WIN32
  return {static_cast<int>(GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}
[[noreturn]] void IoFailure(const NativeFile& file,
                            StlFileDiagnosticCode code) {
  Fail(StlFileStatus::kIoFailure, file.stage, code, file.ordinal, {},
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
                   const StlFileLimits& limits, StlFilePhase stage) {
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
      Fail(StlFileStatus::kIoFailure, stage, StlFileDiagnosticCode::kOpen, {},
           {}, NativeError());
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      Fail(StlFileStatus::kUnsupportedProfile, stage,
           StlFileDiagnosticCode::kPathUnsupported);
    const auto is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat metadata{};
    if (lstat(prefix.c_str(), &metadata) != 0)
      Fail(StlFileStatus::kIoFailure, stage, StlFileDiagnosticCode::kOpen, {},
           {}, NativeError());
    if (S_ISLNK(metadata.st_mode))
      Fail(StlFileStatus::kUnsupportedProfile, stage,
           StlFileDiagnosticCode::kPathUnsupported);
    const auto is_directory = S_ISDIR(metadata.st_mode);
    if (end == path.size() && !directory && !S_ISREG(metadata.st_mode))
      Fail(StlFileStatus::kUnsupportedProfile, stage,
           StlFileDiagnosticCode::kNotRegular);
#endif
    if ((end < path.size() || directory) && !is_directory)
      Fail(StlFileStatus::kUnsupportedProfile, stage,
           StlFileDiagnosticCode::kNotRegular);
  }
}
NativeFile OpenFile(NativeView path, std::uint64_t ordinal,
                    const FileHooks& hooks, bool directory,
                    StlFilePhase stage) {
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
    IoFailure(result, StlFileDiagnosticCode::kOpen);
  result.value = reinterpret_cast<std::intptr_t>(handle);
  if (result.hooks.opened) result.hooks.opened(ordinal, result.hooks.state);
  FILE_ATTRIBUTE_TAG_INFO tag{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                    sizeof(tag)))
    IoFailure(result, StlFileDiagnosticCode::kRead);
  if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    Fail(StlFileStatus::kUnsupportedProfile, stage,
         StlFileDiagnosticCode::kPathUnsupported, ordinal);
  if (((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
      GetFileType(handle) != FILE_TYPE_DISK)
    Fail(StlFileStatus::kUnsupportedProfile, stage,
         StlFileDiagnosticCode::kNotRegular, ordinal);
#else
  result.value =
      open(path.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                            (directory ? O_DIRECTORY : 0));
  if (result.value == kInvalidFileHandle)
    IoFailure(result, StlFileDiagnosticCode::kOpen);
  if (result.hooks.opened) result.hooks.opened(ordinal, result.hooks.state);
#endif
  return result;
}
NativeMetadata Metadata(const NativeFile& file, bool directory) {
#ifdef _WIN32
  constexpr unsigned kWordBits = 32;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(file.value), &info))
    IoFailure(file, StlFileDiagnosticCode::kRead);
  if (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
      GetFileType(reinterpret_cast<HANDLE>(file.value)) != FILE_TYPE_DISK)
    Fail(StlFileStatus::kUnsupportedProfile, file.stage,
         StlFileDiagnosticCode::kNotRegular, file.ordinal);
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
    IoFailure(file, StlFileDiagnosticCode::kRead);
  if (!(directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) ||
      info.st_size < 0)
    Fail(StlFileStatus::kUnsupportedProfile, file.stage,
         StlFileDiagnosticCode::kNotRegular, file.ordinal);
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
                           const StlFileLimits& limits) {
  NativeString result{allocation::Allocator<NativeChar>(allocation)};
#ifdef _WIN32
  constexpr std::wstring_view kDosPrefix = L"\\\\?\\";
  const auto needed =
      GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(file.value), nullptr,
                                0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!needed) IoFailure(file, StlFileDiagnosticCode::kRead);
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
    IoFailure(file, StlFileDiagnosticCode::kRead);
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
    Fail(StlFileStatus::kUnsupportedProfile, file.stage,
         StlFileDiagnosticCode::kPathUnsupported);
#elif defined(__APPLE__)
  result.reserve(MAXPATHLEN);
  while (result.size() < MAXPATHLEN) {
    allocation.Checkpoint();
    result.push_back(0);
  }
  if (fcntl(static_cast<int>(file.value), F_GETPATH, result.data()) != 0)
    IoFailure(file, StlFileDiagnosticCode::kRead);
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
  if (length < 0) {
    const auto error = NativeError();
    if (error == std::errc::no_such_file_or_directory ||
        error == std::errc::not_a_directory)
      Fail(StlFileStatus::kUnsupportedProfile, file.stage,
           StlFileDiagnosticCode::kPathUnsupported, file.ordinal, {}, error);
    IoFailure(file, StlFileDiagnosticCode::kRead);
  }
  if (static_cast<std::size_t>(length) >= result.size())
    Fail(StlFileStatus::kLimitExceeded, file.stage,
         StlFileDiagnosticCode::kCountLimit, file.ordinal);
  result.resize(static_cast<std::size_t>(length));
#else
  Fail(StlFileStatus::kUnsupportedProfile, file.stage,
       StlFileDiagnosticCode::kPathUnsupported, file.ordinal);
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
    IoFailure(file, StlFileDiagnosticCode::kRead);
  DWORD read = 0;
  if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read,
                nullptr))
    IoFailure(file, StlFileDiagnosticCode::kRead);
  if (read != bytes.size())
    Fail(StlFileStatus::kFileChanged, file.stage,
         StlFileDiagnosticCode::kChanged, file.ordinal, offset + read);
#else
  Count(offset, std::numeric_limits<off_t>::max(), file.stage);
  Count(bytes.size(), std::numeric_limits<ssize_t>::max(), file.stage);
  ssize_t read;
  do {
    read = pread(static_cast<int>(file.value), bytes.data(), bytes.size(),
                 static_cast<off_t>(offset));
  } while (read < 0 && errno == EINTR);
  if (read < 0) IoFailure(file, StlFileDiagnosticCode::kRead);
  if (static_cast<std::size_t>(read) != bytes.size())
    Fail(StlFileStatus::kFileChanged, file.stage,
         StlFileDiagnosticCode::kChanged, file.ordinal, offset + read);
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
    IoFailure(file, StlFileDiagnosticCode::kRead);
  DWORD count = 0;
  if (!ReadFile(handle, &byte, 1, &count, nullptr))
    IoFailure(file, StlFileDiagnosticCode::kRead);
#else
  Count(offset, std::numeric_limits<off_t>::max(), file.stage);
  ssize_t count;
  do {
    count = pread(static_cast<int>(file.value), &byte, 1,
                  static_cast<off_t>(offset));
  } while (count < 0 && errno == EINTR);
  if (count < 0) IoFailure(file, StlFileDiagnosticCode::kRead);
#endif
  if (count != 0)
    Fail(StlFileStatus::kFileChanged, file.stage,
         StlFileDiagnosticCode::kChanged, file.ordinal, offset);
}
}  // namespace meshvale::interchange::stl_files_detail

namespace meshvale::interchange::stl_files_detail {
namespace {
constexpr std::string_view kStagePrefix = ".meshvale-stl-stage-";
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
bool Present(NativeView path,
             StlFilePhase phase = StlFilePhase::kPathPlanning) {
#ifdef _WIN32
  if (GetFileAttributesW(path.data()) != INVALID_FILE_ATTRIBUTES) return true;
#else
  struct stat value{};
  if (lstat(path.data(), &value) == 0) return true;
#endif
  const auto error = Error();
  if (Missing(error)) return false;
  Fail(StlFileStatus::kIoFailure, phase, StlFileDiagnosticCode::kOpen, {}, {},
       error);
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
                  std::error_code& error, bool directory = true) noexcept {
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
  same =
      same &&
      (((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory) &&
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
      (directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) &&
      static_cast<std::uint64_t>(info.st_dev) == expected.volume &&
      static_cast<std::uint64_t>(info.st_ino) == expected.identity;
#endif
  if (!same && !error)
    error = std::make_error_code(std::errc::state_not_recoverable);
  return same && !error;
}
}  // namespace
void CloseChecked(NativeFile& file, const TestOptions& testing,
                  StlFilePhase stage) {
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
    Fail(StlFileStatus::kIoFailure, stage, StlFileDiagnosticCode::kClose,
         file.ordinal, {}, error);
}
#ifdef _WIN32
NativeSourceKeys::NativeSourceKeys(
    const allocation::AllocationContext& allocator)
    : primary(allocation::Allocator<NativeChar>(allocator)),
      submitted(allocation::Allocator<NativeChar>(allocator)),
      short_alias(allocation::Allocator<NativeChar>(allocator)) {}
#else
NativeSourceKeys::NativeSourceKeys(
    const allocation::AllocationContext& allocator)
    : primary(allocation::Allocator<NativeChar>(allocator)) {}
#endif
std::array<NativeView, kNativeSourceKeySlots> NativeSourceKeys::Views()
    const noexcept {
#ifdef _WIN32
  return {NativeView(primary), NativeView(submitted), NativeView(short_alias)};
#else
  return {NativeView(primary)};
#endif
}
NativePathAdmission::NativePathAdmission(
    const allocation::AllocationContext& allocator)
    : keys(allocator) {}
namespace {
#ifdef _WIN32
constexpr std::size_t kNativeRootUnits = kDriveRootUnits;
#else
constexpr std::size_t kNativeRootUnits = 1;
#endif
struct NativeComponentCursor {
  NativeView path;
  std::size_t position = kNativeRootUnits;
  NativeView Next(const allocation::AllocationContext& allocator) {
    if (position >= path.size()) return {};
    const auto begin = position;
    while (position < path.size() && path[position] != kSeparator) {
      allocator.Checkpoint();
      ++position;
    }
    const auto component = path.substr(begin, position - begin);
    if (position < path.size()) ++position;
    return component;
  }
};
using NativeKeyCursors =
    std::array<NativeComponentCursor, kNativeSourceKeySlots>;
NativeKeyCursors KeyCursors(const NativeSourceKeys& keys) {
  NativeKeyCursors result{};
  const auto views = keys.Views();
  for (std::size_t index = 0; index < views.size(); ++index)
    result[index].path = views[index];
  return result;
}
bool EqualRoot(NativeView first, NativeView second,
               const allocation::AllocationContext& allocator) {
  return first.size() >= kNativeRootUnits &&
         second.size() >= kNativeRootUnits &&
         EqualNative(first.substr(0, kNativeRootUnits),
                     second.substr(0, kNativeRootUnits), allocator);
}
void RequireAligned(const NativeSourceKeys& keys) {
  if (!keys.aligned || keys.primary.empty())
    Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
         StlFileDiagnosticCode::kPathUnsupported);
}
#ifdef _WIN32
enum class WindowsPathSpelling { kLong, kShort };
std::size_t ValidateWindowsKey(NativeView path,
                               const allocation::AllocationContext& allocator,
                               const StlFileLimits& limits,
                               StlFilePhase stage) {
  Count(path.size() + 1, limits.max_native_path_code_units, stage);
  if (path.size() < kNativeRootUnits || path[1] != L':' ||
      path[2] != kSeparator ||
      !((path[0] >= L'A' && path[0] <= L'Z') ||
        (path[0] >= L'a' && path[0] <= L'z')) ||
      (path.size() > kNativeRootUnits && path.back() == kSeparator))
    Fail(StlFileStatus::kUnsupportedProfile, stage,
         StlFileDiagnosticCode::kPathUnsupported);
  std::size_t components = 0;
  NativeComponentCursor cursor{path};
  while (cursor.position < path.size()) {
    const auto component = cursor.Next(allocator);
    if (component.empty() || component == L"." || component == L"..")
      Fail(StlFileStatus::kUnsupportedProfile, stage,
           StlFileDiagnosticCode::kPathUnsupported);
    for (auto unit : component) {
      allocator.Checkpoint();
      if (!unit || unit == L'/' || unit == L':')
        Fail(StlFileStatus::kUnsupportedProfile, stage,
             StlFileDiagnosticCode::kPathUnsupported);
    }
    ++components;
  }
  return components;
}
NativeString QueryWindowsPath(NativeView path, WindowsPathSpelling spelling,
                              const allocation::AllocationContext& allocator,
                              const StlFileLimits& limits, StlFilePhase stage) {
  Count(limits.max_native_path_code_units, std::numeric_limits<DWORD>::max(),
        stage);
  Count(limits.max_native_path_code_units,
        std::numeric_limits<std::size_t>::max(), stage);
  NativeString result{allocation::Allocator<NativeChar>(allocator)};
  Zeros(result, static_cast<std::size_t>(limits.max_native_path_code_units),
        allocator);
  allocator.Checkpoint();
  const auto length =
      spelling == WindowsPathSpelling::kLong
          ? GetLongPathNameW(path.data(), result.data(),
                             static_cast<DWORD>(result.size()))
          : GetShortPathNameW(path.data(), result.data(),
                              static_cast<DWORD>(result.size()));
  if (!length)
    Fail(StlFileStatus::kIoFailure, stage,
         StlFileDiagnosticCode::kPathUnsupported, {}, {}, NativeError());
  if (length >= result.size())
    Fail(StlFileStatus::kLimitExceeded, stage,
         StlFileDiagnosticCode::kCountLimit);
  allocator.Checkpoint();
  result.resize(length);
  (void)ValidateWindowsKey(result, allocator, limits, stage);
  return result;
}
// Only private planning buffers are temporarily terminated. In particular the
// caller's original traversal and public retained keys are never modified.
class NativePrefixTerminator {
 public:
  NativePrefixTerminator(NativeString& path, std::size_t end) noexcept
      : path_(path),
        end_(end),
        saved_(end < path.size() ? path[end] : NativeChar{}) {
    if (end_ < path_.size()) path_[end_] = 0;
  }
  ~NativePrefixTerminator() noexcept {
    if (end_ < path_.size()) path_[end_] = saved_;
  }
  NativePrefixTerminator(const NativePrefixTerminator&) = delete;
  NativePrefixTerminator& operator=(const NativePrefixTerminator&) = delete;
  NativeView View() const noexcept { return {path_.data(), end_}; }

 private:
  NativeString& path_;
  std::size_t end_;
  NativeChar saved_;
};
void AlignWindowsKeys(NativeSourceKeys& keys, const NativeMetadata& expected,
                      const allocation::AllocationContext& allocator,
                      const StlFileLimits& limits, const TestOptions& testing,
                      StlFilePhase stage, std::uint64_t ordinal) {
  const std::array paths{&keys.primary, &keys.submitted, &keys.short_alias};
  const auto count = ValidateWindowsKey(keys.primary, allocator, limits, stage);
  bool alternative = false;
  for (auto* path : paths) {
    if (path->empty()) continue;
    if (!EqualRoot(*path, keys.primary, allocator) ||
        ValidateWindowsKey(*path, allocator, limits, stage) != count)
      Fail(StlFileStatus::kUnsupportedProfile, stage,
           StlFileDiagnosticCode::kPathUnsupported, ordinal);
    alternative = alternative || path != &keys.primary;
  }
  if (!alternative) return;
  auto cursors = KeyCursors(keys);
  // Each slot is an observed complete path. At each ordinal, real prefix
  // identities prove that its component is an alternative in that directory.
  // No mixed full path is generated, and no per-component table is allocated.
  for (std::size_t level = 0; level <= count; ++level) {
    std::optional<NativeMetadata> reference;
    const bool directory = level < count;
    for (std::size_t index = 0; index < paths.size(); ++index) {
      auto& path = *paths[index];
      if (path.empty()) continue;
      allocator.Checkpoint();
      std::size_t end = kNativeRootUnits;
      if (level) {
        const auto component = cursors[index].Next(allocator);
        end = static_cast<std::size_t>(component.data() - path.data()) +
              component.size();
      }
      NativePrefixTerminator prefix(path, end);
      auto file =
          OpenFile(prefix.View(), ordinal, testing.files, directory, stage);
      const auto observed = Metadata(file, directory);
      if (reference && (observed.volume != reference->volume ||
                        observed.identity != reference->identity))
        Fail(StlFileStatus::kFileChanged, stage,
             StlFileDiagnosticCode::kChanged, ordinal);
      if (!reference) reference = observed;
      if (!directory) {
        if (observed != expected)
          Fail(StlFileStatus::kFileChanged, stage,
               StlFileDiagnosticCode::kChanged, ordinal);
        auto canonical = CanonicalPath(file, allocator, limits);
        // primary may currently expose its final prefix: it is the complete
        // source at this level, and the terminator guard remains in scope.
        if (!EqualNative(keys.primary, canonical, allocator))
          Fail(StlFileStatus::kFileChanged, stage,
               StlFileDiagnosticCode::kChanged, ordinal);
      }
      CloseChecked(file, testing, stage);
      allocator.Checkpoint();
    }
  }
}
#endif
}  // namespace
NativeSourceKeys PlanNativePath(NativeView original, NativePathUse use,
                                const allocation::AllocationContext& allocator,
                                const StlFileLimits& limits,
                                StlFilePhase stage) {
  RejectAliases(original, use == NativePathUse::kParentDirectory, allocator,
                limits, stage);
  auto normalized = Normalize(original, allocator);
  NativeSourceKeys keys(allocator);
#ifdef _WIN32
  keys.primary = QueryWindowsPath(normalized, WindowsPathSpelling::kLong,
                                  allocator, limits, stage);
  if (use == NativePathUse::kRetainedSource) {
    if (!EqualNative(normalized, keys.primary, allocator))
      keys.submitted = std::move(normalized);
    auto short_path = QueryWindowsPath(
        keys.primary, WindowsPathSpelling::kShort, allocator, limits, stage);
    if (!EqualNative(short_path, keys.primary, allocator) &&
        (keys.submitted.empty() ||
         !EqualNative(short_path, keys.submitted, allocator)))
      keys.short_alias = std::move(short_path);
  }
#else
  keys.primary = std::move(normalized);
#endif
  return keys;
}
NativePathAdmission OpenAdmittedNativePath(
    NativeView original, NativePathUse use, NativeSourceKeys&& keys,
    const allocation::AllocationContext& allocator, const StlFileLimits& limits,
    const TestOptions& testing, StlFilePhase stage, std::uint64_t ordinal) {
  const bool directory = use == NativePathUse::kParentDirectory;
  RejectAliases(original, directory, allocator, limits, stage);
  NativePathAdmission result(allocator);
  result.keys = std::move(keys);
  result.file = OpenFile(original, ordinal, testing.files, directory, stage);
  result.identity = Metadata(result.file, directory);
  auto canonical = CanonicalPath(result.file, allocator, limits);
  if (!EqualNative(result.keys.primary, canonical, allocator))
    Fail(StlFileStatus::kFileChanged, stage, StlFileDiagnosticCode::kChanged,
         ordinal);
#ifdef _WIN32
  if (use == NativePathUse::kRetainedSource)
    AlignWindowsKeys(result.keys, result.identity, allocator, limits, testing,
                     stage, ordinal);
#endif
  const auto after = Metadata(result.file, directory);
  const bool changed = directory
                           ? after.volume != result.identity.volume ||
                                 after.identity != result.identity.identity
                           : after != result.identity;
  if (changed)
    Fail(StlFileStatus::kFileChanged, stage, StlFileDiagnosticCode::kChanged,
         ordinal);
  result.keys.aligned = true;
  allocator.Checkpoint();
  return result;
}
bool SourceOverlaps(NativeView destination, const NativeSourceKeys& source,
                    const allocation::AllocationContext& allocator) {
  RequireAligned(source);
  if (!EqualRoot(destination, source.primary, allocator)) return false;
  NativeComponentCursor target{destination};
  auto sources = KeyCursors(source);
  for (;;) {
    const auto component = target.Next(allocator);
    std::array<NativeView, kNativeSourceKeySlots> alternatives{};
    for (std::size_t index = 0; index < alternatives.size(); ++index)
      alternatives[index] = sources[index].Next(allocator);
    if (component.empty() || alternatives.front().empty()) return true;
    bool matches = false;
    for (auto alternative : alternatives)
      if (!alternative.empty() &&
          EqualNative(component, alternative, allocator))
        matches = true;
    if (!matches) return false;
  }
}
bool SourcesEqual(const NativeSourceKeys& first, const NativeSourceKeys& second,
                  const allocation::AllocationContext& allocator) {
  RequireAligned(first);
  RequireAligned(second);
  if (!EqualRoot(first.primary, second.primary, allocator)) return false;
  auto a = KeyCursors(first), b = KeyCursors(second);
  for (;;) {
    std::array<NativeView, kNativeSourceKeySlots> left{}, right{};
    for (std::size_t index = 0; index < left.size(); ++index) {
      left[index] = a[index].Next(allocator);
      right[index] = b[index].Next(allocator);
    }
    if (left.front().empty() || right.front().empty())
      return left.front().empty() && right.front().empty();
    bool matches = false;
    for (auto x : left)
      for (auto y : right)
        if (!x.empty() && !y.empty() && EqualNative(x, y, allocator))
          matches = true;
    if (!matches) return false;
  }
}
void WriteFile(NativeView path, std::span<const std::byte> bytes,
               std::uint64_t ordinal, const StlFileWorkPolicy& work,
               const allocation::AllocationContext& allocator,
               const TestOptions& testing,
               std::optional<NativeMetadata>* owned_identity,
               StlFilePhase phase) {
  NativeFile file;
  file.ordinal = ordinal;
  file.stage = phase;
  file.hooks = testing.files;
  if (file.hooks.before_open) file.hooks.before_open(ordinal, file.hooks.state);
#ifdef _WIN32
  const auto handle =
      CreateFileW(path.data(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    Fail(StlFileStatus::kIoFailure, phase, StlFileDiagnosticCode::kOpen,
         ordinal, {}, Error());
  file.value = reinterpret_cast<std::intptr_t>(handle);
#else
  constexpr mode_t kPrivateFileMode = 0600;
  file.value =
      open(path.data(),
           O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_CREAT | O_EXCL,
           kPrivateFileMode);
  if (file.value == kInvalidFileHandle)
    Fail(StlFileStatus::kIoFailure, phase, StlFileDiagnosticCode::kOpen,
         ordinal, {}, Error());
#endif
  if (file.hooks.opened) file.hooks.opened(ordinal, file.hooks.state);
  const auto identity = Metadata(file);
  if (owned_identity) owned_identity->emplace(identity);
  const auto block = static_cast<std::size_t>(
      std::min({work.io_block_bytes, work.cancellation_block_bytes,
                MaximumReadBlockBytes()}));
  for (std::size_t offset = 0; offset < bytes.size();) {
    if (testing.checkpoint) testing.checkpoint(phase, offset, testing.state);
    allocator.Checkpoint();
    const auto size = std::min(block, bytes.size() - offset);
    if (file.hooks.before_write)
      file.hooks.before_write(ordinal, offset, file.hooks.state);
#ifdef _WIN32
    DWORD written = 0;
    if (!::WriteFile(reinterpret_cast<HANDLE>(file.value),
                     bytes.data() + offset, static_cast<DWORD>(size), &written,
                     nullptr))
      Fail(StlFileStatus::kIoFailure, phase, StlFileDiagnosticCode::kWrite,
           ordinal, offset, Error());
    const auto count = static_cast<std::size_t>(written);
#else
    ssize_t written;
    do {
      written =
          write(static_cast<int>(file.value), bytes.data() + offset, size);
    } while (written < 0 && errno == EINTR);
    if (written < 0)
      Fail(StlFileStatus::kIoFailure, phase, StlFileDiagnosticCode::kWrite,
           ordinal, offset, Error());
    const auto count = static_cast<std::size_t>(written);
#endif
    if (!count)
      Fail(StlFileStatus::kIoFailure, phase, StlFileDiagnosticCode::kWrite,
           ordinal, offset);
    offset += count;
  }
  CloseChecked(file, testing, phase);
  allocator.Checkpoint();
}
NativePublication::NativePublication(
    const allocation::AllocationContext& allocator, const StlFileLimits& limits,
    const TestOptions& testing)
    : allocation_(allocator),
      limits_(limits),
      testing_(testing),
      destination_(allocation::Allocator<NativeChar>(allocator)),
      parent_(allocation::Allocator<NativeChar>(allocator)),
      logical_parent_(allocation::Allocator<NativeChar>(allocator)),
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
    Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPathPlanning,
         StlFileDiagnosticCode::kPathUnsupported);
  Assign(parent_, NativeView(destination_).substr(0, std::max(separator, root)),
         allocation_);
  Assign(logical_parent_, NativeView(parent_), allocation_);
  auto keys = PlanNativePath(parent_, NativePathUse::kParentDirectory,
                             allocation_, limits_, StlFilePhase::kPathPlanning);
  auto parent = OpenAdmittedNativePath(
      parent_, NativePathUse::kParentDirectory, std::move(keys), allocation_,
      limits_, testing_, StlFilePhase::kPathPlanning, 0);
  parent_identity_ = parent.identity;
  CloseChecked(parent.file, testing_, StlFilePhase::kPathPlanning);
  // Admission precedes use of canonical parent. Logical alias traversal has
  // already been refused; canonicalization never makes an alias acceptable.
  parent_ = std::move(parent.keys.primary);
  if (Present(destination_))
    Fail(StlFileStatus::kDestinationExists, StlFilePhase::kPathPlanning,
         StlFileDiagnosticCode::kDestinationExists);
  // The new leaf has no native alias yet. Its existing parent has one admitted
  // long key; retain the requested leaf unchanged under that parent.
  const auto destination_name = NativeView(destination_).substr(separator + 1);
  const std::size_t separator_units = parent_.back() == kSeparator ? 0 : 1;
  const auto native_capacity =
      std::min<std::uint64_t>(limits_.max_native_path_code_units,
                              std::numeric_limits<std::size_t>::max());
  Count(destination_name.size(), native_capacity - 1);
  Count(parent_.size(), native_capacity - 1 - destination_name.size());
  Count(separator_units,
        native_capacity - 1 - destination_name.size() - parent_.size());
  NativeString admitted_destination{
      allocation::Allocator<NativeChar>(allocation_)};
  admitted_destination.reserve(parent_.size() + separator_units +
                               destination_name.size());
  Extend(admitted_destination, NativeView(parent_), allocation_);
  if (separator_units) {
    allocation_.Checkpoint();
    admitted_destination.push_back(kSeparator);
  }
  Extend(admitted_destination, destination_name, allocation_);
  destination_ = std::move(admitted_destination);
  if (Present(destination_))
    Fail(StlFileStatus::kDestinationExists, StlFilePhase::kPathPlanning,
         StlFileDiagnosticCode::kDestinationExists);
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
  Fail(StlFileStatus::kIoFailure, StlFilePhase::kPathPlanning,
       StlFileDiagnosticCode::kOpen);
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
    Fail(StlFileStatus::kIoFailure, StlFilePhase::kStaging,
         StlFileDiagnosticCode::kOpen, {}, {}, Error());
  owned_ = true;
  auto directory =
      OpenFile(stage_, 0, testing_.files, true, StlFilePhase::kStaging);
  identity_ = Metadata(directory, true);
  CloseChecked(directory, testing_, StlFilePhase::kStaging);
  if (testing_.stage_ready) testing_.stage_ready(stage_, testing_.state);
  std::error_code identity_error;
  if (!SameIdentity(stage_, identity_, identity_error))
    Fail(StlFileStatus::kPreservationMismatch, StlFilePhase::kStaging,
         StlFileDiagnosticCode::kStagedInventory, {}, {}, identity_error);
}
void NativePublication::Inventory(bool receipt_present,
                                  std::uint64_t entry_bytes,
                                  std::uint64_t receipt_bytes) {
  allocation_.Checkpoint();
  if (testing_.before_inventory)
    testing_.before_inventory(stage_, testing_.state);
  std::error_code error;
  if (!SameIdentity(stage_, identity_, error))
    Fail(StlFileStatus::kPreservationMismatch, StlFilePhase::kInventory,
         StlFileDiagnosticCode::kStagedInventory, {}, {}, error);
  bool entry = false, receipt = false;
#ifdef _WIN32
  constexpr unsigned kNativeSizeWordBits = 32;
#endif
  const auto same_name = [&](NativeView first, NativeView second) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
      allocation_.Checkpoint();
      if (first[index] != second[index]) return false;
    }
    return true;
  };
  const auto match = [&](NativeView name, bool regular, std::uint64_t bytes) {
    allocation_.Checkpoint();
    if (!regular) return false;
    const auto entry_name =
        NativeView(entry_).substr(LastSeparator(entry_, allocation_) + 1);
    const auto receipt_name = NativeView(receipt_).substr(
        receipt_.empty() ? 0 : LastSeparator(receipt_, allocation_) + 1);
    if (same_name(name, entry_name) && !entry && bytes == entry_bytes) {
      entry = true;
      return true;
    }
    if (receipt_present && same_name(name, receipt_name) && !receipt &&
        bytes == receipt_bytes) {
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
    Fail(StlFileStatus::kIoFailure, StlFilePhase::kInventory,
         StlFileDiagnosticCode::kStagedInventory, {}, {}, Error());
  do {
    const auto storage = std::span<const NativeChar>(data.cFileName);
    const NativeView name(
        data.cFileName,
        TerminatedLength(storage, allocation_, StlFilePhase::kInventory));
    if (name == L"." || name == L"..") continue;
    if (!match(name,
               (data.dwFileAttributes &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0,
               (static_cast<std::uint64_t>(data.nFileSizeHigh)
                << kNativeSizeWordBits) |
                   data.nFileSizeLow))
      Fail(StlFileStatus::kPreservationMismatch, StlFilePhase::kInventory,
           StlFileDiagnosticCode::kStagedInventory);
  } while (FindNextFileW(enumeration.handle, &data));
  if (GetLastError() != ERROR_NO_MORE_FILES)
    Fail(StlFileStatus::kIoFailure, StlFilePhase::kInventory,
         StlFileDiagnosticCode::kStagedInventory, {}, {}, Error());
  const auto handle = std::exchange(enumeration.handle, INVALID_HANDLE_VALUE);
  if (!FindClose(handle))
    Fail(StlFileStatus::kIoFailure, StlFilePhase::kInventory,
         StlFileDiagnosticCode::kClose, {}, {}, Error());
#else
  struct Enumeration {
    DIR* handle = nullptr;
    ~Enumeration() noexcept {
      if (handle) closedir(handle);
    }
  } enumeration;
  enumeration.handle = opendir(stage_.c_str());
  if (!enumeration.handle)
    Fail(StlFileStatus::kIoFailure, StlFilePhase::kInventory,
         StlFileDiagnosticCode::kStagedInventory, {}, {}, Error());
  for (;;) {
    errno = 0;
    auto* item = readdir(enumeration.handle);
    if (!item) {
      if (errno)
        Fail(StlFileStatus::kIoFailure, StlFilePhase::kInventory,
             StlFileDiagnosticCode::kStagedInventory, {}, {}, Error());
      break;
    }
    const auto storage = std::span<const NativeChar>(item->d_name);
    const NativeView name(
        item->d_name,
        TerminatedLength(storage, allocation_, StlFilePhase::kInventory));
    if (name == "." || name == "..") continue;
    struct stat info{};
    if (fstatat(dirfd(enumeration.handle), item->d_name, &info,
                AT_SYMLINK_NOFOLLOW) != 0)
      Fail(StlFileStatus::kIoFailure, StlFilePhase::kInventory,
           StlFileDiagnosticCode::kStagedInventory, {}, {}, Error());
    if (!match(name, S_ISREG(info.st_mode) && info.st_size >= 0,
               static_cast<std::uint64_t>(info.st_size)))
      Fail(StlFileStatus::kPreservationMismatch, StlFilePhase::kInventory,
           StlFileDiagnosticCode::kStagedInventory);
  }
  const auto handle = std::exchange(enumeration.handle, nullptr);
  if (closedir(handle) != 0)
    Fail(StlFileStatus::kIoFailure, StlFilePhase::kInventory,
         StlFileDiagnosticCode::kClose, {}, {}, Error());
#endif
  if (!entry || receipt != receipt_present)
    Fail(StlFileStatus::kPreservationMismatch, StlFilePhase::kInventory,
         StlFileDiagnosticCode::kStagedInventory);
}
StlFileCleanupOutcome NativePublication::Cleanup(
    std::error_code& error) noexcept {
  if (!owned_) return StlFileCleanupOutcome::kNotRequired;
  if (testing_.cleanup_error)
    error = testing_.cleanup_error(stage_, testing_.state);
  if (!error) (void)SameIdentity(stage_, identity_, error);
  if (!error) {
    const std::array paths{NativeView(entry_), NativeView(receipt_)};
    for (std::size_t i = 0; i < paths.size(); ++i) {
      if (paths[i].empty() || !member_identity_[i]) continue;
      std::error_code identity_error;
      if (!SameIdentity(paths[i], *member_identity_[i], identity_error,
                        false)) {
        if (Missing(identity_error)) continue;
        error = identity_error;
        break;
      }
      const auto removed = Remove(paths[i], false);
      if (removed && !Missing(removed)) {
        error = removed;
        break;
      }
    }
    if (!error) error = Remove(stage_, true);
  }
  owned_ = false;
  const auto result =
      error ? StlFileCleanupOutcome::kFailed : StlFileCleanupOutcome::kRemoved;
  if (testing_.cleanup_finished)
    testing_.cleanup_finished(result, error, testing_.state);
  return result;
}
void NativePublication::Commit() {
  allocation_.Checkpoint();
  if (testing_.before_commit) testing_.before_commit(stage_, testing_.state);
  auto keys = PlanNativePath(logical_parent_, NativePathUse::kParentDirectory,
                             allocation_, limits_, StlFilePhase::kPublication);
  RejectAliases(parent_, true, allocation_, limits_,
                StlFilePhase::kPublication);
  auto parent = OpenAdmittedNativePath(
      logical_parent_, NativePathUse::kParentDirectory, std::move(keys),
      allocation_, limits_, testing_, StlFilePhase::kPublication, 0);
  const auto observed_parent = parent.identity;
  if (observed_parent.volume != parent_identity_.volume ||
      observed_parent.identity != parent_identity_.identity ||
      !EqualNative(parent.keys.primary, parent_, allocation_))
    Fail(StlFileStatus::kFileChanged, StlFilePhase::kPublication,
         StlFileDiagnosticCode::kChanged);
  CloseChecked(parent.file, testing_, StlFilePhase::kPublication);
  std::error_code identity_error;
  if (!SameIdentity(stage_, identity_, identity_error))
    Fail(StlFileStatus::kPreservationMismatch, StlFilePhase::kPublication,
         StlFileDiagnosticCode::kStagedInventory, {}, {}, identity_error);
  if (Present(destination_, StlFilePhase::kPublication))
    Fail(StlFileStatus::kDestinationExists, StlFilePhase::kPublication,
         StlFileDiagnosticCode::kDestinationExists);
  allocation_.Checkpoint();
  using file_publication_detail::CommitStatus;
  // The private hook can inject only failure; successful publication still
  // requires the actual native operation. Both failures use one classification.
  const auto injected_error = testing_.commit_error
                                  ? testing_.commit_error(testing_.state)
                                  : std::error_code{};
  if (!injected_error) allocation_.Checkpoint();
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
    Fail(StlFileStatus::kDestinationExists, StlFilePhase::kPublication,
         StlFileDiagnosticCode::kDestinationExists, {}, {}, result.error);
  // ENOSYS identifies missing renameat2 support. EINVAL is kept as a real
  // operation failure because it can also identify an invalid request.
  if (result.status == CommitStatus::kUnsupported ||
      result.error == std::errc::function_not_supported)
    Fail(StlFileStatus::kUnsupportedProfile, StlFilePhase::kPublication,
         StlFileDiagnosticCode::kCommitUnsupported, {}, {}, result.error);
  Fail(StlFileStatus::kPublicationFailed, StlFilePhase::kPublication,
       StlFileDiagnosticCode::kCommit, {}, {}, result.error);
}
}  // namespace meshvale::interchange::stl_files_detail

namespace meshvale::interchange::stl_files_detail {
namespace geo = geometry;
using Stage = StlFilePhase;
using Status = StlFileStatus;
using Code = StlFileDiagnosticCode;
using DigestValue = std::array<std::byte, kStlFileSha256Bytes>;
constexpr std::string_view kReceiptName = "stl-publication.json";
constexpr std::size_t kComponents = 3;
constexpr std::uint64_t kBinaryPrefixBytes = 84, kBinaryFacetBytes = 50;
constexpr std::string_view kNormalName = "facet_normal",
                           kNormalSemantic = "normal";
constexpr geo::index_t kImportedFacetNormalAttribute = 0;
constexpr std::size_t kImportedNormalAttributeCount = 1;
constexpr std::uint64_t kEntryIndex = 0, kReceiptIndex = 1;
struct Isolation {
  Isolation(const allocation::AllocationContext& a,
            const geo::ExecutionContext& c, std::uint64_t cap,
            allocation::AllocationBackend backend)
      : allocation(a),
        context(c),
        max_requests(cap),
        allocation_backend(backend),
        name(allocation::Allocator<char>(a)),
        keys(a) {}
  allocation::AllocationContext allocation;
  geo::ExecutionContext context;
  std::uint64_t max_requests;
  allocation::AllocationBackend allocation_backend;
  String name;
  NativeSourceKeys keys;
  NativeMetadata identity;
  DigestValue digest{};
};
struct CaptureState {
  CaptureState(const allocation::AllocationContext& a,
               std::shared_ptr<const Isolation> t, Buffer&& b)
      : allocation(a), token(std::move(t)), bytes(std::move(b)) {}
  allocation::AllocationContext allocation;
  std::shared_ptr<const Isolation> token;
  Buffer bytes;
};
struct AssetState {
  AssetState(const allocation::AllocationContext& a,
             std::shared_ptr<const Isolation> t)
      : allocation(a), token(std::move(t)) {}
  allocation::AllocationContext allocation;
  std::shared_ptr<const Isolation> token;
  std::optional<StlDocument> document;
};
struct VerifiedState {
  VerifiedState(const allocation::AllocationContext& a, const StlFileAsset& s,
                const StlOutput& o, const StlDocument& d, const DigestValue& h)
      : allocation(a), source(s), output(o), document(d), digest(h) {}
  allocation::AllocationContext allocation;
  StlFileAsset source;
  StlOutput output;
  StlDocument document;
  DigestValue digest;
};
struct BundleState {
  BundleState(const allocation::AllocationContext& a,
              const StlVerifiedRewrite& v, Buffer&& r, const DigestValue& h)
      : allocation(a), verified(v), receipt(std::move(r)), digest(h) {}
  allocation::AllocationContext allocation;
  StlVerifiedRewrite verified;
  Buffer receipt;
  DigestValue digest;
};
static_assert(std::is_nothrow_copy_constructible_v<StlPublishedBundle>);
static_assert(std::is_nothrow_copy_constructible_v<StlVerifiedRewrite>);
static_assert(std::is_nothrow_copy_assignable_v<StlFileFacts>);
static_assert(std::is_nothrow_move_constructible_v<StlFileRewriteResult>);
struct Access {
  static const CaptureState& State(const StlCapturedFile& c) noexcept {
    return *c.state_;
  }
  static const AssetState& State(const StlFileAsset& a) noexcept {
    return *a.state_;
  }
  static StlCapturedFile Capture(
      std::shared_ptr<const CaptureState> s) noexcept {
    return StlCapturedFile(std::move(s));
  }
  static StlFileAsset Asset(std::shared_ptr<const AssetState> s) noexcept {
    return StlFileAsset(std::move(s));
  }
  static StlVerifiedRewrite Verified(
      std::shared_ptr<const VerifiedState> s) noexcept {
    return StlVerifiedRewrite(std::move(s));
  }
  static StlPublishedBundle Bundle(
      std::shared_ptr<const BundleState> s) noexcept {
    return StlPublishedBundle(std::move(s));
  }
};
[[noreturn]] void Fail(Status status, Stage phase, Code code,
                       std::optional<std::uint64_t> file,
                       std::optional<std::uint64_t> offset,
                       std::optional<std::error_code> error) {
  StlFileDiagnostic d;
  d.status = status;
  d.phase = phase;
  d.code = code;
  d.file_index = file;
  d.byte_offset = offset;
  d.system_error = error;
  throw Failure{d};
}
void Count(std::uint64_t x, std::uint64_t cap, Stage phase) {
  if (x > cap) Fail(Status::kLimitExceeded, phase, Code::kCountLimit);
}
namespace {
std::size_t Host(std::uint64_t x) {
  if (x >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  return static_cast<std::size_t>(x);
}
void Positive(std::uint64_t x) {
  if (!x)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kRequiredFieldZero);
  (void)Host(x);
}
std::uint64_t Add(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  (void)Host(a + b);
  return a + b;
}
std::uint64_t Product(std::uint64_t a, std::uint64_t b) {
  if (a && b > std::numeric_limits<std::uint64_t>::max() / a)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  (void)Host(a * b);
  return a * b;
}
void ValidateEncoding(StlEncoding e) {
  if (e != StlEncoding::kAscii && e != StlEncoding::kBinary)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration, Code::kEncoding);
}
void Validate(const geo::MeshMaterializationLimits& l) {
  for (auto x :
       {l.max_vertices, l.max_faces, l.max_corners, l.max_face_offsets,
        l.max_attributes, l.max_attribute_scalars, l.max_attribute_offsets,
        l.max_presence_bytes, l.max_metadata_entries, l.max_descriptor_bytes,
        l.max_new_requested_bytes})
    Positive(x);
}
void Validate(const geo::MeshInspectionLimits& l) {
  Positive(l.max_issues);
  Positive(l.max_new_requested_bytes);
}
void Validate(const StlWorkPolicy& w) {
  for (auto x : {w.cancellation_block_bytes, w.cancellation_block_tokens,
                 w.cancellation_block_facets, w.geometry.parallel_min_bytes,
                 w.geometry.partition_bytes,
                 w.geometry.cancellation_block_scalars, w.geometry.max_jobs})
    Positive(x);
}
void Validate(const StlReadLimits& l, const StlWorkPolicy& w) {
  for (auto x :
       {l.max_source_bytes, l.max_facets, l.max_name_bytes, l.max_line_bytes,
        l.max_token_bytes, l.max_codec_new_requested_bytes,
        l.max_total_new_requested_bytes})
    Positive(x);
  Validate(l.materialization);
  Validate(l.inspection);
  Validate(w);
  (void)Product(l.max_facets, kComponents);
  (void)Add(l.max_facets, 1);
  (void)Add(kBinaryPrefixBytes, Product(l.max_facets, kBinaryFacetBytes));
  if (Add(Add(l.max_codec_new_requested_bytes,
              l.materialization.max_new_requested_bytes),
          l.inspection.max_new_requested_bytes) >
      l.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const StlWriteLimits& l, const StlWorkPolicy& w) {
  for (auto x :
       {l.max_output_bytes, l.max_facets, l.max_name_bytes, l.max_line_bytes,
        l.max_token_bytes, l.max_codec_new_requested_bytes,
        l.max_total_new_requested_bytes})
    Positive(x);
  Validate(l.inspection);
  Validate(w);
  (void)Product(l.max_facets, kComponents);
  (void)Add(l.max_facets, 1);
  (void)Add(kBinaryPrefixBytes, Product(l.max_facets, kBinaryFacetBytes));
  if (Add(l.max_codec_new_requested_bytes,
          l.inspection.max_new_requested_bytes) >
      l.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const StlFileLimits& l, const StlFileWorkPolicy& w) {
  for (auto x :
       {l.max_entry_bytes, l.max_receipt_bytes, l.max_total_file_bytes,
        l.max_entry_name_bytes, l.max_native_path_code_units,
        l.max_unit_label_bytes, l.max_caller_report_bytes,
        l.max_file_new_requested_bytes, w.io_block_bytes, w.parallel_min_bytes,
        w.partition_bytes, w.cancellation_block_bytes, w.max_jobs})
    Positive(x);
  (void)Product(l.max_native_path_code_units, sizeof(NativeChar));
  (void)Add(l.max_entry_bytes, l.max_receipt_bytes);
}
StlFileDiagnostic Admission(const allocation::AdmissionFailure& e,
                            Stage stage) {
  StlFileDiagnostic d;
  d.phase = stage;
  switch (e.Code()) {
    case allocation::AdmissionCode::kSizeLimit:
      d.status = Status::kLimitExceeded;
      d.code = Code::kRequestBudget;
      break;
    case allocation::AdmissionCode::kBudgetExceeded:
      d.status = Status::kBudgetExceeded;
      d.code = Code::kSharedBudget;
      break;
    case allocation::AdmissionCode::kCanceled:
      d.status = Status::kCanceled;
      d.code = Code::kStopRequested;
      break;
    case allocation::AdmissionCode::kInvalidObject:
      d.status = Status::kInvalidRequest;
      d.code = Code::kSharedBudget;
      break;
  }
  return d;
}
NativeFile OpenRangeWriter(NativeView path, std::uint64_t ordinal,
                           const TestOptions& testing, Stage phase) {
  NativeFile file;
  file.ordinal = ordinal;
  file.stage = phase;
  file.hooks = testing.files;
  if (file.hooks.before_open) file.hooks.before_open(ordinal, file.hooks.state);
#ifdef _WIN32
  const auto handle = CreateFileW(
      path.data(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) IoFailure(file, Code::kOpen);
  file.value = reinterpret_cast<std::intptr_t>(handle);
  FILE_ATTRIBUTE_TAG_INFO tag{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                    sizeof(tag)))
    IoFailure(file, Code::kWrite);
  if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    Fail(Status::kUnsupportedProfile, phase, Code::kPathUnsupported, ordinal);
#else
  file.value =
      open(path.data(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (file.value == kInvalidFileHandle) IoFailure(file, Code::kOpen);
#endif
  if (file.hooks.opened) file.hooks.opened(ordinal, file.hooks.state);
  (void)Metadata(file);
  return file;
}
void WriteRange(const NativeFile& file, std::uint64_t offset,
                std::span<const std::byte> bytes) {
  if (file.hooks.before_write)
    file.hooks.before_write(file.ordinal, offset, file.hooks.state);
  while (!bytes.empty()) {
#ifdef _WIN32
    Count(offset, std::numeric_limits<LONGLONG>::max(), file.stage);
    Count(bytes.size(), std::numeric_limits<DWORD>::max(), file.stage);
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    const auto handle = reinterpret_cast<HANDLE>(file.value);
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
      IoFailure(file, Code::kWrite);
    DWORD written = 0;
    if (!::WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()),
                     &written, nullptr))
      IoFailure(file, Code::kWrite);
    const auto count = static_cast<std::size_t>(written);
#else
    Count(offset, std::numeric_limits<off_t>::max(), file.stage);
    ssize_t written;
    do {
      written = pwrite(static_cast<int>(file.value), bytes.data(), bytes.size(),
                       static_cast<off_t>(offset));
    } while (written < 0 && errno == EINTR);
    if (written < 0) IoFailure(file, Code::kWrite);
    const auto count = static_cast<std::size_t>(written);
#endif
    if (!count)
      Fail(Status::kIoFailure, file.stage, Code::kWrite, file.ordinal, offset);
    offset += count;
    bytes = bytes.subspan(count);
  }
}
struct Session {
  const allocation::AllocationContext& allocation;
  const geo::ExecutionContext& context;
  const StlFileWorkPolicy& work;
  const TestOptions& testing;
  std::stop_token stop;
  void Check(Stage stage, std::uint64_t offset = 0) const {
    if (testing.checkpoint) testing.checkpoint(stage, offset, testing.state);
    if (stop.stop_requested())
      Fail(Status::kCanceled, stage, Code::kStopRequested, {}, offset);
    allocation.Checkpoint();
  }
};
enum class ByteAction { kRead, kWrite, kCopy, kCompare };
struct JobResult {
  std::optional<StlFileDiagnostic> failure;
  std::exception_ptr unexpected;
};
// Share one first-failure slot between the actual launch boundary and jobs.
// A launch records its event before RunOperationJobs joins started workers;
// failures first observed during those joins cannot displace that event.
struct LaunchFailureOrder {
  allocation::NativeWorkerBackend backend;
  std::atomic<std::size_t>& first_failure;
  std::size_t launch_failure_slot;
  // Written only by the synchronous launch wrapper on the caller thread.
  // Origin is independent of which worker/launcher wins the primary slot.
  bool launch_system_error = false;
};
std::uintptr_t LaunchWithFailureOrder(allocation::NativeWorkerRecord* record,
                                      void* opaque) {
  auto& order = *static_cast<LaunchFailureOrder*>(opaque);
  try {
    return order.backend.launch(record, order.backend.state);
  } catch (const std::system_error&) {
    order.launch_system_error = true;
    auto absent = std::numeric_limits<std::size_t>::max();
    (void)order.first_failure.compare_exchange_strong(
        absent, order.launch_failure_slot);
    throw;
  }
}
void JoinWithFailureOrder(std::uintptr_t handle, void* opaque) noexcept {
  auto& order = *static_cast<LaunchFailureOrder*>(opaque);
  order.backend.join(handle, order.backend.state);
}
struct ByteJobs {
  const Session& session;
  ByteAction action;
  Stage stage;
  std::size_t size, partition;
  std::span<const std::byte> expected;
  std::span<std::byte> destination;
  std::span<const std::byte> actual;
  NativeView path;
  NativeMetadata identity;
  std::uint64_t ordinal;
  std::span<JobResult> results;
  std::atomic<std::size_t> first_failure{
      std::numeric_limits<std::size_t>::max()};
};
void DoByteJob(void* state, std::size_t job) noexcept {
  auto& jobs = *static_cast<ByteJobs*>(state);
  auto& result = jobs.results[job];
  try {
    const auto begin = job * jobs.partition;
    const auto end = begin + std::min(jobs.partition, jobs.size - begin);
    std::optional<NativeFile> file;
    if (jobs.action == ByteAction::kWrite) {
      file.emplace(OpenRangeWriter(jobs.path, jobs.ordinal,
                                   jobs.session.testing, jobs.stage));
      const auto actual = Metadata(*file);
      if (actual.volume != jobs.identity.volume ||
          actual.identity != jobs.identity.identity)
        Fail(Status::kFileChanged, jobs.stage, Code::kChanged, jobs.ordinal,
             begin);
    }
    if (jobs.action == ByteAction::kRead) {
      file.emplace(OpenFile(jobs.path, jobs.ordinal, jobs.session.testing.files,
                            false, jobs.stage));
      if (Metadata(*file) != jobs.identity)
        Fail(Status::kFileChanged, jobs.stage, Code::kChanged, jobs.ordinal,
             begin);
    }
    const auto requested_block =
        std::min(jobs.session.work.io_block_bytes,
                 jobs.session.work.cancellation_block_bytes);
    const auto block = Host(
        (jobs.action == ByteAction::kRead || jobs.action == ByteAction::kWrite)
            ? std::min(requested_block, MaximumReadBlockBytes())
            : requested_block);
    for (std::size_t offset = begin; offset < end;) {
      jobs.session.Check(jobs.stage, offset);
      const auto count = std::min(block, end - offset);
      if (jobs.action == ByteAction::kRead)
        ReadBlock(*file, offset, jobs.destination.subspan(offset, count));
      else if (jobs.action == ByteAction::kWrite)
        WriteRange(*file, offset, jobs.expected.subspan(offset, count));
      else
        for (std::size_t index = offset; index < offset + count; ++index) {
          if (jobs.action == ByteAction::kCopy)
            jobs.destination[index] = jobs.expected[index];
          else if (jobs.expected[index] != jobs.actual[index])
            Fail(Status::kPreservationMismatch, jobs.stage, Code::kByteMismatch,
                 jobs.ordinal, index);
        }
      offset += count;
    }
    if (file) CloseChecked(*file, jobs.session.testing, jobs.stage);
    jobs.session.Check(jobs.stage, end);
  } catch (const Failure& failure) {
    result.failure = failure.diagnostic;
    auto absent = std::numeric_limits<std::size_t>::max();
    (void)jobs.first_failure.compare_exchange_strong(absent, job);
  } catch (const allocation::AdmissionFailure& failure) {
    result.failure = Admission(failure, jobs.stage);
    auto absent = std::numeric_limits<std::size_t>::max();
    (void)jobs.first_failure.compare_exchange_strong(absent, job);
  } catch (...) {
    result.unexpected = std::current_exception();
  }
}
void RunBytes(const Session& session, ByteAction action, Stage stage,
              std::span<const std::byte> expected,
              std::span<std::byte> destination, NativeView path,
              NativeMetadata identity, std::uint64_t ordinal,
              geo::MaterializationExecutionStats& statistics,
              std::span<const std::byte> actual_bytes = {},
              std::size_t scalar_width = 1) {
  const auto size = action == ByteAction::kWrite
                        ? expected.size()
                        : (action == ByteAction::kCompare ? actual_bytes.size()
                                                          : destination.size());
  if (action != ByteAction::kRead && expected.size() != size)
    Fail(Status::kPreservationMismatch, stage, Code::kByteMismatch, ordinal);
  session.Check(stage);
  if (!size) return;
  if (!scalar_width || size % scalar_width != 0)
    Fail(Status::kInvalidRequest, stage, Code::kCountLimit, ordinal);
  const auto units = size / scalar_width;
  const auto partition = std::max(
      std::size_t{1}, Host(session.work.partition_bytes) / scalar_width);
  const auto natural = units / partition + (units % partition != 0);
  const auto jobs = size >= session.work.parallel_min_bytes
                        ? std::min(natural, Host(session.work.max_jobs))
                        : std::size_t{1};
  const auto width = (units / jobs + (units % jobs != 0)) * scalar_width;
  const auto actual = size / width + (size % width != 0);
  std::vector<JobResult, allocation::Allocator<JobResult>> results{
      allocation::Allocator<JobResult>(session.allocation)};
  results.reserve(actual);
  for (std::size_t i = 0; i < actual; ++i) {
    session.Check(stage);
    results.emplace_back();
  }
  ByteJobs batch{session,     action,       stage, size,     width,   expected,
                 destination, actual_bytes, path,  identity, ordinal, results};
  statistics.planned_jobs += actual;
  auto lease = geo::TryReserveWorkers(session.context, actual);
  if (lease.Count()) {
    allocation::OperationWorkerStats workers;
    LaunchFailureOrder launch_order{session.testing.workers.backend,
                                    batch.first_failure, actual};
    auto execution = session.testing.workers;
    execution.backend = {LaunchWithFailureOrder, JoinWithFailureOrder,
                         &launch_order};
    try {
      allocation::RunOperationJobs(session.allocation, lease, actual, DoByteJob,
                                   &batch, execution, workers);
    } catch (const std::system_error& e) {
      statistics.launched_workers += workers.launched;
      statistics.joined_workers += workers.joined;
      if (!launch_order.launch_system_error) throw;
      for (const auto& result : results)
        if (result.unexpected) std::rethrow_exception(result.unexpected);
      const auto first = batch.first_failure.load();
      if (first < results.size()) throw Failure{*results[first].failure};
      Fail(Status::kIoFailure, stage, Code::kOpen, {}, {}, e.code());
    } catch (...) {
      statistics.launched_workers += workers.launched;
      statistics.joined_workers += workers.joined;
      throw;
    }
    statistics.launched_workers += workers.launched;
    statistics.joined_workers += workers.joined;
  } else
    for (std::size_t job = 0; job < actual; ++job) {
      ++statistics.caller_jobs;
      DoByteJob(&batch, job);
    }
  for (const auto& result : results)
    if (result.unexpected) std::rethrow_exception(result.unexpected);
  const auto first = batch.first_failure.load();
  if (first != std::numeric_limits<std::size_t>::max())
    throw Failure{*results[first].failure};
  session.Check(stage, size);
}
void WritePhysical(NativeView path, std::span<const std::byte> bytes,
                   std::uint64_t ordinal, NativePublication& transaction,
                   const StlFileWorkPolicy& work, const Session& s,
                   geo::MaterializationExecutionStats& stats,
                   Stage phase = Stage::kStaging) {
  s.Check(phase);
  auto& identity = transaction.MemberIdentity(Host(ordinal));
  WriteFile(path, {}, ordinal, work, s.allocation, s.testing, &identity, phase);
  if (!identity) Fail(Status::kIoFailure, phase, Code::kWrite, ordinal);
  RunBytes(s, ByteAction::kWrite, phase, bytes, {}, path, *identity, ordinal,
           stats);
  s.Check(phase);
}
Buffer ReadPhysicalPlan(NativeView path, NativePathUse use,
                        NativeSourceKeys&& keys, std::uint64_t ordinal,
                        std::uint64_t maximum, const StlFileLimits& limits,
                        const Session& session, Stage stage,
                        geo::MaterializationExecutionStats& statistics,
                        NativeMetadata* observed = nullptr,
                        NativeSourceKeys* retained = nullptr) {
  auto admission =
      OpenAdmittedNativePath(path, use, std::move(keys), session.allocation,
                             limits, session.testing, stage, ordinal);
  auto& file = admission.file;
  const auto before = admission.identity;
#ifdef _WIN32
  Count(before.size, std::numeric_limits<LONGLONG>::max(), stage);
#else
  Count(before.size, std::numeric_limits<off_t>::max(), stage);
#endif
  Count(before.size, maximum, stage);
  Count(before.size, limits.max_total_file_bytes, stage);
  const auto size = Host(before.size);
  auto check = OpenFile(admission.keys.primary, ordinal, session.testing.files,
                        false, stage);
  if (Metadata(check) != before)
    Fail(Status::kFileChanged, stage, Code::kChanged, ordinal);
  CloseChecked(check, session.testing, stage);
  Buffer bytes(session.allocation, size);
  RunBytes(session, ByteAction::kRead, stage, {}, bytes.Bytes(), path, before,
           ordinal, statistics);
  RequireEnd(file, before.size);
  if (Metadata(file) != before)
    Fail(Status::kFileChanged, stage, Code::kChanged, ordinal);
  CloseChecked(file, session.testing, stage);
  if (observed) *observed = before;
  if (retained) *retained = std::move(admission.keys);
  return bytes;
}
Buffer ReadPhysical(NativeView path, std::uint64_t ordinal,
                    std::uint64_t maximum, const StlFileLimits& limits,
                    const Session& session, Stage stage,
                    geo::MaterializationExecutionStats& statistics) {
  auto keys = PlanNativePath(path, NativePathUse::kReadback, session.allocation,
                             limits, stage);
  return ReadPhysicalPlan(path, NativePathUse::kReadback, std::move(keys),
                          ordinal, maximum, limits, session, stage, statistics);
}
void Compare(const Session& session, std::span<const std::byte> expected,
             std::span<const std::byte> actual, Stage stage,
             std::uint64_t ordinal,
             geo::MaterializationExecutionStats& statistics,
             std::size_t scalar_width = 1) {
  RunBytes(session, ByteAction::kCompare, stage, expected, {}, {}, {}, ordinal,
           statistics, actual, scalar_width);
}
// FIPS 180-4 sections 4.1.2, 5.1.1 and 6.2.2 own these SHA-256
// word dimensions, schedule distances, rotations and padding octet.
constexpr std::size_t kShaBlockBytes = 64, kShaLengthBytes = 8, kByteBits = 8;
constexpr std::size_t kShaWordBytes = 4, kShaDigestWords = 8,
                      kShaInputWords = 16, kShaCompressionRounds = 64,
                      kShaMaximumTailBlocks = 2, kShaPaddingMarkerBytes = 1;
constexpr std::size_t kShaScheduleSigma0Distance = 15,
                      kShaScheduleSigma1Distance = 2,
                      kShaScheduleAccumulatorDistance = 7;
constexpr unsigned kShaSmallSigma0RotateA = 7, kShaSmallSigma0RotateB = 18,
                   kShaSmallSigma0Shift = 3, kShaSmallSigma1RotateA = 17,
                   kShaSmallSigma1RotateB = 19, kShaSmallSigma1Shift = 10,
                   kShaLargeSigma0RotateA = 2, kShaLargeSigma0RotateB = 13,
                   kShaLargeSigma0RotateC = 22, kShaLargeSigma1RotateA = 6,
                   kShaLargeSigma1RotateB = 11, kShaLargeSigma1RotateC = 25;
constexpr std::uint32_t kByteMask = 255;
constexpr std::byte kShaPaddingMarker{0x80};
constexpr std::array<std::uint32_t, kShaDigestWords> kShaInitial{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
constexpr std::array<std::uint32_t, kShaCompressionRounds> kShaRounds{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

void Compress(std::array<std::uint32_t, kShaDigestWords>& hash,
              std::span<const std::byte, kShaBlockBytes> block) noexcept {
  std::array<std::uint32_t, kShaCompressionRounds> words{};
  for (std::size_t i = 0; i < kShaInputWords; ++i)
    for (std::size_t j = 0; j < kShaWordBytes; ++j)
      words[i] = (words[i] << kByteBits) |
                 std::to_integer<std::uint32_t>(block[i * kShaWordBytes + j]);
  for (std::size_t i = kShaInputWords; i < words.size(); ++i) {
    const auto a = words[i - kShaScheduleSigma0Distance],
               b = words[i - kShaScheduleSigma1Distance];
    words[i] =
        words[i - kShaInputWords] +
        (std::rotr(a, kShaSmallSigma0RotateA) ^
         std::rotr(a, kShaSmallSigma0RotateB) ^ (a >> kShaSmallSigma0Shift)) +
        words[i - kShaScheduleAccumulatorDistance] +
        (std::rotr(b, kShaSmallSigma1RotateA) ^
         std::rotr(b, kShaSmallSigma1RotateB) ^ (b >> kShaSmallSigma1Shift));
  }
  auto a = hash[0], b = hash[1], c = hash[2], d = hash[3];
  auto e = hash[4], f = hash[5], g = hash[6], h = hash[7];
  for (std::size_t i = 0; i < words.size(); ++i) {
    const auto first = h +
                       (std::rotr(e, kShaLargeSigma1RotateA) ^
                        std::rotr(e, kShaLargeSigma1RotateB) ^
                        std::rotr(e, kShaLargeSigma1RotateC)) +
                       ((e & f) ^ (~e & g)) + kShaRounds[i] + words[i];
    const auto second = (std::rotr(a, kShaLargeSigma0RotateA) ^
                         std::rotr(a, kShaLargeSigma0RotateB) ^
                         std::rotr(a, kShaLargeSigma0RotateC)) +
                        ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + first;
    d = c;
    c = b;
    b = a;
    a = first + second;
  }
  hash[0] += a;
  hash[1] += b;
  hash[2] += c;
  hash[3] += d;
  hash[4] += e;
  hash[5] += f;
  hash[6] += g;
  hash[7] += h;
}
DigestValue Digest(std::span<const std::byte> bytes, const Session& session,
                   Stage stage) {
  if (bytes.size() > UINT64_MAX / kByteBits)
    Fail(Status::kLimitExceeded, stage, Code::kCountLimit);
  auto hash = kShaInitial;
  std::size_t offset = 0;
  // SHA's compression chain is dependent; bounded serial blocks are genuine.
  while (bytes.size() - offset >= kShaBlockBytes) {
    session.Check(stage, offset);
    Compress(hash, std::span<const std::byte, kShaBlockBytes>(
                       bytes.subspan(offset, kShaBlockBytes)));
    offset += kShaBlockBytes;
  }
  std::array<std::byte, kShaBlockBytes * kShaMaximumTailBlocks> tail{};
  const auto rest = bytes.size() - offset;
  for (std::size_t i = 0; i < rest; ++i) tail[i] = bytes[offset + i];
  tail[rest] = kShaPaddingMarker;
  const auto length =
      rest + kShaLengthBytes + kShaPaddingMarkerBytes > kShaBlockBytes
          ? kShaBlockBytes * kShaMaximumTailBlocks
          : kShaBlockBytes;
  const auto bits = static_cast<std::uint64_t>(bytes.size()) * kByteBits;
  for (std::size_t i = 0; i < kShaLengthBytes; ++i)
    tail[length - 1 - i] =
        static_cast<std::byte>((bits >> (i * kByteBits)) & kByteMask);
  for (std::size_t i = 0; i < length; i += kShaBlockBytes) {
    session.Check(stage, offset + i);
    Compress(hash, std::span<const std::byte, kShaBlockBytes>(tail.data() + i,
                                                              kShaBlockBytes));
  }
  DigestValue result{};
  for (std::size_t i = 0; i < hash.size(); ++i)
    for (std::size_t j = 0; j < kShaWordBytes; ++j)
      result[i * kShaWordBytes + j] = static_cast<std::byte>(
          (hash[i] >> ((kShaWordBytes - 1 - j) * kByteBits)) & kByteMask);
  return result;
}

StlFileCaptureFacts CaptureFacts(const StlFileAsset& a) noexcept {
  return {a.EntryPath(), static_cast<std::uint64_t>(a.EntryBytes().size()),
          a.Sha256()};
}
StlFileCaptureFacts CaptureFacts(const StlCapturedFile& a) noexcept {
  return {a.EntryPath(), static_cast<std::uint64_t>(a.EntryBytes().size()),
          a.Sha256()};
}
StlFileReadFacts ReadFacts(const StlDocument& d) noexcept {
  const auto v = d.Mesh().View();
  StlFileReadFacts f;
  f.encoding = d.Encoding();
  f.facets = v.face_offsets.size() - 1;
  f.vertices = v.positions.size() / kComponents;
  f.corners = v.corner_vertices.size();
  if (d.Encoding() == StlEncoding::kAscii)
    f.ascii_name = d.AsciiName();
  else {
    std::array<std::byte, kStlFileBinaryHeaderBytes> h{};
    for (std::size_t i = 0; i < h.size(); ++i) h[i] = d.BinaryHeader()[i];
    f.binary_header = h;
  }
  return f;
}
void Succeeded(StlFileObservation& o, StlFileFacts f = {}) noexcept {
  o.state = StlFileObservationState::kSucceeded;
  o.diagnostic.reset();
  o.facts = f;
}
void NotApplicable(StlFileObservation& o) noexcept {
  o.state = StlFileObservationState::kNotApplicable;
}
void ReadOnly(StlFileObservations& o) noexcept {
  for (auto* p :
       {&o.write, &o.staged_reread, &o.fresh_read, &o.verification, &o.provider,
        &o.receipt_verification, &o.inventory, &o.publication})
    NotApplicable(*p);
}
StlFileObservation* Observation(StlFileObservations& o, Stage p) noexcept {
  switch (p) {
    case Stage::kCapture:
      return &o.capture;
    case Stage::kRead:
      return &o.read;
    case Stage::kWrite:
      return &o.write;
    case Stage::kStagedReread:
      return &o.staged_reread;
    case Stage::kFreshRead:
      return &o.fresh_read;
    case Stage::kVerification:
      return &o.verification;
    case Stage::kProvider:
      return &o.provider;
    case Stage::kReceiptVerification:
      return &o.receipt_verification;
    case Stage::kInventory:
      return &o.inventory;
    case Stage::kPublication:
      return &o.publication;
    default:
      return nullptr;
  }
}
void Failed(StlFileResult& r, const StlFileDiagnostic& d) noexcept {
  r.status = d.status;
  r.phase = d.phase;
  r.diagnostic = d;
  if (auto* o = Observation(r.observations, d.phase)) {
    o->state = d.status == Status::kCanceled
                   ? StlFileObservationState::kCanceled
                   : StlFileObservationState::kFailed;
    o->diagnostic = d;
    o->facts = std::monostate{};
  }
}
void Complete(StlFileResult& r) noexcept {
  r.status = Status::kSuccess;
  r.phase = Stage::kComplete;
  r.diagnostic.status = Status::kSuccess;
  r.diagnostic.phase = Stage::kComplete;
}
void RequireStl(StlStatus status, const StlDiagnostic& actual, Stage phase) {
  if (status == StlStatus::kSuccess) return;
  StlFileDiagnostic d;
  d.status =
      status == StlStatus::kCanceled ? Status::kCanceled : Status::kStlFailure;
  d.phase = phase;
  d.code = Code::kDelegatedStl;
  d.stl_status = status;
  d.stl_diagnostic = actual;
  throw Failure{d};
}
std::span<const std::byte> TextBytes(std::string_view text) noexcept {
  return std::as_bytes(std::span<const char>(text.data(), text.size()));
}
void Utf8(std::span<const std::byte> text,
          const allocation::AllocationContext* a, Stage phase, Code code,
          bool controls) {
  constexpr std::uint32_t kUnicodeMax = 0x10ffff, kSurrogateBegin = 0xd800,
                          kSurrogateEnd = 0xdfff, kControlLimit = 0x20,
                          kDeleteBegin = 0x7f, kDeleteEnd = 0xa0;
  constexpr unsigned char kTailMask = 0xc0, kTailTag = 0x80,
                          kPayloadMask = 0x3f;
  constexpr unsigned kTailBits = 6;
  for (std::size_t i = 0; i < text.size();) {
    if (a) a->Checkpoint();
    const auto lead = std::to_integer<unsigned char>(text[i++]);
    std::uint32_t value = lead, minimum = 0;
    if (lead >= kAsciiEnd) {
      const Utf8Lead* selected = nullptr;
      for (const auto& l : kUtf8Leads)
        if (lead >= l.first && lead <= l.last) {
          selected = &l;
          break;
        }
      if (!selected) Fail(Status::kInvalidRequest, phase, code);
      const auto tails = selected->tails;
      minimum = selected->minimum;
      value = lead & selected->mask;
      if (tails > text.size() - i) Fail(Status::kInvalidRequest, phase, code);
      for (std::size_t t = 0; t < tails; ++t) {
        const auto b = std::to_integer<unsigned char>(text[i++]);
        if ((b & kTailMask) != kTailTag)
          Fail(Status::kInvalidRequest, phase, code);
        value = (value << kTailBits) | (b & kPayloadMask);
      }
    }
    if (value < minimum || value > kUnicodeMax ||
        (value >= kSurrogateBegin && value <= kSurrogateEnd) ||
        (controls && (value < kControlLimit ||
                      (value >= kDeleteBegin && value < kDeleteEnd))))
      Fail(Status::kInvalidRequest, phase, code);
  }
}
void AsciiName(std::string_view name, std::uint64_t cap) {
  Count(name.size(), cap, Stage::kConfiguration);
  for (unsigned char c : name) {
    if (c < kControlEnd || c >= kDelete)
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kMetadata);
  }
}
StlWriteOptions Options(const StlFileAsset& source,
                        const StlFileRewriteOptions& o,
                        const StlFileRewriteLimits& l) {
  ValidateEncoding(o.encoding);
  if (!o.acknowledge_corner_expansion ||
      (o.metadata_mode != StlFileMetadataMode::kRetain &&
       o.metadata_mode != StlFileMetadataMode::kReplace))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration, Code::kMetadata);
  const auto& d = source.Document();
  if (d.Encoding() == StlEncoding::kAscii)
    AsciiName(d.AsciiName(), l.write.max_name_bytes);
  else if (d.BinaryHeader().size() != kStlFileBinaryHeaderBytes)
    Fail(Status::kInvalidRequest, Stage::kConfiguration, Code::kMetadata);
  StlWriteOptions r;
  r.encoding = o.encoding;
  r.face_normal_attribute = kImportedFacetNormalAttribute;
  r.acknowledge_corner_expansion = true;
  r.source_document = &d;
  if (o.metadata_mode == StlFileMetadataMode::kRetain) {
    if (o.encoding != d.Encoding() || !o.ascii_name.empty() ||
        !o.binary_header.empty())
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kMetadata);
    r.ascii_name = d.AsciiName();
    r.binary_header = d.BinaryHeader();
  } else {
    r.ascii_name = o.ascii_name;
    r.binary_header = o.binary_header;
  }
  if (o.encoding == StlEncoding::kAscii) {
    if (!r.binary_header.empty())
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kMetadata);
    AsciiName(r.ascii_name,
              std::min(l.write.max_name_bytes, l.fresh_read.max_name_bytes));
  } else if (r.binary_header.size() != kStlFileBinaryHeaderBytes ||
             !r.ascii_name.empty())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration, Code::kMetadata);
  Count(o.unit_label.size(), l.files.max_unit_label_bytes,
        Stage::kConfiguration);
  if ((o.unit_kind == StlFileUnitKind::kUnknown && !o.unit_label.empty()) ||
      (o.unit_kind == StlFileUnitKind::kLabel && o.unit_label.empty()) ||
      (o.unit_kind != StlFileUnitKind::kUnknown &&
       o.unit_kind != StlFileUnitKind::kLabel))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kUnitDeclaration);
  Utf8(TextBytes(o.unit_label), nullptr, Stage::kConfiguration,
       Code::kUnitDeclaration, true);
  if (o.report && !o.report->provide)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration, Code::kProvider);
  return r;
}

bool EqualText(std::string_view a, std::string_view b, const Session& s,
               Stage phase) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    s.Check(phase, i);
    if (a[i] != b[i]) return false;
  }
  return true;
}
bool EqualBytes(std::span<const std::byte> a, std::span<const std::byte> b,
                const Session& s, Stage phase) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    s.Check(phase, i);
    if (a[i] != b[i]) return false;
  }
  return true;
}
struct Shape {
  geo::MeshReadView mesh;
  std::span<const double> normals;
  std::size_t facets = 0;
};
Shape CheckShape(const StlDocument& d, const Session& s, Stage phase,
                 const StlReadLimits& l) {
  s.Check(phase);
  const auto m = d.Mesh().View();
  if (!d.Inspection().IsStorageValid() || !d.Inspection().Issues().empty() ||
      m.face_offsets.empty() ||
      m.attributes.size() != kImportedNormalAttributeCount)
    Fail(Status::kPreservationMismatch, phase, Code::kOrderMismatch);
  const auto facets = m.face_offsets.size() - 1;
  const auto corners = Host(Product(facets, kComponents));
  if (m.positions.size() != Product(corners, kComponents) ||
      m.corner_vertices.size() != corners)
    Fail(Status::kPreservationMismatch, phase, Code::kOrderMismatch);
  Count(facets, std::min(l.max_facets, l.materialization.max_faces), phase);
  Count(corners, l.materialization.max_vertices, phase);
  Count(corners, l.materialization.max_corners, phase);
  Count(m.face_offsets.size(), l.materialization.max_face_offsets, phase);
  Count(m.attributes.size(), l.materialization.max_attributes, phase);
  Count(corners, l.materialization.max_attribute_scalars, phase);
  const auto& n = m.attributes[kImportedFacetNormalAttribute];
  const auto* values = std::get_if<std::span<const double>>(&n.values);
  if (n.domain != geo::AttributeDomain::face || n.components != kComponents ||
      n.set_index || n.offsets || n.present || !n.metadata.empty() || !values ||
      values->size() != corners || !EqualText(n.name, kNormalName, s, phase) ||
      !EqualText(n.semantic, kNormalSemantic, s, phase))
    Fail(Status::kPreservationMismatch, phase, Code::kNormalMismatch);
  Count(Add(n.name.size(), n.semantic.size()),
        l.materialization.max_descriptor_bytes, phase);
  return {m, *values, facets};
}
constexpr std::uint64_t kDoubleExponentMask = 0x7ff0000000000000ULL;
bool FiniteBits(std::uint64_t w) noexcept {
  return (w & kDoubleExponentMask) != kDoubleExponentMask;
}
struct ValueJobs {
  const Session& session;
  Stage phase;
  Shape source, fresh;
  std::span<const geo::index_t> map;
  bool check_map;
  std::size_t partition;
  std::span<JobResult> results;
  std::atomic<std::size_t> first_failure{
      std::numeric_limits<std::size_t>::max()};
};
void DoValueJob(void* opaque, std::size_t job) noexcept {
  auto& b = *static_cast<ValueJobs*>(opaque);
  auto& r = b.results[job];
  try {
    const auto begin = job * b.partition;
    const auto end = begin + std::min(b.partition, b.source.facets - begin);
    for (std::size_t facet = begin; facet < end; ++facet) {
      b.session.Check(b.phase, facet);
      const auto first = facet * kComponents;
      for (const auto* m : {&b.source.mesh, &b.fresh.mesh})
        if (m->face_offsets[facet] != first ||
            m->face_offsets[facet + 1] != first + kComponents)
          Fail(Status::kPreservationMismatch, b.phase, Code::kOrderMismatch);
      for (std::size_t c = 0; c < kComponents; ++c) {
        const auto corner = first + c;
        if (b.source.mesh.corner_vertices[corner] != corner ||
            b.fresh.mesh.corner_vertices[corner] != corner ||
            (b.check_map &&
             b.map[corner] != b.source.mesh.corner_vertices[corner]))
          Fail(Status::kPreservationMismatch, b.phase, Code::kOrderMismatch);
        for (std::size_t axis = 0; axis < kComponents; ++axis) {
          const auto index = corner * kComponents + axis;
          const auto a = std::bit_cast<std::uint64_t>(
                         b.source.mesh.positions[index]),
                     v = std::bit_cast<std::uint64_t>(
                         b.fresh.mesh.positions[index]);
          if (!FiniteBits(a) || !FiniteBits(v) || a != v)
            Fail(Status::kPreservationMismatch, b.phase, Code::kValueMismatch);
        }
        const auto a = std::bit_cast<std::uint64_t>(b.source.normals[corner]),
                   v = std::bit_cast<std::uint64_t>(b.fresh.normals[corner]);
        if (!FiniteBits(a) || !FiniteBits(v) || a != v)
          Fail(Status::kPreservationMismatch, b.phase, Code::kNormalMismatch);
      }
    }
    b.session.Check(b.phase, end);
  } catch (const Failure& e) {
    r.failure = e.diagnostic;
    auto absent = std::numeric_limits<std::size_t>::max();
    (void)b.first_failure.compare_exchange_strong(absent, job);
  } catch (const allocation::AdmissionFailure& e) {
    r.failure = Admission(e, b.phase);
    auto absent = std::numeric_limits<std::size_t>::max();
    (void)b.first_failure.compare_exchange_strong(absent, job);
  } catch (...) {
    r.unexpected = std::current_exception();
  }
}
void Values(const Shape& a, const Shape& b, std::span<const geo::index_t> map,
            bool check_map, const Session& s, Stage phase,
            geo::MaterializationExecutionStats& stats) {
  if (a.facets != b.facets ||
      (check_map && map.size() != a.mesh.corner_vertices.size()) ||
      a.mesh.face_offsets.back() != a.facets * kComponents ||
      b.mesh.face_offsets.back() != b.facets * kComponents)
    Fail(Status::kPreservationMismatch, phase, Code::kOrderMismatch);
  s.Check(phase);
  if (!a.facets) return;
  constexpr std::size_t kFacetValueBytes =
      (kComponents * kComponents + kComponents) * sizeof(double);
  const auto partition =
      std::max(std::size_t{1}, Host(s.work.partition_bytes) / kFacetValueBytes);
  const auto natural = a.facets / partition + (a.facets % partition != 0);
  const auto jobs =
      Product(a.facets, kFacetValueBytes) >= s.work.parallel_min_bytes
          ? std::min(natural, Host(s.work.max_jobs))
          : std::size_t{1};
  const auto width = a.facets / jobs + (a.facets % jobs != 0);
  const auto actual = a.facets / width + (a.facets % width != 0);
  std::vector<JobResult, allocation::Allocator<JobResult>> results{
      allocation::Allocator<JobResult>(s.allocation)};
  results.reserve(actual);
  for (std::size_t j = 0; j < actual; ++j) {
    s.Check(phase);
    results.emplace_back();
  }
  ValueJobs batch{s, phase, a, b, map, check_map, width, results};
  stats.planned_jobs += actual;
  auto lease = geo::TryReserveWorkers(s.context, actual);
  if (lease.Count()) {
    allocation::OperationWorkerStats workers;
    LaunchFailureOrder launch_order{s.testing.workers.backend,
                                    batch.first_failure, actual};
    auto execution = s.testing.workers;
    execution.backend = {LaunchWithFailureOrder, JoinWithFailureOrder,
                         &launch_order};
    try {
      allocation::RunOperationJobs(s.allocation, lease, actual, DoValueJob,
                                   &batch, execution, workers);
    } catch (const std::system_error& e) {
      stats.launched_workers += workers.launched;
      stats.joined_workers += workers.joined;
      if (!launch_order.launch_system_error) throw;
      for (const auto& result : results)
        if (result.unexpected) std::rethrow_exception(result.unexpected);
      const auto first = batch.first_failure.load();
      if (first < results.size()) throw Failure{*results[first].failure};
      Fail(Status::kIoFailure, phase, Code::kOpen, {}, {}, e.code());
    } catch (...) {
      stats.launched_workers += workers.launched;
      stats.joined_workers += workers.joined;
      throw;
    }
    stats.launched_workers += workers.launched;
    stats.joined_workers += workers.joined;
  } else
    for (std::size_t j = 0; j < actual; ++j) {
      ++stats.caller_jobs;
      DoValueJob(&batch, j);
    }
  for (const auto& r : results)
    if (r.unexpected) std::rethrow_exception(r.unexpected);
  const auto first = batch.first_failure.load();
  if (first != std::numeric_limits<std::size_t>::max())
    throw Failure{*results[first].failure};
  s.Check(phase, a.facets);
}
void Verify(const StlFileAsset& source, const StlOutput& output,
            const StlDocument& fresh, const StlWriteOptions& o,
            const StlReadLimits& l, const Session& s,
            geo::MaterializationExecutionStats& stats) {
  const auto phase = Stage::kVerification;
  const auto first = CheckShape(source.Document(), s, phase, l),
             second = CheckShape(fresh, s, phase, l);
  Values(first, second, output.CornerSourceVertices(), true, s, phase, stats);
  const auto& facts = output.Facts();
  const auto& original = source.Document();
  const bool same =
      original.Encoding() == o.encoding &&
      (o.encoding == StlEncoding::kAscii
           ? EqualText(original.AsciiName(), o.ascii_name, s, phase)
           : EqualBytes(original.BinaryHeader(), o.binary_header, s, phase));
  const auto policy = same ? StlMetadataPolicy::kRetainedSourceMetadata
                           : StlMetadataPolicy::kReplacedSourceMetadata;
  if (facts.encoding != o.encoding ||
      facts.source_encoding != original.Encoding() ||
      facts.face_normal_attribute != kImportedFacetNormalAttribute ||
      !facts.corner_expansion_acknowledged ||
      facts.source_vertex_count != first.mesh.positions.size() / kComponents ||
      facts.source_face_count != first.facets ||
      facts.metadata_policy != policy)
    Fail(Status::kPreservationMismatch, phase, Code::kOutputFacts);
  if (fresh.Encoding() != o.encoding ||
      !EqualText(fresh.AsciiName(), o.ascii_name, s, phase) ||
      !EqualBytes(fresh.BinaryHeader(), o.binary_header, s, phase))
    Fail(Status::kPreservationMismatch, phase, Code::kMetadataMismatch);
}

namespace receipt_key {
constexpr std::string_view kSchema = "schema", kProfile = "profile",
                           kSource = "source", kPayload = "payload",
                           kEncoding = "encoding", kEntry = "entry",
                           kBytes = "bytes", kSha256 = "sha256",
                           kCounts = "counts", kFacets = "facets",
                           kVertices = "vertices", kCorners = "corners",
                           kMetadata = "metadata", kRequested = "requested",
                           kActual = "actual", kAsciiName = "ascii_name",
                           kBinaryHeaderHex = "binary_header_hex",
                           kUnits = "units", kKind = "kind", kLabel = "label",
                           kRescaled = "rescaled", kFidelity = "fidelity",
                           kPositionBits = "position_bits",
                           kRawNormalBits = "raw_normal_bits",
                           kFacetOrder = "facet_order",
                           kCornerOrder = "corner_order",
                           kSoupIdentity = "soup_identity",
                           kExactBytes = "exact_bytes",
                           kVerification = "verification",
                           kCallerReport = "caller_report";
}
namespace receipt_tag {
constexpr std::string_view kSchema = "meshvale.stl-publication/1",
                           kProfile = "codec-preserving", kAscii = "ascii",
                           kBinary = "binary", kRetain = "retain",
                           kReplace = "replace",
                           kRetained = "retained_source_metadata",
                           kReplaced = "replaced_source_metadata",
                           kUnknown = "unknown", kLabel = "label",
                           kVerification =
                               "staged-reread-and-fresh-codec-verified";
}
class ReceiptJson {
 public:
  ReceiptJson(const Session& s, std::uint64_t cap, std::byte* out)
      : session_(s), cap_(cap), out_(out) {}
  void Raw(std::string_view text) {
    for (char c : text) Put(c);
  }
  void Text(std::string_view text) {
    constexpr unsigned char kJsonControl = 0x20;
    Put('"');
    for (unsigned char c : text) {
      if (c == '"' || c == '\\') {
        Put('\\');
        Put(static_cast<char>(c));
      } else if (c < kJsonControl) {
        Raw("\\u00");
        Put(kHex[c >> kNibbleBits]);
        Put(kHex[c & kNibbleMask]);
      } else
        Put(static_cast<char>(c));
    }
    Put('"');
  }
  void Key(std::string_view key) {
    Text(key);
    Put(':');
  }
  void Number(std::uint64_t n) {
    constexpr std::size_t kDecimalDigits = 20;
    std::array<char, kDecimalDigits> digits{};
    const auto end =
        std::to_chars(digits.data(), digits.data() + digits.size(), n);
    Raw({digits.data(), static_cast<std::size_t>(end.ptr - digits.data())});
  }
  void Hex(std::span<const std::byte> bytes) {
    Put('"');
    for (auto b : bytes) {
      const auto c = std::to_integer<unsigned char>(b);
      Put(kHex[c >> kNibbleBits]);
      Put(kHex[c & kNibbleMask]);
    }
    Put('"');
  }
  std::size_t Size() const noexcept { return size_; }

 private:
  void Put(char c) {
    session_.Check(Stage::kReceiptVerification, size_);
    if (size_ >= cap_)
      Fail(Status::kLimitExceeded, Stage::kReceiptVerification,
           Code::kCountLimit);
    if (out_)
      out_[size_] = static_cast<std::byte>(static_cast<unsigned char>(c));
    ++size_;
  }
  static constexpr std::string_view kHex = "0123456789abcdef";
  static constexpr unsigned kNibbleBits = 4, kNibbleMask = 0xf;
  const Session& session_;
  std::uint64_t cap_;
  std::byte* out_;
  std::size_t size_ = 0;
};
std::string_view EncodingName(StlEncoding e) noexcept {
  return e == StlEncoding::kAscii ? receipt_tag::kAscii : receipt_tag::kBinary;
}
void EmitReceipt(ReceiptJson& j, const StlVerifiedRewrite& v,
                 const StlFileRewriteOptions& o,
                 std::span<const std::byte> report) {
  using namespace receipt_key;
  j.Raw("{");
  j.Key(kSchema);
  j.Text(receipt_tag::kSchema);
  j.Raw(",");
  j.Key(kProfile);
  j.Text(receipt_tag::kProfile);
  const auto member =
      [&](std::string_view key, StlEncoding e, std::string_view entry,
          std::span<const std::byte> bytes, const DigestValue& digest) {
        j.Raw(",");
        j.Key(key);
        j.Raw("{");
        j.Key(kEncoding);
        j.Text(EncodingName(e));
        j.Raw(",");
        j.Key(kEntry);
        j.Text(entry);
        j.Raw(",");
        j.Key(kBytes);
        j.Number(bytes.size());
        j.Raw(",");
        j.Key(kSha256);
        j.Hex(digest);
        j.Raw("}");
      };
  member(kSource, v.Source().Document().Encoding(), v.Source().EntryPath(),
         v.Source().EntryBytes(), v.Source().Sha256());
  member(kPayload, v.Document().Encoding(), v.EntryPath(), v.EntryBytes(),
         v.Sha256());
  const auto m = v.Document().Mesh().View();
  j.Raw(",");
  j.Key(kCounts);
  j.Raw("{");
  j.Key(kFacets);
  j.Number(m.face_offsets.size() - 1);
  j.Raw(",");
  j.Key(kVertices);
  j.Number(m.positions.size() / kComponents);
  j.Raw(",");
  j.Key(kCorners);
  j.Number(m.corner_vertices.size());
  j.Raw("}");
  j.Raw(",");
  j.Key(kMetadata);
  j.Raw("{");
  j.Key(kRequested);
  j.Text(o.metadata_mode == StlFileMetadataMode::kRetain
             ? receipt_tag::kRetain
             : receipt_tag::kReplace);
  j.Raw(",");
  j.Key(kActual);
  j.Text(v.Output().Facts().metadata_policy ==
                 StlMetadataPolicy::kRetainedSourceMetadata
             ? receipt_tag::kRetained
             : receipt_tag::kReplaced);
  j.Raw(",");
  j.Key(kAsciiName);
  if (v.Document().Encoding() == StlEncoding::kAscii)
    j.Text(v.Document().AsciiName());
  else
    j.Raw("null");
  j.Raw(",");
  j.Key(kBinaryHeaderHex);
  if (v.Document().Encoding() == StlEncoding::kBinary)
    j.Hex(v.Document().BinaryHeader());
  else
    j.Raw("null");
  j.Raw("}");
  j.Raw(",");
  j.Key(kUnits);
  j.Raw("{");
  j.Key(kKind);
  j.Text(o.unit_kind == StlFileUnitKind::kUnknown ? receipt_tag::kUnknown
                                                  : receipt_tag::kLabel);
  j.Raw(",");
  j.Key(kLabel);
  if (o.unit_kind == StlFileUnitKind::kLabel)
    j.Text(o.unit_label);
  else
    j.Raw("null");
  j.Raw(",");
  j.Key(kRescaled);
  j.Raw("false}");
  j.Raw(",");
  j.Key(kFidelity);
  j.Raw("{");
  bool comma = false;
  for (auto key : {kPositionBits, kRawNormalBits, kFacetOrder, kCornerOrder,
                   kSoupIdentity, kExactBytes}) {
    if (comma) j.Raw(",");
    comma = true;
    j.Key(key);
    j.Raw("true");
  }
  j.Raw("},");
  j.Key(kVerification);
  j.Text(receipt_tag::kVerification);
  j.Raw(",");
  j.Key(kCallerReport);
  if (o.report)
    j.Text(report.empty()
               ? std::string_view{}
               : std::string_view(reinterpret_cast<const char*>(report.data()),
                                  report.size()));
  else
    j.Raw("null");
  j.Raw("}");
}
Buffer Receipt(const StlVerifiedRewrite& v, const StlFileRewriteOptions& o,
               std::span<const std::byte> report, const StlFileLimits& l,
               const Session& s) {
  ReceiptJson count(s, l.max_receipt_bytes, nullptr);
  EmitReceipt(count, v, o, report);
  Count(Add(v.EntryBytes().size(), count.Size()), l.max_total_file_bytes,
        Stage::kReceiptVerification);
  Buffer bytes(s.allocation, count.Size());
  ReceiptJson out(s, l.max_receipt_bytes, bytes.data);
  EmitReceipt(out, v, o, report);
  return bytes;
}

void UpdateRequests(StlFileReadResult& r) noexcept {
  r.requested_bytes.file_capture =
      r.asset ? r.asset->NewRequestedBytes()
              : (r.captured ? r.captured->NewRequestedBytes() : 0);
  if (r.asset) r.requested_bytes.read = r.asset->Document().NewRequestedBytes();
}
void UpdateRequests(StlFileRewriteResult& r) noexcept {
  r.requested_bytes.file_capture = r.source.NewRequestedBytes();
  r.requested_bytes.read = r.source.Document().NewRequestedBytes();
  if (r.output) r.requested_bytes.write = r.output->NewRequestedBytes();
  if (r.fresh_document)
    r.requested_bytes.fresh_read = r.fresh_document->NewRequestedBytes();
  r.requested_bytes.file_publication =
      r.bundle ? r.bundle->NewRequestedBytes()
               : (r.verified ? r.verified->NewRequestedBytes() : 0);
}
}  // namespace
StlFileCaptureResult CaptureNative(NativeView input,
                                   const StlFileLimits& limits,
                                   const StlFileWorkPolicy& work,
                                   const geo::ExecutionContext& context,
                                   std::stop_token stop,
                                   const TestOptions& testing) {
  StlFileCaptureResult result;
  ReadOnly(result.observations);
  NotApplicable(result.observations.read);
  auto phase = Stage::kConfiguration;
  try {
    Validate(limits, work);
    Count(input.size(), limits.max_native_path_code_units - 1, phase);
    auto a =
        allocation::AllocationContext(context, stop, testing.allocation_backend)
            .WithRequestLimit(Host(limits.max_file_new_requested_bytes));
    Session s{a, context, work, testing, stop};
    s.Check(phase);
    phase = Stage::kPathPlanning;
    auto token = std::allocate_shared<Isolation>(
        allocation::Allocator<Isolation>(a), a, context,
        limits.max_file_new_requested_bytes, testing.allocation_backend);
    auto path = AbsolutePath(input, a, limits);
    token->name = EntryName(path, a, limits);
    if (EqualFold(token->name, kReceiptName, &a))
      Fail(Status::kInvalidRequest, phase, Code::kNameCollision);
    auto keys =
        PlanNativePath(path, NativePathUse::kRetainedSource, a, limits, phase);
    phase = Stage::kCapture;
    s.Check(phase);
    auto bytes = ReadPhysicalPlan(
        path, NativePathUse::kRetainedSource, std::move(keys), kEntryIndex,
        limits.max_entry_bytes, limits, s, phase, result.execution.capture,
        &token->identity, &token->keys);
    token->digest = Digest(bytes.Bytes(), s, phase);
    auto state = std::allocate_shared<CaptureState>(
        allocation::Allocator<CaptureState>(a), a, token, std::move(bytes));
    s.Check(phase);
    result.captured.emplace(Access::Capture(std::move(state)));
    Succeeded(result.observations.capture, CaptureFacts(*result.captured));
    Complete(result);
  } catch (const Failure& e) {
    Failed(result, e.diagnostic);
  } catch (const allocation::AdmissionFailure& e) {
    Failed(result, Admission(e, phase));
  }
  if (result.captured)
    result.requested_bytes.file_capture = result.captured->NewRequestedBytes();
  return result;
}
StlFileReadResult Decode(const StlCapturedFile& captured, StlEncoding encoding,
                         const StlReadLimits& limits, const StlWorkPolicy& work,
                         std::stop_token stop) {
  StlFileReadResult result;
  ReadOnly(result.observations);
  result.captured = captured;
  Succeeded(result.observations.capture, CaptureFacts(captured));
  auto phase = Stage::kConfiguration;
  try {
    ValidateEncoding(encoding);
    Validate(limits, work);
    phase = Stage::kRead;
    const auto& c = Access::State(captured);
    const auto active = c.allocation.OperationActiveBytes();
    if (active >= c.token->max_requests)
      Fail(Status::kLimitExceeded, Stage::kRead, Code::kRequestBudget);
    // New decode allocations observe only this operation's token. Existing
    // capture/isolator occupancy stays live in the same root ledger.
    auto a = allocation::AllocationContext(c.token->context, stop,
                                           c.token->allocation_backend)
                 .WithRequestLimit(Host(c.token->max_requests - active));
    if (stop.stop_requested())
      Fail(Status::kCanceled, Stage::kRead, Code::kStopRequested);
    auto state = std::allocate_shared<AssetState>(
        allocation::Allocator<AssetState>(a), a, c.token);
    phase = Stage::kRead;
    auto decoded = ReadStl(captured.EntryBytes(), encoding, limits, work,
                           c.token->context, stop);
    result.execution.read = decoded.execution;
    RequireStl(decoded.status, decoded.diagnostic, phase);
    if (!decoded.document)
      Fail(Status::kStlFailure, phase, Code::kDelegatedStl);
    state->document.emplace(*decoded.document);
    result.asset.emplace(Access::Asset(std::move(state)));
    Succeeded(result.observations.read, ReadFacts(result.asset->Document()));
    result.captured.reset();
    Complete(result);
  } catch (const Failure& e) {
    Failed(result, e.diagnostic);
  } catch (const allocation::AdmissionFailure& e) {
    Failed(result, Admission(e, phase));
  }
  UpdateRequests(result);
  return result;
}
StlFileReadResult ReadNative(NativeView input, StlEncoding encoding,
                             const StlFileReadLimits& limits,
                             const StlFileReadWorkPolicy& work,
                             const geo::ExecutionContext& context,
                             std::stop_token stop, const TestOptions& testing) {
  StlFileReadResult result;
  ReadOnly(result.observations);
  try {
    Validate(limits.files, work.files);
    ValidateEncoding(encoding);
    Validate(limits.read, work.read);
    (void)Add(limits.files.max_file_new_requested_bytes,
              limits.read.max_total_new_requested_bytes);
  } catch (const Failure& e) {
    Failed(result, e.diagnostic);
    return result;
  }
  auto capture =
      CaptureNative(input, limits.files, work.files, context, stop, testing);
  static_cast<StlFileResult&>(result) =
      static_cast<const StlFileResult&>(capture);
  result.observations.read.state = StlFileObservationState::kNotAttempted;
  if (!capture.captured) return result;
  result = Decode(*capture.captured, encoding, limits.read, work.read, stop);
  result.execution.capture = capture.execution.capture;
  capture.captured.reset();
  UpdateRequests(result);
  return result;
}
StlFileRewriteResult RewriteNative(const StlFileAsset& source,
                                   NativeView destination,
                                   const StlFileRewriteOptions& options,
                                   const StlFileRewriteLimits& limits,
                                   const StlFileRewriteWorkPolicy& work,
                                   std::stop_token stop,
                                   const TestOptions& testing) {
  StlFileRewriteResult result(source);
  Succeeded(result.observations.capture, CaptureFacts(source));
  Succeeded(result.observations.read, ReadFacts(source.Document()));
  if (!options.report) NotApplicable(result.observations.provider);
  auto phase = Stage::kConfiguration;
  std::optional<NativePublication> transaction;
  const auto cleanup = [&]() noexcept {
    if (transaction) {
      std::error_code error;
      result.cleanup = transaction->Cleanup(error);
      if (error) result.cleanup_error = error;
    }
  };
  try {
    Validate(limits.files, work.files);
    Validate(limits.write, work.write);
    Validate(limits.fresh_read, work.fresh_read);
    Positive(limits.max_total_new_requested_bytes);
    if (Add(Add(limits.files.max_file_new_requested_bytes,
                limits.write.max_total_new_requested_bytes),
            limits.fresh_read.max_total_new_requested_bytes) >
        limits.max_total_new_requested_bytes)
      Fail(Status::kInvalidConfiguration, phase, Code::kConfigurationExtent);
    Count(destination.size(), limits.files.max_native_path_code_units - 1,
          phase);
    // Reject every option before a stop checkpoint or tracked allocation.
    const auto write_options = Options(source, options, limits);
    const auto context = source.Document().Mesh().ShareExecutionContext();
    auto a =
        allocation::AllocationContext(context, stop, testing.allocation_backend)
            .WithRequestLimit(Host(limits.files.max_file_new_requested_bytes));
    Session s{a, context, work.files, testing, stop};
    s.Check(phase);
    Count(source.EntryBytes().size(), limits.files.max_entry_bytes, phase);
    PortableName(source.EntryPath(), limits.files.max_entry_name_bytes, a);
    if (EqualFold(source.EntryPath(), kReceiptName, &a))
      Fail(Status::kInvalidRequest, phase, Code::kNameCollision);
    const auto shape =
        CheckShape(source.Document(), s, phase, limits.fresh_read);
    Count(shape.facets, limits.write.max_facets, phase);
    Values(shape, shape, {}, false, s, phase,
           result.execution.value_verification);
    phase = Stage::kPathPlanning;
    transaction.emplace(a, limits.files, testing);
    transaction->Plan(destination, source.EntryPath(), kReceiptName);
    const auto& token = *Access::State(source).token;
    if (SourceOverlaps(transaction->Destination(), token.keys, a))
      Fail(Status::kInvalidRequest, phase, Code::kSourceDestinationOverlap);
    phase = Stage::kWrite;
    s.Check(phase);
    auto written = WriteStl(source.Document().Mesh(), write_options,
                            limits.write, work.write, stop);
    result.execution.write = written.execution;
    RequireStl(written.status, written.diagnostic, phase);
    if (!written.output) Fail(Status::kStlFailure, phase, Code::kDelegatedStl);
    result.output = written.output;
    Count(result.output->Bytes().size(), limits.files.max_entry_bytes, phase);
    Count(result.output->Bytes().size(), limits.fresh_read.max_source_bytes,
          phase);
    Count(result.output->Bytes().size(), limits.files.max_total_file_bytes,
          phase);
    const auto output_hash = Digest(result.output->Bytes(), s, phase);
    Succeeded(result.observations.write,
              StlFileWriteFacts{
                  result.output->Facts(),
                  static_cast<std::uint64_t>(result.output->Bytes().size()),
                  output_hash});
    phase = Stage::kStaging;
    s.Check(phase);
    transaction->Create();
    WritePhysical(transaction->Entry(), result.output->Bytes(), kEntryIndex,
                  *transaction, work.files, s, result.execution.writing_files);
    s.Check(phase);
    phase = Stage::kStagedReread;
    auto physical = ReadPhysical(transaction->Entry(), kEntryIndex,
                                 limits.files.max_entry_bytes, limits.files, s,
                                 phase, result.execution.staged_reread);
    const auto physical_hash = Digest(physical.Bytes(), s, phase);
    Succeeded(result.observations.staged_reread,
              StlFilePayloadFacts{static_cast<std::uint64_t>(physical.size),
                                  physical_hash});
    phase = Stage::kFreshRead;
    s.Check(phase);
    auto fresh = ReadStl(physical.Bytes(), options.encoding, limits.fresh_read,
                         work.fresh_read, context, stop);
    result.execution.fresh_read = fresh.execution;
    RequireStl(fresh.status, fresh.diagnostic, phase);
    if (!fresh.document) Fail(Status::kStlFailure, phase, Code::kDelegatedStl);
    result.fresh_document = fresh.document;
    Succeeded(result.observations.fresh_read,
              ReadFacts(*result.fresh_document));
    phase = Stage::kVerification;
    s.Check(phase);
    Compare(s, result.output->Bytes(), physical.Bytes(), phase, kEntryIndex,
            result.execution.byte_verification);
    Compare(s, physical.Bytes(), result.fresh_document->OriginalBytes(), phase,
            kEntryIndex, result.execution.byte_verification);
    const auto source_hash = Digest(source.EntryBytes(), s, phase);
    if (source_hash != source.Sha256() || physical_hash != output_hash)
      Fail(Status::kPreservationMismatch, phase, Code::kByteMismatch);
    Verify(source, *result.output, *result.fresh_document, write_options,
           limits.fresh_read, s, result.execution.value_verification);
    auto verified = std::allocate_shared<VerifiedState>(
        allocation::Allocator<VerifiedState>(a), a, source, *result.output,
        *result.fresh_document, physical_hash);
    result.verified.emplace(Access::Verified(std::move(verified)));
    Succeeded(
        result.observations.verification,
        StlFileVerificationFacts{true, true, true, true, true, true, true});
    // Release the temporary physical copy; the genuine fresh codec owns bytes.
    {
      Buffer release(std::move(physical));
    }
    std::optional<Buffer> report;
    if (options.report) {
      phase = Stage::kProvider;
      s.Check(phase);
      std::span<const std::byte> provided;
      try {
        provided = options.report->provide(
            StlFileVerifiedContent{source, *result.verified},
            options.report->state);
      } catch (const std::bad_alloc&) {
        throw;
      } catch (...) {
        Fail(Status::kProviderFailure, phase, Code::kProvider);
      }
      s.Check(phase);
      Count(provided.size(), limits.files.max_caller_report_bytes, phase);
      Utf8(provided, &a, phase, Code::kReportUtf8, false);
      report.emplace(a, provided.size());
      RunBytes(s, ByteAction::kCopy, phase, provided, report->Bytes(), {}, {},
               kReceiptIndex, result.execution.provider_import);
      Succeeded(result.observations.provider);
    }
    phase = Stage::kReceiptVerification;
    s.Check(phase);
    auto receipt =
        Receipt(*result.verified, options,
                report ? report->Bytes() : std::span<const std::byte>{},
                limits.files, s);
    const auto receipt_hash = Digest(receipt.Bytes(), s, phase);
    WritePhysical(transaction->Receipt(), receipt.Bytes(), kReceiptIndex,
                  *transaction, work.files, s, result.execution.writing_files,
                  phase);
    auto receipt_read = ReadPhysical(
        transaction->Receipt(), kReceiptIndex, limits.files.max_receipt_bytes,
        limits.files, s, phase, result.execution.receipt_verification);
    Compare(s, receipt.Bytes(), receipt_read.Bytes(), phase, kReceiptIndex,
            result.execution.receipt_verification);
    if (Digest(receipt_read.Bytes(), s, phase) != receipt_hash)
      Fail(Status::kPreservationMismatch, phase, Code::kByteMismatch,
           kReceiptIndex);
    auto entry_read = ReadPhysical(
        transaction->Entry(), kEntryIndex, limits.files.max_entry_bytes,
        limits.files, s, phase, result.execution.receipt_verification);
    Compare(s, result.verified->EntryBytes(), entry_read.Bytes(), phase,
            kEntryIndex, result.execution.receipt_verification);
    const auto entry_hash = Digest(entry_read.Bytes(), s, phase);
    if (entry_hash != result.verified->Sha256())
      Fail(Status::kPreservationMismatch, phase, Code::kByteMismatch,
           kEntryIndex);
    Succeeded(result.observations.receipt_verification,
              StlFileCaptureFacts{kReceiptName,
                                  static_cast<std::uint64_t>(receipt.size),
                                  receipt_hash});
    phase = Stage::kInventory;
    s.Check(phase);
    transaction->Inventory(true, entry_read.size, receipt_read.size);
    s.Check(phase);
    StlFileInventoryFacts inventory{
        {StlFileCaptureFacts{result.verified->EntryPath(),
                             static_cast<std::uint64_t>(entry_read.size),
                             entry_hash},
         StlFileCaptureFacts{kReceiptName,
                             static_cast<std::uint64_t>(receipt_read.size),
                             receipt_hash}}};
    if (inventory.entries[1].entry < inventory.entries[0].entry)
      std::swap(inventory.entries[0], inventory.entries[1]);
    Succeeded(result.observations.inventory, inventory);
    phase = Stage::kPublication;
    s.Check(phase);
    auto state = std::allocate_shared<BundleState>(
        allocation::Allocator<BundleState>(a), a, *result.verified,
        std::move(receipt), receipt_hash);
    const auto prepared = Access::Bundle(std::move(state));
    phase = Stage::kPublication;
    s.Check(phase);
    transaction->Commit();
    // Native commit is the final fallible mandatory action. Every owner,
    // result slot, borrowed fact and terminated path already exists.
    result.bundle.emplace(prepared);
    Succeeded(result.observations.publication, StlFilePublicationFacts{true});
    Complete(result);
  } catch (const Failure& e) {
    Failed(result, e.diagnostic);
    cleanup();
  } catch (const allocation::AdmissionFailure& e) {
    Failed(result, Admission(e, phase));
    cleanup();
  } catch (...) {
    cleanup();
    throw;
  }
  transaction.reset();
  UpdateRequests(result);
  return result;
}
// Public filesystem values and private charged borrowed path plans enter the
// same cores. The borrowed native ranges need remain readable only through
// synchronous return and joins; existing charged scopes copy retained plans.
StlFileCaptureResult CaptureForTesting(const std::filesystem::path& input,
                                       const StlFileLimits& limits,
                                       const StlFileWorkPolicy& work,
                                       const geo::ExecutionContext& context,
                                       std::stop_token stop,
                                       const TestOptions& testing) {
  return CaptureNative(input.native(), limits, work, context, stop, testing);
}
StlFileReadResult ReadForTesting(const std::filesystem::path& input,
                                 StlEncoding encoding,
                                 const StlFileReadLimits& limits,
                                 const StlFileReadWorkPolicy& work,
                                 const geo::ExecutionContext& context,
                                 std::stop_token stop,
                                 const TestOptions& testing) {
  return ReadNative(input.native(), encoding, limits, work, context, stop,
                    testing);
}
StlFileRewriteResult RewriteForTesting(const StlFileAsset& source,
                                       const std::filesystem::path& destination,
                                       const StlFileRewriteOptions& options,
                                       const StlFileRewriteLimits& limits,
                                       const StlFileRewriteWorkPolicy& work,
                                       std::stop_token stop,
                                       const TestOptions& testing) {
  return RewriteNative(source, destination.native(), options, limits, work,
                       stop, testing);
}
}  // namespace meshvale::interchange::stl_files_detail

namespace meshvale::interchange {
StlCapturedFile::StlCapturedFile(
    std::shared_ptr<const stl_files_detail::CaptureState> s) noexcept
    : state_(std::move(s)) {}
StlCapturedFile::StlCapturedFile(const StlCapturedFile&) noexcept = default;
StlCapturedFile& StlCapturedFile::operator=(const StlCapturedFile&) noexcept =
    default;
StlCapturedFile::~StlCapturedFile() noexcept = default;
std::string_view StlCapturedFile::EntryPath() const noexcept {
  return state_->token->name;
}
std::span<const std::byte> StlCapturedFile::EntryBytes() const noexcept {
  return state_->bytes.Bytes();
}
std::array<std::byte, kStlFileSha256Bytes> StlCapturedFile::Sha256()
    const noexcept {
  return state_->token->digest;
}
geometry::ExecutionContext StlCapturedFile::ShareExecutionContext()
    const noexcept {
  return state_->token->context;
}
std::uint64_t StlCapturedFile::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
StlFileAsset::StlFileAsset(
    std::shared_ptr<const stl_files_detail::AssetState> s) noexcept
    : state_(std::move(s)) {}
StlFileAsset::StlFileAsset(const StlFileAsset&) noexcept = default;
StlFileAsset& StlFileAsset::operator=(const StlFileAsset&) noexcept = default;
StlFileAsset::~StlFileAsset() noexcept = default;
const StlDocument& StlFileAsset::Document() const noexcept {
  return *state_->document;
}
std::string_view StlFileAsset::EntryPath() const noexcept {
  return state_->token->name;
}
std::span<const std::byte> StlFileAsset::EntryBytes() const noexcept {
  return Document().OriginalBytes();
}
std::array<std::byte, kStlFileSha256Bytes> StlFileAsset::Sha256()
    const noexcept {
  return state_->token->digest;
}
std::uint64_t StlFileAsset::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes() +
         state_->token->allocation.OperationActiveBytes();
}
StlVerifiedRewrite::StlVerifiedRewrite(
    std::shared_ptr<const stl_files_detail::VerifiedState> s) noexcept
    : state_(std::move(s)) {}
StlVerifiedRewrite::StlVerifiedRewrite(const StlVerifiedRewrite&) noexcept =
    default;
StlVerifiedRewrite& StlVerifiedRewrite::operator=(
    const StlVerifiedRewrite&) noexcept = default;
StlVerifiedRewrite::~StlVerifiedRewrite() noexcept = default;
const StlFileAsset& StlVerifiedRewrite::Source() const noexcept {
  return state_->source;
}
const StlOutput& StlVerifiedRewrite::Output() const noexcept {
  return state_->output;
}
const StlDocument& StlVerifiedRewrite::Document() const noexcept {
  return state_->document;
}
std::string_view StlVerifiedRewrite::EntryPath() const noexcept {
  return state_->source.EntryPath();
}
std::span<const std::byte> StlVerifiedRewrite::EntryBytes() const noexcept {
  return state_->document.OriginalBytes();
}
std::array<std::byte, kStlFileSha256Bytes> StlVerifiedRewrite::Sha256()
    const noexcept {
  return state_->digest;
}
std::uint64_t StlVerifiedRewrite::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
StlPublishedBundle::StlPublishedBundle(
    std::shared_ptr<const stl_files_detail::BundleState> s) noexcept
    : state_(std::move(s)) {}
StlPublishedBundle::StlPublishedBundle(const StlPublishedBundle&) noexcept =
    default;
StlPublishedBundle& StlPublishedBundle::operator=(
    const StlPublishedBundle&) noexcept = default;
StlPublishedBundle::~StlPublishedBundle() noexcept = default;
const StlVerifiedRewrite& StlPublishedBundle::Verified() const noexcept {
  return state_->verified;
}
std::string_view StlPublishedBundle::ReceiptPath() const noexcept {
  return stl_files_detail::kReceiptName;
}
std::span<const std::byte> StlPublishedBundle::ReceiptBytes() const noexcept {
  return state_->receipt.Bytes();
}
std::array<std::byte, kStlFileSha256Bytes> StlPublishedBundle::ReceiptSha256()
    const noexcept {
  return state_->digest;
}
std::uint64_t StlPublishedBundle::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
StlFileCaptureResult CaptureStlFile(const std::filesystem::path& p,
                                    const StlFileLimits& l,
                                    const StlFileWorkPolicy& w,
                                    const geometry::ExecutionContext& c,
                                    std::stop_token stop) {
  return stl_files_detail::CaptureForTesting(p, l, w, c, stop, {});
}
StlFileReadResult DecodeStlFile(const StlCapturedFile& c, StlEncoding e,
                                const StlReadLimits& l, const StlWorkPolicy& w,
                                std::stop_token stop) {
  return stl_files_detail::Decode(c, e, l, w, stop);
}
StlFileReadResult ReadStlFileAsset(const std::filesystem::path& p,
                                   StlEncoding e, const StlFileReadLimits& l,
                                   const StlFileReadWorkPolicy& w,
                                   const geometry::ExecutionContext& c,
                                   std::stop_token stop) {
  return stl_files_detail::ReadForTesting(p, e, l, w, c, stop, {});
}
StlFileRewriteResult RewriteStlFile(const StlFileAsset& s,
                                    const std::filesystem::path& p,
                                    const StlFileRewriteOptions& o,
                                    const StlFileRewriteLimits& l,
                                    const StlFileRewriteWorkPolicy& w,
                                    std::stop_token stop) {
  return stl_files_detail::RewriteForTesting(s, p, o, l, w, stop, {});
}
}  // namespace meshvale::interchange
