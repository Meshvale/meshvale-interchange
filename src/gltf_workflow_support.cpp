// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <bit>
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
#include "gltf_deformation_read.h"
#include "gltf_deformation_work.h"
#include "gltf_direct_translation.h"
#include "gltf_numerical_profile.h"
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
constexpr std::size_t kMinimumParallelJobs = 2;
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
  gltf_detail::DeformationWork* deformation;
  std::size_t unit_bytes;
  std::span<gltf_detail::DeformationWorkCounters> counters;
};
std::pair<std::size_t, std::size_t> RangeBounds(const Team& team,
                                                std::size_t ordinal) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (team.deformation) {
    const auto units = 1 + (team.bytes - 1) / team.unit_bytes;
    const auto base = units / team.slots.size();
    const auto extra = units % team.slots.size();
    const auto first = ordinal * base + std::min(ordinal, extra);
    begin = first * team.unit_bytes;
    const auto count = base + (ordinal < extra ? 1 : 0);
    end = begin + std::min(team.bytes - begin, count * team.unit_bytes);
  } else {
    begin = ordinal * team.width;
    end = begin + std::min(team.width, team.bytes - begin);
  }
  return {begin, end};
}
void CheckRange(void* opaque, std::size_t ordinal) noexcept {
  auto& team = *static_cast<Team*>(opaque);
  auto& slot = team.slots[ordinal];
  const auto [begin, end] = RangeBounds(team, ordinal);
  try {
    std::optional<gltf_detail::DeformationWork::Local> local;
    if (team.deformation)
      local.emplace(*team.deformation, team.counters[ordinal]);
    slot.diagnostic =
        team.check(team.state, begin, end, false, local ? &*local : nullptr);
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
void TranslationFailure(const gltf_detail::DirectTranslationResult& result,
                        Stage stage) {
  const auto& diagnostic = result.diagnostic;
  switch (diagnostic.status) {
    case GltfStatus::kUnsupportedProfile:
      if (diagnostic.code == GltfDiagnosticCode::kLimitExceeded ||
          diagnostic.code == GltfDiagnosticCode::kSizeOverflow)
        Fail(Status::kLimitExceeded, stage,
             result.request_limit ? Code::kInspectionRequestLimit
                                  : Code::kCountLimit);
      Fail(Status::kUnsupportedSelection, stage, Code::kSourceIdentity);
    case GltfStatus::kBudgetExceeded:
      Fail(Status::kBudgetExceeded, stage, Code::kRequestBudget);
    case GltfStatus::kCanceled:
      Fail(Status::kCanceled, stage, Code::kStopRequested);
    case GltfStatus::kResourceBindingMismatch:
      Fail(Status::kOutputMismatch, stage, Code::kSourceProvenance, {}, {}, {},
           diagnostic.json_span);
    default:
      Fail(Status::kSourceContractFailure, stage, Code::kSourceProvenance, {},
           {}, {}, diagnostic.json_span);
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
namespace {
void RunRangesImpl(const Operation& operation, std::size_t bytes,
                   RangeCheck check, void* state, Stats& stats,
                   std::size_t unit_bytes,
                   const gltf_detail::DeformationRangeExtent* extent) {
  if (bytes == 0) return;
  auto desired = std::min<std::size_t>(
      operation.work.max_jobs,
      1 + (bytes - 1) /
              static_cast<std::size_t>(operation.work.partition_bytes));
  bool eligible = bytes >= operation.work.parallel_min_bytes;
  const auto units = 1 + (bytes - 1) / unit_bytes;
  if (operation.deformation_work) {
    const auto& policy = operation.deformation_work->Policy();
    const gltf_detail::DeformationRangeExtent byte_extent{0, 0, 0, bytes};
    const auto& bound = extent ? *extent : byte_extent;
    const auto scalar = operation.deformation_work->ScalarWork(bound);
    const auto scalar_jobs = scalar / policy.partition_scalars +
                             (scalar % policy.partition_scalars != 0);
    const auto byte_jobs = bound.bytes / policy.partition_bytes +
                           (bound.bytes % policy.partition_bytes != 0);
    eligible = scalar >= policy.parallel_min_scalars ||
               bound.bytes >= policy.parallel_min_bytes;
    desired = static_cast<std::size_t>(std::min<std::uint64_t>(
        {units, policy.max_jobs,
         std::max<std::uint64_t>(1, std::max(scalar_jobs, byte_jobs))}));
    if (!eligible) desired = 1;
  }
  const auto width =
      gltf_detail::CheckedProduct(1 + (units - 1) / desired, unit_bytes);
  const auto jobs =
      operation.deformation_work ? desired : 1 + (bytes - 1) / width;
  Vector<Slot> slots{gltf_detail::Allocator<Slot>(operation.allocation)};
  slots.resize(jobs);
  Vector<gltf_detail::DeformationWorkCounters> counters{
      gltf_detail::Allocator<gltf_detail::DeformationWorkCounters>(
          operation.allocation)};
  if (operation.deformation_work) counters.resize(jobs);
  Team team{check,      state,   bytes,
            width,      slots,   operation.deformation_work,
            unit_bytes, counters};
  auto merge = [&] {
    if (!operation.deformation_work) return;
    for (const auto& partial : counters)
      operation.deformation_work->Merge(partial);
    operation.deformation_work->CheckMerged();
  };
  stats.planned_jobs += jobs;
  const bool parallel =
      eligible && (!operation.deformation_work || jobs >= kMinimumParallelJobs);
  auto lease = parallel ? geometry::TryReserveWorkers(operation.context, jobs)
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
      merge();
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
  merge();
  for (const auto& slot : slots)
    if (slot.exception) std::rethrow_exception(slot.exception);
  std::optional<std::size_t> first;
  bool canceled = false;
  for (std::size_t ordinal = 0; ordinal < jobs; ++ordinal) {
    canceled |= slots[ordinal].canceled;
    if (!first && slots[ordinal].diagnostic) first = ordinal;
  }
  if (first) {
    const auto end = RangeBounds(team, *first).second;
    auto diagnostic = slots[*first].diagnostic;
    if (canceled) {
      std::optional<gltf_detail::DeformationWork::FinalizeMismatchPrefix>
          prefix;
      if (operation.deformation_work)
        prefix.emplace(*operation.deformation_work);
      diagnostic = check(state, 0, end, true, nullptr);
    }
    if (diagnostic)
      throw Failure(MismatchStatus(diagnostic->stage), *diagnostic);
  }
  if (canceled || operation.stop.stop_requested())
    throw gltf_detail::AdmissionFailure(gltf_detail::AdmissionCode::kCanceled);
}
}  // namespace
void RunRanges(const Operation& operation, std::size_t bytes, RangeCheck check,
               void* state, Stats& stats, std::size_t unit_bytes,
               const gltf_detail::DeformationRangeExtent* extent) {
  const auto before = stats;
  auto observe = [&] {
    if (!operation.deformation) return;
    auto& target = operation.deformation->execution;
    target.planned_jobs =
        Add(target.planned_jobs, stats.planned_jobs - before.planned_jobs);
    target.caller_jobs =
        Add(target.caller_jobs, stats.caller_jobs - before.caller_jobs);
    target.launched_workers =
        Add(target.launched_workers,
            stats.launched_workers - before.launched_workers);
    target.joined_workers = Add(target.joined_workers,
                                stats.joined_workers - before.joined_workers);
  };
  try {
    RunRangesImpl(operation, bytes, check, state, stats, unit_bytes, extent);
  } catch (...) {
    observe();
    throw;
  }
  observe();
}
BytePlan::BytePlan(const Operation& operation)
    : operation_(operation),
      checks_(gltf_detail::Allocator<ByteCheck>(operation.allocation)) {}
void BytePlan::Reference() { operation_.Reference(); }
void BytePlan::Reset() {
  checks_.clear();
  bytes_ = 0;
  known_failure_.reset();
  known_prefix_ = 0;
  known_pending_.reset();
}
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
  std::optional<gltf_detail::DeformationWork::FinalizeMismatchPrefix> prefix;
  if (operation_.deformation_work) prefix.emplace(*operation_.deformation_work);
  const auto earlier = Check(this, 0, known_prefix_, true, nullptr);
  if (earlier) throw Failure(MismatchStatus(earlier->stage), *earlier);
  // If cancellation refused the plan record itself, its borrowed spans still
  // permit allocation-free validation of the earlier common byte prefix.
  if (known_pending_) {
    operation_.Reference();
    const auto& pending = *known_pending_;
    const auto common =
        std::min(pending.expected.size(), pending.actual.size());
    for (std::size_t byte = 0; byte < common; ++byte) {
      operation_.Bytes(1);
      if (pending.expected[byte] != pending.actual[byte]) {
        const auto diagnostic =
            ByteLocation(pending.diagnostic, byte, pending.scalar_bytes);
        throw Failure(MismatchStatus(diagnostic.stage), diagnostic);
      }
    }
  }
  throw Failure(MismatchStatus(known_failure_->stage), *known_failure_);
}
std::optional<Diagnostic> BytePlan::Check(
    void* opaque, std::size_t begin, std::size_t end, bool finalizing,
    gltf_detail::DeformationWork::Local* local) {
  auto& plan = *static_cast<BytePlan*>(opaque);
  auto current =
      std::upper_bound(plan.checks_.begin(), plan.checks_.end(), begin,
                       [&](std::size_t position, const ByteCheck& check) {
                         plan.operation_.Reference(local);
                         return position < check.prefix;
                       });
  if (current != plan.checks_.begin()) --current;
  for (; current != plan.checks_.end() && current->prefix < end; ++current) {
    plan.operation_.Reference(local);
    plan.operation_.Checkpoint(finalizing);
    const auto common =
        std::min(current->expected.size(), current->actual.size());
    const auto local_begin =
        begin > current->prefix ? begin - current->prefix : 0;
    const auto local_end = std::min(common, end - current->prefix);
    for (auto byte = local_begin; byte < local_end; ++byte) {
      if (!finalizing &&
          byte % plan.operation_.work.cancellation_block_bytes == 0)
        plan.operation_.Checkpoint(finalizing);
      plan.operation_.Bytes(1, local);
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
    gltf_detail::DeformationRangeExtent extent;
    if (operation_.deformation_work) {
      auto& owner = *operation_.deformation_work;
      extent.relationships = owner.Multiply(
          owner.Policy().max_jobs,
          owner.Add(std::bit_width(checks_.size()), checks_.size()));
      for (const auto& check : checks_) {
        operation_.Reference();
        extent.bytes = owner.Add(
            extent.bytes, std::min(check.expected.size(), check.actual.size()));
      }
    }
    RunRanges(operation_, bytes_, Check, this, stats, 1, &extent);
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
    const InspectionTestOptions& options,
    const GltfDeformationPolicy* deformation,
    const gltf_detail::AllocationContext* complete_parent) {
  GltfTopologyOutputInspectionResult result;
  Stage stage = Stage::kAdmission;
  try {
    Configuration(limits, work);
    const auto* deformation_proof =
        request.source.SourceExtraction().Deformation();
    if ((deformation_proof != nullptr) != (deformation != nullptr))
      Fail(Status::kInvalidConfiguration, Stage::kAdmission,
           Code::kDeformationPolicyRequired);
    if (deformation) {
      gltf_detail::ValidateDeformationConfiguration(*deformation);
      result.deformation.emplace();
      result.deformation->work = deformation->work;
      result.deformation->phase = GltfDeformationPhase::kNumericGate;
      result.deformation->numeric.emplace();
      if (!gltf_detail::EstablishGltfNumericalProfile(
              *result.deformation->numeric, {}, true))
        gltf_detail::Fail(GltfStatus::kUnsupportedProfile,
                          GltfDiagnosticCode::kNumericalProfile);
      result.deformation->phase = GltfDeformationPhase::kCompare;
      result.deformation->predicates.output_reload = false;
      if (!request.reloaded_extraction.Deformation() ||
          !request.output.DeformationFacts() ||
          !request.output.DeformationFacts()->complete)
        Fail(Status::kSourceContractFailure, Stage::kAdmission,
             Code::kSourceIdentity);
    }
    const bool translated =
        request.source.SourceExtraction().ProcessingProfile() ==
        GltfProcessingProfile::kDirectTranslationTrianglesV1;
    if (translated) {
      try {
        gltf_detail::ValidateDirectTranslationConfiguration(
            limits.direct_translation, work.direct_translation);
      } catch (const gltf_detail::GltfFailure& failure) {
        Fail(Status::kInvalidConfiguration, Stage::kAdmission,
             failure.Diagnostic().code == GltfDiagnosticCode::kZeroLimit
                 ? Code::kRequiredFieldZero
                 : Code::kConfigurationExtent);
      }
    }
    if (!deformation) AdmitInputs(request, limits, work, stop);
    auto context = request.source.Mesh().ShareExecutionContext();
    if (!deformation) Identity(request, context);
    const gltf_detail::AllocationContext root(context, stop,
                                              options.allocation_backend);
    std::optional<gltf_detail::DeformationWork> deformation_work;
    if (deformation)
      deformation_work.emplace(
          *deformation, *result.deformation, root,
          [](void* state, gltf_detail::DeformationWorkRefusal) {
            Fail(Status::kLimitExceeded, *static_cast<Stage*>(state),
                 Code::kCountLimit);
          },
          &stage);
    if (deformation) {
      AdmitInputs(request, limits, work, stop, &*deformation_work);
      Identity(request, context);
    }
    if (complete_parent && (!complete_parent->HasRequestLimit() ||
                            !complete_parent->SharesExecutionBudget(root)))
      Fail(Status::kForeignBudget, Stage::kAdmission, Code::kForeignBudget);
    auto allocation =
        complete_parent ? *complete_parent
                        : root.WithRequestLimit(limits.max_new_requested_bytes);
    const auto complete_allocation = allocation;
    std::optional<gltf_detail::AllocationContext> deformation_scope;
    if (deformation) {
      const auto live = allocation.OperationActiveBytes();
      if (live >= limits.max_new_requested_bytes)
        throw gltf_detail::AdmissionFailure(
            gltf_detail::AdmissionCode::kSizeLimit);
      result.deformation->effective_request_cap =
          std::min(deformation->limits.max_new_requested_bytes,
                   limits.max_new_requested_bytes - live);
      allocation = allocation.WithAdditionalRequestLimit(
          static_cast<std::size_t>(result.deformation->effective_request_cap));
      deformation_scope = allocation;
      allocation = allocation.WithAdditionalRequestLimit(
          static_cast<std::size_t>(deformation->limits.max_descriptor_bytes));
    }
    auto comparison_work = work;
    if (deformation) {
      comparison_work.parallel_min_bytes = deformation->work.parallel_min_bytes;
      comparison_work.partition_bytes = deformation->work.partition_bytes;
      comparison_work.cancellation_block_bytes =
          deformation->work.cancellation_block_bytes;
      comparison_work.max_jobs = deformation->work.max_jobs;
    }
    Operation operation{request,
                        limits,
                        comparison_work,
                        context,
                        allocation,
                        stop,
                        {options.worker_backend, options.observer},
                        result.deformation ? &*result.deformation : nullptr};
    operation.deformation_work =
        deformation_work ? &*deformation_work : nullptr;
    if (deformation_work) deformation_work->SetAllocation(allocation);
    const auto* proof = gltf_detail::Access::ExtractionTranslation(
        request.source.SourceExtraction());
    const auto* reload_proof =
        gltf_detail::Access::ExtractionTranslation(request.reloaded_extraction);
    if (translated &&
        (!proof || !reload_proof || !proof->facts.complete ||
         !reload_proof->facts.complete ||
         proof->asset !=
             gltf_detail::Access::Owner(request.source.SourceAsset()) ||
         reload_proof->asset !=
             gltf_detail::Access::Owner(request.reloaded_asset) ||
         request.output.Source().SourceExtraction().ProcessingProfile() !=
             GltfProcessingProfile::kDirectTranslationTrianglesV1))
      Fail(Status::kSourceContractFailure, Stage::kAdmission,
           Code::kSourceIdentity);
    if (!translated && !deformation &&
        request.reloaded_extraction.ProcessingProfile() !=
            GltfProcessingProfile::kStaticLocalTrianglesV1)
      Fail(Status::kSourceContractFailure, Stage::kAdmission,
           Code::kReloadIdentity);
    auto state = std::allocate_shared<InspectionState>(
        gltf_detail::Allocator<InspectionState>(allocation), allocation,
        request);
    state->allocation = complete_allocation;
    stage = Stage::kSourceCandidate;
    if (result.deformation)
      result.deformation->predicates.canonical_association = false;
    VerifyGeometry(operation, result.execution.source_candidate);
    result.coverage.source_candidate =
        GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kCorrespondence;
    VerifyCorrespondence(operation, result.execution.correspondence);
    if (result.deformation)
      result.deformation->predicates.canonical_association = true;
    result.coverage.correspondence = GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kLexical;
    VerifyLexical(operation, result.execution.lexical);
    result.coverage.lexical = GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kResources;
    VerifyResources(operation, result.execution.resources);
    result.coverage.resources = GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kReloadedPrimitive;
    VerifyReload(operation, result.execution.reloaded_primitive);
    if (deformation)
      VerifyDeformationReload(operation, result.execution.reloaded_primitive,
                              *deformation, *result.deformation);
    if (translated) {
      const auto cap = static_cast<std::size_t>(limits.max_new_requested_bytes);
      const auto live = allocation.OperationActiveBytes();
      if (live >= cap)
        Fail(Status::kLimitExceeded, stage, Code::kInspectionRequestLimit);
      auto validation = gltf_detail::ValidateDirectTranslation(
          gltf_detail::Access::Owner(request.reloaded_asset),
          reload_proof->primitive, limits.direct_translation,
          work.direct_translation, root, cap - live, &operation.execution,
          proof);
      result.direct_translation = validation.observation;
      if (!validation.proof) TranslationFailure(validation, stage);
    }
    result.coverage.reloaded_primitive =
        GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kProvenance;
    VerifyProvenance(operation, *state, result.execution.provenance);
    result.coverage.provenance = GltfTopologyOutputCheckCoverage::kComplete;
    stage = Stage::kPublication;
    allocation.Checkpoint();
    if (result.deformation) {
      result.deformation->predicates.output_reload = true;
      result.deformation->new_requested_bytes =
          deformation_scope->OperationActiveBytes();
      result.deformation->status = GltfStatus::kAccepted;
      result.deformation->diagnostic = gltf_detail::AcceptedDiagnostic();
      result.deformation->phase = GltfDeformationPhase::kComplete;
      result.deformation->complete = true;
      state->deformation = result.deformation;
    }
    state->coverage = result.coverage;
    state->direct_translation = result.direct_translation;
    result.inspection = Access::Make(std::move(state));
    result.status = Status::kSuccess;
    result.diagnostic = Location(stage, Code::kNone);
  } catch (const Failure& failure) {
    result.status = failure.status;
    result.diagnostic = failure.diagnostic;
  } catch (const gltf_detail::GltfFailure& failure) {
    result.status = failure.Diagnostic().status == GltfStatus::kInvalidLimits
                        ? Status::kInvalidConfiguration
                        : Status::kUnsupportedSelection;
    result.diagnostic = Location(stage, Code::kDeformation);
    if (result.deformation) {
      result.deformation->status = failure.Diagnostic().status;
      result.deformation->diagnostic = failure.Diagnostic();
    }
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
  if (result.deformation && result.status != Status::kSuccess) {
    result.deformation->complete = false;
    if (result.diagnostic.code != Code::kDeformation) {
      result.deformation->status =
          result.status == Status::kCanceled ? GltfStatus::kCanceled
          : result.status == Status::kBudgetExceeded
              ? GltfStatus::kBudgetExceeded
          : result.status == Status::kInvalidConfiguration
              ? GltfStatus::kInvalidLimits
          : result.status == Status::kLimitExceeded
              ? GltfStatus::kUnsupportedProfile
              : GltfStatus::kResourceBindingMismatch;
      result.deformation->diagnostic.status = result.deformation->status;
      result.deformation->diagnostic.code =
          result.status == Status::kCanceled ? GltfDiagnosticCode::kCanceled
          : result.status == Status::kBudgetExceeded
              ? GltfDiagnosticCode::kPayloadBudget
          : result.status == Status::kLimitExceeded
              ? GltfDiagnosticCode::kLimitExceeded
              : GltfDiagnosticCode::kInvalidSelection;
    }
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
const GltfDirectTranslationObservation*
GltfTopologyOutputInspection::DirectTranslationFacts() const noexcept {
  return owner_->direct_translation ? &*owner_->direct_translation : nullptr;
}
const GltfDeformationObservation*
GltfTopologyOutputInspection::DeformationFacts() const noexcept {
  return owner_->deformation ? &*owner_->deformation : nullptr;
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
GltfTopologyOutputInspectionResult InspectGltfTopologyOutput(
    const GltfTopologyOutputInspectionRequest& request,
    const GltfDeformationPolicy& deformation,
    const GltfTopologyOutputInspectionLimits& limits,
    const GltfTopologyOutputInspectionWorkPolicy& work, std::stop_token stop) {
  return gltf_workflow_support_detail::InspectGltfTopologyOutputForTest(
      request, limits, work, stop, {}, &deformation);
}
}  // namespace meshvale::interchange
