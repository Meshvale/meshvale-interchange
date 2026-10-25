// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/ply_files.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "ply_file_internal.h"

namespace meshvale::interchange::ply_files_detail {
struct AssetState {
  explicit AssetState(const allocation::AllocationContext& a)
      : allocation(a), name(allocation::Allocator<char>(a)) {}
  allocation::AllocationContext allocation;
  String name;
  std::optional<PlyDocument> document;
};
struct BundleState {
  BundleState(const allocation::AllocationContext& a, const PlyFileAsset& s,
              const geometry::MaterializedMesh& e)
      : allocation(a),
        source(s),
        expected(e),
        entry_name(allocation::Allocator<char>(a)),
        receipt_name(allocation::Allocator<char>(a)) {}
  allocation::AllocationContext allocation;
  PlyFileAsset source;
  geometry::MaterializedMesh expected;
  String entry_name, receipt_name;
  std::optional<PlyDocument> document;
  std::optional<Buffer> receipt;
};
struct Access {
  static PlyFileAsset Asset(std::shared_ptr<const AssetState> state) noexcept {
    return PlyFileAsset(std::move(state));
  }
  static PlyPublishedBundle Bundle(
      std::shared_ptr<const BundleState> state) noexcept {
    return PlyPublishedBundle(std::move(state));
  }
};
[[noreturn]] void Fail(PlyFileStatus status, PlyFileStage stage,
                       PlyFileDiagnosticCode code,
                       std::optional<std::uint64_t> file,
                       std::optional<std::uint64_t> offset,
                       std::optional<std::error_code> error) {
  PlyFileDiagnostic diagnostic;
  diagnostic.status = status;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.file_index = file;
  diagnostic.byte_offset = offset;
  diagnostic.system_error = error;
  throw Failure{diagnostic};
}
void Count(std::uint64_t value, std::uint64_t maximum, PlyFileStage stage) {
  if (value > maximum)
    Fail(PlyFileStatus::kLimitExceeded, stage,
         PlyFileDiagnosticCode::kCountLimit);
}
namespace {
namespace geo = geometry;
using Stage = PlyFileStage;
using Status = PlyFileStatus;
using Code = PlyFileDiagnosticCode;
constexpr std::uint64_t kEntryIndex = 0, kReceiptIndex = 1;
constexpr std::size_t kPositionWidth = 3;
constexpr std::string_view kVertexElementName = "vertex";
constexpr std::string_view kFaceElementName = "face";
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
void Validate(const PlyReadLimits& limits, const PlyWorkPolicy& work) {
  for (auto value :
       {limits.max_source_bytes, limits.max_header_bytes,
        limits.max_header_lines, limits.max_name_bytes,
        limits.max_total_name_bytes, limits.max_properties,
        limits.max_list_rows, limits.max_list_items_per_row,
        limits.max_property_values, limits.max_vertices, limits.max_faces,
        limits.max_corners, limits.max_corners_per_face, limits.max_line_bytes,
        limits.max_token_bytes, limits.max_comment_bytes,
        limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.materialization);
  Validate(limits.inspection);
  Validate(work.geometry);
  for (auto value :
       {work.cancellation_block_bytes, work.cancellation_block_values,
        work.cancellation_block_records})
    Positive(value);
  if (Add(Add(limits.max_codec_new_requested_bytes,
              limits.materialization.max_new_requested_bytes),
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const PlyFileLimits& limits, const PlyFileWorkPolicy& work) {
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
PlyFileDiagnostic Admission(const allocation::AdmissionFailure& failure,
                            Stage stage) {
  PlyFileDiagnostic diagnostic;
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
  const PlyFileWorkPolicy& work;
  const TestOptions& testing;
  void Check(Stage stage, std::uint64_t offset = 0) const {
    if (testing.checkpoint) testing.checkpoint(stage, offset, testing.state);
    allocation.Checkpoint();
  }
};
enum class ByteAction { kRead, kCopy, kCompare };
struct JobResult {
  std::optional<PlyFileDiagnostic> failure;
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
    const auto requested_block =
        std::min(jobs.session.work.io_block_bytes,
                 jobs.session.work.cancellation_block_bytes);
    const auto block =
        Host(jobs.action == ByteAction::kRead
                 ? std::min(requested_block, MaximumReadBlockBytes())
                 : requested_block);
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
              std::span<const std::byte> actual_bytes = {},
              std::size_t scalar_width = 1) {
  const auto size =
      action == ByteAction::kCompare ? actual_bytes.size() : destination.size();
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
                    std::uint64_t maximum, const PlyFileLimits& limits,
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
void RequirePly(const PlyReadResult& actual, Stage stage) {
  if (actual.status == PlyStatus::kSuccess && actual.document) return;
  PlyFileDiagnostic diagnostic;
  diagnostic.status = actual.status == PlyStatus::kCanceled
                          ? Status::kCanceled
                          : Status::kPlyFailure;
  diagnostic.stage = stage;
  diagnostic.code = Code::kDelegatedPly;
  diagnostic.ply_status = actual.status;
  diagnostic.ply_diagnostic = actual.diagnostic;
  throw Failure{diagnostic};
}
void Compare(const Session& session, std::span<const std::byte> expected,
             std::span<const std::byte> actual, Stage stage,
             std::uint64_t ordinal,
             geo::MaterializationExecutionStats& statistics,
             std::size_t scalar_width = 1) {
  RunBytes(session, ByteAction::kCompare, stage, expected, {}, {}, {}, ordinal,
           statistics, actual, scalar_width);
}
void Accumulate(std::uint64_t& total, std::uint64_t value,
                std::uint64_t maximum, Stage stage) {
  if (total > maximum || value > maximum - total)
    Fail(Status::kLimitExceeded, stage, Code::kCountLimit);
  total += value;
}
std::uint64_t BoundSchema(const Session& session, const PlyDocument& document,
                          const PlyReadLimits& limits) {
  constexpr std::uint64_t kMaximumElementKinds = 2;
  Count(document.Schema().size(), kMaximumElementKinds,
        Stage::kExpectedInspection);
  std::uint64_t properties = 0, names = 0;
  for (const auto& element : document.Schema()) {
    session.Check(Stage::kExpectedInspection);
    Accumulate(properties, element.properties.size(), limits.max_properties,
               Stage::kExpectedInspection);
  }
  for (const auto& element : document.Schema()) {
    const std::string_view name = element.kind == PlyElementKind::kVertex
                                      ? kVertexElementName
                                      : kFaceElementName;
    Count(name.size(), limits.max_name_bytes, Stage::kExpectedInspection);
    Accumulate(names, name.size(), limits.max_total_name_bytes,
               Stage::kExpectedInspection);
    for (const auto& property : element.properties) {
      session.Check(Stage::kExpectedInspection);
      Count(property.name.size(), limits.max_name_bytes,
            Stage::kExpectedInspection);
      Accumulate(names, property.name.size(), limits.max_total_name_bytes,
                 Stage::kExpectedInspection);
    }
  }
  return properties;
}
void BoundExpected(const Session& session, const geo::MaterializedMesh& mesh,
                   const PlyReadLimits& limits) {
  const auto view = mesh.View();
  const auto stage = Stage::kExpectedInspection;
  const auto& storage = limits.materialization;
  Count(view.positions.size() / kPositionWidth,
        std::min(limits.max_vertices, storage.max_vertices), stage);
  Count(view.face_offsets.size(), storage.max_face_offsets, stage);
  Count(view.face_offsets.empty() ? 0 : view.face_offsets.size() - 1,
        std::min(limits.max_faces, storage.max_faces), stage);
  Count(view.corner_vertices.size(),
        std::min(limits.max_corners, storage.max_corners), stage);
  Count(view.attributes.size(), storage.max_attributes, stage);
  std::uint64_t scalars = 0, offsets = 0, presence = 0, metadata = 0,
                descriptors = 0;
  std::uint64_t list_rows =
      view.face_offsets.empty() ? 0 : view.face_offsets.size() - 1;
  Count(list_rows, limits.max_list_rows, stage);
  for (const auto& attribute : view.attributes) {
    session.Check(stage);
    Accumulate(descriptors, attribute.name.size(), storage.max_descriptor_bytes,
               stage);
    Accumulate(descriptors, attribute.semantic.size(),
               storage.max_descriptor_bytes, stage);
    Accumulate(metadata, attribute.metadata.size(),
               storage.max_metadata_entries, stage);
    for (const auto& entry : attribute.metadata) {
      session.Check(stage);
      Accumulate(descriptors, entry.key.size(), storage.max_descriptor_bytes,
                 stage);
      Accumulate(descriptors, entry.value.size(), storage.max_descriptor_bytes,
                 stage);
    }
    const auto count =
        std::visit([](auto values) { return values.size(); }, attribute.values);
    Accumulate(
        scalars, count,
        std::min(storage.max_attribute_scalars, limits.max_property_values),
        stage);
    if (attribute.offsets) {
      Accumulate(offsets, attribute.offsets->size(),
                 storage.max_attribute_offsets, stage);
      Accumulate(list_rows,
                 attribute.offsets->empty() ? 0 : attribute.offsets->size() - 1,
                 limits.max_list_rows, stage);
    }
    if (attribute.present)
      Accumulate(presence, attribute.present->size(),
                 storage.max_presence_bytes, stage);
  }
}
template <class T>
void CompareValues(const Session& session, std::span<const T> expected,
                   std::span<const T> actual,
                   geo::MaterializationExecutionStats& statistics) {
  if (expected.size() != actual.size())
    Fail(Status::kPreservationMismatch, Stage::kVerification,
         Code::kAttributeMismatch);
  auto aligned = session.work;
  const auto width = static_cast<std::uint64_t>(sizeof(T));
  const auto align_down = [width](std::uint64_t bytes) {
    return std::max(width, bytes - bytes % width);
  };
  aligned.partition_bytes = align_down(aligned.partition_bytes);
  aligned.io_block_bytes = align_down(aligned.io_block_bytes);
  aligned.cancellation_block_bytes =
      align_down(aligned.cancellation_block_bytes);
  const Session typed{session.allocation, session.context, aligned,
                      session.testing};
  Compare(typed, std::as_bytes(expected), std::as_bytes(actual),
          Stage::kVerification, kEntryIndex, statistics, sizeof(T));
}
void CompareText(const Session& session, std::string_view expected,
                 std::string_view actual,
                 geo::MaterializationExecutionStats& statistics) {
  CompareValues(session, std::span(expected.data(), expected.size()),
                std::span(actual.data(), actual.size()), statistics);
}
void CompareAttribute(const Session& session,
                      const geo::AttributeReadView& expected,
                      const geo::AttributeReadView& actual,
                      geo::MaterializationExecutionStats& statistics) {
  if (expected.domain != actual.domain ||
      expected.components != actual.components ||
      expected.set_index != actual.set_index ||
      expected.values.index() != actual.values.index() ||
      expected.offsets.has_value() != actual.offsets.has_value() ||
      expected.present.has_value() != actual.present.has_value() ||
      expected.metadata.size() != actual.metadata.size())
    Fail(Status::kPreservationMismatch, Stage::kVerification,
         Code::kAttributeMismatch);
  CompareText(session, expected.name, actual.name, statistics);
  CompareText(session, expected.semantic, actual.semantic, statistics);
  std::visit(
      [&](auto values) {
        using Span = decltype(values);
        CompareValues(session, values, std::get<Span>(actual.values),
                      statistics);
      },
      expected.values);
  if (expected.offsets)
    CompareValues(session, *expected.offsets, *actual.offsets, statistics);
  if (expected.present)
    CompareValues(session, *expected.present, *actual.present, statistics);
  for (std::size_t i = 0; i < expected.metadata.size(); ++i) {
    session.Check(Stage::kVerification, i);
    CompareText(session, expected.metadata[i].key, actual.metadata[i].key,
                statistics);
    CompareText(session, expected.metadata[i].value, actual.metadata[i].value,
                statistics);
  }
}
void CompareMesh(const Session& session, const geo::MaterializedMesh& expected,
                 const PlyDocument& actual,
                 geo::MaterializationExecutionStats& statistics) {
  const auto first = expected.View(), second = actual.Mesh().View();
  if (first.positions.size() != second.positions.size() ||
      first.face_offsets.size() != second.face_offsets.size() ||
      first.corner_vertices.size() != second.corner_vertices.size() ||
      first.attributes.size() != second.attributes.size())
    Fail(Status::kPreservationMismatch, Stage::kVerification,
         Code::kMeshMismatch);
  CompareValues(session, first.positions, second.positions, statistics);
  CompareValues(session, first.face_offsets, second.face_offsets, statistics);
  CompareValues(session, first.corner_vertices, second.corner_vertices,
                statistics);
  for (std::size_t i = 0; i < first.attributes.size(); ++i) {
    session.Check(Stage::kVerification, i);
    CompareAttribute(session, first.attributes[i], second.attributes[i],
                     statistics);
  }
}
void CompareSchema(const Session& session, const PlyDocument& source,
                   const PlyDocument& actual,
                   const geo::MaterializedMesh& expected, PlyEncoding encoding,
                   geo::MaterializationExecutionStats& statistics) {
  const auto first = source.Schema(), second = actual.Schema();
  if (first.size() != second.size() || actual.Encoding() != encoding)
    Fail(Status::kPreservationMismatch, Stage::kVerification,
         Code::kSchemaMismatch);
  const auto view = expected.View();
  for (std::size_t i = 0; i < first.size(); ++i) {
    session.Check(Stage::kVerification, i);
    const auto& a = first[i];
    const auto& b = second[i];
    const auto rows = a.kind == PlyElementKind::kVertex
                          ? view.positions.size() / kPositionWidth
                          : view.face_offsets.size() - 1;
    if (a.kind != b.kind || b.original_count != rows ||
        a.properties.size() != b.properties.size())
      Fail(Status::kPreservationMismatch, Stage::kVerification,
           Code::kSchemaMismatch);
    for (std::size_t j = 0; j < a.properties.size(); ++j) {
      session.Check(Stage::kVerification, j);
      const auto& p = a.properties[j];
      const auto& q = b.properties[j];
      if (p.kind != q.kind || p.value_type != q.value_type ||
          p.count_type != q.count_type || p.binding != q.binding ||
          p.attribute != q.attribute)
        Fail(Status::kPreservationMismatch, Stage::kVerification,
             Code::kSchemaMismatch);
      CompareText(session, p.name, q.name, statistics);
    }
  }
}
void Facts(const PlyFilePublicationRequest& request, std::uint64_t properties) {
  const auto view = request.expected.View();
  const auto original = request.source.Document().Mesh().View();
  const auto& facts = request.output.Facts();
  const auto encoding = facts.encoding;
  if ((encoding != PlyEncoding::kAscii &&
       encoding != PlyEncoding::kBinaryLittleEndian &&
       encoding != PlyEncoding::kBinaryBigEndian) ||
      facts.source_encoding != request.source.Document().Encoding() ||
      facts.source_vertex_count != original.positions.size() / kPositionWidth ||
      facts.source_face_count != original.face_offsets.size() - 1 ||
      facts.rewrite_policy != PlyRewritePolicy::kCanonical ||
      facts.correspondence != PlyCorrespondence::kIdentity ||
      facts.property_count != properties ||
      facts.vertex_count != view.positions.size() / kPositionWidth ||
      facts.face_count != view.face_offsets.size() - 1 ||
      facts.corner_count != view.corner_vertices.size())
    Fail(Status::kPreservationMismatch, Stage::kVerification,
         Code::kOutputFacts);
}
}  // namespace

PlyFileReadResult ReadForTesting(const std::filesystem::path& input,
                                 const PlyFileReadLimits& limits,
                                 const PlyFileReadWorkPolicy& work,
                                 const geo::ExecutionContext& context,
                                 std::stop_token stop,
                                 const TestOptions& testing) {
  PlyFileReadResult result;
  auto phase = Stage::kConfiguration;
  try {
    Validate(limits.files, work.files);
    Validate(limits.ply, work.ply);
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
    phase = Stage::kPlyRead;
    auto read = ReadPly(bytes.Bytes(), limits.ply, work.ply, context, stop);
    result.execution.ply = read.execution;
    RequirePly(read, phase);
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
PlyFilePublicationResult PublishForTesting(
    const PlyFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const PlyFilePublicationOptions& options,
    const PlyFilePublicationLimits& limits,
    const PlyFilePublicationWorkPolicy& work, std::stop_token stop,
    const TestOptions& testing) {
  PlyFilePublicationResult result;
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
    const auto properties =
        BoundSchema(session, request.source.Document(), limits.reload);
    BoundExpected(session, request.expected, limits.reload);
    auto inspection = geo::InspectMaterializedMesh(
        request.expected, limits.expected_inspection, work.expected_inspection,
        stop);
    result.execution.expected_inspection = inspection.execution;
    if (inspection.status != geo::MaterializationStatus::kSuccess ||
        !inspection.inspection || !inspection.inspection->IsStorageValid()) {
      PlyFileDiagnostic diagnostic;
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
    Facts(request, properties);
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
          ReadPly(bytes.Bytes(), limits.reload, work.reload, context, stop);
      result.execution.ply = read.execution;
      RequirePly(read, phase);
      state->document.emplace(*read.document);
    }
    phase = Stage::kVerification;
    Compare(session, request.output.Bytes(), state->document->OriginalBytes(),
            phase, kEntryIndex, result.execution.byte_verification);
    CompareMesh(session, request.expected, *state->document,
                result.execution.mesh_verification);
    CompareSchema(session, request.source.Document(), *state->document,
                  request.expected, request.output.Facts().encoding,
                  result.execution.mesh_verification);
    phase = Stage::kInventory;
    transaction->Inventory(false);
    if (options.receipt) {
      phase = Stage::kProvider;
      session.Check(phase);
      std::span<const std::byte> supplied;
      try {
        supplied = options.receipt->provide(
            PlyFileVerifiedContent{*state->document, state->entry_name,
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
    static_assert(std::is_nothrow_copy_constructible_v<PlyPublishedBundle>);
    phase = Stage::kPublication;
    transaction->Commit();
    // Successful native commit is the final fallible action.
    result.bundle.emplace(bundle);
    result.outcome = PlyFilePublicationOutcome::kPublished;
    result.phase = Stage::kComplete;
    result.diagnostic.stage = Stage::kComplete;
  } catch (const Failure& failure) {
    result.diagnostic = failure.diagnostic;
    result.phase = failure.diagnostic.stage;
    result.outcome = failure.diagnostic.status == Status::kCanceled
                         ? PlyFilePublicationOutcome::kCanceled
                         : PlyFilePublicationOutcome::kFailed;
    cleanup();
  } catch (const allocation::AdmissionFailure& failure) {
    result.diagnostic = Admission(failure, phase);
    result.phase = phase;
    result.outcome = result.diagnostic.status == Status::kCanceled
                         ? PlyFilePublicationOutcome::kCanceled
                         : PlyFilePublicationOutcome::kFailed;
    cleanup();
  } catch (...) {
    cleanup();
    throw;
  }
  return result;
}
}  // namespace meshvale::interchange::ply_files_detail

namespace meshvale::interchange {
PlyFileAsset::PlyFileAsset(
    std::shared_ptr<const ply_files_detail::AssetState> state) noexcept
    : state_(std::move(state)) {}
PlyFileAsset::PlyFileAsset(const PlyFileAsset&) noexcept = default;
PlyFileAsset& PlyFileAsset::operator=(const PlyFileAsset&) noexcept = default;
PlyFileAsset::~PlyFileAsset() noexcept = default;
const PlyDocument& PlyFileAsset::Document() const noexcept {
  return *state_->document;
}
std::string_view PlyFileAsset::EntryPath() const noexcept {
  return state_->name;
}
std::span<const std::byte> PlyFileAsset::EntryBytes() const noexcept {
  return Document().OriginalBytes();
}
std::uint64_t PlyFileAsset::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
PlyPublishedBundle::PlyPublishedBundle(
    std::shared_ptr<const ply_files_detail::BundleState> state) noexcept
    : state_(std::move(state)) {}
PlyPublishedBundle::PlyPublishedBundle(const PlyPublishedBundle&) noexcept =
    default;
PlyPublishedBundle& PlyPublishedBundle::operator=(
    const PlyPublishedBundle&) noexcept = default;
PlyPublishedBundle::~PlyPublishedBundle() noexcept = default;
const PlyFileAsset& PlyPublishedBundle::Source() const noexcept {
  return state_->source;
}
const geometry::MaterializedMesh& PlyPublishedBundle::ExpectedMesh()
    const noexcept {
  return state_->expected;
}
const PlyDocument& PlyPublishedBundle::Document() const noexcept {
  return *state_->document;
}
std::string_view PlyPublishedBundle::EntryPath() const noexcept {
  return state_->entry_name;
}
std::span<const std::byte> PlyPublishedBundle::EntryBytes() const noexcept {
  return Document().OriginalBytes();
}
std::optional<std::string_view> PlyPublishedBundle::ReceiptPath()
    const noexcept {
  return state_->receipt ? std::optional<std::string_view>(state_->receipt_name)
                         : std::nullopt;
}
std::optional<std::span<const std::byte>> PlyPublishedBundle::ReceiptBytes()
    const noexcept {
  return state_->receipt ? std::optional<std::span<const std::byte>>(
                               state_->receipt->Bytes())
                         : std::nullopt;
}
std::uint64_t PlyPublishedBundle::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
PlyFileReadResult ReadPlyFileAsset(const std::filesystem::path& input,
                                   const PlyFileReadLimits& limits,
                                   const PlyFileReadWorkPolicy& work,
                                   const geometry::ExecutionContext& context,
                                   std::stop_token stop) {
  return ply_files_detail::ReadForTesting(input, limits, work, context, stop,
                                          {});
}
PlyFilePublicationResult PublishPlyBundle(
    const PlyFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const PlyFilePublicationOptions& options,
    const PlyFilePublicationLimits& limits,
    const PlyFilePublicationWorkPolicy& work, std::stop_token stop) {
  return ply_files_detail::PublishForTesting(request, destination, options,
                                             limits, work, stop, {});
}
}  // namespace meshvale::interchange
