// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
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
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <utility>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_file_publish_internal.h"
#include "gltf_files_internal.h"
#include "gltf_files_test_internal.h"
#include "gltf_json_snapshot.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange {
namespace {
namespace detail = gltf_files_detail;
namespace allocation = gltf_detail;
using Status = GltfFileStatus;
using Stage = GltfFileStage;
using Code = GltfFileDiagnosticCode;
constexpr std::size_t kMinimumWorkers = 2;
constexpr std::uint64_t kStripMode = 5;
constexpr std::uint64_t kFanMode = 6;
constexpr std::string_view kBuffers = "buffers";
constexpr std::string_view kImages = "images";
constexpr std::string_view kUri = "uri";
constexpr std::string_view kInlineScheme = "data:";
char Fold(char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
bool FoldPrefix(std::string_view a, std::string_view b) noexcept {
  return a.size() >= b.size() &&
         std::equal(b.begin(), b.end(), a.begin(),
                    [](char x, char y) { return Fold(x) == Fold(y); });
}
bool Collision(std::string_view a, std::string_view b) noexcept {
  return (a.size() == b.size() && FoldPrefix(a, b)) ||
         (a.size() > b.size() && FoldPrefix(a, b) && a[b.size()] == '/') ||
         (b.size() > a.size() && FoldPrefix(b, a) && b[a.size()] == '/');
}
Status DelegatedStatus(GltfStatus status) noexcept {
  switch (status) {
    case GltfStatus::kCanceled:
      return Status::kCanceled;
    case GltfStatus::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case GltfStatus::kUnsupportedProfile:
      return Status::kUnsupportedProfile;
    case GltfStatus::kMalformed:
      return Status::kMalformed;
    case GltfStatus::kInvalidLimits:
      return Status::kInvalidConfiguration;
    case GltfStatus::kForeignOwner:
      return Status::kSourceContractFailure;
    default:
      return Status::kInvalidRequest;
  }
}
Status InspectionStatus(GltfTopologyOutputInspectionStatus status) noexcept {
  using Inspection = GltfTopologyOutputInspectionStatus;
  switch (status) {
    case Inspection::kInvalidConfiguration:
      return Status::kInvalidConfiguration;
    case Inspection::kLimitExceeded:
      return Status::kLimitExceeded;
    case Inspection::kForeignBudget:
      return Status::kForeignBudget;
    case Inspection::kSourceContractFailure:
      return Status::kSourceContractFailure;
    case Inspection::kUnsupportedSelection:
      return Status::kUnsupportedProfile;
    case Inspection::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case Inspection::kCanceled:
      return Status::kCanceled;
    default:
      return Status::kPreservationMismatch;
  }
}
struct PathRecord {
  detail::NativeString path;
  bool directory;
  bool seen = false;
};
enum class ByteAction { kImport, kWrite, kCompare };
struct ByteJob {
  const allocation::AllocationContext* allocation = nullptr;
  const GltfFileWorkPolicy* work = nullptr;
  const detail::GltfFilePublicationTestOptions* testing = nullptr;
  ByteAction action = ByteAction::kImport;
  std::span<const std::byte> source;
  std::span<std::byte> destination;
  detail::NativeFile handle;
  std::uint64_t file = 0;
  std::uint64_t offset = 0;
  std::optional<std::uint64_t> mismatch;
  std::optional<GltfFileDiagnostic> failure;
  std::exception_ptr exception;
  bool canceled = false;
};
void RunByteJob(void* opaque, std::size_t ordinal) noexcept {
  auto& job = static_cast<ByteJob*>(opaque)[ordinal];
  try {
    std::size_t begin = 0;
    while (begin < job.source.size()) {
      job.allocation->Checkpoint();
      const auto count = std::min<std::size_t>(
          {job.source.size() - begin,
           static_cast<std::size_t>(job.work->io_block_bytes),
           static_cast<std::size_t>(job.work->cancellation_block_bytes),
           static_cast<std::size_t>(detail::MaximumReadBlockBytes())});
      const auto source = job.source.subspan(begin, count);
      if (job.action == ByteAction::kWrite) {
        const auto& hooks = job.testing->hooks;
        if (hooks.before_write)
          hooks.before_write(job.file, job.offset + begin, hooks.state);
        detail::WriteBlock(job.handle, job.offset + begin, source);
      } else if (job.action == ByteAction::kImport) {
        std::copy(source.begin(), source.end(),
                  job.destination.begin() + begin);
      } else {
        const auto found = std::mismatch(source.begin(), source.end(),
                                         job.destination.begin() + begin);
        if (found.first != source.end()) {
          job.mismatch = job.offset + begin +
                         static_cast<std::size_t>(found.first - source.begin());
          return;
        }
      }
      begin += count;
    }
    if (job.action == ByteAction::kWrite) detail::CloseWrittenFile(job.handle);
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
class Publication {
 public:
  Publication(const GltfFilePublicationRequest& request,
              const GltfFilePublicationOptions& options,
              const GltfFilePublicationLimits& limits,
              const GltfFilePublicationWorkPolicy& work, std::stop_token stop,
              const detail::GltfFilePublicationTestOptions& testing,
              const allocation::AllocationContext& allocator,
              GltfFilePublicationResult& result)
      : request_(request),
        options_(options),
        limits_(limits),
        work_(work),
        stop_(stop),
        testing_(testing),
        allocation_(allocator),
        context_(allocation::Access::State(request.source.Asset()).context),
        result_(result),
        transaction_(allocator, limits.files, testing.hooks, stop),
        expected_(allocation::Allocator<detail::FileRecord>(allocator)),
        paths_(allocation::Allocator<PathRecord>(allocator)),
        opaque_(allocation::Allocator<std::string_view>(allocator)) {}
  void Run(const std::filesystem::path& destination) {
    Phase(GltfFilePublicationPhase::kPreflight);
    for (const auto& file : request_.source.Files())
      Import(file.path, file.path == request_.source.EntryPath()
                            ? request_.inspected_output.Output().Json()
                            : file.bytes);
    for (const auto& file : options_.supplemental_files)
      Import(file.path, file.bytes);
    std::sort(expected_.begin(), expected_.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
    BindingCount();
    transaction_.Plan(destination.native());
    Identity();
    transaction_.AdmitParent();
    PreparePaths();
    Phase(GltfFilePublicationPhase::kStaging);
    transaction_.CreateStage();
    for (const auto& path : paths_)
      if (path.directory) transaction_.CreateSubdirectory(path.path);
    for (std::size_t i = 0; i < expected_.size(); ++i) Write(i);
    Phase(GltfFilePublicationPhase::kVerification);
    detail::FileAssetState initial(allocation_,
                                   request_.source.ExtensionFilesPolicy());
    BuildOpaque();
    const GltfFileReadLimits read_limits{limits_.files, limits_.reload,
                                         limits_.container};
    const GltfFileReadWorkPolicy read_work{work_.files, work_.reload,
                                           work_.container};
    detail::GltfFileTestOptions capture_testing;
    capture_testing.execution = testing_.capture_execution;
    capture_testing.hooks = testing_.file_hooks;
    const auto read = detail::CaptureFiles(
        initial, transaction_.Stage(), request_.source.EntryPath(), opaque_,
        read_limits, read_work, context_, stop_, capture_testing,
        result_.execution);
    if (read.status != Status::kSuccess) throw detail::Failure{read};
    const auto& original = request_.inspected_output.Source();
    const auto& selected = original.SourcePrimitive();
    std::optional<GltfPrimitiveHandle> primitive;
    for (std::uint64_t i = 0; i < initial.asset->PrimitiveCount(); ++i) {
      allocation_.Checkpoint();
      auto current = initial.asset->Primitive(i);
      if (current && current->MeshIndex() == selected.MeshIndex() &&
          current->PrimitiveIndex() == selected.PrimitiveIndex()) {
        primitive = std::move(current);
        break;
      }
    }
    if (!primitive)
      detail::Fail(Status::kSourceContractFailure, Stage::kExtraction,
                   Code::kSourceIdentity);
    GltfExtractionRequest extraction_request;
    const auto& source_extraction = original.SourceExtraction();
    const auto mode = source_extraction.SourceMode();
    extraction_request.triangles =
        !request_.inspected_output.Output().ChangedBinding() &&
                (mode == kStripMode || mode == kFanMode)
            ? GltfTriangleRequest::kConvertStripOrFan
            : GltfTriangleRequest::kRequireTriangles;
    extraction_request.values = GltfValueRequest::kRaw;
    for (const auto& channel : source_extraction.Channels())
      if (!channel.normalized_values.empty())
        extraction_request.values = GltfValueRequest::kRawAndNormalized;
    auto extracted = ExtractGltfPrimitiveCapped(*initial.asset, *primitive,
                                                extraction_request,
                                                limits_.extraction, stop_);
    if (!extracted.extraction) {
      GltfFileDiagnostic diagnostic;
      diagnostic.status = DelegatedStatus(extracted.diagnostic.status);
      diagnostic.stage = Stage::kExtraction;
      diagnostic.code = Code::kDelegatedExtraction;
      diagnostic.gltf_diagnostic = extracted.diagnostic;
      throw detail::Failure{diagnostic};
    }
    const auto& inspected = request_.inspected_output;
    const GltfTopologyOutputInspectionRequest inspection_request{
        inspected.Source(),      inspected.Candidate(),
        inspected.SourceFaces(), inspected.SourceCorners(),
        inspected.Output(),      *initial.asset,
        *extracted.extraction};
    auto inspection = InspectGltfTopologyOutput(
        inspection_request, limits_.inspection, work_.inspection, stop_);
    if (!inspection.inspection) {
      GltfFileDiagnostic diagnostic;
      diagnostic.status = InspectionStatus(inspection.status);
      diagnostic.stage = Stage::kInspection;
      diagnostic.code = Code::kDelegatedInspection;
      diagnostic.inspection_status = inspection.status;
      diagnostic.inspection_diagnostic = inspection.diagnostic;
      throw detail::Failure{diagnostic};
    }
    Verify(initial);
    Inventory();
    const auto initial_size = expected_.size();
    if (options_.on_verified) {
      const GltfFileVerifiedContent verified{
          *inspection.inspection, GltfFileFormat::kGltfJson,
          request_.source.EntryPath(), nullptr, initial.views};
      auto supplements = [&] {
        try {
          return options_.on_verified(verified);
        } catch (const std::bad_alloc&) {
          throw;
        } catch (...) {
          detail::Fail(Status::kCallbackFailure, Stage::kSupplemental,
                       Code::kVerifiedCallback);
        }
      }();
      for (const auto& file : supplements) Import(file.path, file.bytes);
    }
    // Admit the complete dynamic inventory before its first filesystem write.
    PreparePaths();
    for (const auto& path : paths_)
      if (path.directory && !path.seen)
        transaction_.CreateSubdirectory(path.path);
    for (std::size_t i = initial_size; i < expected_.size(); ++i) Write(i);
    std::sort(expected_.begin(), expected_.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
    BuildOpaque();
    detail::FileAssetState final_files(allocation_,
                                       request_.source.ExtensionFilesPolicy());
    detail::CapturePhysicalFiles(final_files, transaction_.Stage(),
                                 request_.source.EntryPath(), opaque_,
                                 read_limits, read_work, context_, stop_,
                                 capture_testing, result_.execution);
    Verify(final_files);
    Inventory();
    auto state = std::allocate_shared<detail::PublishedBundleState>(
        allocation::Allocator<detail::PublishedBundleState>(allocation_),
        request_.source, *inspection.inspection, std::move(final_files));
    auto owner = detail::Access::MakePublished(std::move(state));
    Phase(GltfFilePublicationPhase::kPublication);
    transaction_.Commit();
    // Native commit is the last fallible action. These copies/transfers are
    // declared nonthrowing and no new stop, callback or allocation follows.
    result_.bundle.emplace(owner);
    result_.outcome = GltfFilePublicationOutcome::kPublished;
    result_.diagnostic.status = Status::kSuccess;
    result_.diagnostic.stage = Stage::kComplete;
    result_.diagnostic.code = Code::kNone;
  }
  void Rollback() noexcept {
    std::error_code error;
    result_.cleanup = transaction_.Cleanup(error);
    if (error) result_.cleanup_error = error;
  }

 private:
  void Phase(GltfFilePublicationPhase phase) {
    result_.phase = phase;
    allocation_.Checkpoint();
    if (options_.on_phase) {
      try {
        options_.on_phase(phase);
      } catch (const std::bad_alloc&) {
        throw;
      } catch (...) {
        const auto stage =
            phase == GltfFilePublicationPhase::kPreflight ? Stage::kPathPlanning
            : phase == GltfFilePublicationPhase::kStaging ? Stage::kStaging
            : phase == GltfFilePublicationPhase::kVerification
                ? Stage::kCapture
                : Stage::kPublication;
        detail::Fail(Status::kCallbackFailure, stage, Code::kProgressCallback);
      }
    }
    allocation_.Checkpoint();
  }
  void Bytes(ByteAction action, std::size_t file,
             std::span<const std::byte> source,
             std::span<std::byte> destination = {},
             detail::NativeView path = {}) {
    const auto bytes = source.size();
    auto count = std::max<std::size_t>(
        1,
        std::min<std::size_t>(work_.files.max_jobs,
                              bytes / work_.files.partition_bytes +
                                  (bytes % work_.files.partition_bytes != 0)));
    auto lease = bytes >= work_.files.parallel_min_bytes
                     ? geometry::TryReserveWorkers(context_, count)
                     : geometry::WorkerLease{};
    const bool native = lease.Count() >= kMinimumWorkers;
    if (!native) count = 1;
    detail::Vector<ByteJob> jobs{allocation::Allocator<ByteJob>(allocation_)};
    jobs.resize(count);
    const auto width = bytes / count;
    const auto remainder = bytes % count;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < count; ++i) {
      allocation_.Checkpoint();
      auto& job = jobs[i];
      job.allocation = &allocation_;
      job.work = &work_.files;
      job.testing = &testing_;
      job.action = action;
      job.file = file;
      job.offset = begin;
      const auto extent = width + (i < remainder ? 1 : 0);
      job.source = source.subspan(begin, extent);
      if (action != ByteAction::kWrite)
        job.destination = destination.subspan(begin, extent);
      else
        job.handle = detail::OpenWrite(path, file, testing_.file_hooks, false);
      begin += extent;
    }
    auto& stats = result_.execution.byte_verification;
    const bool measured = action == ByteAction::kCompare;
    if (measured) stats.planned_jobs += count;
    const auto& execution = measured ? testing_.verification_execution
                            : action == ByteAction::kImport
                                ? testing_.import_execution
                                : testing_.write_execution;
    allocation::OperationWorkerStats actual;
    if (native) {
      try {
        allocation::RunOperationJobs(allocation_, lease, count, RunByteJob,
                                     jobs.data(), execution, actual);
      } catch (...) {
        if (measured) {
          stats.launched_workers += actual.launched;
          stats.joined_workers += actual.joined;
        }
        throw;
      }
      if (measured) {
        stats.launched_workers += actual.launched;
        stats.joined_workers += actual.joined;
      }
    } else {
      if (measured) ++stats.caller_jobs;
      RunByteJob(jobs.data(), 0);
    }
    for (const auto& job : jobs)
      if (job.exception) std::rethrow_exception(job.exception);
    for (const auto& job : jobs)
      if (job.failure) throw detail::Failure{*job.failure};
    for (const auto& job : jobs)
      if (job.mismatch) {
        // An earlier partition may have stopped. Finalize only the known
        // prefix, without allocations or stop checks, after every join.
        const auto found =
            std::mismatch(source.begin(), source.begin() + *job.mismatch + 1,
                          destination.begin());
        Mismatch(file,
                 static_cast<std::uint64_t>(found.first - source.begin()));
      }
    for (const auto& job : jobs)
      if (job.canceled)
        detail::Fail(Status::kCanceled,
                     action == ByteAction::kWrite ? Stage::kStaging
                                                  : Stage::kSupplemental,
                     Code::kStopRequested, file);
  }
  void Import(std::string_view path, std::span<const std::byte> bytes) {
    const auto ordinal = expected_.size();
    detail::Count(ordinal + 1, limits_.files.max_files);
    detail::Count(path.size(), limits_.files.max_path_bytes);
    detail::Count(bytes.size(), limits_.files.max_file_bytes);
    if (path.size() > limits_.files.max_total_path_bytes - total_paths_ ||
        bytes.size() > limits_.files.max_total_file_bytes - total_bytes_)
      detail::Fail(Status::kLimitExceeded, Stage::kPathPlanning,
                   Code::kCountLimit, ordinal);
    auto admitted = detail::PortablePath(path, allocation_, limits_.files);
    for (const auto& old : expected_)
      if (Collision(old.path, admitted))
        detail::Fail(Status::kInvalidRequest, Stage::kPathPlanning,
                     Code::kPathCollision, ordinal);
    total_paths_ += path.size();
    total_bytes_ += bytes.size();
    expected_.emplace_back(allocation_);
    auto& file = expected_.back();
    file.path = std::move(admitted);
    file.bytes.resize(bytes.size());
    Bytes(ByteAction::kImport, ordinal, bytes, file.bytes);
  }
  void BindingCount() {
    const auto& document =
        allocation::Access::State(request_.source.Asset()).document;
    std::uint64_t count = 0;
    for (const auto table_name : {kBuffers, kImages}) {
      const auto table = allocation::FindMember(document, 0, table_name);
      if (!table) continue;
      const auto rows = allocation::ChildCount(document, *table);
      for (std::uint64_t i = 0; i < rows; ++i) {
        const auto row = *allocation::ChildAt(document, *table, i);
        const auto uri = allocation::FindMember(document, row, kUri);
        if (uri && document.Kind(*uri) == allocation::JsonKind::kString &&
            !document.String(*uri).starts_with(kInlineScheme))
          detail::Count(++count, limits_.files.max_resource_bindings);
      }
    }
  }
  void Identity() {
    const auto& asset = request_.source.Asset();
    const auto& inspected = request_.inspected_output;
    const auto& output = inspected.Output();
    if (!detail::Access::State(request_.source)
             .allocation.SharesExecutionBudget(
                 allocation::Access::State(asset).allocation) ||
        !geometry::SharesExecutionBudget(
            context_, inspected.Source().Mesh().ShareExecutionContext()) ||
        !geometry::SharesExecutionBudget(
            context_, inspected.Candidate().Source().ShareExecutionContext()) ||
        !geometry::SharesExecutionBudget(
            context_, output.Source().Mesh().ShareExecutionContext()) ||
        !geometry::SharesExecutionBudget(
            context_,
            allocation::Access::State(output.OriginalAsset()).context) ||
        !geometry::SharesExecutionBudget(
            context_,
            allocation::Access::State(inspected.ReloadedAsset()).context) ||
        !geometry::SharesExecutionBudget(context_,
                                         allocation::Access::ExtractionContext(
                                             inspected.ReloadedExtraction())))
      detail::Fail(Status::kForeignBudget, Stage::kPathPlanning,
                   Code::kForeignBudget);
    const auto& primitive = inspected.Source().SourcePrimitive();
    const auto same = [&](const GltfPrimitiveHandle& other) {
      return allocation::Access::Owner(other) ==
                 allocation::Access::Owner(asset) &&
             other.MeshIndex() == primitive.MeshIndex() &&
             other.PrimitiveIndex() == primitive.PrimitiveIndex();
    };
    if (allocation::Access::Owner(asset) !=
            allocation::Access::Owner(inspected.Source().SourceAsset()) ||
        allocation::Access::Owner(asset) !=
            allocation::Access::Owner(output.OriginalAsset()) ||
        allocation::Access::Owner(asset) !=
            allocation::Access::Owner(output.Source().SourceAsset()) ||
        !same(primitive) || !same(output.OriginalPrimitive()) ||
        !same(output.Source().SourcePrimitive()))
      detail::Fail(Status::kSourceContractFailure, Stage::kPathPlanning,
                   Code::kSourceIdentity);
  }
  void PreparePaths() {
    for (const auto& file : expected_) {
      const auto add = [&](std::string_view relative, bool directory) {
        auto path = detail::JoinPath(transaction_.Stage(), relative,
                                     allocation_, limits_.files);
        if (std::none_of(paths_.begin(), paths_.end(),
                         [&](const auto& p) { return p.path == path; }))
          paths_.push_back({std::move(path), directory, false});
      };
      for (auto end = file.path.find('/'); end != file.path.npos;
           end = file.path.find('/', end + 1))
        add(std::string_view(file.path).substr(0, end), true);
      add(file.path, false);
    }
    std::sort(paths_.begin(), paths_.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
  }
  void Write(std::size_t file) {
    constexpr std::size_t kExclusiveHandleCount = 1;
    const auto path = detail::JoinPath(
        transaction_.Stage(), expected_[file].path, allocation_, limits_.files);
    detail::Vector<detail::NativeFile> handles{
        allocation::Allocator<detail::NativeFile>(allocation_)};
    handles.resize(kExclusiveHandleCount);
    handles[0] = detail::OpenWrite(path, file, testing_.file_hooks, true);
    detail::CloseWrittenFile(handles[0]);
    Bytes(ByteAction::kWrite, file, expected_[file].bytes, {}, path);
  }
  void BuildOpaque() {
    opaque_.clear();
    opaque_.reserve(expected_.size());
    for (const auto& file : expected_)
      if (file.path != request_.source.EntryPath())
        opaque_.push_back(file.path);
  }
  [[noreturn]] void Mismatch(std::size_t file, std::uint64_t offset) {
    detail::Fail(Status::kPreservationMismatch, Stage::kInventory,
                 expected_[file].path == request_.source.EntryPath()
                     ? Code::kEntryBytes
                     : Code::kResourceBytes,
                 file, offset);
  }
  void Verify(detail::FileAssetState& actual) {
    if (actual.records.size() != expected_.size())
      detail::Fail(Status::kPreservationMismatch, Stage::kInventory,
                   Code::kStagedInventory);
    for (std::size_t i = 0; i < expected_.size(); ++i) {
      auto& actual_file = actual.records[i];
      const auto& expected_file = expected_[i];
      if (actual_file.path != expected_file.path)
        detail::Fail(Status::kPreservationMismatch, Stage::kInventory,
                     Code::kStagedInventory, i);
      const auto size =
          std::min(expected_file.bytes.size(), actual_file.bytes.size());
      const bool changed_size =
          expected_file.bytes.size() != actual_file.bytes.size();
      try {
        Bytes(ByteAction::kCompare, i,
              std::span(expected_file.bytes).first(size),
              std::span(actual_file.bytes).first(size));
      } catch (const allocation::AdmissionFailure& failure) {
        if (!changed_size ||
            failure.Code() != allocation::AdmissionCode::kCanceled)
          throw;
        const auto found = std::mismatch(expected_file.bytes.begin(),
                                         expected_file.bytes.begin() + size,
                                         actual_file.bytes.begin());
        Mismatch(i, static_cast<std::uint64_t>(found.first -
                                               expected_file.bytes.begin()));
      } catch (const detail::Failure& failure) {
        if (!changed_size || failure.diagnostic.status != Status::kCanceled)
          throw;
        const auto found = std::mismatch(expected_file.bytes.begin(),
                                         expected_file.bytes.begin() + size,
                                         actual_file.bytes.begin());
        Mismatch(i, static_cast<std::uint64_t>(found.first -
                                               expected_file.bytes.begin()));
      }
      if (changed_size) Mismatch(i, size);
    }
  }
  static bool Visit(detail::NativeView path, bool directory,
                    void* opaque) noexcept {
    auto& operation = *static_cast<Publication*>(opaque);
    const auto found = std::find_if(
        operation.paths_.begin(), operation.paths_.end(), [&](const auto& p) {
          return p.path == path && p.directory == directory;
        });
    if (found == operation.paths_.end() || found->seen) return false;
    found->seen = true;
    return true;
  }
  void Inventory() {
    for (auto& path : paths_) path.seen = false;
    transaction_.Inventory(Visit, this);
    if (std::any_of(paths_.begin(), paths_.end(),
                    [](const auto& p) { return !p.seen; }))
      detail::Fail(Status::kPreservationMismatch, Stage::kInventory,
                   Code::kStagedInventory);
  }
  const GltfFilePublicationRequest& request_;
  const GltfFilePublicationOptions& options_;
  const GltfFilePublicationLimits& limits_;
  const GltfFilePublicationWorkPolicy& work_;
  std::stop_token stop_;
  const detail::GltfFilePublicationTestOptions& testing_;
  allocation::AllocationContext allocation_;
  const geometry::ExecutionContext& context_;
  GltfFilePublicationResult& result_;
  detail::NativePublication transaction_;
  detail::Vector<detail::FileRecord> expected_;
  detail::Vector<PathRecord> paths_;
  detail::Vector<std::string_view> opaque_;
  std::uint64_t total_paths_ = 0;
  std::uint64_t total_bytes_ = 0;
};
}  // namespace
GltfFilePublicationResult detail::TestPublishGltfTopologyBundle(
    const GltfFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const GltfFilePublicationOptions& options,
    const GltfFilePublicationLimits& limits,
    const GltfFilePublicationWorkPolicy& work, std::stop_token stop,
    const GltfFilePublicationTestOptions& testing) {
  GltfFilePublicationResult result;
  try {
    ValidatePublicationConfiguration(limits, work);
    Count(
        options.supplemental_files.size(),
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
            sizeof(GltfFileView),
        Stage::kConfiguration);
    if (stop.stop_requested())
      Fail(Status::kCanceled, Stage::kConfiguration, Code::kStopRequested);
    if (request.source.Format() != GltfFileFormat::kGltfJson)
      Fail(Status::kUnsupportedProfile, Stage::kPublication,
           Code::kPublicationUnsupported);
    auto allocator =
        allocation::AllocationContext(
            allocation::Access::State(request.source.Asset()).context, stop,
            testing.allocation_backend)
            .WithRequestLimit(
                static_cast<std::size_t>(limits.files.max_new_requested_bytes));
    Publication operation(request, options, limits, work, stop, testing,
                          allocator, result);
    try {
      operation.Run(destination);
    } catch (...) {
      operation.Rollback();
      throw;
    }
  } catch (const Failure& failure) {
    result.diagnostic = failure.diagnostic;
    if (failure.diagnostic.status == Status::kCanceled)
      result.outcome = GltfFilePublicationOutcome::kCanceled;
  } catch (const allocation::AdmissionFailure& failure) {
    if (failure.Code() == allocation::AdmissionCode::kInvalidObject) throw;
    result.diagnostic.status =
        failure.Code() == allocation::AdmissionCode::kCanceled
            ? Status::kCanceled
        : failure.Code() == allocation::AdmissionCode::kSizeLimit
            ? Status::kLimitExceeded
            : Status::kBudgetExceeded;
    result.diagnostic.stage =
        result.phase == GltfFilePublicationPhase::kPreflight
            ? Stage::kPathPlanning
            : Stage::kStaging;
    result.diagnostic.code =
        failure.Code() == allocation::AdmissionCode::kCanceled
            ? Code::kStopRequested
        : failure.Code() == allocation::AdmissionCode::kSizeLimit
            ? Code::kFileRequestLimit
            : Code::kRequestBudget;
    if (failure.Code() == allocation::AdmissionCode::kCanceled)
      result.outcome = GltfFilePublicationOutcome::kCanceled;
  }
  return result;
}
GltfFilePublicationResult PublishGltfTopologyBundle(
    const GltfFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const GltfFilePublicationOptions& options,
    const GltfFilePublicationLimits& limits,
    const GltfFilePublicationWorkPolicy& work, std::stop_token stop) {
  return detail::TestPublishGltfTopologyBundle(request, destination, options,
                                               limits, work, stop, {});
}
}  // namespace meshvale::interchange
