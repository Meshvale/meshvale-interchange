// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <utility>

#include "glb_detail.h"
#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_file_read_provider.h"
#include "gltf_files_internal.h"
#include "gltf_files_test_internal.h"
#include "gltf_json_snapshot.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_files_detail {
[[noreturn]] void Fail(GltfFileStatus status, GltfFileStage stage,
                       GltfFileDiagnosticCode code,
                       std::optional<std::uint64_t> file,
                       std::optional<std::uint64_t> offset,
                       std::optional<std::error_code> error) {
  GltfFileDiagnostic diagnostic;
  diagnostic.status = status;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.file_index = file;
  diagnostic.byte_offset = offset;
  diagnostic.system_error = error;
  throw Failure{diagnostic};
}
void Count(std::uint64_t value, std::uint64_t maximum, GltfFileStage stage) {
  if (value > maximum)
    Fail(GltfFileStatus::kLimitExceeded, stage,
         GltfFileDiagnosticCode::kCountLimit);
}
FileAssetState::FileAssetState(const gltf_detail::AllocationContext& allocator,
                               GltfExtensionFilePolicy policy)
    : allocation(allocator),
      extensions(policy),
      records(gltf_detail::Allocator<FileRecord>(allocator)),
      views(gltf_detail::Allocator<GltfFileView>(allocator)) {}
GltfFileAsset Access::Make(std::shared_ptr<const FileAssetState> state) {
  return GltfFileAsset(std::move(state));
}
const FileAssetState& Access::State(const GltfFileAsset& asset) noexcept {
  return *asset.owner_;
}
GltfPublishedBundle Access::MakePublished(
    std::shared_ptr<const PublishedBundleState> state) noexcept {
  return GltfPublishedBundle(std::move(state));
}
PublishedBundleState::PublishedBundleState(
    const GltfFileAsset& original, const GltfTopologyOutputInspection& verified,
    FileAssetState&& captured)
    : source(original), inspection(verified), files(std::move(captured)) {}
}  // namespace meshvale::interchange::gltf_files_detail

namespace meshvale::interchange {
GltfFileAsset::GltfFileAsset(
    std::shared_ptr<const gltf_files_detail::FileAssetState> owner) noexcept
    : owner_(std::move(owner)) {}
GltfFileAsset::GltfFileAsset(const GltfFileAsset&) noexcept = default;
GltfFileAsset& GltfFileAsset::operator=(const GltfFileAsset&) noexcept =
    default;
GltfFileAsset::~GltfFileAsset() noexcept = default;
const GltfAsset& GltfFileAsset::Asset() const noexcept {
  return *owner_->asset;
}
GltfFileFormat GltfFileAsset::Format() const noexcept {
  return owner_->container ? GltfFileFormat::kGlb : GltfFileFormat::kGltfJson;
}
std::string_view GltfFileAsset::EntryPath() const noexcept {
  return owner_->records[owner_->entry].path;
}
std::span<const std::byte> GltfFileAsset::EntryBytes() const noexcept {
  return owner_->records[owner_->entry].bytes;
}
std::span<const GltfFileView> GltfFileAsset::Files() const noexcept {
  return owner_->views;
}
const GlbContainer* GltfFileAsset::Container() const noexcept {
  return owner_->container ? &*owner_->container : nullptr;
}
GltfExtensionFilePolicy GltfFileAsset::ExtensionFilesPolicy() const noexcept {
  return owner_->extensions;
}
std::uint64_t GltfFileAsset::NewRequestedBytes() const noexcept {
  return owner_->allocation.OperationActiveBytes();
}
GltfPublishedBundle::GltfPublishedBundle(
    std::shared_ptr<const gltf_files_detail::PublishedBundleState>
        owner) noexcept
    : owner_(std::move(owner)) {}
GltfPublishedBundle::GltfPublishedBundle(const GltfPublishedBundle&) noexcept =
    default;
GltfPublishedBundle& GltfPublishedBundle::operator=(
    const GltfPublishedBundle&) noexcept = default;
GltfPublishedBundle::~GltfPublishedBundle() noexcept = default;
const GltfFileAsset& GltfPublishedBundle::Source() const noexcept {
  return owner_->source;
}
const GltfTopologyOutputInspection& GltfPublishedBundle::Inspection()
    const noexcept {
  return owner_->inspection;
}
GltfFileFormat GltfPublishedBundle::Format() const noexcept {
  return GltfFileFormat::kGltfJson;
}
std::string_view GltfPublishedBundle::EntryPath() const noexcept {
  return owner_->files.records[owner_->files.entry].path;
}
std::span<const std::byte> GltfPublishedBundle::EntryBytes() const noexcept {
  return owner_->files.records[owner_->files.entry].bytes;
}
std::span<const GltfFileView> GltfPublishedBundle::Files() const noexcept {
  return owner_->files.views;
}
const GlbContainer* GltfPublishedBundle::Container() const noexcept {
  return nullptr;
}
std::uint64_t GltfPublishedBundle::NewRequestedBytes() const noexcept {
  return owner_->files.allocation.OperationActiveBytes();
}
namespace {
namespace detail = gltf_files_detail;
namespace allocation = gltf_detail;
using Status = GltfFileStatus;
using Stage = GltfFileStage;
using Code = GltfFileDiagnosticCode;
constexpr std::size_t kMinimumWorkers = 2;
constexpr std::array<std::byte, 4> kGlbMagic{std::byte{'g'}, std::byte{'l'},
                                             std::byte{'T'}, std::byte{'F'}};
constexpr std::string_view kBuffers = "buffers";
constexpr std::string_view kImages = "images";
constexpr std::string_view kUri = "uri";
constexpr std::string_view kByteLength = "byteLength";
constexpr std::string_view kDataScheme = "data:";
constexpr std::string_view kExtensions = "extensions";
constexpr std::string_view kExtensionsUsed = "extensionsUsed";
constexpr std::string_view kExtensionsRequired = "extensionsRequired";
void Positive(std::uint64_t value) {
  if (value == 0)
    detail::Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
                 Code::kRequiredFieldZero);
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    detail::Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
                 Code::kConfigurationExtent);
}
void Validate(const GltfFileLimits& limits, const GltfFileWorkPolicy& work,
              const GlbLimits& container, const GlbWorkPolicy& container_work) {
  for (auto value :
       {limits.max_files, limits.max_resource_bindings, limits.max_file_bytes,
        limits.max_total_file_bytes, limits.max_path_bytes,
        limits.max_total_path_bytes, limits.max_native_path_code_units,
        limits.max_new_requested_bytes, work.io_block_bytes,
        work.parallel_min_bytes, work.partition_bytes,
        work.cancellation_block_bytes, work.max_jobs})
    Positive(value);
  for (auto value :
       {container.max_container_bytes, container.max_chunks,
        container.max_requested_bytes, container_work.parallel_min_bytes,
        container_work.partition_bytes})
    Positive(value);
}
void ReadConfiguration(const GltfReadOperationLimits& limits,
                       const GltfWorkPolicy& work) {
  try {
    allocation::ValidateFileReadConfiguration(limits, work);
  } catch (const allocation::GltfFailure& failure) {
    GltfFileDiagnostic diagnostic;
    diagnostic.status = Status::kInvalidConfiguration;
    diagnostic.stage = Stage::kConfiguration;
    diagnostic.code = Code::kDelegatedRead;
    diagnostic.gltf_diagnostic = failure.Diagnostic();
    throw detail::Failure{diagnostic};
  }
}
[[noreturn]] void FileAdmission(const allocation::AdmissionFailure& failure,
                                Stage stage) {
  if (failure.Code() == allocation::AdmissionCode::kInvalidObject) throw;
  const auto status = failure.Code() == allocation::AdmissionCode::kCanceled
                          ? Status::kCanceled
                      : failure.Code() == allocation::AdmissionCode::kSizeLimit
                          ? Status::kLimitExceeded
                          : Status::kBudgetExceeded;
  const auto code = failure.Code() == allocation::AdmissionCode::kCanceled
                        ? Code::kStopRequested
                    : failure.Code() == allocation::AdmissionCode::kSizeLimit
                        ? Code::kFileRequestLimit
                        : Code::kRequestBudget;
  detail::Fail(status, stage, code);
}
char Fold(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
bool FoldPrefix(std::string_view a, std::string_view b) {
  return a.size() >= b.size() &&
         std::equal(b.begin(), b.end(), a.begin(),
                    [](char x, char y) { return Fold(x) == Fold(y); });
}
struct Binding {
  GltfResourceKind kind;
  std::uint64_t index;
  detail::String path;
};
struct Capture {
  detail::FileAssetState& state;
  const GltfFileReadOptions& options;
  const GltfFileReadLimits& limits;
  const GltfFileReadWorkPolicy& work;
  const geometry::ExecutionContext& context;
  std::stop_token stop;
  const detail::GltfFileTestOptions& testing;
  GltfFileExecution& execution;
  detail::NativeString root;
  detail::String entry;
  detail::Vector<Binding> plan;
  detail::Vector<SuppliedGltfResource> bindings;
  std::optional<SuppliedGltfResource> container_binding;
  std::uint64_t total_bytes = 0;
  std::uint64_t total_paths = 0;
  Capture(detail::FileAssetState& state, const GltfFileReadOptions& options,
          const GltfFileReadLimits& limits, const GltfFileReadWorkPolicy& work,
          const geometry::ExecutionContext& context, std::stop_token stop,
          const detail::GltfFileTestOptions& testing,
          GltfFileExecution& execution)
      : state(state),
        options(options),
        limits(limits),
        work(work),
        context(context),
        stop(stop),
        testing(testing),
        execution(execution),
        root(allocation::Allocator<detail::NativeChar>(state.allocation)),
        entry(allocation::Allocator<char>(state.allocation)),
        plan(allocation::Allocator<Binding>(state.allocation)),
        bindings(
            allocation::Allocator<SuppliedGltfResource>(state.allocation)) {}
  std::size_t Add(std::string_view path) {
    state.allocation.Checkpoint();
    for (std::size_t i = 0; i < state.records.size(); ++i) {
      const std::string_view existing = state.records[i].path;
      if (existing == path) return i;
      if ((existing.size() == path.size() && FoldPrefix(existing, path)) ||
          (existing.size() > path.size() && FoldPrefix(existing, path) &&
           existing[path.size()] == '/') ||
          (path.size() > existing.size() && FoldPrefix(path, existing) &&
           path[existing.size()] == '/'))
        detail::Fail(Status::kUnsupportedProfile, Stage::kPathPlanning,
                     Code::kPathCollision);
    }
    detail::Count(state.records.size() + 1, limits.files.max_files);
    detail::Count(path.size(), limits.files.max_path_bytes);
    if (path.size() > limits.files.max_total_path_bytes - total_paths)
      detail::Fail(Status::kLimitExceeded, Stage::kPathPlanning,
                   Code::kCountLimit);
    total_paths += path.size();
    state.records.emplace_back(state.allocation);
    state.records.back().path.assign(path);
    return state.records.size() - 1;
  }
  struct Job {
    Capture* operation = nullptr;
    detail::NativeFile handle;
    std::span<std::byte> output;
    std::uint64_t offset = 0;
    std::exception_ptr exception;
    std::optional<GltfFileDiagnostic> failure;
    bool canceled = false;
  };
  static void ReadJob(void* opaque, std::size_t ordinal) noexcept {
    auto& job = static_cast<Job*>(opaque)[ordinal];
    auto& operation = *job.operation;
    try {
      std::size_t begin = 0;
      while (begin < job.output.size()) {
        operation.state.allocation.Checkpoint();
        const auto count = std::min<std::size_t>(
            {job.output.size() - begin,
             static_cast<std::size_t>(operation.work.files.io_block_bytes),
             static_cast<std::size_t>(
                 operation.work.files.cancellation_block_bytes),
             static_cast<std::size_t>(detail::MaximumReadBlockBytes())});
        detail::ReadBlock(job.handle, job.offset + begin,
                          job.output.subspan(begin, count));
        begin += count;
      }
    } catch (const detail::Failure& failure) {
      job.failure = failure.diagnostic;
    } catch (const allocation::AdmissionFailure& failure) {
      if (failure.Code() == allocation::AdmissionCode::kCanceled)
        job.canceled = true;
      else
        job.exception = std::current_exception();
    } catch (...) {
      job.exception = std::current_exception();
    }
  }
  void Read(std::size_t index) {
    constexpr std::size_t kVerificationHandleCount = 2;
    auto& record = state.records[index];
    auto path =
        detail::JoinPath(root, record.path, state.allocation, limits.files);
    detail::RejectAliases(path, false, state.allocation, limits.files);
    detail::Vector<detail::NativeFile> handles{
        allocation::Allocator<detail::NativeFile>(state.allocation)};
    handles.resize(kVerificationHandleCount);
    handles[0] = detail::OpenFile(path, index, testing.hooks);
    detail::RejectAliases(path, false, state.allocation, limits.files);
    const auto canonical =
        detail::CanonicalPath(handles[0], state.allocation, limits.files);
    (void)detail::RelativeEntry(root, canonical, state.allocation,
                                limits.files);
    const auto before = detail::Metadata(handles[0]);
    detail::Count(before.size, limits.files.max_file_bytes, Stage::kCapture);
    if (before.size > limits.files.max_total_file_bytes - total_bytes)
      detail::Fail(Status::kLimitExceeded, Stage::kCapture, Code::kCountLimit,
                   index);
    total_bytes += before.size;
    record.bytes.resize(static_cast<std::size_t>(before.size));
    const auto bytes = record.bytes.size();
    auto jobs_count = bytes / work.files.partition_bytes +
                      (bytes % work.files.partition_bytes != 0);
    jobs_count = std::max<std::size_t>(
        1, std::min<std::size_t>(jobs_count, work.files.max_jobs));
    auto lease = bytes >= work.files.parallel_min_bytes
                     ? geometry::TryReserveWorkers(context, jobs_count)
                     : geometry::WorkerLease{};
    const bool native = lease.Count() >= kMinimumWorkers;
    if (!native) jobs_count = 1;
    detail::Vector<Job> jobs{allocation::Allocator<Job>(state.allocation)};
    jobs.resize(jobs_count);
    const auto partition = bytes / jobs_count;
    const auto remainder = bytes % jobs_count;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < jobs_count; ++i) {
      state.allocation.Checkpoint();
      auto& job = jobs[i];
      job.operation = this;
      job.handle = detail::OpenFile(path, index, testing.hooks);
      if (detail::Metadata(job.handle) != before)
        detail::Fail(Status::kIoFailure, Stage::kCapture, Code::kFileChanged,
                     index);
      const auto size = partition + (i < remainder ? 1 : 0);
      job.output = std::span(record.bytes).subspan(begin, size);
      job.offset = begin;
      begin += size;
    }
    auto& stats = execution.capture;
    stats.planned_jobs += jobs_count;
    allocation::OperationWorkerStats actual;
    if (native) {
      try {
        allocation::RunOperationJobs(state.allocation, lease, jobs_count,
                                     ReadJob, jobs.data(), testing.execution,
                                     actual);
      } catch (...) {
        stats.launched_workers += actual.launched;
        stats.joined_workers += actual.joined;
        throw;
      }
      stats.launched_workers += actual.launched;
      stats.joined_workers += actual.joined;
    } else {
      ++stats.caller_jobs;
      ReadJob(jobs.data(), 0);
    }
    // Runtime exceptions win; known typed IO failures win over a later stop.
    for (const auto& job : jobs)
      if (job.exception) std::rethrow_exception(job.exception);
    for (const auto& job : jobs)
      if (job.failure) throw detail::Failure{*job.failure};
    for (const auto& job : jobs)
      if (job.canceled)
        detail::Fail(Status::kCanceled, Stage::kCapture, Code::kStopRequested,
                     index);
    state.allocation.Checkpoint();
    if (detail::Metadata(handles[0]) != before)
      detail::Fail(Status::kIoFailure, Stage::kCapture, Code::kFileChanged,
                   index);
    handles[1] = detail::OpenFile(path, index, testing.hooks);
    if (detail::Metadata(handles[1]) != before)
      detail::Fail(Status::kIoFailure, Stage::kCapture, Code::kFileChanged,
                   index);
    detail::RejectAliases(path, false, state.allocation, limits.files);
  }
  void Initialize(const std::filesystem::path& input) {
    auto absolute =
        detail::AbsolutePath(input.native(), state.allocation, limits.files);
    if (options.resource_root.empty()) {
#ifdef _WIN32
      const auto parent = absolute.rfind(L'\\');
      const auto root_units = parent == detail::kDriveRootUnits - 1
                                  ? detail::kDriveRootUnits
                                  : parent;
#else
      const auto parent = absolute.rfind('/');
      const auto root_units = parent == 0 ? 1 : parent;
#endif
      root.assign(detail::NativeView(absolute).substr(0, root_units));
    } else
      root = detail::AbsolutePath(options.resource_root.native(),
                                  state.allocation, limits.files);
    InitializeRoot(absolute);
  }
  void InitializeNative(detail::NativeView admitted_root,
                        std::string_view relative_entry,
                        bool detect_container = true) {
    root.assign(admitted_root);
    const auto absolute =
        detail::JoinPath(root, relative_entry, state.allocation, limits.files);
    InitializeRoot(absolute, detect_container);
  }
  void InitializeRoot(detail::NativeView absolute,
                      bool detect_container = true) {
    detail::RejectAliases(root, true, state.allocation, limits.files);
    entry =
        detail::RelativeEntry(root, absolute, state.allocation, limits.files);
    const detail::GltfFileHooks no_hooks;
    detail::Vector<detail::NativeFile> root_handle{
        allocation::Allocator<detail::NativeFile>(state.allocation)};
    root_handle.resize(1);
    root_handle[0] = detail::OpenFile(root, 0, no_hooks, true);
    root =
        detail::CanonicalPath(root_handle[0], state.allocation, limits.files);
    state.entry = Add(entry);
    Read(state.entry);
    const auto& bytes = state.records[state.entry].bytes;
    if (!detect_container || bytes.size() < kGlbMagic.size() ||
        !std::equal(kGlbMagic.begin(), kGlbMagic.end(), bytes.begin()))
      return;
    auto read = glb_detail::Read(bytes, limits.container, work.container,
                                 context, stop, testing.container);
    if (read.status != GlbStatus::kSuccess) {
      GltfFileDiagnostic diagnostic;
      switch (read.status) {
        case GlbStatus::kInvalidOptions:
          diagnostic.status = Status::kInvalidConfiguration;
          break;
        case GlbStatus::kMalformed:
          diagnostic.status = Status::kMalformed;
          break;
        case GlbStatus::kUnsupportedVersion:
          diagnostic.status = Status::kUnsupportedProfile;
          break;
        case GlbStatus::kLimitExceeded:
          diagnostic.status = Status::kLimitExceeded;
          break;
        case GlbStatus::kBudgetExceeded:
          diagnostic.status = Status::kBudgetExceeded;
          break;
        case GlbStatus::kCanceled:
          diagnostic.status = Status::kCanceled;
          break;
        case GlbStatus::kSuccess:
          break;
      }
      diagnostic.stage = Stage::kContainer;
      diagnostic.code = Code::kDelegatedContainer;
      diagnostic.file_index = state.entry;
      diagnostic.container_status = read.status;
      diagnostic.container_byte_offset = read.error_byte_offset;
      throw detail::Failure{diagnostic};
    }
    state.container.emplace(std::move(read.container));
  }
  std::span<const std::byte> Json() const {
    constexpr std::size_t kJsonChunkIndex = 0;
    return state.container
               ? state.container->Chunk(kJsonChunkIndex).bytes
               : std::span<const std::byte>(state.records[state.entry].bytes);
  }
  [[noreturn]] void ContainerFailure(
      Status status, Code code, std::optional<std::uint64_t> buffer = {},
      std::optional<std::uint64_t> offset = {}) const {
    GltfFileDiagnostic diagnostic;
    diagnostic.status = status;
    diagnostic.stage = Stage::kContainer;
    diagnostic.code = code;
    diagnostic.file_index = state.entry;
    diagnostic.byte_offset = offset;
    if (buffer) diagnostic.resource_kind = GltfResourceKind::kBuffer;
    diagnostic.resource_index = buffer;
    throw detail::Failure{diagnostic};
  }
  void CoupleBin(const allocation::AssetState& asset) {
    if (!state.container) return;
    constexpr std::size_t kBinChunkIndex = 1;
    constexpr std::size_t kMaximumBinPaddingBytes = 3;
    constexpr std::uint64_t kBinBufferIndex = 0;
    const auto& container = *state.container;
    std::optional<std::span<const std::byte>> bin;
    if (container.ChunkCount() > kBinChunkIndex &&
        container.Chunk(kBinChunkIndex).type == kGlbBinChunkType)
      bin = container.Chunk(kBinChunkIndex).bytes;
    const auto& document = asset.document;
    const auto buffers = allocation::FindMember(document, 0, kBuffers);
    const auto count = buffers ? allocation::ChildCount(document, *buffers) : 0;
    for (std::uint64_t index = 0; index < count; ++index) {
      state.allocation.Checkpoint();
      const auto row = *allocation::ChildAt(document, *buffers, index);
      if (allocation::FindMember(document, row, kUri)) continue;
      if (index != kBinBufferIndex)
        ContainerFailure(Status::kUnsupportedProfile, Code::kContainerBinding,
                         index);
      if (!bin)
        ContainerFailure(Status::kMalformed, Code::kContainerBinding, index);
      const auto logical = allocation::ReadExactUnsigned(
          document, *allocation::FindMember(document, row, kByteLength));
      const auto start =
          static_cast<std::uint64_t>(bin->data() - container.Bytes().data());
      if (bin->size() < logical)
        ContainerFailure(Status::kMalformed, Code::kContainerBinding, index,
                         start + bin->size());
      const auto padding = bin->size() - logical;
      if (padding > kMaximumBinPaddingBytes)
        ContainerFailure(Status::kMalformed, Code::kContainerPadding, index,
                         start + logical + kMaximumBinPaddingBytes);
      for (std::uint64_t byte = logical; byte < bin->size(); ++byte)
        if ((*bin)[static_cast<std::size_t>(byte)] != std::byte{0})
          ContainerFailure(Status::kMalformed, Code::kContainerPadding, index,
                           start + byte);
      detail::Count(1, limits.files.max_resource_bindings);
      container_binding = SuppliedGltfResource{GltfResourceKind::kBuffer,
                                               kBinBufferIndex, *bin};
    }
    if (bin && !container_binding) {
      std::optional<std::uint64_t> buffer;
      if (count) buffer = kBinBufferIndex;
      ContainerFailure(Status::kUnsupportedProfile, Code::kContainerBinding,
                       buffer);
    }
  }
  static std::span<const SuppliedGltfResource> Provide(
      const allocation::AssetState& asset, void* opaque) {
    try {
      return Discover(asset, opaque);
    } catch (const allocation::AdmissionFailure& failure) {
      // The canonical reader handles its own admission failures. A provider's
      // file-operation refusal must retain its actual scope and fixed code.
      FileAdmission(failure, Stage::kCapture);
    }
  }
  static std::span<const SuppliedGltfResource> Discover(
      const allocation::AssetState& asset, void* opaque) {
    auto& operation = *static_cast<Capture*>(opaque);
    const auto& document = asset.document;
    if (operation.options.extensions ==
        GltfExtensionFilePolicy::kRejectOpaqueExtensions) {
      for (std::size_t node = 0; node < document.NodeCount(); ++node) {
        operation.state.allocation.Checkpoint();
        const auto key = document.Key(node);
        if ((key == kExtensions || key == kExtensionsUsed ||
             key == kExtensionsRequired) &&
            document.FirstChild(node))
          detail::Fail(Status::kUnsupportedProfile, Stage::kPathPlanning,
                       Code::kOpaqueInventoryRequired);
      }
    }
    operation.CoupleBin(asset);
    constexpr std::array kResourceTables{
        std::pair{kBuffers, GltfResourceKind::kBuffer},
        std::pair{kImages, GltfResourceKind::kImage}};
    for (const auto& [name, kind] : kResourceTables) {
      const auto table = allocation::FindMember(document, 0, name);
      if (!table) continue;
      const auto count = allocation::ChildCount(document, *table);
      for (std::uint64_t i = 0; i < count; ++i) {
        operation.state.allocation.Checkpoint();
        const auto item = allocation::ChildAt(document, *table, i);
        const auto uri =
            item ? allocation::FindMember(document, *item, kUri) : std::nullopt;
        if (!uri) {
          if (kind == GltfResourceKind::kBuffer && !operation.container_binding)
            detail::Fail(Status::kUnsupportedProfile, Stage::kPathPlanning,
                         Code::kContainerBinding);
          continue;
        }
        // Malformed URI values remain the canonical reader's admission problem.
        if (document.Kind(*uri) != allocation::JsonKind::kString) continue;
        const auto spelling = document.String(*uri);
        if (spelling.starts_with(kDataScheme)) continue;
        detail::Count(
            operation.plan.size() + (operation.container_binding ? 1 : 0) + 1,
            operation.limits.files.max_resource_bindings);
        auto path = detail::ResourcePath(operation.entry, spelling,
                                         operation.state.allocation,
                                         operation.limits.files);
        operation.Add(path);
        operation.plan.push_back({kind, i, std::move(path)});
      }
    }
    for (const auto path : operation.options.opaque_source_files) {
      auto admitted = detail::PortablePath(path, operation.state.allocation,
                                           operation.limits.files);
      operation.Add(admitted);
    }
    std::sort(operation.state.records.begin(), operation.state.records.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
    for (std::size_t i = 0; i < operation.state.records.size(); ++i) {
      if (operation.state.records[i].path == operation.entry)
        operation.state.entry = i;
      else
        operation.Read(i);
    }
    operation.bindings.reserve(operation.plan.size() +
                               (operation.container_binding ? 1 : 0));
    if (operation.container_binding)
      operation.bindings.push_back(*operation.container_binding);
    for (const auto& binding : operation.plan) {
      const auto file = std::find_if(
          operation.state.records.begin(), operation.state.records.end(),
          [&](const auto& record) { return record.path == binding.path; });
      operation.bindings.push_back({binding.kind, binding.index, file->bytes});
    }
    return operation.bindings;
  }
};
GltfFileDiagnostic FinishCapture(Capture& operation) {
  auto read = allocation::ReadGltfAssetFromFiles(
      operation.Json(), operation.limits.asset, operation.work.asset,
      operation.context, operation.stop, Capture::Provide, &operation);
  GltfFileDiagnostic diagnostic;
  if (!read.asset) {
    diagnostic.status =
        read.diagnostic.status == GltfStatus::kCanceled ? Status::kCanceled
        : read.diagnostic.status == GltfStatus::kBudgetExceeded
            ? Status::kBudgetExceeded
        : read.diagnostic.status == GltfStatus::kUnsupportedProfile
            ? Status::kUnsupportedProfile
        : read.diagnostic.status == GltfStatus::kMalformed
            ? Status::kMalformed
            : Status::kInvalidRequest;
    diagnostic.stage = Stage::kAssetRead;
    diagnostic.code = Code::kDelegatedRead;
    diagnostic.gltf_diagnostic = read.diagnostic;
    return diagnostic;
  }
  auto& state = operation.state;
  state.asset = std::move(read.asset);
  state.views.reserve(state.records.size());
  for (const auto& file : state.records)
    state.views.push_back({file.path, file.bytes});
  state.allocation.Checkpoint();
  diagnostic.status = Status::kSuccess;
  diagnostic.stage = Stage::kComplete;
  return diagnostic;
}
}  // namespace
GltfFileDiagnostic detail::CaptureFiles(
    FileAssetState& state, NativeView root, std::string_view entry,
    std::span<const std::string_view> opaque_files,
    const GltfFileReadLimits& limits, const GltfFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const GltfFileTestOptions& testing, GltfFileExecution& execution) {
  GltfFileReadOptions options;
  options.extensions = state.extensions;
  options.opaque_source_files = opaque_files;
  Capture operation(state, options, limits, work, context, stop, testing,
                    execution);
  operation.InitializeNative(root, entry);
  return FinishCapture(operation);
}
void detail::CapturePhysicalFiles(
    FileAssetState& state, NativeView root, std::string_view entry,
    std::span<const std::string_view> paths, const GltfFileReadLimits& limits,
    const GltfFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const GltfFileTestOptions& testing, GltfFileExecution& execution) {
  GltfFileReadOptions options;
  Capture operation(state, options, limits, work, context, stop, testing,
                    execution);
  operation.InitializeNative(root, entry, false);
  for (const auto path : paths) operation.Add(path);
  std::sort(state.records.begin(), state.records.end(),
            [](const auto& a, const auto& b) { return a.path < b.path; });
  for (std::size_t i = 0; i < state.records.size(); ++i) {
    if (state.records[i].path == entry)
      state.entry = i;
    else
      operation.Read(i);
  }
  state.views.reserve(state.records.size());
  for (const auto& file : state.records)
    state.views.push_back({file.path, file.bytes});
}
GltfFileReadResult detail::TestReadGltfFileAsset(
    const std::filesystem::path& input, const GltfFileReadOptions& options,
    const GltfFileReadLimits& limits, const GltfFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const GltfFileTestOptions& testing) {
  GltfFileReadResult result;
  try {
    Validate(limits.files, work.files, limits.container, work.container);
    ReadConfiguration(limits.asset, work.asset);
    if ((options.extensions !=
             GltfExtensionFilePolicy::kRejectOpaqueExtensions &&
         options.extensions !=
             GltfExtensionFilePolicy::kExplicitOpaqueInventory) ||
        (!options.opaque_source_files.empty() &&
         options.extensions !=
             GltfExtensionFilePolicy::kExplicitOpaqueInventory))
      detail::Fail(Status::kInvalidRequest, Stage::kConfiguration,
                   Code::kInvalidRequest);
    detail::Count(
        options.opaque_source_files.size(),
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
            sizeof(std::string_view),
        Stage::kConfiguration);
    auto allocator =
        allocation::AllocationContext(context, stop, testing.allocation_backend)
            .WithRequestLimit(
                static_cast<std::size_t>(limits.files.max_new_requested_bytes));
    auto state = std::allocate_shared<detail::FileAssetState>(
        allocation::Allocator<detail::FileAssetState>(allocator), allocator,
        options.extensions);
    Capture operation(*state, options, limits, work, context, stop, testing,
                      result.execution);
    operation.Initialize(input);
    result.diagnostic = FinishCapture(operation);
    if (result.diagnostic.status == Status::kSuccess)
      result.asset = detail::Access::Make(state);
  } catch (const detail::Failure& failure) {
    result.diagnostic = failure.diagnostic;
  } catch (const allocation::AdmissionFailure& failure) {
    if (failure.Code() == allocation::AdmissionCode::kInvalidObject) throw;
    result.diagnostic.status =
        failure.Code() == allocation::AdmissionCode::kCanceled
            ? Status::kCanceled
        : failure.Code() == allocation::AdmissionCode::kSizeLimit
            ? Status::kLimitExceeded
            : Status::kBudgetExceeded;
    result.diagnostic.stage = Stage::kCapture;
    result.diagnostic.code =
        failure.Code() == allocation::AdmissionCode::kCanceled
            ? Code::kStopRequested
        : failure.Code() == allocation::AdmissionCode::kSizeLimit
            ? Code::kFileRequestLimit
            : Code::kRequestBudget;
  }
  return result;
}
GltfFileReadResult ReadGltfFileAsset(const std::filesystem::path& input,
                                     const GltfFileReadOptions& options,
                                     const GltfFileReadLimits& limits,
                                     const GltfFileReadWorkPolicy& work,
                                     const geometry::ExecutionContext& context,
                                     std::stop_token stop) {
  return detail::TestReadGltfFileAsset(input, options, limits, work, context,
                                       stop, {});
}
void detail::ValidatePublicationConfiguration(
    const GltfFilePublicationLimits& limits,
    const GltfFilePublicationWorkPolicy& work) {
  Validate(limits.files, work.files, limits.container, work.container);
  ReadConfiguration(limits.reload, work.reload);
  for (auto value : {limits.extraction.max_components,
                     limits.extraction.max_triangle_corners,
                     limits.extraction.max_output_requested_bytes,
                     limits.inspection.max_json_bytes,
                     limits.inspection.max_json_nodes,
                     limits.inspection.max_json_depth,
                     limits.inspection.max_resources,
                     limits.inspection.max_resource_bytes,
                     limits.inspection.max_vertices,
                     limits.inspection.max_channels,
                     limits.inspection.max_attribute_scalars,
                     limits.inspection.max_descriptor_bytes,
                     limits.inspection.max_candidate_faces,
                     limits.inspection.max_candidate_corners,
                     limits.inspection.max_index_bytes,
                     limits.inspection.max_new_requested_bytes,
                     work.inspection.parallel_min_bytes,
                     work.inspection.partition_bytes,
                     work.inspection.cancellation_block_bytes,
                     work.inspection.max_jobs})
    Positive(value);
}
}  // namespace meshvale::interchange
