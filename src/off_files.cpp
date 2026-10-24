// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/off_files.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "off_file_internal.h"

namespace meshvale::interchange::off_files_detail {
struct AssetState {
  explicit AssetState(const allocation::AllocationContext& a)
      : allocation(a), name(allocation::Allocator<char>(a)) {}
  allocation::AllocationContext allocation;
  String name;
  std::optional<OffDocument> document;
};
struct BundleState {
  BundleState(const allocation::AllocationContext& a, const OffFileAsset& s,
              const geometry::MaterializedMesh& e)
      : allocation(a),
        source(s),
        expected(e),
        entry_name(allocation::Allocator<char>(a)),
        receipt_name(allocation::Allocator<char>(a)) {}
  allocation::AllocationContext allocation;
  OffFileAsset source;
  geometry::MaterializedMesh expected;
  String entry_name, receipt_name;
  std::optional<OffDocument> document;
  std::optional<Buffer> receipt;
};
struct Access {
  static OffFileAsset Asset(std::shared_ptr<const AssetState> state) noexcept {
    return OffFileAsset(std::move(state));
  }
  static OffPublishedBundle Bundle(
      std::shared_ptr<const BundleState> state) noexcept {
    return OffPublishedBundle(std::move(state));
  }
};
[[noreturn]] void Fail(OffFileStatus status, OffFileStage stage,
                       OffFileDiagnosticCode code,
                       std::optional<std::uint64_t> file,
                       std::optional<std::uint64_t> offset,
                       std::optional<std::error_code> error) {
  OffFileDiagnostic diagnostic;
  diagnostic.status = status;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.file_index = file;
  diagnostic.byte_offset = offset;
  diagnostic.system_error = error;
  throw Failure{diagnostic};
}
void Count(std::uint64_t value, std::uint64_t maximum, OffFileStage stage) {
  if (value > maximum)
    Fail(OffFileStatus::kLimitExceeded, stage,
         OffFileDiagnosticCode::kCountLimit);
}
namespace {
namespace geo = geometry;
using Stage = OffFileStage;
using Status = OffFileStatus;
using Code = OffFileDiagnosticCode;
constexpr std::uint64_t kEntryIndex = 0, kReceiptIndex = 1;
constexpr std::size_t kPositionWidth = 3;
std::size_t Host(std::uint64_t value) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  return static_cast<std::size_t>(value);
}
void Positive(std::uint64_t value) {
  if (!value)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kRequiredFieldZero);
  (void)Host(value);
}
std::uint64_t Add(std::uint64_t first, std::uint64_t second) {
  if (second > std::numeric_limits<std::uint64_t>::max() - first)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  const auto value = first + second;
  (void)Host(value);
  return value;
}
void Validate(const geo::MeshMaterializationLimits& limits) {
  for (auto value :
       {limits.max_vertices, limits.max_faces, limits.max_corners,
        limits.max_face_offsets, limits.max_attributes,
        limits.max_attribute_scalars, limits.max_attribute_offsets,
        limits.max_presence_bytes, limits.max_metadata_entries,
        limits.max_descriptor_bytes, limits.max_new_requested_bytes})
    Positive(value);
}
void Validate(const geo::MeshInspectionLimits& limits) {
  Positive(limits.max_issues);
  Positive(limits.max_new_requested_bytes);
}
void Validate(const geo::MeshMaterializationWorkPolicy& work) {
  for (auto value : {work.parallel_min_bytes, work.partition_bytes,
                     work.cancellation_block_scalars, work.max_jobs})
    Positive(value);
}
void Validate(const OffReadLimits& limits, const OffWorkPolicy& work) {
  for (auto value :
       {limits.max_source_bytes, limits.max_vertices, limits.max_faces,
        limits.max_corners, limits.max_corners_per_face, limits.max_line_bytes,
        limits.max_token_bytes, limits.max_comment_bytes,
        limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.materialization);
  Validate(limits.inspection);
  Validate(work.geometry);
  for (auto value :
       {work.cancellation_block_bytes, work.cancellation_block_tokens,
        work.cancellation_block_records})
    Positive(value);
  if (Add(Add(limits.max_codec_new_requested_bytes,
              limits.materialization.max_new_requested_bytes),
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const OffFileLimits& limits, const OffFileWorkPolicy& work) {
  for (auto value :
       {limits.max_entry_bytes, limits.max_receipt_bytes,
        limits.max_total_file_bytes, limits.max_entry_name_bytes,
        limits.max_receipt_name_bytes, limits.max_native_path_code_units,
        limits.max_file_new_requested_bytes, work.io_block_bytes,
        work.parallel_min_bytes, work.partition_bytes,
        work.cancellation_block_bytes, work.max_jobs})
    Positive(value);
  // Every native buffer is a terminated string. Check products before any
  // path/accounting allocation even when a branch needs no receipt.
  if (limits.max_native_path_code_units >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
          sizeof(NativeChar))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  (void)Add(limits.max_entry_bytes, limits.max_receipt_bytes);
}
OffFileDiagnostic Admission(const allocation::AdmissionFailure& failure,
                            Stage stage) {
  OffFileDiagnostic diagnostic;
  diagnostic.stage = stage;
  switch (failure.Code()) {
    case allocation::AdmissionCode::kSizeLimit:
      diagnostic.status = Status::kLimitExceeded;
      diagnostic.code = Code::kRequestBudget;
      break;
    case allocation::AdmissionCode::kBudgetExceeded:
      diagnostic.status = Status::kBudgetExceeded;
      diagnostic.code = Code::kSharedBudget;
      break;
    case allocation::AdmissionCode::kCanceled:
      diagnostic.status = Status::kCanceled;
      diagnostic.code = Code::kStopRequested;
      break;
    case allocation::AdmissionCode::kInvalidObject:
      diagnostic.status = Status::kInvalidRequest;
      diagnostic.code = Code::kLedgerMismatch;
      break;
  }
  return diagnostic;
}
struct Session {
  const allocation::AllocationContext& allocation;
  const geo::ExecutionContext& context;
  const OffFileWorkPolicy& work;
  const TestOptions& testing;
  void Check(Stage stage, std::uint64_t offset = 0) const {
    if (testing.checkpoint) testing.checkpoint(stage, offset, testing.state);
    allocation.Checkpoint();
  }
};
enum class ByteAction { kRead, kCopy, kCompare };
struct JobResult {
  std::optional<OffFileDiagnostic> failure;
  std::exception_ptr unexpected;
};
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
};
void DoByteJob(void* state, std::size_t job) noexcept {
  auto& jobs = *static_cast<ByteJobs*>(state);
  auto& result = jobs.results[job];
  try {
    const auto begin = job * jobs.partition;
    const auto end = begin + std::min(jobs.partition, jobs.size - begin);
    std::optional<NativeFile> file;
    if (jobs.action == ByteAction::kRead) {
      file.emplace(OpenFile(jobs.path, jobs.ordinal, jobs.session.testing.files,
                            false, jobs.stage));
      if (Metadata(*file) != jobs.identity)
        Fail(Status::kFileChanged, jobs.stage, Code::kChanged, jobs.ordinal,
             begin);
    }
    const auto block = Host(std::min(
        {jobs.session.work.io_block_bytes,
         jobs.session.work.cancellation_block_bytes, MaximumReadBlockBytes()}));
    for (std::size_t offset = begin; offset < end;) {
      jobs.session.Check(jobs.stage, offset);
      const auto count = std::min(block, end - offset);
      if (jobs.action == ByteAction::kRead)
        ReadBlock(*file, offset, jobs.destination.subspan(offset, count));
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
  } catch (const allocation::AdmissionFailure& failure) {
    result.failure = Admission(failure, jobs.stage);
  } catch (...) {
    result.unexpected = std::current_exception();
  }
}
void RunBytes(const Session& session, ByteAction action, Stage stage,
              std::span<const std::byte> expected,
              std::span<std::byte> destination, NativeView path,
              NativeMetadata identity, std::uint64_t ordinal,
              geo::MaterializationExecutionStats& statistics,
              std::span<const std::byte> actual_bytes = {}) {
  const auto size =
      action == ByteAction::kCompare ? actual_bytes.size() : destination.size();
  if (action != ByteAction::kRead && expected.size() != size)
    Fail(Status::kPreservationMismatch, stage, Code::kByteMismatch, ordinal);
  session.Check(stage);
  if (!size) return;
  const auto partition = Host(session.work.partition_bytes);
  const auto natural = size / partition + (size % partition != 0);
  const auto jobs = size >= session.work.parallel_min_bytes
                        ? std::min(natural, Host(session.work.max_jobs))
                        : std::size_t{1};
  const auto width = size / jobs + (size % jobs != 0);
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
    try {
      allocation::RunOperationJobs(session.allocation, lease, actual, DoByteJob,
                                   &batch, session.testing.workers, workers);
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
  // Ordered diagnostics retain an earlier known prefix mismatch over a later
  // cancellation. No diagnostic is manufactured for unperformed chunks.
  for (const auto& result : results)
    if (result.failure) throw Failure{*result.failure};
  session.Check(stage, size);
}
Buffer ReadPhysical(NativeView path, std::uint64_t ordinal,
                    std::uint64_t maximum, const OffFileLimits& limits,
                    const Session& session, Stage stage,
                    geo::MaterializationExecutionStats& statistics) {
  RejectAliases(path, false, session.allocation, limits, stage);
  auto file = OpenFile(path, ordinal, session.testing.files, false, stage);
  const auto before = Metadata(file);
  Count(before.size, maximum, stage);
  Count(before.size, limits.max_total_file_bytes, stage);
  const auto size = Host(before.size);
  auto canonical = CanonicalPath(file, session.allocation, limits);
  auto check =
      OpenFile(canonical, ordinal, session.testing.files, false, stage);
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
  return bytes;
}
void RequireOff(const OffReadResult& actual, Stage stage) {
  if (actual.status == OffStatus::kSuccess && actual.document) return;
  OffFileDiagnostic diagnostic;
  diagnostic.status = actual.status == OffStatus::kCanceled
                          ? Status::kCanceled
                          : Status::kOffFailure;
  diagnostic.stage = stage;
  diagnostic.code = Code::kDelegatedOff;
  diagnostic.off_status = actual.status;
  diagnostic.off_diagnostic = actual.diagnostic;
  throw Failure{diagnostic};
}
void Compare(const Session& session, std::span<const std::byte> expected,
             std::span<const std::byte> actual, Stage stage,
             std::uint64_t ordinal,
             geo::MaterializationExecutionStats& statistics) {
  RunBytes(session, ByteAction::kCompare, stage, expected, {}, {}, {}, ordinal,
           statistics, actual);
}
void CompareMesh(const Session& session, const geo::MaterializedMesh& expected,
                 const OffDocument& actual,
                 geo::MaterializationExecutionStats& statistics) {
  const auto first = expected.View(), second = actual.Mesh().View();
  if (!first.attributes.empty() || !second.attributes.empty())
    Fail(Status::kPreservationMismatch, Stage::kVerification,
         Code::kAttributeUnsupported);
  if (first.positions.size() != second.positions.size() ||
      first.face_offsets.size() != second.face_offsets.size() ||
      first.corner_vertices.size() != second.corner_vertices.size() ||
      actual.DeclaredEdgeCount() != 0)
    Fail(Status::kPreservationMismatch, Stage::kVerification,
         Code::kMeshMismatch);
  Compare(session, std::as_bytes(first.positions),
          std::as_bytes(second.positions), Stage::kVerification, kEntryIndex,
          statistics);
  Compare(session, std::as_bytes(first.face_offsets),
          std::as_bytes(second.face_offsets), Stage::kVerification, kEntryIndex,
          statistics);
  Compare(session, std::as_bytes(first.corner_vertices),
          std::as_bytes(second.corner_vertices), Stage::kVerification,
          kEntryIndex, statistics);
}
void Facts(const OffFilePublicationRequest& request) {
  const auto view = request.expected.View();
  const auto& facts = request.output.Facts();
  if (facts.profile != OffProfile::kAsciiRecords ||
      facts.text_policy != OffTextPolicy::kCanonicalAscii ||
      facts.edge_count_policy != OffEdgeCountPolicy::kUnknownZero ||
      facts.correspondence != OffCorrespondence::kIdentity ||
      facts.emitted_edge_count != 0 || !facts.source_declared_edge_count ||
      *facts.source_declared_edge_count !=
          request.source.Document().DeclaredEdgeCount() ||
      facts.vertex_count != view.positions.size() / kPositionWidth ||
      facts.face_count != view.face_offsets.size() - 1 ||
      facts.corner_count != view.corner_vertices.size())
    Fail(Status::kPreservationMismatch, Stage::kVerification,
         Code::kOutputFacts);
}
}  // namespace

OffFileReadResult ReadForTesting(const std::filesystem::path& input,
                                 const OffFileReadLimits& limits,
                                 const OffFileReadWorkPolicy& work,
                                 const geo::ExecutionContext& context,
                                 std::stop_token stop,
                                 const TestOptions& testing) {
  OffFileReadResult result;
  auto phase = Stage::kConfiguration;
  try {
    Validate(limits.files, work.files);
    Validate(limits.off, work.off);
    Count(input.native().size(), limits.files.max_native_path_code_units - 1);
    auto allocator =
        allocation::AllocationContext(context, stop, testing.allocation_backend)
            .WithRequestLimit(Host(limits.files.max_file_new_requested_bytes));
    Session session{allocator, context, work.files, testing};
    session.Check(phase);
    phase = Stage::kPathPlanning;
    auto path = AbsolutePath(input.native(), allocator, limits.files);
    auto name = EntryName(path, allocator, limits.files);
    auto state = std::allocate_shared<AssetState>(
        allocation::Allocator<AssetState>(allocator), allocator);
    Assign(state->name, std::string_view(name), allocator);
    phase = Stage::kCapture;
    auto bytes =
        ReadPhysical(path, kEntryIndex, limits.files.max_entry_bytes,
                     limits.files, session, phase, result.execution.capture);
    phase = Stage::kOffRead;
    auto read = ReadOff(bytes.Bytes(), limits.off, work.off, context, stop);
    result.execution.off = read.execution;
    RequireOff(read, phase);
    state->document.emplace(*read.document);
    session.Check(phase);
    result.asset.emplace(Access::Asset(std::move(state)));
    result.status = Status::kSuccess;
    result.diagnostic.stage = Stage::kComplete;
  } catch (const Failure& failure) {
    result.diagnostic = failure.diagnostic;
    result.status = failure.diagnostic.status;
  } catch (const allocation::AdmissionFailure& failure) {
    result.diagnostic = Admission(failure, phase);
    result.status = result.diagnostic.status;
  }
  return result;
}
OffFilePublicationResult PublishForTesting(
    const OffFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const OffFilePublicationOptions& options,
    const OffFilePublicationLimits& limits,
    const OffFilePublicationWorkPolicy& work, std::stop_token stop,
    const TestOptions& testing) {
  OffFilePublicationResult result;
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
    Validate(limits.reload, work.reload);
    Validate(limits.expected_inspection);
    Validate(work.expected_inspection);
    Count(destination.native().size(),
          limits.files.max_native_path_code_units - 1);
    auto context = request.source.Document().Mesh().ShareExecutionContext();
    auto expected_context = request.expected.ShareExecutionContext();
    if (!geo::SharesExecutionBudget(context, expected_context))
      Fail(Status::kForeignBudget, phase, Code::kLedgerMismatch);
    auto allocator =
        allocation::AllocationContext(context, stop, testing.allocation_backend)
            .WithRequestLimit(Host(limits.files.max_file_new_requested_bytes));
    Session session{allocator, context, work.files, testing};
    session.Check(phase);
    PortableName(request.source.EntryPath(), limits.files.max_entry_name_bytes,
                 allocator);
    Count(request.output.Bytes().size(), limits.files.max_entry_bytes);
    Count(request.output.Bytes().size(), limits.files.max_total_file_bytes);
    if (options.receipt) {
      if (!options.receipt->provide)
        Fail(Status::kInvalidRequest, phase, Code::kProvider);
      PortableName(options.receipt->basename,
                   limits.files.max_receipt_name_bytes, allocator);
      if (EqualFold(request.source.EntryPath(), options.receipt->basename,
                    &allocator))
        Fail(Status::kInvalidRequest, phase, Code::kNameCollision);
    }
    phase = Stage::kExpectedInspection;
    auto inspection = geo::InspectMaterializedMesh(
        request.expected, limits.expected_inspection, work.expected_inspection,
        stop);
    result.execution.expected_inspection = inspection.execution;
    if (inspection.status != geo::MaterializationStatus::kSuccess ||
        !inspection.inspection || !inspection.inspection->IsStorageValid()) {
      OffFileDiagnostic diagnostic;
      diagnostic.status =
          inspection.status == geo::MaterializationStatus::kCanceled
              ? Status::kCanceled
              : Status::kGeometryFailure;
      diagnostic.stage = phase;
      diagnostic.code = Code::kDelegatedInspection;
      diagnostic.geometry_status = inspection.status;
      diagnostic.geometry_diagnostic = inspection.diagnostic;
      throw Failure{diagnostic};
    }
    if (!request.expected.View().attributes.empty())
      Fail(Status::kInvalidRequest, phase, Code::kAttributeUnsupported);
    Facts(request);
    auto state = std::allocate_shared<BundleState>(
        allocation::Allocator<BundleState>(allocator), allocator,
        request.source, request.expected);
    Assign(state->entry_name, request.source.EntryPath(), allocator);
    if (options.receipt)
      Assign(state->receipt_name, options.receipt->basename, allocator);
    phase = Stage::kPathPlanning;
    transaction.emplace(allocator, limits.files, testing);
    transaction->Plan(destination.native(), state->entry_name,
                      options.receipt
                          ? std::optional<std::string_view>(state->receipt_name)
                          : std::nullopt);
    phase = Stage::kStaging;
    transaction->Create();
    WriteFile(transaction->Entry(), request.output.Bytes(), kEntryIndex,
              work.files, allocator, testing);
    phase = Stage::kReload;
    {
      auto bytes = ReadPhysical(transaction->Entry(), kEntryIndex,
                                limits.files.max_entry_bytes, limits.files,
                                session, phase, result.execution.capture);
      auto read =
          ReadOff(bytes.Bytes(), limits.reload, work.reload, context, stop);
      result.execution.off = read.execution;
      RequireOff(read, phase);
      state->document.emplace(*read.document);
    }
    phase = Stage::kVerification;
    Compare(session, request.output.Bytes(), state->document->OriginalBytes(),
            phase, kEntryIndex, result.execution.byte_verification);
    CompareMesh(session, request.expected, *state->document,
                result.execution.mesh_verification);
    phase = Stage::kInventory;
    transaction->Inventory(false);
    if (options.receipt) {
      phase = Stage::kProvider;
      session.Check(phase);
      std::span<const std::byte> supplied;
      try {
        supplied = options.receipt->provide(
            OffFileVerifiedContent{*state->document, state->entry_name,
                                   state->document->OriginalBytes()},
            options.receipt->state);
      } catch (const std::bad_alloc&) {
        throw;
      } catch (...) {
        Fail(Status::kProviderFailure, phase, Code::kProvider, kReceiptIndex);
      }
      Count(supplied.size(), limits.files.max_receipt_bytes, phase);
      Count(Add(request.output.Bytes().size(), supplied.size()),
            limits.files.max_total_file_bytes, phase);
      state->receipt.emplace(allocator, supplied.size());
      RunBytes(session, ByteAction::kCopy, phase, supplied,
               state->receipt->Bytes(), {}, {}, kReceiptIndex,
               result.execution.copying);
      phase = Stage::kStaging;
      WriteFile(transaction->Receipt(), state->receipt->Bytes(), kReceiptIndex,
                work.files, allocator, testing);
    } else
      Count(request.output.Bytes().size(), limits.files.max_total_file_bytes,
            phase);
    phase = Stage::kInventory;
    transaction->Inventory(options.receipt.has_value());
    {
      auto bytes = ReadPhysical(transaction->Entry(), kEntryIndex,
                                limits.files.max_entry_bytes, limits.files,
                                session, phase, result.execution.capture);
      Compare(session, state->document->OriginalBytes(), bytes.Bytes(), phase,
              kEntryIndex, result.execution.byte_verification);
    }
    if (state->receipt) {
      auto bytes = ReadPhysical(transaction->Receipt(), kReceiptIndex,
                                limits.files.max_receipt_bytes, limits.files,
                                session, phase, result.execution.capture);
      Compare(session, state->receipt->Bytes(), bytes.Bytes(), phase,
              kReceiptIndex, result.execution.byte_verification);
    }
    transaction->Inventory(options.receipt.has_value());
    const auto bundle = Access::Bundle(std::move(state));
    static_assert(std::is_nothrow_copy_constructible_v<OffPublishedBundle>);
    phase = Stage::kPublication;
    transaction->Commit();
    // Successful native commit is the final fallible action.
    result.bundle.emplace(bundle);
    result.outcome = OffFilePublicationOutcome::kPublished;
    result.phase = Stage::kComplete;
    result.diagnostic.stage = Stage::kComplete;
  } catch (const Failure& failure) {
    result.diagnostic = failure.diagnostic;
    result.phase = failure.diagnostic.stage;
    result.outcome = failure.diagnostic.status == Status::kCanceled
                         ? OffFilePublicationOutcome::kCanceled
                         : OffFilePublicationOutcome::kFailed;
    cleanup();
  } catch (const allocation::AdmissionFailure& failure) {
    result.diagnostic = Admission(failure, phase);
    result.phase = phase;
    result.outcome = result.diagnostic.status == Status::kCanceled
                         ? OffFilePublicationOutcome::kCanceled
                         : OffFilePublicationOutcome::kFailed;
    cleanup();
  } catch (...) {
    cleanup();
    throw;
  }
  return result;
}
}  // namespace meshvale::interchange::off_files_detail

namespace meshvale::interchange {
OffFileAsset::OffFileAsset(
    std::shared_ptr<const off_files_detail::AssetState> state) noexcept
    : state_(std::move(state)) {}
OffFileAsset::OffFileAsset(const OffFileAsset&) noexcept = default;
OffFileAsset& OffFileAsset::operator=(const OffFileAsset&) noexcept = default;
OffFileAsset::~OffFileAsset() noexcept = default;
const OffDocument& OffFileAsset::Document() const noexcept {
  return *state_->document;
}
std::string_view OffFileAsset::EntryPath() const noexcept {
  return state_->name;
}
std::span<const std::byte> OffFileAsset::EntryBytes() const noexcept {
  return Document().OriginalBytes();
}
std::uint64_t OffFileAsset::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
OffPublishedBundle::OffPublishedBundle(
    std::shared_ptr<const off_files_detail::BundleState> state) noexcept
    : state_(std::move(state)) {}
OffPublishedBundle::OffPublishedBundle(const OffPublishedBundle&) noexcept =
    default;
OffPublishedBundle& OffPublishedBundle::operator=(
    const OffPublishedBundle&) noexcept = default;
OffPublishedBundle::~OffPublishedBundle() noexcept = default;
const OffFileAsset& OffPublishedBundle::Source() const noexcept {
  return state_->source;
}
const geometry::MaterializedMesh& OffPublishedBundle::ExpectedMesh()
    const noexcept {
  return state_->expected;
}
const OffDocument& OffPublishedBundle::Document() const noexcept {
  return *state_->document;
}
std::string_view OffPublishedBundle::EntryPath() const noexcept {
  return state_->entry_name;
}
std::span<const std::byte> OffPublishedBundle::EntryBytes() const noexcept {
  return Document().OriginalBytes();
}
std::optional<std::string_view> OffPublishedBundle::ReceiptPath()
    const noexcept {
  return state_->receipt ? std::optional<std::string_view>(state_->receipt_name)
                         : std::nullopt;
}
std::optional<std::span<const std::byte>> OffPublishedBundle::ReceiptBytes()
    const noexcept {
  return state_->receipt ? std::optional<std::span<const std::byte>>(
                               state_->receipt->Bytes())
                         : std::nullopt;
}
std::uint64_t OffPublishedBundle::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
OffFileReadResult ReadOffFileAsset(const std::filesystem::path& input,
                                   const OffFileReadLimits& limits,
                                   const OffFileReadWorkPolicy& work,
                                   const geometry::ExecutionContext& context,
                                   std::stop_token stop) {
  return off_files_detail::ReadForTesting(input, limits, work, context, stop,
                                          {});
}
OffFilePublicationResult PublishOffBundle(
    const OffFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const OffFilePublicationOptions& options,
    const OffFilePublicationLimits& limits,
    const OffFilePublicationWorkPolicy& work, std::stop_token stop) {
  return off_files_detail::PublishForTesting(request, destination, options,
                                             limits, work, stop, {});
}
}  // namespace meshvale::interchange
