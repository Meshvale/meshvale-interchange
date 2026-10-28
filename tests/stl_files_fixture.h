// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_STL_FILES_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_STL_FILES_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>
#include <meshvale/interchange/stl_files.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace meshvale::interchange::stl_files_fixture {
// Failure-only observations never replace the original product input or retry
// its operation. Enum integers refer to the public stl_files.h declarations.
inline constexpr std::string_view kReadFailureTag = "[STL-read-diagnostic] ";
inline void PrintDiagnosticPath(std::string_view label,
                                const std::filesystem::path& path) {
  const auto utf8 = path.u8string();
  std::cerr << kReadFailureTag << label << '='
            << std::quoted(std::string(
                   reinterpret_cast<const char*>(utf8.data()), utf8.size()))
            << '\n';
}
#ifdef _WIN32
inline void PrintWindowsPathFacts(const std::filesystem::path& input) {
  // Fixed diagnostic-only capacity accommodates extended-length spellings;
  // oversized observations report their returned length without retrying.
  constexpr DWORD kDiagnosticPathCodeUnits = 32768;
  std::array<wchar_t, kDiagnosticPathCodeUnits> buffer{};
  auto size = GetFullPathNameW(input.c_str(), kDiagnosticPathCodeUnits,
                               buffer.data(), nullptr);
  auto error = size == 0 ? GetLastError() : ERROR_SUCCESS;
  std::cerr << kReadFailureTag << "GetFullPathNameW length=" << size
            << " error=" << error << '\n';
  if (size > 0 && size < kDiagnosticPathCodeUnits)
    PrintDiagnosticPath("full spelling", std::filesystem::path(buffer.data()));
  size =
      GetLongPathNameW(input.c_str(), buffer.data(), kDiagnosticPathCodeUnits);
  error = size == 0 ? GetLastError() : ERROR_SUCCESS;
  std::cerr << kReadFailureTag << "GetLongPathNameW length=" << size
            << " error=" << error << '\n';
  if (size > 0 && size < kDiagnosticPathCodeUnits)
    PrintDiagnosticPath("long spelling", std::filesystem::path(buffer.data()));
  auto prefix = input.root_path();
  for (const auto& component : input.relative_path()) {
    prefix /= component;
    const auto attributes = GetFileAttributesW(prefix.c_str());
    error =
        attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    PrintDiagnosticPath("prefix", prefix);
    std::cerr << kReadFailureTag << "attributes=" << attributes << " reparse="
              << (attributes != INVALID_FILE_ATTRIBUTES &&
                  (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
              << " error=" << error << '\n';
  }
  struct DiagnosticHandle {
    HANDLE value;
    ~DiagnosticHandle() {
      if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
    DWORD Close() noexcept {
      const auto owned = value;
      value = INVALID_HANDLE_VALUE;
      return CloseHandle(owned) ? ERROR_SUCCESS : GetLastError();
    }
  };
  DiagnosticHandle handle{
      CreateFileW(input.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
  if (handle.value == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    std::cerr << kReadFailureTag << "observer CreateFileW error=" << error
              << '\n';
    return;
  }
  size = GetFinalPathNameByHandleW(handle.value, buffer.data(),
                                   kDiagnosticPathCodeUnits,
                                   FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  error = size == 0 ? GetLastError() : ERROR_SUCCESS;
  const auto close_error = handle.Close();
  std::cerr << kReadFailureTag << "GetFinalPathNameByHandleW length=" << size
            << " error=" << error << " observer close error=" << close_error
            << '\n';
  if (size > 0 && size < kDiagnosticPathCodeUnits)
    PrintDiagnosticPath("handle final spelling",
                        std::filesystem::path(buffer.data()));
}
#endif
inline void PrintReadFailure(const StlFileResult& result,
                             const std::filesystem::path& input) noexcept {
  try {
    std::cerr << kReadFailureTag
              << "status(enum)=" << static_cast<int>(result.status)
              << " phase(enum)=" << static_cast<int>(result.phase)
              << " diagnostic.status(enum)="
              << static_cast<int>(result.diagnostic.status)
              << " diagnostic.phase(enum)="
              << static_cast<int>(result.diagnostic.phase)
              << " diagnostic.code(enum)="
              << static_cast<int>(result.diagnostic.code) << '\n';
    if (result.diagnostic.system_error) {
      const auto& error = *result.diagnostic.system_error;
      std::cerr << kReadFailureTag
                << "system error category=" << error.category().name()
                << " value=" << error.value()
                << " message=" << std::quoted(error.message()) << '\n';
    } else {
      std::cerr << kReadFailureTag << "system error absent\n";
    }
    if (result.diagnostic.stl_status)
      std::cerr << kReadFailureTag << "delegated STL status(enum)="
                << static_cast<int>(*result.diagnostic.stl_status) << '\n';
    std::cerr << kReadFailureTag << "capture observation(enum)="
              << static_cast<int>(result.observations.capture.state)
              << " read observation(enum)="
              << static_cast<int>(result.observations.read.state)
              << " capture workers launched="
              << result.execution.capture.launched_workers
              << " joined=" << result.execution.capture.joined_workers << '\n';
    std::cerr << kReadFailureTag
              << "independent later filesystem observations (not original "
                 "operation state)\n";
    PrintDiagnosticPath("unchanged input", input);
    std::error_code error;
    const auto size = std::filesystem::file_size(input, error);
    std::cerr << kReadFailureTag << "observed file bytes=" << size
              << " error category=" << error.category().name()
              << " value=" << error.value() << '\n';
#ifdef _WIN32
    PrintWindowsPathFacts(input);
#endif
  } catch (...) {
    // Observation failure must not replace the unchanged original assertion.
  }
}
inline void Require(bool value, std::string_view message,
                    std::source_location at = std::source_location::current()) {
  if (!value)
    throw std::runtime_error(std::string(at.file_name()) + ":" +
                             std::to_string(at.line()) + ": " +
                             std::string(message));
}
inline std::span<const std::byte> Bytes(std::string_view value) {
  return std::as_bytes(std::span(value));
}
inline constexpr std::uint64_t kMiB = 1048576;
inline constexpr std::uint64_t kContextBytes = 1024 * kMiB;
inline constexpr std::string_view kReceipt = "stl-publication.json";
inline constexpr std::string_view kName = "raw facets";
inline constexpr std::string_view kEmpty = "solid \nendsolid \n";
// Third facet is finite and completely collapsed. The first two share XYZ
// values but have opposed/nonunit and zero normals; soup identities stay
// distinct.
inline constexpr std::string_view kAscii =
    "solid raw facets\n"
    "facet normal -0 0 -2\nouter loop\nvertex -0 0 0\nvertex 1 0 0\n"
    "vertex 0 1 0\nendloop\nendfacet\n"
    "facet normal 0 0 0\nouter loop\nvertex -0 0 0\nvertex 1 0 0\n"
    "vertex 0 1 0\nendloop\nendfacet\n"
    "facet normal 2 0 0\nouter loop\nvertex 1 1 1\nvertex 1 1 1\n"
    "vertex 1 1 1\nendloop\nendfacet\nendsolid raw facets\n";
using Facet = std::array<std::uint32_t, 12>;
inline constexpr std::array<Facet, 3> kFacetBits{
    {{0x80000000, 0, 0xc0000000, 0x80000000, 0, 0, 0x3f800000, 0, 0, 0,
      0x3f800000, 0},
     {0, 0, 0, 0x80000000, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0},
     {0x40000000, 0, 0, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000,
      0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000}}};
// Independent IEEE-754 boundary values. No production constants provide wire
// offsets, field bits, numeric expectations or physical length expectations.
inline constexpr std::array<Facet, 1> kBoundaryBits{
    {{0x80000000, 0x00000001, 0x7f7fffff, 0x80000000, 0x007fffff, 0x00800000,
      0x7f7fffff, 0xff7fffff, 0x00000001, 0x3f800000, 0xbf800000, 0}}};
inline std::array<std::byte, 80> Header() {
  std::array<std::byte, 80> result{};
  for (std::size_t i = 0; i != result.size(); ++i)
    result[i] = std::byte(static_cast<unsigned char>(i * 37));
  return result;
}
inline void PutWord(std::span<std::byte> bytes, std::size_t offset,
                    std::uint32_t word) {
  for (unsigned i = 0; i != 4; ++i)
    bytes[offset + i] = std::byte((word >> (8 * i)) & 255);
}
inline std::uint32_t Word(std::span<const std::byte> bytes,
                          std::size_t offset) {
  Require(offset <= bytes.size() && bytes.size() - offset >= 4,
          "independent binary field fits");
  std::uint32_t value = 0;
  for (unsigned i = 0; i != 4; ++i)
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
  return value;
}
inline std::vector<std::byte> Binary(std::span<const Facet> facets) {
  std::vector<std::byte> bytes(84 + 50 * facets.size());
  const auto header = Header();
  std::ranges::copy(header, bytes.begin());
  PutWord(bytes, 80, static_cast<std::uint32_t>(facets.size()));
  for (std::size_t i = 0; i != facets.size(); ++i)
    for (std::size_t j = 0; j != 12; ++j)
      PutWord(bytes, 84 + 50 * i + 4 * j, facets[i][j]);
  return bytes;
}
inline void CheckBinary(std::span<const std::byte> bytes,
                        std::span<const Facet> facets,
                        std::span<const std::byte> header) {
  Require(bytes.size() == 84 + 50 * facets.size() &&
              Word(bytes, 80) == facets.size(),
          "exact 84+50N extent and little-endian count");
  Require(std::ranges::equal(bytes.first(80), header),
          "complete opaque header");
  for (std::size_t i = 0; i != facets.size(); ++i) {
    for (std::size_t j = 0; j != 12; ++j)
      Require(Word(bytes, 84 + 50 * i + 4 * j) == facets[i][j],
              "physical ordered normal/position binary32 bits");
    Require(bytes[84 + 50 * i + 48] == std::byte{0} &&
                bytes[84 + 50 * i + 49] == std::byte{0},
            "both attribute bytes are zero");
  }
}
inline void SameBits(double actual, double expected) {
  Require(std::bit_cast<std::uint64_t>(actual) ==
              std::bit_cast<std::uint64_t>(expected),
          "binary64 value and zero sign");
}
inline void CheckAscii(std::span<const std::byte> bytes,
                       std::span<const Facet> facets, std::string_view name) {
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                              bytes.size());
  const auto opening_end = text.find('\n');
  Require(opening_end != std::string_view::npos,
          "independent ASCII opening line");
  auto opening = text.substr(0, opening_end);
  if (!opening.empty() && opening.back() == '\r') opening.remove_suffix(1);
  Require(opening == "solid " + std::string(name) ||
              (name.empty() && opening == "solid"),
          "independent complete ASCII solid name");
  std::size_t cursor = opening_end + 1;
  const auto token = [&]() {
    while (cursor < text.size() &&
           (text[cursor] == ' ' || text[cursor] == '\t' ||
            text[cursor] == '\r' || text[cursor] == '\n'))
      ++cursor;
    const auto start = cursor;
    while (cursor < text.size() && text[cursor] != ' ' &&
           text[cursor] != '\t' && text[cursor] != '\r' && text[cursor] != '\n')
      ++cursor;
    Require(start != cursor, "independent ASCII token exists");
    return text.substr(start, cursor - start);
  };
  const auto word = [&](std::string_view expected) {
    Require(token() == expected, "independent ordered ASCII grammar field");
  };
  const auto component = [&](std::uint32_t bits) {
    const auto value = token();
    double decoded = 0;
    const auto conversion =
        std::from_chars(value.data(), value.data() + value.size(), decoded,
                        std::chars_format::general);
    Require(conversion.ec == std::errc{} &&
                conversion.ptr == value.data() + value.size(),
            "independent complete ASCII numeric conversion");
    SameBits(decoded, static_cast<double>(std::bit_cast<float>(bits)));
  };
  for (const auto& facet : facets) {
    word("facet");
    word("normal");
    for (std::size_t j = 0; j != 3; ++j) component(facet[j]);
    word("outer");
    word("loop");
    for (std::size_t corner = 0; corner != 3; ++corner) {
      word("vertex");
      for (std::size_t j = 0; j != 3; ++j) component(facet[3 + corner * 3 + j]);
    }
    word("endloop");
    word("endfacet");
  }
  word("endsolid");
  // Name belongs to the closing line, not numeric token spelling.
  const auto closing_end = text.find('\n', cursor);
  auto closing = text.substr(cursor, closing_end == std::string_view::npos
                                         ? text.size() - cursor
                                         : closing_end - cursor);
  if (!closing.empty() && closing.back() == '\r') closing.remove_suffix(1);
  Require(
      closing == " " + std::string(name) || (name.empty() && closing.empty()),
      "independent complete ASCII closing name");
  cursor =
      closing_end == std::string_view::npos ? text.size() : closing_end + 1;
  Require(std::ranges::all_of(text.substr(cursor),
                              [](char c) {
                                return c == ' ' || c == '\t' || c == '\n' ||
                                       c == '\r';
                              }),
          "ASCII has no trailing facet or non-whitespace bytes");
}
inline void CheckDocument(const StlDocument& document,
                          std::span<const Facet> facets) {
  const auto mesh = document.Mesh().View();
  Require(document.Inspection().IsStorageValid(), "real complete inspection");
  Require(mesh.positions.size() == facets.size() * 9 &&
              mesh.corner_vertices.size() == facets.size() * 3 &&
              mesh.face_offsets.size() == facets.size() + 1,
          "3N vertices/corners and N+1 offsets including empty domain");
  for (std::size_t i = 0; i != mesh.face_offsets.size(); ++i)
    Require(mesh.face_offsets[i] == i * 3, "complete ordered triangle offsets");
  for (std::size_t i = 0; i != mesh.corner_vertices.size(); ++i)
    Require(mesh.corner_vertices[i] == i, "complete distinct soup identity");
  Require(mesh.attributes.size() == 1,
          "sole normal descriptor even when empty");
  const auto& attribute = mesh.attributes.front();
  Require(attribute.domain == geometry::AttributeDomain::face &&
              attribute.name == "facet_normal" &&
              attribute.semantic == "normal" && attribute.components == 3 &&
              !attribute.set_index && !attribute.offsets &&
              !attribute.present && attribute.metadata.empty(),
          "entire normal descriptor, no synthesized channels");
  const auto* normal = std::get_if<std::span<const double>>(&attribute.values);
  Require(normal && normal->size() == facets.size() * 3,
          "dense binary64 normals");
  for (std::size_t facet = 0; facet != facets.size(); ++facet) {
    for (std::size_t j = 0; j != 3; ++j)
      SameBits((*normal)[3 * facet + j],
               static_cast<double>(std::bit_cast<float>(facets[facet][j])));
    for (std::size_t j = 0; j != 9; ++j)
      SameBits(mesh.positions[9 * facet + j],
               static_cast<double>(std::bit_cast<float>(facets[facet][j + 3])));
  }
}
// Small finite fixtures use this independent worksheet, not the large preset
// as an allocation target. Every positive field is assigned explicitly.
inline StlFileLimits FileLimits() {
  return {kMiB, kMiB, 2 * kMiB, 240, 32768, 128, 65536, 16 * kMiB};
}
inline StlFileWorkPolicy FileWork() { return {128, 1, 64, 16, 4}; }
inline StlReadLimits CodecReadLimits() {
  return {kMiB,
          8,
          1024,
          8192,
          128,
          16 * kMiB,
          48 * kMiB,
          {24, 8, 24, 9, 1, 24, 1, 1, 1, 4096, 16 * kMiB},
          {1024, 16 * kMiB}};
}
inline StlWorkPolicy CodecWork() { return {16, 8, 1, {1, 64, 8, 4}}; }
inline StlFileReadLimits ReadLimits() {
  return {FileLimits(), CodecReadLimits()};
}
inline StlFileReadWorkPolicy ReadWork() { return {FileWork(), CodecWork()}; }
inline StlFileRewriteLimits RewriteLimits() {
  return {FileLimits(),
          {kMiB, 8, 1024, 8192, 128, 16 * kMiB, 32 * kMiB, {1024, 16 * kMiB}},
          CodecReadLimits(),
          96 * kMiB};
}
inline StlFileRewriteWorkPolicy RewriteWork() {
  return {FileWork(), CodecWork(), CodecWork()};
}
inline StlFileRewriteOptions Retain(StlEncoding encoding) {
  StlFileRewriteOptions options;
  options.encoding = encoding;
  options.acknowledge_corner_expansion = true;
  return options;
}
inline void Write(const std::filesystem::path& path,
                  std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(stream.good(), "fixture physical write and close");
}
inline std::vector<std::byte> Read(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  Require(stream.good(), "fixture physically reopens output");
  const auto size = stream.tellg();
  Require(size >= 0, "fixture extent available");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  Require(stream.good(), "fixture exact physical read");
  stream.close();
  Require(!stream.fail(), "fixture physical read close");
  return bytes;
}
struct TempTree {
  std::filesystem::path root;
  TempTree() {
    const auto seed =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned i = 0; i != 100; ++i) {
      root = std::filesystem::temp_directory_path() /
             ("meshvale-stl-files-" + std::to_string(seed) + "-" +
              std::to_string(i));
      if (std::filesystem::create_directory(root)) return;
    }
    throw std::runtime_error("fixture cannot create unique temporary root");
  }
  ~TempTree() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
inline std::map<std::string, std::vector<std::byte>> Inventory(
    const std::filesystem::path& root) {
  std::map<std::string, std::vector<std::byte>> result;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    Require(entry.is_regular_file() && !entry.is_symlink(),
            "flat regular inventory");
    const auto utf8 = entry.path().filename().u8string();
    const std::string name(reinterpret_cast<const char*>(utf8.data()),
                           utf8.size());
    result.emplace(name, Read(entry.path()));
  }
  return result;
}

// Independent SHA-256 equations from FIPS 180-4 sections 5.1.1, 5.3.3 and
// 6.2.2; no publisher/codec digest implementation or facts are consulted.
inline constexpr std::size_t kSha256DigestBytes = 32;
inline std::array<std::byte, kSha256DigestBytes> Sha256(
    std::span<const std::byte> input) {
  constexpr std::size_t kBlockBytes = 64, kLengthFieldBytes = 8, kWordBytes = 4;
  constexpr std::size_t kLengthFieldOffset = kBlockBytes - kLengthFieldBytes;
  constexpr std::size_t kInitialWords = kBlockBytes / kWordBytes,
                        kScheduleWords = 64;
  constexpr std::size_t kStateWords = 8,
                        kDigestBytes = kStateWords * kWordBytes;
  constexpr unsigned kOctetBits = 8, kOctetMask = 255;
  constexpr std::byte kPaddingMarker{0x80};
  constexpr std::size_t kSchedulePrevious = 16, kScheduleX = 15, kScheduleY = 2,
                        kScheduleAdd = 7;
  constexpr std::array<int, 3> kSmallSigmaX{7, 18, 3}, kSmallSigmaY{17, 19, 10};
  constexpr std::array<int, 3> kBigSigmaA{2, 13, 22}, kBigSigmaE{6, 11, 25};
  constexpr std::array<std::uint32_t, kScheduleWords> kRoundConstants{
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  constexpr std::array<std::uint32_t, kStateWords> kInitialState{
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  auto hash = kInitialState;
  std::vector<std::byte> padded(input.begin(), input.end());
  padded.push_back(kPaddingMarker);
  while (padded.size() % kBlockBytes != kLengthFieldOffset)
    padded.push_back(std::byte{0});
  const auto bits = static_cast<std::uint64_t>(input.size()) * kOctetBits;
  for (std::size_t i = kLengthFieldBytes; i != 0; --i)
    padded.push_back(std::byte((bits >> ((i - 1) * kOctetBits)) & kOctetMask));
  for (std::size_t block = 0; block < padded.size(); block += kBlockBytes) {
    std::array<std::uint32_t, kScheduleWords> words{};
    for (std::size_t i = 0; i != kInitialWords; ++i)
      for (std::size_t j = 0; j != kWordBytes; ++j)
        words[i] =
            (words[i] << kOctetBits) |
            std::to_integer<std::uint32_t>(padded[block + kWordBytes * i + j]);
    for (std::size_t i = kInitialWords; i != kScheduleWords; ++i) {
      const auto x = words[i - kScheduleX], y = words[i - kScheduleY];
      words[i] = words[i - kSchedulePrevious] +
                 (std::rotr(x, kSmallSigmaX[0]) ^
                  std::rotr(x, kSmallSigmaX[1]) ^ (x >> kSmallSigmaX[2])) +
                 words[i - kScheduleAdd] +
                 (std::rotr(y, kSmallSigmaY[0]) ^
                  std::rotr(y, kSmallSigmaY[1]) ^ (y >> kSmallSigmaY[2]));
    }
    auto state = hash;
    for (std::size_t i = 0; i != kScheduleWords; ++i) {
      const auto a = state[0], b = state[1], c = state[2], d = state[3];
      const auto e = state[4], f = state[5], g = state[6], h = state[7];
      const auto t1 =
          h +
          (std::rotr(e, kBigSigmaE[0]) ^ std::rotr(e, kBigSigmaE[1]) ^
           std::rotr(e, kBigSigmaE[2])) +
          ((e & f) ^ (~e & g)) + kRoundConstants[i] + words[i];
      const auto t2 =
          (std::rotr(a, kBigSigmaA[0]) ^ std::rotr(a, kBigSigmaA[1]) ^
           std::rotr(a, kBigSigmaA[2])) +
          ((a & b) ^ (a & c) ^ (b & c));
      state = {t1 + t2, a, b, c, d + t1, e, f, g};
    }
    for (std::size_t i = 0; i != hash.size(); ++i) hash[i] += state[i];
  }
  std::array<std::byte, kDigestBytes> result{};
  for (std::size_t i = 0; i != hash.size(); ++i)
    for (std::size_t j = 0; j != kWordBytes; ++j)
      result[kWordBytes * i + j] = std::byte(
          (hash[i] >> ((kWordBytes - 1 - j) * kOctetBits)) & kOctetMask);
  return result;
}
inline std::string Hex(std::span<const std::byte> bytes) {
  // Lowercase hexadecimal represents each octet as two four-bit nibbles.
  constexpr unsigned kNibbleBits = 4, kNibbleMask = 15;
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string result;
  for (auto byte : bytes) {
    const auto value = std::to_integer<unsigned>(byte);
    result += kHex[value >> kNibbleBits];
    result += kHex[value & kNibbleMask];
  }
  return result;
}
inline void HashKnownAnswers() {
  Require(
      Hex(Sha256({})) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "independent hash empty known answer");
  Require(
      Hex(Sha256(Bytes("abc"))) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "independent hash abc known answer");
}
// Receipt has objects and scalar leaves only. This independent strict parser
// records object nodes as well, so extra/empty objects and duplicate keys fail.
class ReceiptParser {
 public:
  explicit ReceiptParser(std::span<const std::byte> bytes)
      : text_(reinterpret_cast<const char*>(bytes.data()), bytes.size()) {}
  std::map<std::string, std::string> Parse() {
    Object("");
    Space();
    Require(offset_ == text_.size(), "receipt has no trailing JSON data");
    return values_;
  }

 private:
  // RFC 8259 JSON escapes use four hex digits. Unicode scalar/surrogate and
  // UTF-8 payload boundaries below are independent encoding parameters.
  static constexpr unsigned kHexRadix = 16, kAlphabeticHexOffset = 10;
  static constexpr unsigned kEscapeHexDigits = 4, kControlEnd = 32;
  static constexpr unsigned kHighSurrogateBegin = 0xd800,
                            kHighSurrogateEnd = 0xdbff;
  static constexpr unsigned kLowSurrogateBegin = 0xdc00,
                            kLowSurrogateEnd = 0xdfff;
  static constexpr unsigned kSupplementaryBegin = 0x10000,
                            kSurrogatePayloadBits = 10;
  static constexpr std::size_t kUnicodeEscapePrefixBytes = 2;
  static constexpr unsigned kAsciiEnd = 128, kUtf8TwoByteEnd = 2048,
                            kUtf8ThreeByteEnd = 65536;
  static constexpr unsigned kUtf8TwoByteTag = 0xc0, kUtf8ThreeByteTag = 0xe0,
                            kUtf8FourByteTag = 0xf0;
  static constexpr unsigned kUtf8ContinuationTag = 0x80, kUtf8PayloadMask = 63;
  static constexpr unsigned kUtf8PayloadBits = 6, kUtf8TwoPayloadShift = 12,
                            kUtf8ThreePayloadShift = 18;
  void Space() {
    while (offset_ < text_.size() &&
           (text_[offset_] == ' ' || text_[offset_] == '\n' ||
            text_[offset_] == '\r' || text_[offset_] == '\t'))
      ++offset_;
  }
  bool Take(char value) {
    Space();
    if (offset_ == text_.size() || text_[offset_] != value) return false;
    ++offset_;
    return true;
  }
  unsigned Hex4() {
    unsigned result = 0;
    for (unsigned i = 0; i != kEscapeHexDigits; ++i) {
      Require(offset_ < text_.size(), "complete Unicode escape");
      const char c = text_[offset_++];
      const unsigned digit =
          c >= '0' && c <= '9' ? static_cast<unsigned>(c - '0')
          : c >= 'a' && c <= 'f'
              ? static_cast<unsigned>(c - 'a') + kAlphabeticHexOffset
          : c >= 'A' && c <= 'F'
              ? static_cast<unsigned>(c - 'A') + kAlphabeticHexOffset
              : kHexRadix;
      Require(digit < kHexRadix, "hexadecimal Unicode escape");
      result = result * kHexRadix + digit;
    }
    return result;
  }
  std::string String() {
    Require(Take('"'), "JSON string begins");
    std::string result;
    while (offset_ < text_.size()) {
      const auto value = static_cast<unsigned char>(text_[offset_++]);
      if (value == '"') return result;
      Require(value >= kControlEnd, "JSON string has no raw controls");
      if (value != '\\') {
        result += static_cast<char>(value);
        continue;
      }
      Require(offset_ < text_.size(), "complete JSON escape");
      const char escaped = text_[offset_++];
      if (escaped == '"' || escaped == '\\' || escaped == '/')
        result += escaped;
      else if (escaped == 'b')
        result += '\b';
      else if (escaped == 'f')
        result += '\f';
      else if (escaped == 'n')
        result += '\n';
      else if (escaped == 'r')
        result += '\r';
      else if (escaped == 't')
        result += '\t';
      else {
        Require(escaped == 'u', "known JSON escape");
        unsigned scalar = Hex4();
        if (scalar >= kHighSurrogateBegin && scalar <= kHighSurrogateEnd) {
          Require(offset_ + kUnicodeEscapePrefixBytes <= text_.size() &&
                      text_.substr(offset_, kUnicodeEscapePrefixBytes) == "\\u",
                  "surrogate has second escape");
          offset_ += kUnicodeEscapePrefixBytes;
          const unsigned low = Hex4();
          Require(low >= kLowSurrogateBegin && low <= kLowSurrogateEnd,
                  "paired surrogate");
          scalar = kSupplementaryBegin +
                   ((scalar - kHighSurrogateBegin) << kSurrogatePayloadBits) +
                   low - kLowSurrogateBegin;
        } else
          Require(scalar < kLowSurrogateBegin || scalar > kLowSurrogateEnd,
                  "no lone surrogate");
        if (scalar < kAsciiEnd)
          result += static_cast<char>(scalar);
        else {
          if (scalar < kUtf8TwoByteEnd)
            result += static_cast<char>(kUtf8TwoByteTag |
                                        (scalar >> kUtf8PayloadBits));
          else {
            if (scalar >= kUtf8ThreeByteEnd) {
              result += static_cast<char>(kUtf8FourByteTag |
                                          (scalar >> kUtf8ThreePayloadShift));
              result += static_cast<char>(
                  kUtf8ContinuationTag |
                  ((scalar >> kUtf8TwoPayloadShift) & kUtf8PayloadMask));
            } else
              result += static_cast<char>(kUtf8ThreeByteTag |
                                          (scalar >> kUtf8TwoPayloadShift));
            result += static_cast<char>(
                kUtf8ContinuationTag |
                ((scalar >> kUtf8PayloadBits) & kUtf8PayloadMask));
          }
          result += static_cast<char>(kUtf8ContinuationTag |
                                      (scalar & kUtf8PayloadMask));
        }
      }
    }
    throw std::runtime_error("unterminated receipt string");
  }
  void Object(const std::string& path) {
    Require(Take('{'), "receipt object begins");
    Require(values_.emplace(path, "object").second, "unique object key");
    if (Take('}')) return;
    do {
      const auto key = String();
      Require(key.find('/') == std::string::npos,
              "closed receipt key has no slash");
      Require(Take(':'), "JSON key/value separator");
      const auto child = path + "/" + key;
      Space();
      Require(offset_ < text_.size(), "JSON value exists");
      if (text_[offset_] == '{')
        Object(child);
      else {
        std::string value;
        if (text_[offset_] == '"')
          value = "s:" + String();
        else {
          const auto start = offset_;
          while (offset_ < text_.size() && text_[offset_] != ',' &&
                 text_[offset_] != '}' && text_[offset_] != ' ' &&
                 text_[offset_] != '\r' && text_[offset_] != '\n' &&
                 text_[offset_] != '\t')
            ++offset_;
          const auto token = text_.substr(start, offset_ - start);
          if (token == "null" || token == "true" || token == "false")
            value = token;
          else {
            Require(!token.empty() &&
                        (token.size() == 1 || token.front() != '0') &&
                        std::ranges::all_of(
                            token, [](char c) { return c >= '0' && c <= '9'; }),
                    "receipt integer is canonical nonnegative JSON number");
            value = "n:" + std::string(token);
          }
        }
        Require(values_.emplace(child, value).second,
                "no duplicate receipt key");
      }
    } while (Take(','));
    Require(Take('}'), "receipt object ends");
  }
  std::string_view text_;
  std::size_t offset_ = 0;
  std::map<std::string, std::string> values_;
};
inline void CheckReceipt(const StlPublishedBundle& bundle,
                         StlFileMetadataMode requested,
                         StlMetadataPolicy actual, std::size_t facets,
                         std::optional<std::string_view> report = {},
                         std::optional<std::string_view> unit = {}) {
  const auto& verified = bundle.Verified();
  const auto& source = verified.Source();
  const auto& document = verified.Document();
  std::map<std::string, std::string> expected{
      {"", "object"},
      {"/source", "object"},
      {"/payload", "object"},
      {"/counts", "object"},
      {"/metadata", "object"},
      {"/units", "object"},
      {"/fidelity", "object"},
      {"/schema", "s:meshvale.stl-publication/1"},
      {"/profile", "s:codec-preserving"},
      {"/verification", "s:staged-reread-and-fresh-codec-verified"},
      {"/counts/facets", "n:" + std::to_string(facets)},
      {"/counts/vertices", "n:" + std::to_string(facets * 3)},
      {"/counts/corners", "n:" + std::to_string(facets * 3)},
      {"/metadata/requested",
       requested == StlFileMetadataMode::kRetain ? "s:retain" : "s:replace"},
      {"/metadata/actual", actual == StlMetadataPolicy::kRetainedSourceMetadata
                               ? "s:retained_source_metadata"
                               : "s:replaced_source_metadata"},
      {"/metadata/ascii_name", document.Encoding() == StlEncoding::kAscii
                                   ? "s:" + std::string(document.AsciiName())
                                   : "null"},
      {"/metadata/binary_header_hex",
       document.Encoding() == StlEncoding::kBinary
           ? "s:" + Hex(document.BinaryHeader())
           : "null"},
      {"/units/kind", unit ? "s:label" : "s:unknown"},
      {"/units/label", unit ? "s:" + std::string(*unit) : "null"},
      {"/units/rescaled", "false"},
      {"/caller_report", report ? "s:" + std::string(*report) : "null"}};
  for (auto field : {"position_bits", "raw_normal_bits", "facet_order",
                     "corner_order", "soup_identity", "exact_bytes"})
    expected.emplace("/fidelity/" + std::string(field), "true");
  const auto add_payload = [&](std::string prefix, StlEncoding encoding,
                               std::string_view entry,
                               std::span<const std::byte> bytes) {
    expected.emplace(prefix + "/encoding",
                     encoding == StlEncoding::kAscii ? "s:ascii" : "s:binary");
    expected.emplace(prefix + "/entry", "s:" + std::string(entry));
    expected.emplace(prefix + "/bytes", "n:" + std::to_string(bytes.size()));
    expected.emplace(prefix + "/sha256", "s:" + Hex(Sha256(bytes)));
  };
  add_payload("/source", source.Document().Encoding(), source.EntryPath(),
              source.EntryBytes());
  add_payload("/payload", document.Encoding(), verified.EntryPath(),
              verified.EntryBytes());
  Require(ReceiptParser(bundle.ReceiptBytes()).Parse() == expected,
          "complete neutral receipt keys/types/values, no final "
          "commit/self-hash claim");
}
inline void Published(const StlFileRewriteResult& result) {
  Require(result.status == StlFileStatus::kSuccess && result.output &&
              result.fresh_document && result.verified && result.bundle,
          "all genuine success owners");
  Require(result.cleanup == StlFileCleanupOutcome::kNotRequired &&
              result.observations.publication.state ==
                  StlFileObservationState::kSucceeded &&
              std::get<StlFilePublicationFacts>(
                  result.observations.publication.facts)
                  .published,
          "actual native commit observation");
}
inline bool Unsupported(const StlFileReadResult& result, bool expected) {
  if (!expected) {
    Require(result.status == StlFileStatus::kSuccess && result.asset,
            "qualified real nonempty codec decode");
    return false;
  }
  Require(
      result.status == StlFileStatus::kStlFailure && !result.asset &&
          result.captured &&
          result.diagnostic.stl_status == StlStatus::kGeometryFailure &&
          result.diagnostic.stl_diagnostic &&
          result.diagnostic.stl_diagnostic->geometry_status ==
              geometry::MaterializationStatus::kUnsupportedAllocationProfile,
      "actual nonempty Geometry unsupported-profile refusal");
  return true;
}
}  // namespace meshvale::interchange::stl_files_fixture
#endif  // MESHVALE_INTERCHANGE_TESTS_STL_FILES_FIXTURE_H_
