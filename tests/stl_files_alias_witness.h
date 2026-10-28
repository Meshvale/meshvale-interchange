// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_STL_FILES_ALIAS_WITNESS_H_
#define MESHVALE_INTERCHANGE_TESTS_STL_FILES_ALIAS_WITNESS_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/stl_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#error The required STL alias gate is Windows-only.
#endif

#include "stl_files_fixture.h"

namespace meshvale::interchange::stl_files_alias_fixture {
namespace fs = std::filesystem;
namespace base = stl_files_fixture;
namespace geo = geometry;
using base::Require;
// This fixture protocol is TSV; path/string payloads use UTF-8 hexadecimal so
// whitespace, Unicode and arbitrary OS diagnostics cannot add records.
inline constexpr std::string_view kProtocol = "STL_ALIAS_1";
namespace record {
inline constexpr std::string_view kPlanned = "PLANNED", kReached = "REACHED",
                                  kCase = "CASE", kMode = "MODE",
                                  kWitness = "WITNESS", kPath = "PATH",
                                  kSummary = "SUMMARY", kFail = "FAIL",
                                  kResult = "RESULT", kWork = "WORK",
                                  kBytes = "BYTES", kClosure = "CLOSURE",
                                  kDistinct = "DISTINCT", kHook = "HOOK";
inline constexpr std::string_view kPass = "PASS";
inline constexpr std::string_view kPathRole = "path",
                                  kParentRole = "parent-alias",
                                  kLeafRole = "source-leaf-alias",
                                  kMixedRole = "mixed-component",
                                  kReparseRole = "reparse-traversal";
inline constexpr std::string_view kClosureScope = "GENERIC_READ_ONLY";
inline constexpr std::string_view kMissingWitness = "MISSED_WITNESS",
                                  kAssertion = "ASSERTION_OR_EXCEPTION";
}  // namespace record
inline bool observations_are_trace = false;
inline std::ostream& RecordHead(std::string_view kind) {
  return std::cout << kProtocol << '\t' << kind << '\t';
}
inline constexpr std::string_view kRequiredArgument =
    "--require-windows-aliases";
inline constexpr std::size_t kNativeCases = 29;
inline constexpr DWORD kPathCapacityUnits = 32768;
inline constexpr DWORD kSharedAccess =
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
inline constexpr std::u8string_view kSourceLeaf =
    u8"Alias original \u90e8\u54c1 source.stl";
inline constexpr std::string_view kSentinel = "replacement parent sentinel\n";
inline constexpr std::array<std::string_view, kNativeCases> kCaseIds{
    "N01.1", "N01.2", "N01.3", "N01.4", "N02.1", "N02.2", "N03.1", "N03.2",
    "N04.1", "N04.2", "N04.3", "N04.4", "N05.1", "N05.2", "N05.3", "N05.4",
    "N05.5", "N05.6", "N05.7", "N05.8", "N06.1", "N06.2", "N07.1", "N08.1",
    "N09.1", "N10.1", "N10.2", "T01.1", "T01.2"};
class WitnessFailure : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};
inline void Witness(bool value, std::string_view message) {
  if (!value) throw WitnessFailure(std::string(message));
}
[[noreturn]] inline void OsFailure(std::string_view operation, DWORD error) {
  throw WitnessFailure(std::string(operation) +
                       " Win32=" + std::to_string(error));
}
inline std::string Utf8(const fs::path& path) {
  const auto text = path.u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}
inline std::string TextHex(std::string_view value) {
  return base::Hex(base::Bytes(value));
}
inline std::wstring Key(const fs::path& path) {
  auto result = path.lexically_normal().native();
  for (auto& c : result) {
    if (c == L'/') c = L'\\';
    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + (L'a' - L'A'));
  }
  return result;
}
class Handle {
 public:
  explicit Handle(HANDLE value) noexcept : value_(value) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() noexcept {
    if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
  }
  HANDLE Get() const noexcept { return value_; }
  void Close() {
    const auto value = std::exchange(value_, INVALID_HANDLE_VALUE);
    if (!CloseHandle(value)) OsFailure("observer CloseHandle", GetLastError());
  }

 private:
  HANDLE value_;
};
struct Identity {
  std::uint64_t volume, file, bytes;
  DWORD attributes;
  bool operator==(const Identity&) const = default;
};
inline Identity Information(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(handle, &info))
    OsFailure("GetFileInformationByHandle", GetLastError());
  constexpr unsigned kDwordBits = 32;
  return {
      info.dwVolumeSerialNumber,
      (std::uint64_t{info.nFileIndexHigh} << kDwordBits) | info.nFileIndexLow,
      (std::uint64_t{info.nFileSizeHigh} << kDwordBits) | info.nFileSizeLow,
      info.dwFileAttributes};
}
inline Identity Observe(const fs::path& path,
                        std::string_view role = record::kPathRole) {
  Handle handle(CreateFileW(path.c_str(), GENERIC_READ, kSharedAccess, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                            nullptr));
  if (handle.Get() == INVALID_HANDLE_VALUE)
    OsFailure("observer CreateFileW", GetLastError());
  const auto info = Information(handle.Get());
  std::array<wchar_t, kPathCapacityUnits> final{};
  const auto length =
      GetFinalPathNameByHandleW(handle.Get(), final.data(), kPathCapacityUnits,
                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!length) OsFailure("GetFinalPathNameByHandleW", GetLastError());
  Witness(length < kPathCapacityUnits, "MISSED_WITNESS final path capacity");
  handle.Close();
  if (observations_are_trace)
    RecordHead(record::kPath);
  else
    RecordHead(record::kWitness) << role << '\t' << record::kPass << '\t';
  std::cout << TextHex(Utf8(path)) << '\t'
            << TextHex(Utf8(fs::path(final.data()))) << '\t' << info.volume
            << '\t' << info.file << '\t' << info.bytes << '\t'
            << info.attributes << '\n';
  return info;
}
inline void OrdinaryPrefixes(const fs::path& path) {
  auto prefix = path.root_path();
  for (const auto& component : path.relative_path()) {
    prefix /= component;
    const auto attributes = GetFileAttributesW(prefix.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
      OsFailure("GetFileAttributesW", GetLastError());
    Witness((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
            "MISSED_WITNESS original prefix is reparse");
  }
}
inline fs::path Spelling(const fs::path& input, bool short_name) {
  std::array<wchar_t, kPathCapacityUnits> buffer{};
  const auto size =
      short_name
          ? GetShortPathNameW(input.c_str(), buffer.data(), kPathCapacityUnits)
          : GetLongPathNameW(input.c_str(), buffer.data(), kPathCapacityUnits);
  if (!size)
    OsFailure(short_name ? "GetShortPathNameW" : "GetLongPathNameW",
              GetLastError());
  Witness(size < kPathCapacityUnits, "MISSED_WITNESS spelling capacity");
  fs::path result(buffer.data());
  OrdinaryPrefixes(result);
  Witness(Observe(input) == Observe(result),
          "MISSED_WITNESS spelling identity/extent changed");
  return result;
}
inline void Closure(const fs::path& input, const Identity& identity) {
  // Every fixture observer is checked-closed before this positive control.
  Handle control(CreateFileW(input.c_str(), GENERIC_READ, kSharedAccess,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr));
  if (control.Get() == INVALID_HANDLE_VALUE)
    OsFailure("closure control open", GetLastError());
  Witness(Information(control.Get()) == identity, "closure control identity");
  Handle blocked(CreateFileW(input.c_str(), GENERIC_READ, 0, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  const auto blocked_error =
      blocked.Get() == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
  Witness(blocked.Get() == INVALID_HANDLE_VALUE &&
              blocked_error == ERROR_SHARING_VIOLATION,
          "closure positive control must share-conflict");
  control.Close();
  Handle exclusive(CreateFileW(input.c_str(), GENERIC_READ, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (exclusive.Get() == INVALID_HANDLE_VALUE)
    OsFailure("closure nonsharing open with retained owner", GetLastError());
  Witness(Information(exclusive.Get()) == identity &&
              (identity.attributes &
               (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0,
          "closure observation retains regular same identity");
  exclusive.Close();
  RecordHead(record::kClosure)
      << TextHex(Utf8(input)) << '\t' << record::kClosureScope << '\t'
      << record::kPass << '\n';
}
struct Source {
  fs::path parent_long, parent_short, long_path, short_path;
  Identity identity;
  Source(const fs::path& root, std::string_view id,
         std::span<const std::byte> bytes) {
    const auto parent = root / ("Alias source parent " + std::string(id));
    Witness(fs::create_directory(parent), "exclusive source directory");
    base::Write(parent / fs::path(kSourceLeaf), bytes);
    parent_long = Spelling(parent, false);
    parent_short = Spelling(parent, true);
    long_path = Spelling(parent / fs::path(kSourceLeaf), false);
    short_path = Spelling(long_path, true);
    identity = Observe(long_path);
    Witness(Key(parent_long) != Key(parent_short),
            "MISSED_WITNESS differing source parent alias");
    Witness(Key(long_path.filename()) != Key(short_path.filename()),
            "MISSED_WITNESS differing source leaf alias");
    for (unsigned variant = 0; variant != 4; ++variant) {
      const auto path = Path(variant);
      OrdinaryPrefixes(path);
      Witness(Observe(path) == identity &&
                  base::Read(path) ==
                      std::vector<std::byte>(bytes.begin(), bytes.end()),
              "MISSED_WITNESS four real same-file alias combinations");
    }
  }
  fs::path Path(unsigned variant) const {
    return (variant < 2 ? parent_long : parent_short) /
           (variant % 2 == 0 ? long_path.filename() : short_path.filename());
  }
  void Remove(const fs::path& submitted) const {
    Closure(submitted, identity);
    Witness(fs::remove(long_path), "remove checked-closed source");
    Witness(!fs::exists(long_path) && !fs::exists(short_path),
            "source absent through recorded long and short spellings");
  }
};
class Tree {
 public:
  fs::path root, grand_long, grand_short, link;
  explicit Tree() {
    const auto seed =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::temp_directory_path() /
           ("Meshvale alias gate long root " + std::to_string(seed));
    Witness(fs::create_directory(root), "exclusive actual TEMP root");
    try {
      const auto grand = root / "Alias distinguishing grandparent directory";
      Witness(fs::create_directory(grand), "exclusive grandparent directory");
      grand_long = Spelling(grand, false);
      grand_short = Spelling(grand, true);
      Witness(Key(grand_long) != Key(grand_short),
              "MISSED_DISTINGUISHING_WITNESS grandparent alias");
      Source first(grand_long, "witness", base::Bytes(base::kAscii));
      const auto mixed = grand_long / first.parent_short.filename();
      Witness(Observe(mixed) == Observe(first.parent_long),
              "MISSED_DISTINGUISHING_WITNESS mixed identity");
      for (const auto& whole : {first.long_path.parent_path(),
                                Spelling(first.long_path, false).parent_path(),
                                first.short_path.parent_path()})
        Witness(Key(mixed) != Key(whole),
                "MISSED_DISTINGUISHING_WITNESS mixed equals whole prefix");
      link = grand_long / "observed directory link";
      if (!CreateSymbolicLinkW(
              link.c_str(), first.parent_long.c_str(),
              SYMBOLIC_LINK_FLAG_DIRECTORY |
                  SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        OsFailure("required real directory symlink", GetLastError());
      const auto attributes = GetFileAttributesW(link.c_str());
      Witness(attributes != INVALID_FILE_ATTRIBUTES &&
                  (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                  Observe(link / first.long_path.filename()) == first.identity,
              "MISSED_WITNESS reparse target identity");
      (void)Observe(grand_short, record::kParentRole);
      (void)Observe(first.short_path, record::kLeafRole);
      (void)Observe(mixed, record::kMixedRole);
      (void)Observe(link / first.long_path.filename(), record::kReparseRole);
      reparse_source_ = first.long_path;
    } catch (...) {
      Cleanup();
      throw;
    }
  }
  Tree(const Tree&) = delete;
  Tree& operator=(const Tree&) = delete;
  ~Tree() noexcept { Cleanup(); }
  void Close() {
    Witness(fs::remove(link), "owned reparse witness cleanup");
    link.clear();
    Witness(fs::remove_all(root) > 0 && !fs::exists(root),
            "exclusive owned gate root cleanup");
    root.clear();
  }
  const fs::path& ReparseSource() const noexcept { return reparse_source_; }

 private:
  void Cleanup() noexcept {
    try {
      std::error_code ignored;
      if (!link.empty()) fs::remove(link, ignored);
      if (!root.empty()) fs::remove_all(root, ignored);
    } catch (...) {
      // Even error-code filesystem overloads may allocate. Preserve the first
      // constructor/assertion failure during this best-effort fallback.
    }
  }
  fs::path reparse_source_;
};
class Inventory {
 public:
  Inventory() {
    for (auto id : kCaseIds) RecordHead(record::kPlanned) << id << '\n';
  }
  void Begin(std::string_view id) {
    Require(next_ < kCaseIds.size() && id == kCaseIds[next_],
            "exact required gate case order");
    observations_are_trace = true;
    RecordHead(record::kReached) << id << '\n';
  }
  void Pass(std::string_view id) {
    Require(id == kCaseIds[next_], "required case cannot silently disappear");
    ++next_;
    RecordHead(record::kCase) << id << '\t' << record::kPass << '\n';
  }
  void Complete() const {
    Require(next_ == kNativeCases, "all required native cases reached");
    RecordHead(record::kSummary)
        << record::kPass << '\t' << next_ << '\t' << kNativeCases << '\n';
  }

 private:
  std::size_t next_ = 0;
};
struct HookCounters {
  std::atomic<std::uint64_t> opened{0}, closed{0};
};
#ifdef MESHVALE_STL_ALIAS_INTERNAL_PROBE
StlFileCaptureResult CaptureObserved(const fs::path&,
                                     const geo::ExecutionContext&,
                                     HookCounters&);
StlFileReadResult ReadObserved(const fs::path&, const geo::ExecutionContext&,
                               HookCounters&);
#endif
inline void CheckHooks(const HookCounters& counters, std::string_view id) {
#ifdef MESHVALE_STL_ALIAS_INTERNAL_PROBE
  Require(counters.opened.load() > 0 &&
              counters.opened.load() == counters.closed.load(),
          "actual source-test opened/closed callbacks balanced at return");
  RecordHead(record::kHook) << id << '\t' << counters.opened.load() << '\t'
                            << counters.closed.load() << '\n';
#else
  (void)counters;
  (void)id;
#endif
}
}  // namespace meshvale::interchange::stl_files_alias_fixture
#endif  // MESHVALE_INTERCHANGE_TESTS_STL_FILES_ALIAS_WITNESS_H_
