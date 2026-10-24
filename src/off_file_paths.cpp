// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#include "off_file_internal.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace meshvale::interchange::off_files_detail {
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
  Fail(OffFileStatus::kUnsupportedProfile, OffFileStage::kPathPlanning,
       OffFileDiagnosticCode::kNameUnsupported);
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
                             OffFileStage stage) {
  for (std::size_t index = 0; index < storage.size(); ++index) {
    allocator.Checkpoint();
    if (storage[index] == NativeChar{}) return index;
  }
  Fail(OffFileStatus::kLimitExceeded, stage,
       OffFileDiagnosticCode::kCountLimit);
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
  constexpr std::size_t kNumberedDeviceLength = 4;
  constexpr std::size_t kDeviceStemLength = 3;
  constexpr char kFirstDeviceNumber = '1', kLastDeviceNumber = '9';
  if (stem.size() == kNumberedDeviceLength &&
      (EqualFold(stem.substr(0, kDeviceStemLength), "com") ||
       EqualFold(stem.substr(0, kDeviceStemLength), "lpt")) &&
      stem.back() >= kFirstDeviceNumber && stem.back() <= kLastDeviceNumber)
    Unsupported();
}
NativeString AbsolutePath(NativeView input,
                          const allocation::AllocationContext& allocator,
                          const OffFileLimits& limits) {
  Count(input.size(), limits.max_native_path_code_units - 1);
  if (input.empty())
    Fail(OffFileStatus::kUnsupportedProfile, OffFileStage::kPathPlanning,
         OffFileDiagnosticCode::kPathUnsupported);
  for (auto value : input) {
    allocator.Checkpoint();
    if (value == NativeChar{})
      Fail(OffFileStatus::kUnsupportedProfile, OffFileStage::kPathPlanning,
           OffFileDiagnosticCode::kPathUnsupported);
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
      Fail(OffFileStatus::kUnsupportedProfile, OffFileStage::kPathPlanning,
           OffFileDiagnosticCode::kPathUnsupported);
    Count(limits.max_native_path_code_units, std::numeric_limits<DWORD>::max());
    Zeros(output, static_cast<std::size_t>(limits.max_native_path_code_units),
          allocator);
    const auto count =
        GetCurrentDirectoryW(static_cast<DWORD>(output.size()), output.data());
    if (!count || count >= output.size())
      Fail(OffFileStatus::kIoFailure, OffFileStage::kPathPlanning,
           OffFileDiagnosticCode::kPathUnsupported, {}, {},
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
      Fail(OffFileStatus::kIoFailure, OffFileStage::kPathPlanning,
           OffFileDiagnosticCode::kPathUnsupported, {}, {},
           std::error_code(errno, std::generic_category()));
    output.resize(TerminatedLength(
        std::span<const NativeChar>(output.data(), output.size()), allocator,
        OffFileStage::kPathPlanning));
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
                 const OffFileLimits& limits) {
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
                      const OffFileLimits& limits) {
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
    Fail(OffFileStatus::kLimitExceeded, OffFileStage::kPathPlanning,
         OffFileDiagnosticCode::kCountLimit);
  NativeString result{allocation::Allocator<NativeChar>(allocator)};
  result.reserve(root.size() + separator + tail.size());
  Extend(result, root, allocator);
  if (separator) result.push_back(kSeparator);
  Extend(result, NativeView(tail), allocator);
  return result;
}
}  // namespace meshvale::interchange::off_files_detail
