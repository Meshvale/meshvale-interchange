// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
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
#include <unistd.h>
#endif

namespace meshvale::interchange::gltf_files_detail {
namespace {
constexpr unsigned char kControlEnd = 0x20;
constexpr unsigned char kDelete = 0x7f;
constexpr std::string_view kProhibited = ":<>\"|?*\\";
constexpr std::size_t kInitialCwdUnits = 128;
constexpr unsigned kHexRadix = 16;
constexpr unsigned kHexLetterOffset = 10;
constexpr std::size_t kPercentEscapeCharacters = 3;
constexpr unsigned char kAsciiEnd = 0x80;
constexpr std::uint32_t kLatinControlEnd = 0xa0;
constexpr unsigned char kContinuationMask = 0xc0;
constexpr unsigned char kContinuationTag = 0x80;
constexpr unsigned char kContinuationPayload = 0x3f;
constexpr unsigned kContinuationBits = 6;
constexpr std::uint32_t kUnicodeMaximum = 0x10ffff;
constexpr std::uint32_t kSurrogateFirst = 0xd800;
constexpr std::uint32_t kSurrogateLast = 0xdfff;
struct Lead {
  unsigned char first, last, mask;
  std::size_t tail;
  std::uint32_t minimum;
};
constexpr std::array kLeads{Lead{0xc2, 0xdf, 0x1f, 1, 0x80},
                            Lead{0xe0, 0xef, 0x0f, 2, 0x800},
                            Lead{0xf0, 0xf4, 0x07, 3, 0x10000}};
void Utf8(std::string_view input, GltfFileDiagnosticCode code) {
  const auto malformed = [&] {
    Fail(code == GltfFileDiagnosticCode::kUriMalformed
             ? GltfFileStatus::kMalformed
             : GltfFileStatus::kUnsupportedProfile,
         GltfFileStage::kPathPlanning, code);
  };
  for (std::size_t i = 0; i < input.size();) {
    const auto lead = static_cast<unsigned char>(input[i++]);
    if (lead < kAsciiEnd) continue;
    const auto found = std::find_if(
        kLeads.begin(), kLeads.end(),
        [lead](auto r) { return lead >= r.first && lead <= r.last; });
    if (found == kLeads.end() || found->tail > input.size() - i) malformed();
    std::uint32_t scalar = lead & found->mask;
    for (std::size_t j = 0; j < found->tail; ++j) {
      const auto c = static_cast<unsigned char>(input[i++]);
      if ((c & kContinuationMask) != kContinuationTag) malformed();
      scalar = (scalar << kContinuationBits) | (c & kContinuationPayload);
    }
    if (scalar < found->minimum || scalar > kUnicodeMaximum ||
        (scalar >= kSurrogateFirst && scalar <= kSurrogateLast))
      malformed();
    if (scalar >= kAsciiEnd && scalar < kLatinControlEnd)
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           code == GltfFileDiagnosticCode::kUriMalformed
               ? GltfFileDiagnosticCode::kUriUnsupported
               : code);
  }
}
char Fold(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
bool EqualFold(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](char x, char y) { return Fold(x) == Fold(y); });
}
bool Reserved(std::string_view part) {
  constexpr std::array<std::string_view, 4> kDeviceStems{"con", "prn", "aux",
                                                         "nul"};
  constexpr std::array<std::string_view, 2> kNumberedDevices{"com", "lpt"};
  constexpr char kFirstDeviceNumber = '1';
  constexpr char kLastDeviceNumber = '9';
  const auto stem = part.substr(0, part.find('.'));
  for (const auto device : kDeviceStems)
    if (EqualFold(stem, device)) return true;
  for (const auto device : kNumberedDevices)
    if (stem.size() == device.size() + 1 &&
        EqualFold(stem.substr(0, device.size()), device) &&
        stem.back() >= kFirstDeviceNumber && stem.back() <= kLastDeviceNumber)
      return true;
  return false;
}
NativeString Native(std::string_view input,
                    const gltf_detail::AllocationContext& allocation,
                    const GltfFileLimits& limits) {
  NativeString output{gltf_detail::Allocator<NativeChar>(allocation)};
#ifdef _WIN32
  Count(input.size(), std::numeric_limits<int>::max());
  const auto units =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                          static_cast<int>(input.size()), nullptr, 0);
  if (units == 0 && !input.empty())
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported);
  Count(static_cast<std::uint64_t>(units) + 1,
        limits.max_native_path_code_units);
  output.resize(static_cast<std::size_t>(units));
  if (units && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                    static_cast<int>(input.size()),
                                    output.data(), units))
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported);
  std::replace(output.begin(), output.end(), L'/', L'\\');
#else
  Count(input.size() + 1, limits.max_native_path_code_units);
  output.assign(input);
#endif
  return output;
}
String Utf8Native(NativeView input,
                  const gltf_detail::AllocationContext& allocation,
                  const GltfFileLimits& limits) {
  String output{gltf_detail::Allocator<char>(allocation)};
#ifdef _WIN32
  Count(input.size(), std::numeric_limits<int>::max());
  const auto bytes = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  if (bytes == 0 && !input.empty())
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported);
  Count(static_cast<std::uint64_t>(bytes), limits.max_path_bytes);
  output.resize(static_cast<std::size_t>(bytes));
  if (bytes && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                                    static_cast<int>(input.size()),
                                    output.data(), bytes, nullptr, nullptr))
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported);
  std::replace(output.begin(), output.end(), '\\', '/');
#else
  Count(input.size(), limits.max_path_bytes);
  output.assign(input);
#endif
  return output;
}
int Hex(char c) {
  constexpr int kInvalidHexDigit = -1;
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + kHexLetterOffset;
  if (c >= 'a' && c <= 'f') return c - 'a' + kHexLetterOffset;
  return kInvalidHexDigit;
}
}  // namespace
#ifdef _WIN32
void RequireDriveAbsolutePath(NativeView path) {
  if (path.size() < kDriveRootUnits || path[kDriveRootUnits - 2] != L':' ||
      path[kDriveRootUnits - 1] != L'\\')
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported);
}
#endif
String PortablePath(std::string_view input,
                    const gltf_detail::AllocationContext& allocation,
                    const GltfFileLimits& limits) {
  Count(input.size(), limits.max_path_bytes);
  Utf8(input, GltfFileDiagnosticCode::kPathUnsupported);
  if (input.empty() || input.front() == '/')
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported);
  for (unsigned char c : input)
    if (c < kControlEnd || c == kDelete ||
        kProhibited.find(static_cast<char>(c)) != std::string_view::npos)
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kPathUnsupported);
  for (std::size_t begin = 0; begin < input.size();) {
    const auto end = input.find('/', begin);
    const auto part = input.substr(
        begin, end == input.npos ? input.size() - begin : end - begin);
    if (part.empty() || part == "." || part == ".." || part.back() == '.' ||
        part.back() == ' ' || Reserved(part))
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kPathUnsupported);
    if (end == input.npos) break;
    begin = end + 1;
    if (begin == input.size())
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kPathUnsupported);
  }
  return String(input.begin(), input.end(),
                gltf_detail::Allocator<char>(allocation));
}
String ResourcePath(std::string_view entry, std::string_view uri,
                    const gltf_detail::AllocationContext& allocation,
                    const GltfFileLimits& limits) {
  constexpr std::string_view kExcludedUriSyntax = ":?#\\";
  if (uri.empty() || uri.front() == '/' ||
      uri.find_first_of(kExcludedUriSyntax) != uri.npos)
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kUriUnsupported);
  String decoded{gltf_detail::Allocator<char>(allocation)};
  for (std::size_t i = 0; i < uri.size(); ++i) {
    allocation.Checkpoint();
    unsigned char c = static_cast<unsigned char>(uri[i]);
    if (c == '%') {
      if (uri.size() - i < kPercentEscapeCharacters || Hex(uri[i + 1]) < 0 ||
          Hex(uri[i + 2]) < 0)
        Fail(GltfFileStatus::kMalformed, GltfFileStage::kPathPlanning,
             GltfFileDiagnosticCode::kUriMalformed);
      c = static_cast<unsigned char>(Hex(uri[i + 1]) * kHexRadix +
                                     Hex(uri[i + 2]));
      i += kPercentEscapeCharacters - 1;
      if (c == '/' || c == '\\')
        Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
             GltfFileDiagnosticCode::kUriUnsupported);
    }
    if (c < kControlEnd || c == kDelete)
      Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
           GltfFileDiagnosticCode::kUriUnsupported);
    decoded.push_back(static_cast<char>(c));
  }
  Utf8(decoded, GltfFileDiagnosticCode::kUriMalformed);
  const auto separator = decoded.rfind('/');
  const auto leaf = std::string_view(decoded).substr(
      separator == decoded.npos ? 0 : separator + 1);
  if (leaf.empty() || leaf == "." || leaf == "..")
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kUriUnsupported);
  String joined{gltf_detail::Allocator<char>(allocation)};
  const auto parent = entry.rfind('/');
  if (parent != entry.npos) joined.assign(entry.substr(0, parent + 1));
  joined.append(decoded);
  String result{gltf_detail::Allocator<char>(allocation)};
  Vector<std::size_t> starts{gltf_detail::Allocator<std::size_t>(allocation)};
  for (std::size_t begin = 0; begin <= joined.size();) {
    const auto end = joined.find('/', begin);
    const auto part = std::string_view(joined).substr(
        begin, end == joined.npos ? joined.size() - begin : end - begin);
    if (part == "..") {
      if (starts.empty())
        Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
             GltfFileDiagnosticCode::kUriUnsupported);
      result.resize(starts.back());
      starts.pop_back();
    } else if (!part.empty() && part != ".") {
      starts.push_back(result.size());
      if (!result.empty()) result.push_back('/');
      result.append(part);
    }
    if (end == joined.npos) break;
    begin = end + 1;
  }
  return PortablePath(result, allocation, limits);
}
NativeString AbsolutePath(NativeView path,
                          const gltf_detail::AllocationContext& allocation,
                          const GltfFileLimits& limits) {
  if (path.empty())
    Fail(GltfFileStatus::kInvalidRequest, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kInvalidRequest);
  Count(path.size() + 1, limits.max_native_path_code_units);
  NativeString input(path.begin(), path.end(),
                     gltf_detail::Allocator<NativeChar>(allocation));
  NativeString output{gltf_detail::Allocator<NativeChar>(allocation)};
#ifdef _WIN32
  const auto needed = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
  if (!needed)
    Fail(GltfFileStatus::kIoFailure, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported, {}, {},
         std::error_code(static_cast<int>(GetLastError()),
                         std::system_category()));
  Count(needed, limits.max_native_path_code_units);
  output.resize(needed);
  const auto length =
      GetFullPathNameW(input.c_str(), needed, output.data(), nullptr);
  if (!length || length >= needed)
    Fail(GltfFileStatus::kIoFailure, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathUnsupported);
  output.resize(length);
  // Ordinary drive roots are selected; native network/device namespaces are
  // outside the initial local-file profile.
  RequireDriveAbsolutePath(output);
  while (output.size() > kDriveRootUnits && output.back() == L'\\')
    output.pop_back();
#else
  if (input.front() != '/') {
    auto capacity = std::min<std::size_t>(kInitialCwdUnits,
                                          limits.max_native_path_code_units);
    for (;;) {
      output.resize(capacity);
      if (getcwd(output.data(), capacity)) {
        output.resize(std::char_traits<char>::length(output.c_str()));
        break;
      }
      if (errno == ERANGE && capacity == limits.max_native_path_code_units)
        Fail(GltfFileStatus::kLimitExceeded, GltfFileStage::kPathPlanning,
             GltfFileDiagnosticCode::kCountLimit);
      if (errno != ERANGE)
        Fail(GltfFileStatus::kIoFailure, GltfFileStage::kPathPlanning,
             GltfFileDiagnosticCode::kPathUnsupported, {}, {},
             std::error_code(errno, std::generic_category()));
      capacity += std::min<std::size_t>(
          capacity, limits.max_native_path_code_units - capacity);
    }
    output.push_back('/');
    output.append(input);
  } else
    output = std::move(input);
  NativeString normalized{gltf_detail::Allocator<char>(allocation)};
  normalized.push_back('/');
  Vector<std::size_t> starts{gltf_detail::Allocator<std::size_t>(allocation)};
  for (std::size_t begin = 1; begin <= output.size();) {
    const auto end = output.find('/', begin);
    const auto part = std::string_view(output).substr(
        begin, end == output.npos ? output.size() - begin : end - begin);
    if (part == "..") {
      if (!starts.empty()) {
        normalized.resize(starts.back());
        starts.pop_back();
      }
    } else if (!part.empty() && part != ".") {
      starts.push_back(normalized.size());
      if (normalized.size() > 1) normalized.push_back('/');
      normalized.append(part);
    }
    if (end == output.npos) break;
    begin = end + 1;
  }
  output = std::move(normalized);
#endif
  Count(output.size() + 1, limits.max_native_path_code_units);
  return output;
}
String RelativeEntry(NativeView root, NativeView input,
                     const gltf_detail::AllocationContext& allocation,
                     const GltfFileLimits& limits) {
#ifdef _WIN32
  constexpr NativeChar kSeparator = L'\\';
  const auto equal = [](NativeChar a, NativeChar b) {
    if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
    if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
    return a == b;
  };
#else
  constexpr NativeChar kSeparator = '/';
  const auto equal = [](NativeChar a, NativeChar b) { return a == b; };
#endif
  if (input.size() <= root.size() ||
      !std::equal(root.begin(), root.end(), input.begin(), equal) ||
      (root.back() != kSeparator && input[root.size()] != kSeparator))
    Fail(GltfFileStatus::kUnsupportedProfile, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kPathOutsideRoot);
  const auto offset = root.size() + (root.back() == kSeparator ? 0 : 1);
  const auto utf8 = Utf8Native(input.substr(offset), allocation, limits);
  return PortablePath(utf8, allocation, limits);
}
NativeString JoinPath(NativeView root, std::string_view relative,
                      const gltf_detail::AllocationContext& allocation,
                      const GltfFileLimits& limits) {
  auto tail = Native(relative, allocation, limits);
  NativeString output(root.begin(), root.end(),
                      gltf_detail::Allocator<NativeChar>(allocation));
#ifdef _WIN32
  constexpr NativeChar kSeparator = L'\\';
#else
  constexpr NativeChar kSeparator = '/';
#endif
  const auto separator = output.back() == kSeparator ? 0 : 1;
  if (tail.size() >= limits.max_native_path_code_units ||
      output.size() + separator >
          limits.max_native_path_code_units - 1 - tail.size())
    Fail(GltfFileStatus::kLimitExceeded, GltfFileStage::kPathPlanning,
         GltfFileDiagnosticCode::kCountLimit);
  if (separator) output.push_back(kSeparator);
  output.append(tail);
  return output;
}
}  // namespace meshvale::interchange::gltf_files_detail
