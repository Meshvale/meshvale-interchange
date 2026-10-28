// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_STL_FILES_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_STL_FILES_INTERNAL_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/stl_files.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::stl_files_detail {
namespace allocation = gltf_detail;
using String = std::basic_string<char, std::char_traits<char>,
                                 allocation::Allocator<char>>;
#ifdef _WIN32
using NativeChar = wchar_t;
inline constexpr NativeChar kSeparator = L'\\';
inline constexpr std::size_t kDriveRootUnits = 3;
#else
using NativeChar = char;
inline constexpr NativeChar kSeparator = '/';
#endif
using NativeView = std::basic_string_view<NativeChar>;
using NativeString = std::basic_string<NativeChar, std::char_traits<NativeChar>,
                                       allocation::Allocator<NativeChar>>;
struct Failure {
  StlFileDiagnostic diagnostic;
};
[[noreturn]] void Fail(StlFileStatus status, StlFilePhase stage,
                       StlFileDiagnosticCode code,
                       std::optional<std::uint64_t> file = {},
                       std::optional<std::uint64_t> offset = {},
                       std::optional<std::error_code> error = {});
void Count(std::uint64_t value, std::uint64_t maximum,
           StlFilePhase stage = StlFilePhase::kPathPlanning);
// Empty destination capacity is reserved first. No implicit character-storage
// relocation or value initialization hides an unbounded Meshvale copy loop.
template <class Char>
void Assign(std::basic_string<Char, std::char_traits<Char>,
                              allocation::Allocator<Char>>& output,
            std::basic_string_view<Char> input,
            const allocation::AllocationContext& allocator) {
  output.clear();
  output.reserve(input.size());
  for (auto value : input) {
    allocator.Checkpoint();
    output.push_back(value);
  }
}
template <class Char>
void Extend(std::basic_string<Char, std::char_traits<Char>,
                              allocation::Allocator<Char>>& output,
            std::basic_string_view<Char> input,
            const allocation::AllocationContext& allocator) {
  // Callers pre-reserve complete final capacity before populating a path.
  for (auto value : input) {
    allocator.Checkpoint();
    output.push_back(value);
  }
}
struct Buffer {
  std::byte* data = nullptr;
  std::size_t size = 0;
  Buffer() noexcept = default;
  Buffer(const allocation::AllocationContext& allocator, std::size_t count)
      : data(count ? static_cast<std::byte*>(
                         allocator.Allocate(count, alignof(std::byte)))
                   : nullptr),
        size(count) {}
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&& other) noexcept
      : data(std::exchange(other.data, nullptr)),
        size(std::exchange(other.size, 0)) {}
  ~Buffer() noexcept {
    if (data) allocation::AllocationContext::Deallocate(data);
  }
  std::span<std::byte> Bytes() noexcept { return {data, size}; }
  std::span<const std::byte> Bytes() const noexcept { return {data, size}; }
};
struct FileHooks {
  void (*before_open)(std::uint64_t, void*) = nullptr;
  void (*opened)(std::uint64_t, void*) = nullptr;
  void (*closed)(std::uint64_t, void*) noexcept = nullptr;
  void (*before_read)(std::uint64_t, std::uint64_t, void*) = nullptr;
  void (*before_write)(std::uint64_t, std::uint64_t, void*) = nullptr;
  void* state = nullptr;
};
struct TestOptions {
  allocation::AllocationBackend allocation_backend =
      allocation::DefaultAllocationBackend();
  allocation::NativeExecutionOptions workers;
  FileHooks files;
  void (*checkpoint)(StlFilePhase, std::uint64_t, void*) = nullptr;
  void (*stage_ready)(NativeView, void*) = nullptr;
  void (*before_inventory)(NativeView, void*) = nullptr;
  void (*before_commit)(NativeView, void*) = nullptr;
  // Private actual-success witness only, never a provider or production hook.
  // Must allocate nothing. It may request caller stop, but commit remains true
  // and no stop check or other fallible operation follows this observation.
  void (*native_committed)(void*) noexcept = nullptr;
  std::error_code (*close_error)(std::uint64_t, void*) = nullptr;
  std::error_code (*commit_error)(void*) = nullptr;
  std::error_code (*cleanup_error)(NativeView, void*) noexcept = nullptr;
  void (*cleanup_finished)(StlFileCleanupOutcome, std::error_code,
                           void*) noexcept = nullptr;
  void* state = nullptr;
};
struct NativeMetadata {
  std::uint64_t volume = 0, identity = 0, size = 0;
  std::int64_t modified_seconds = 0, modified_fraction = 0;
  bool operator==(const NativeMetadata&) const = default;
};
inline constexpr std::intptr_t kInvalidFileHandle = -1;
struct NativeFile {
  std::intptr_t value = kInvalidFileHandle;
  std::uint64_t ordinal = 0;
  StlFilePhase stage = StlFilePhase::kCapture;
  FileHooks hooks;
  NativeFile() noexcept = default;
  NativeFile(const NativeFile&) = delete;
  NativeFile& operator=(const NativeFile&) = delete;
  NativeFile(NativeFile&& other) noexcept;
  NativeFile& operator=(NativeFile&& other) noexcept;
  ~NativeFile() noexcept;
  void Close() noexcept;
};
NativeString AbsolutePath(NativeView input,
                          const allocation::AllocationContext& allocator,
                          const StlFileLimits& limits);
std::size_t LastSeparator(NativeView path,
                          const allocation::AllocationContext& allocator);
std::size_t TerminatedLength(std::span<const NativeChar> storage,
                             const allocation::AllocationContext& allocator,
                             StlFilePhase stage);
NativeString JoinPath(NativeView root, std::string_view name,
                      const allocation::AllocationContext& allocator,
                      const StlFileLimits& limits);
String EntryName(NativeView path,
                 const allocation::AllocationContext& allocator,
                 const StlFileLimits& limits);
void PortableName(std::string_view name, std::uint64_t cap,
                  const allocation::AllocationContext& allocator);
bool EqualFold(std::string_view first, std::string_view second,
               const allocation::AllocationContext* allocator = nullptr);
void RejectAliases(NativeView path, bool directory,
                   const allocation::AllocationContext& allocator,
                   const StlFileLimits& limits,
                   StlFilePhase stage = StlFilePhase::kPathPlanning);
NativeFile OpenFile(NativeView path, std::uint64_t ordinal,
                    const FileHooks& hooks, bool directory = false,
                    StlFilePhase stage = StlFilePhase::kCapture);
NativeMetadata Metadata(const NativeFile& file, bool directory = false);
NativeString CanonicalPath(const NativeFile& file,
                           const allocation::AllocationContext& allocator,
                           const StlFileLimits& limits);
void ReadBlock(const NativeFile& file, std::uint64_t offset,
               std::span<std::byte> bytes);
void RequireEnd(const NativeFile& file, std::uint64_t offset);
std::uint64_t MaximumReadBlockBytes() noexcept;
void CloseChecked(NativeFile& file, const TestOptions& testing,
                  StlFilePhase stage);
void WriteFile(NativeView path, std::span<const std::byte> bytes,
               std::uint64_t ordinal, const StlFileWorkPolicy& work,
               const allocation::AllocationContext& allocator,
               const TestOptions& testing,
               std::optional<NativeMetadata>* owned_identity = nullptr,
               StlFilePhase phase = StlFilePhase::kStaging);

// Exactly two selected flat files: no recursive path framework. All paths and
// enumeration scratch are prepared before stage creation. Cleanup does no
// Meshvale allocation and never observes cancellation.
class NativePublication {
 public:
  NativePublication(const allocation::AllocationContext&, const StlFileLimits&,
                    const TestOptions&);
  NativePublication(const NativePublication&) = delete;
  NativePublication& operator=(const NativePublication&) = delete;
  ~NativePublication() noexcept;
  void Plan(NativeView destination, std::string_view entry,
            std::optional<std::string_view> receipt);
  void Create();
  void Inventory(bool receipt_present, std::uint64_t entry_bytes,
                 std::uint64_t receipt_bytes);
  void Commit();
  StlFileCleanupOutcome Cleanup(std::error_code&) noexcept;
  NativeView Entry() const noexcept { return entry_; }
  NativeView Receipt() const noexcept { return receipt_; }
  NativeView Destination() const noexcept { return destination_; }
  NativeView Parent() const noexcept { return parent_; }
  std::optional<NativeMetadata>& MemberIdentity(std::size_t index) noexcept {
    return member_identity_[index];
  }

 private:
  allocation::AllocationContext allocation_;
  const StlFileLimits& limits_;
  const TestOptions& testing_;
  NativeString destination_, parent_, logical_parent_, stage_, entry_, receipt_,
      wildcard_;
  NativeMetadata identity_, parent_identity_;
  std::array<std::optional<NativeMetadata>, 2> member_identity_;
  bool owned_ = false;
};
// Borrow native ranges synchronously; the same operation cores copy retained
// logical paths with their existing charged allocators. No handle is retained.
StlFileCaptureResult CaptureNative(NativeView, const StlFileLimits&,
                                   const StlFileWorkPolicy&,
                                   const geometry::ExecutionContext&,
                                   std::stop_token, const TestOptions&);
StlFileReadResult ReadNative(NativeView, StlEncoding, const StlFileReadLimits&,
                             const StlFileReadWorkPolicy&,
                             const geometry::ExecutionContext&, std::stop_token,
                             const TestOptions&);
StlFileRewriteResult RewriteNative(const StlFileAsset&, NativeView,
                                   const StlFileRewriteOptions&,
                                   const StlFileRewriteLimits&,
                                   const StlFileRewriteWorkPolicy&,
                                   std::stop_token, const TestOptions&);
StlFileCaptureResult CaptureForTesting(const std::filesystem::path&,
                                       const StlFileLimits&,
                                       const StlFileWorkPolicy&,
                                       const geometry::ExecutionContext&,
                                       std::stop_token, const TestOptions&);
StlFileReadResult ReadForTesting(const std::filesystem::path&, StlEncoding,
                                 const StlFileReadLimits&,
                                 const StlFileReadWorkPolicy&,
                                 const geometry::ExecutionContext&,
                                 std::stop_token, const TestOptions&);
StlFileRewriteResult RewriteForTesting(const StlFileAsset&,
                                       const std::filesystem::path&,
                                       const StlFileRewriteOptions&,
                                       const StlFileRewriteLimits&,
                                       const StlFileRewriteWorkPolicy&,
                                       std::stop_token, const TestOptions&);
}  // namespace meshvale::interchange::stl_files_detail
#endif  // MESHVALE_INTERCHANGE_SRC_STL_FILES_INTERNAL_H_
