// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_SURFACE_FILES_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_SURFACE_FILES_INTERNAL_H_
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/surface_files.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "surface_files_test_internal.h"
#ifndef _WIN32
#include <pthread.h>
#endif
namespace meshvale::interchange::surface_files_detail {
namespace allocation = gltf_detail;
template <class T>
using Vector = std::vector<T, allocation::Allocator<T>>;
using String = std::basic_string<char, std::char_traits<char>,
                                 allocation::Allocator<char>>;
using NativeString = std::basic_string<NativeChar, std::char_traits<NativeChar>,
                                       allocation::Allocator<NativeChar>>;
#ifdef _WIN32
inline constexpr NativeChar kSeparator = L'\\';
inline constexpr std::size_t kDriveRootUnits = 3;
#else
inline constexpr NativeChar kSeparator = '/';
#endif
struct Failure {
  SurfaceFileDiagnostic diagnostic;
  std::uint64_t read_bytes = 0, written_bytes = 0;
};
[[noreturn]] void Fail(SurfaceFileStatus, SurfaceFileStage, SurfaceFileCode,
                       std::optional<std::uint64_t> = {},
                       std::optional<std::uint64_t> = {},
                       std::optional<std::error_code> = {});
void Count(std::uint64_t, std::uint64_t,
           SurfaceFileStage = SurfaceFileStage::kPathPlanning);
std::size_t Host(std::uint64_t);
std::uint64_t Sum(std::uint64_t, std::uint64_t);
void PortablePath(std::string_view, std::uint64_t);
bool ValidNativeEncoding(NativeView) noexcept;
std::string_view EntryPath(SurfaceEncoding);
inline constexpr std::uint64_t kMaximumJsonEscapeBytes = 6;
bool EqualFold(std::string_view, std::string_view,
               const allocation::AllocationContext* = nullptr);
template <class Char>
void Assign(std::basic_string<Char, std::char_traits<Char>,
                              allocation::Allocator<Char>>& out,
            std::basic_string_view<Char> in,
            const allocation::AllocationContext& a) {
  out.clear();
  out.reserve(in.size());
  for (auto c : in) {
    a.Checkpoint();
    out.push_back(c);
  }
}
template <class Char>
void Extend(std::basic_string<Char, std::char_traits<Char>,
                              allocation::Allocator<Char>>& out,
            std::basic_string_view<Char> in,
            const allocation::AllocationContext& a) {
  for (auto c : in) {
    a.Checkpoint();
    out.push_back(c);
  }
}
struct NativeMetadata {
  std::uint64_t volume = 0, identity = 0, size = 0;
  std::int64_t modified_seconds = 0, modified_fraction = 0;
  bool operator==(const NativeMetadata&) const = default;
};
inline constexpr std::intptr_t kInvalidFileHandle = -1;
struct NativeFile {
  std::intptr_t value = kInvalidFileHandle;
  std::uint64_t ordinal = 0;
  SurfaceFileStage stage = SurfaceFileStage::kReread;
  FileHooks hooks;
  NativeFile() noexcept = default;
  NativeFile(const NativeFile&) = delete;
  NativeFile& operator=(const NativeFile&) = delete;
  NativeFile(NativeFile&&) noexcept;
  NativeFile& operator=(NativeFile&&) noexcept;
  ~NativeFile() noexcept;
  void Close() noexcept;
};
NativeString AbsolutePath(NativeView, const allocation::AllocationContext&,
                          const SurfaceFileLimits&);
std::size_t LastSeparator(NativeView, const allocation::AllocationContext&);
std::size_t TerminatedLength(std::span<const NativeChar>,
                             const allocation::AllocationContext&,
                             SurfaceFileStage);
NativeString JoinPath(NativeView, std::string_view,
                      const allocation::AllocationContext&,
                      const SurfaceFileLimits&);
String EntryName(NativeView, const allocation::AllocationContext&,
                 const SurfaceFileLimits&);
void PortableName(std::string_view, std::uint64_t,
                  const allocation::AllocationContext&);
void RejectAliases(NativeView, bool, const allocation::AllocationContext&,
                   const SurfaceFileLimits&,
                   SurfaceFileStage = SurfaceFileStage::kPathPlanning);
NativeFile OpenFile(NativeView, std::uint64_t, const FileHooks&, bool = false,
                    SurfaceFileStage = SurfaceFileStage::kReread);
NativeFile OpenWrite(NativeView, std::uint64_t, const FileHooks&,
                     SurfaceFileStage, bool exclusive = true);
NativeMetadata Metadata(const NativeFile&, bool = false);
NativeString CanonicalPath(const NativeFile&,
                           const allocation::AllocationContext&,
                           const SurfaceFileLimits&);
std::uint64_t MaximumReadBlockBytes() noexcept;
void ReadBlock(const NativeFile&, std::uint64_t, std::span<std::byte>);
void WriteBlock(const NativeFile&, std::uint64_t, std::span<const std::byte>);
void RequireEnd(const NativeFile&, std::uint64_t);
void CloseChecked(NativeFile&);
struct NativeGate {
  NativeGate() noexcept = default;
  NativeGate(const NativeGate&) = delete;
  NativeGate& operator=(const NativeGate&) = delete;
  ~NativeGate() noexcept;
  void Initialize();
  void Release(bool abort) noexcept;
  bool Wait() noexcept;
  void Close() noexcept;
#ifdef _WIN32
  std::intptr_t event = 0;
#else
  pthread_mutex_t mutex{};
  pthread_cond_t condition{};
#endif
  bool initialized = false;
  bool released = false;
  std::atomic<bool> aborted{false};
};
void NativeYield() noexcept;
struct WalkResult {
  std::error_code error;
  SurfaceFileStatus status = SurfaceFileStatus::kIoFailure;
  SurfaceFileCode code = SurfaceFileCode::kEnumeration;
};
struct WalkFrame;
struct WalkEntry;
using InventoryVisitor = bool (*)(NativeView, bool, void*) noexcept;
class NativePublication {
 public:
  NativePublication(const allocation::AllocationContext&,
                    const SurfaceFileLimits&, TestOptions&, std::stop_token);
  NativePublication(const NativePublication&) = delete;
  NativePublication& operator=(const NativePublication&) = delete;
  ~NativePublication() noexcept;
  void Plan(NativeView);
  void AdmitParent();
  void CheckChildExtent(std::size_t);
  void CreateStage();
  void CreateSubdirectory(NativeView);
  void Inventory(InventoryVisitor, void*, bool receipt_present,
                 bool final = false);
  void Commit(SurfaceFileCommitObservation&, std::error_code);
  SurfaceFileCleanupOutcome Cleanup(std::error_code&) noexcept;
  NativeView Stage() const noexcept { return stage_; }
  void Scrub() noexcept;

 private:
  WalkResult Walk(bool, InventoryVisitor, void*) noexcept;
  allocation::AllocationContext allocation_;
  const SurfaceFileLimits& limits_;
  TestOptions& hooks_;
  NativeString destination_, parent_, stage_, scratch_;
  Vector<WalkFrame> frames_;
  Vector<WalkEntry> entries_;
  Vector<NativeFile> root_handles_;
  NativeMetadata identity_;
  bool owned_ = false;
  std::stop_token stop_;
};
// Uninitialized charged bytes: no hidden serial value-initialization pass.
class Buffer {
 public:
  explicit Buffer(const allocation::AllocationContext& a) : allocation_(a) {}
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&& o) noexcept
      : allocation_(o.allocation_),
        data_(std::exchange(o.data_, nullptr)),
        size_(std::exchange(o.size_, 0)) {}
  Buffer& operator=(Buffer&& o) noexcept {
    if (this != &o) {
      clear();
      allocation_ = o.allocation_;
      data_ = std::exchange(o.data_, nullptr);
      size_ = std::exchange(o.size_, 0);
    }
    return *this;
  }
  ~Buffer() noexcept {
    if (data_) allocation::AllocationContext::Deallocate(data_);
  }
  void resize(std::size_t n) {
    if (n == size_) return;
    auto* next =
        n ? static_cast<std::byte*>(allocation_.Allocate(n, alignof(std::byte)))
          : nullptr;
    if (data_) allocation::AllocationContext::Deallocate(data_);
    data_ = next;
    size_ = n;
  }
  void clear() noexcept {
    if (data_) allocation::AllocationContext::Deallocate(data_);
    data_ = nullptr;
    size_ = 0;
  }
  void shrink_to_fit() noexcept {}
  std::byte* data() noexcept { return data_; }
  const std::byte* data() const noexcept { return data_; }
  std::size_t size() const noexcept { return size_; }
  std::byte* begin() noexcept { return data_; }
  std::byte* end() noexcept { return size_ ? data_ + size_ : data_; }
  const std::byte* begin() const noexcept { return data_; }
  const std::byte* end() const noexcept {
    return size_ ? data_ + size_ : data_;
  }
  std::byte& operator[](std::size_t i) noexcept { return data_[i]; }
  const std::byte& operator[](std::size_t i) const noexcept { return data_[i]; }
  operator std::span<std::byte>() noexcept { return {data_, size_}; }
  operator std::span<const std::byte>() const noexcept {
    return {data_, size_};
  }

 private:
  allocation::AllocationContext allocation_;
  std::byte* data_ = nullptr;
  std::size_t size_ = 0;
};
struct Member {
  explicit Member(const allocation::AllocationContext& a)
      : logical(allocation::Allocator<char>(a)),
        native(allocation::Allocator<NativeChar>(a)),
        captured(a) {}
  String logical;
  NativeString native;
  std::span<const std::byte> expected;
  Buffer captured;
  SurfaceMemberKind kind = SurfaceMemberKind::kGltf;
  SurfaceDigest digest{};
  bool seen = false;
};
struct Directory {
  explicit Directory(const allocation::AllocationContext& a)
      : logical(allocation::Allocator<char>(a)),
        native(allocation::Allocator<NativeChar>(a)) {}
  String logical;
  NativeString native;
  bool seen = false;
};
enum class ByteAction { kWrite, kRead, kCompare, kCopy };
struct ByteJob {
  const allocation::AllocationContext* allocation = nullptr;
  const SurfaceFileWorkPolicy* work = nullptr;
  NativeFile handle;
  FileHooks hooks;
  ByteAction action = ByteAction::kCopy;
  SurfaceFileCode mismatch_code = SurfaceFileCode::kFileChanged;
  SurfaceFileStage stage = SurfaceFileStage::kStaging;
  std::uint64_t ordinal = 0, offset = 0;
  std::span<const std::byte> source;
  std::span<std::byte> destination;
  SurfaceFileIoObservation observation;
  std::optional<SurfaceFileDiagnostic> failure;
  std::exception_ptr exception;
};
struct FinalRange {
  explicit FinalRange(const allocation::AllocationContext& a) : scratch(a) {}
  NativeFile handle;
  NativeMetadata before;
  std::span<const std::byte> expected;
  Buffer scratch;
  std::uint64_t ordinal = 0, offset = 0, file_size = 0;
  SurfaceFileIoObservation observation;
  std::optional<SurfaceFileDiagnostic> failure;
  std::exception_ptr exception;
  bool entered = false;
};
struct FinalJob {
  bool entered = false, completed = false;
};
struct PublishedState {
  PublishedState(const allocation::AllocationContext&,
                 const geometry::ExecutionContext&,
                 const SurfaceOutputInspection&,
                 const SurfaceFilePublicationLimits&,
                 const SurfaceFilePublicationWorkPolicy&, std::stop_token,
                 const TestOptions&);
  allocation::AllocationContext allocation;
  geometry::ExecutionContext context;
  SurfaceOutputInspection input;
  SurfaceFilePublicationLimits limits;
  SurfaceFilePublicationWorkPolicy work;
  TestOptions testing;
  std::stop_token stop;
  NativePublication publication;
  Vector<Member> members;
  Vector<Directory> directories;
  Vector<SuppliedSurfaceMember> supplied;
  Vector<ByteJob> jobs;
  Vector<FinalRange> final_ranges;
  Vector<FinalJob> final_jobs;
  Vector<allocation::NativeWorkerRecord> final_records;
  Vector<std::uintptr_t> final_handles;
  geometry::WorkerLease final_lease;
  NativeGate final_gate;
  SurfaceFileExecutionStats final_execution;
  Buffer scratch;
  String receipt_path;
  NativeString receipt_native;
  Buffer receipt;
  Buffer report;
  SurfaceDigest receipt_digest{};
  std::optional<SurfaceOutputInspection> inspection;
  SurfaceFileFacts facts;
  bool receipt_present = false;
};
struct Access {
  static SurfacePublishedBundle Make(
      std::shared_ptr<const PublishedState>) noexcept;
};
SurfaceDigest Digest(std::span<const std::byte>, PublishedState&);
void Receipt(PublishedState&, bool report_present);
}  // namespace meshvale::interchange::surface_files_detail
#endif
