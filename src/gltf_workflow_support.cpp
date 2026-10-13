// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_workflow_support_internal.h"
#include "gltf_workflow_support_test_internal.h"

namespace meshvale::interchange::gltf_workflow_support_detail {
Diagnostic Location(Stage stage, Code code,
                    std::optional<geometry::index_t> attribute,
                    std::optional<geometry::index_t> element) {
  Diagnostic result;
  result.stage = stage;
  result.code = code;
  result.attribute = attribute;
  result.element = element;
  return result;
}
void Fail(Status status, Stage stage, Code code,
          std::optional<geometry::index_t> attribute,
          std::optional<geometry::index_t> element,
          std::optional<std::uint64_t> resource,
          std::optional<GltfSourceSpan> span) {
  auto diagnostic = Location(stage, code, attribute, element);
  diagnostic.resource = resource;
  diagnostic.json_span = span;
  throw Failure(status, diagnostic);
}
std::size_t Add(std::size_t first, std::size_t second) {
  if (second > std::numeric_limits<std::size_t>::max() - first)
    Fail(Status::kLimitExceeded, Stage::kAdmission, Code::kCountLimit);
  return first + second;
}
void Count(std::uint64_t actual, std::uint64_t maximum) {
  if (actual > maximum)
    Fail(Status::kLimitExceeded, Stage::kAdmission, Code::kCountLimit);
}
InspectionState::InspectionState(
    const gltf_detail::AllocationContext& allocation,
    const GltfTopologyOutputInspectionRequest& request)
    : allocation(allocation),
      source(request.source),
      candidate(request.candidate),
      output(request.output),
      reloaded_asset(request.reloaded_asset),
      reloaded_extraction(request.reloaded_extraction),
      faces(gltf_detail::Allocator<geometry::index_t>(allocation)),
      corners(gltf_detail::Allocator<geometry::index_t>(allocation)),
      original_corners(gltf_detail::Allocator<GltfSourceCorner>(allocation)) {}
GltfTopologyOutputInspection Access::Make(
    std::shared_ptr<const InspectionState> state) {
  return GltfTopologyOutputInspection(std::move(state));
}
namespace {
struct Slot {
  std::exception_ptr exception;
  std::optional<Diagnostic> diagnostic;
  bool canceled = false;
};
struct Team {
  RangeCheck check;
  void* state;
  std::size_t bytes;
  std::size_t width;
  std::span<Slot> slots;
};
void CheckRange(void* opaque, std::size_t ordinal) noexcept {
  auto& team = *static_cast<Team*>(opaque);
  auto& slot = team.slots[ordinal];
  const auto begin = ordinal * team.width;
  const auto end = begin + std::min(team.width, team.bytes - begin);
  try {
    slot.diagnostic = team.check(team.state, begin, end, false);
  } catch (const gltf_detail::AdmissionFailure& failure) {
    if (failure.Code() == gltf_detail::AdmissionCode::kCanceled)
      slot.canceled = true;
    else
      slot.exception = std::current_exception();
  } catch (...) {
    slot.exception = std::current_exception();
  }
}
Status MismatchStatus(Stage stage) {
  return stage == Stage::kCorrespondence ? Status::kInvalidCorrespondence
                                         : Status::kOutputMismatch;
}
Diagnostic ByteLocation(Diagnostic diagnostic, std::size_t byte,
                        std::size_t scalar_bytes) {
  if (scalar_bytes)
    diagnostic.element = diagnostic.element.value_or(0) + byte / scalar_bytes;
  if (diagnostic.json_span) {
    diagnostic.json_span->offset += byte;
    diagnostic.json_span->length = 1;
  }
  return diagnostic;
}
void Configuration(const GltfTopologyOutputInspectionLimits& limits,
                   const GltfTopologyOutputInspectionWorkPolicy& work) {
  const std::uint64_t fields[] = {limits.max_json_bytes,
                                  limits.max_json_nodes,
                                  limits.max_json_depth,
                                  limits.max_resources,
                                  limits.max_resource_bytes,
                                  limits.max_vertices,
                                  limits.max_channels,
                                  limits.max_attribute_scalars,
                                  limits.max_descriptor_bytes,
                                  limits.max_candidate_faces,
                                  limits.max_candidate_corners,
                                  limits.max_index_bytes,
                                  limits.max_new_requested_bytes,
                                  work.parallel_min_bytes,
                                  work.partition_bytes,
                                  work.cancellation_block_bytes,
                                  work.max_jobs};
  for (auto field : fields) {
    if (field == 0)
      Fail(Status::kInvalidConfiguration, Stage::kAdmission,
           Code::kRequiredFieldZero);
    if (field >
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
      Fail(Status::kInvalidConfiguration, Stage::kAdmission,
           Code::kConfigurationExtent);
  }
}
bool SamePrimitive(const GltfAsset& asset, const GltfPrimitiveHandle& first,
                   const GltfPrimitiveHandle& second) {
  return gltf_detail::Access::Owner(asset) ==
             gltf_detail::Access::Owner(first) &&
         gltf_detail::Access::Owner(first) ==
             gltf_detail::Access::Owner(second) &&
         first.MeshIndex() == second.MeshIndex() &&
         first.PrimitiveIndex() == second.PrimitiveIndex();
}
void Identity(const GltfTopologyOutputInspectionRequest& request,
              const geometry::ExecutionContext& context) {
  const auto& original = request.source.SourceAsset();
  const auto& primitive = request.source.SourcePrimitive();
  if (!geometry::SharesExecutionBudget(
          context, request.candidate.Source().ShareExecutionContext()) ||
      !geometry::SharesExecutionBudget(
          context, gltf_detail::Access::State(original).context) ||
      !geometry::SharesExecutionBudget(
          context, request.output.Source().Mesh().ShareExecutionContext()) ||
      !geometry::SharesExecutionBudget(
          context,
          gltf_detail::Access::State(request.output.OriginalAsset()).context) ||
      !geometry::SharesExecutionBudget(
          context,
          gltf_detail::Access::State(request.reloaded_asset).context) ||
      !geometry::SharesExecutionBudget(
          context,
          gltf_detail::Access::ExtractionContext(request.reloaded_extraction)))
    Fail(Status::kForeignBudget, Stage::kAdmission, Code::kForeignBudget);
  if (gltf_detail::Access::Owner(original) !=
          gltf_detail::Access::Owner(request.output.OriginalAsset()) ||
      gltf_detail::Access::Owner(original) !=
          gltf_detail::Access::Owner(request.output.Source().SourceAsset()) ||
      !SamePrimitive(original, primitive, request.output.OriginalPrimitive()) ||
      !SamePrimitive(original, primitive,
                     request.output.Source().SourcePrimitive()))
    Fail(Status::kSourceContractFailure, Stage::kAdmission,
         Code::kSourceIdentity);
  const auto reload_asset =
      gltf_detail::Access::ExtractionAsset(request.reloaded_extraction);
  const auto reload_primitive =
      gltf_detail::Access::ExtractionPrimitive(request.reloaded_extraction);
  if (gltf_detail::Access::Owner(reload_asset) !=
          gltf_detail::Access::Owner(request.reloaded_asset) ||
      gltf_detail::Access::Owner(reload_primitive) !=
          gltf_detail::Access::Owner(request.reloaded_asset) ||
      primitive.MeshIndex() != reload_primitive.MeshIndex() ||
      primitive.PrimitiveIndex() != reload_primitive.PrimitiveIndex())
    Fail(Status::kSourceContractFailure, Stage::kAdmission,
         Code::kReloadIdentity);
  if (!request.candidate.IsStorageValid()) {
    auto diagnostic = Location(Stage::kAdmission, Code::kCandidateStorage);
    if (!request.candidate.Issues().empty())
      diagnostic.storage_issue = request.candidate.Issues().front();
    throw Failure(Status::kInvalidCandidateStorage, diagnostic);
  }
}
}  // namespace
void RunRanges(const Operation& operation, std::size_t bytes, RangeCheck check,
               void* state, Stats& stats, std::size_t unit_bytes) {
  if (bytes == 0) return;
  const auto desired = std::min<std::size_t>(
      operation.work.max_jobs,
      1 + (bytes - 1) /
              static_cast<std::size_t>(operation.work.partition_bytes));
  const auto units = 1 + (bytes - 1) / unit_bytes;
  const auto width =
      gltf_detail::CheckedProduct(1 + (units - 1) / desired, unit_bytes);
  const auto jobs = 1 + (bytes - 1) / width;
  Vector<Slot> slots{gltf_detail::Allocator<Slot>(operation.allocation)};
  slots.resize(jobs);
  Team team{check, state, bytes, width, slots};
  stats.planned_jobs += jobs;
  auto lease = bytes >= operation.work.parallel_min_bytes
                   ? geometry::TryReserveWorkers(operation.context, jobs)
                   : geometry::WorkerLease();
  if (lease.Count()) {
    gltf_detail::OperationWorkerStats actual;
    try {
      gltf_detail::RunOperationJobs(operation.allocation, lease, jobs,
                                    CheckRange, &team, operation.execution,
                                    actual);
    } catch (...) {
      stats.launched_workers += actual.launched;
      stats.joined_workers += actual.joined;
      throw;
    }
    stats.launched_workers += actual.launched;
    stats.joined_workers += actual.joined;
  } else {
    for (std::size_t ordinal = 0; ordinal < jobs; ++ordinal) {
      ++stats.caller_jobs;
      if (operation.execution.observer &&
          operation.execution.observer->job_entered)
        operation.execution.observer->job_entered(
            ordinal, operation.execution.observer->state);
      CheckRange(&team, ordinal);
    }
  }
  for (const auto& slot : slots)
    if (slot.exception) std::rethrow_exception(slot.exception);
  std::optional<std::size_t> first;
  bool canceled = false;
  for (std::size_t ordinal = 0; ordinal < jobs; ++ordinal) {
    canceled |= slots[ordinal].canceled;
    if (!first && slots[ordinal].diagnostic) first = ordinal;
  }
  if (first) {
    const auto begin = *first * width;
    const auto end = begin + std::min(width, bytes - begin);
    const auto diagnostic =
        canceled ? check(state, 0, end, true) : slots[*first].diagnostic;
    if (diagnostic)
      throw Failure(MismatchStatus(diagnostic->stage), *diagnostic);
  }
  if (canceled || operation.stop.stop_requested())
    throw gltf_detail::AdmissionFailure(gltf_detail::AdmissionCode::kCanceled);
}
BytePlan::BytePlan(const Operation& operation)
    : operation_(operation),
      checks_(gltf_detail::Allocator<ByteCheck>(operation.allocation)) {}
void BytePlan::Compare(std::span<const std::byte> expected,
                       std::span<const std::byte> actual, Diagnostic diagnostic,
                       std::size_t scalar_bytes) {
  if (expected.size() != actual.size() && !known_failure_) {
    const auto common = std::min(expected.size(), actual.size());
    known_failure_ = ByteLocation(diagnostic, common, scalar_bytes);
    known_prefix_ = Add(bytes_, common);
    known_pending_ =
        ByteCheck{expected, actual, diagnostic, scalar_bytes, bytes_};
  }
  try {
    checks_.push_back({expected, actual, diagnostic, scalar_bytes, bytes_});
  } catch (const gltf_detail::AdmissionFailure& failure) {
    if (failure.Code() == gltf_detail::AdmissionCode::kCanceled &&
        known_failure_)
      FinalizeKnownFailure();
    throw;
  }
  bytes_ =
      Add(bytes_, std::max<std::size_t>({1, expected.size(), actual.size()}));
}
void BytePlan::Compare(std::string_view expected, std::string_view actual,
                       Diagnostic diagnostic) {
  Compare(std::as_bytes(std::span(expected.data(), expected.size())),
          std::as_bytes(std::span(actual.data(), actual.size())), diagnostic);
}
void BytePlan::Require(bool condition, Diagnostic diagnostic) {
  if (condition) return;
  if (!known_failure_) {
    known_failure_ = diagnostic;
    known_prefix_ = bytes_;
  }
  static constexpr std::byte kMismatchMarker{1};
  Compare({}, std::span(&kMismatchMarker, 1), diagnostic);
}
void BytePlan::Checkpoint() {
  try {
    operation_.allocation.Checkpoint();
  } catch (const gltf_detail::AdmissionFailure& failure) {
    if (failure.Code() == gltf_detail::AdmissionCode::kCanceled &&
        known_failure_)
      FinalizeKnownFailure();
    throw;
  }
}
void BytePlan::FinalizeKnownFailure() {
  const auto earlier = Check(this, 0, known_prefix_, true);
  if (earlier) throw Failure(MismatchStatus(earlier->stage), *earlier);
  // If cancellation refused the plan record itself, its borrowed spans still
  // permit allocation-free validation of the earlier common byte prefix.
  if (known_pending_) {
    const auto& pending = *known_pending_;
    const auto common =
        std::min(pending.expected.size(), pending.actual.size());
    for (std::size_t byte = 0; byte < common; ++byte)
      if (pending.expected[byte] != pending.actual[byte]) {
        const auto diagnostic =
            ByteLocation(pending.diagnostic, byte, pending.scalar_bytes);
        throw Failure(MismatchStatus(diagnostic.stage), diagnostic);
      }
  }
  throw Failure(MismatchStatus(known_failure_->stage), *known_failure_);
}
std::optional<Diagnostic> BytePlan::Check(void* opaque, std::size_t begin,
                                          std::size_t end, bool finalizing) {
  auto& plan = *static_cast<BytePlan*>(opaque);
  auto current =
      std::upper_bound(plan.checks_.begin(), plan.checks_.end(), begin,
                       [](std::size_t position, const ByteCheck& check) {
                         return position < check.prefix;
                       });
  if (current != plan.checks_.begin()) --current;
  for (; current != plan.checks_.end() && current->prefix < end; ++current) {
    if (!finalizing) plan.operation_.allocation.Checkpoint();
    const auto common =
        std::min(current->expected.size(), current->actual.size());
    const auto local_begin =
        begin > current->prefix ? begin - current->prefix : 0;
    const auto local_end = std::min(common, end - current->prefix);
    for (auto byte = local_begin; byte < local_end; ++byte) {
      if (!finalizing &&
          byte % plan.operation_.work.cancellation_block_bytes == 0)
        plan.operation_.allocation.Checkpoint();
      if (current->expected[byte] == current->actual[byte]) continue;
      return ByteLocation(current->diagnostic, byte, current->scalar_bytes);
    }
    if (current->expected.size() != current->actual.size() &&
        local_begin <= common && common < end - current->prefix)
      return ByteLocation(current->diagnostic, common, current->scalar_bytes);
  }
  return {};
}
void BytePlan::Run(Stats& stats) {
  try {
    RunRanges(operation_, bytes_, Check, this, stats);
  } catch (const gltf_detail::AdmissionFailure& failure) {
    if (failure.Code() == gltf_detail::AdmissionCode::kCanceled &&
        known_failure_)
      FinalizeKnownFailure();
    throw;
  }
}

GltfTopologyOutputInspectionResult InspectGltfTopologyOutputForTest(
    const GltfTopologyOutputInspectionRequest& request,
    const GltfTopologyOutputInspectionLimits& limits,
    const GltfTopologyOutputInspectionWorkPolicy& work, std::stop_token stop,
    const InspectionTestOptions& options) {
  GltfTopologyOutputInspectionResult result;
  Stage stage = Stage::kAdmission;
  try {
    Configuration(limits, work);
    AdmitInputs(request, limits, work, stop);
    auto context = request.source.Mesh().ShareExecutionContext();
    Identity(request, context);
    gltf_detail::AllocationContext allocation(context, stop,
                                              options.allocation_backend);
    allocation = allocation.WithRequestLimit(limits.max_new_requested_bytes);
    Operation operation{request,
                        limits,
                        work,
                        context,
                        allocation,
                        stop,
                        {options.worker_backend, options.observer}};
    auto state = std::allocate_shared<InspectionState>(
        gltf_detail::Allocator<InspectionState>(allocation), allocation,
        request);
    stage = Stage::kSourceCandidate;
    VerifyGeometry(operation, result.execution.source_candidate);
    result.coverage.source_candidate =
        GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kCorrespondence;
    VerifyCorrespondence(operation, result.execution.correspondence);
    result.coverage.correspondence = GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kLexical;
    VerifyLexical(operation, result.execution.lexical);
    result.coverage.lexical = GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kResources;
    VerifyResources(operation, result.execution.resources);
    result.coverage.resources = GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kReloadedPrimitive;
    VerifyReload(operation, result.execution.reloaded_primitive);
    result.coverage.reloaded_primitive =
        GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kProvenance;
    VerifyProvenance(operation, *state, result.execution.provenance);
    result.coverage.provenance = GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kPublication;
    allocation.Checkpoint();
    state->coverage = result.coverage;
    result.inspection = Access::Make(std::move(state));
    result.status = Status::kSuccess;
    result.diagnostic = Location(stage, Code::kNone);
  } catch (const Failure& failure) {
    result.status = failure.status;
    result.diagnostic = failure.diagnostic;
  } catch (const gltf_detail::AdmissionFailure& failure) {
    auto code = Code::kInspectionRequestLimit;
    auto status = Status::kLimitExceeded;
    switch (failure.Code()) {
      case gltf_detail::AdmissionCode::kSizeLimit:
        break;
      case gltf_detail::AdmissionCode::kBudgetExceeded:
        code = Code::kRequestBudget;
        status = Status::kBudgetExceeded;
        break;
      case gltf_detail::AdmissionCode::kCanceled:
        code = Code::kStopRequested;
        status = Status::kCanceled;
        break;
      case gltf_detail::AdmissionCode::kInvalidObject:
        throw;
    }
    result.status = status;
    result.diagnostic = Location(stage, code);
  }
  return result;
}
}  // namespace meshvale::interchange::gltf_workflow_support_detail

namespace meshvale::interchange {
GltfTopologyOutputInspection::GltfTopologyOutputInspection(
    std::shared_ptr<const gltf_workflow_support_detail::InspectionState>
        owner) noexcept
    : owner_(std::move(owner)) {}
GltfTopologyOutputInspection::GltfTopologyOutputInspection(
    const GltfTopologyOutputInspection&) noexcept = default;
GltfTopologyOutputInspection& GltfTopologyOutputInspection::operator=(
    const GltfTopologyOutputInspection&) noexcept = default;
GltfTopologyOutputInspection::~GltfTopologyOutputInspection() noexcept =
    default;
const GltfMaterializedPrimitive& GltfTopologyOutputInspection::Source()
    const noexcept {
  return owner_->source;
}
const geometry::MeshInspection& GltfTopologyOutputInspection::Candidate()
    const noexcept {
  return owner_->candidate;
}
const GltfTopologyWriteBundle& GltfTopologyOutputInspection::Output()
    const noexcept {
  return owner_->output;
}
const GltfAsset& GltfTopologyOutputInspection::ReloadedAsset() const noexcept {
  return owner_->reloaded_asset;
}
const GltfPrimitiveExtraction&
GltfTopologyOutputInspection::ReloadedExtraction() const noexcept {
  return owner_->reloaded_extraction;
}
std::span<const geometry::index_t> GltfTopologyOutputInspection::SourceFaces()
    const noexcept {
  return owner_->faces;
}
std::span<const geometry::index_t> GltfTopologyOutputInspection::SourceCorners()
    const noexcept {
  return owner_->corners;
}
std::span<const GltfSourceCorner>
GltfTopologyOutputInspection::OriginalCorners() const noexcept {
  return owner_->original_corners;
}
const GltfTopologyOutputCoverage& GltfTopologyOutputInspection::Coverage()
    const noexcept {
  return owner_->coverage;
}
std::uint64_t GltfTopologyOutputInspection::NewRequestedBytes() const noexcept {
  return owner_->allocation.OperationActiveBytes();
}
GltfTopologyOutputInspectionResult InspectGltfTopologyOutput(
    const GltfTopologyOutputInspectionRequest& request,
    const GltfTopologyOutputInspectionLimits& limits,
    const GltfTopologyOutputInspectionWorkPolicy& work, std::stop_token stop) {
  return gltf_workflow_support_detail::InspectGltfTopologyOutputForTest(
      request, limits, work, stop, {});
}
}  // namespace meshvale::interchange
