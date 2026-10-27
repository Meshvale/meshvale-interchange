// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/surface_write.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "gltf_allocation.h"
#include "gltf_metadata.h"
#include "surface_numeric.h"
#include "surface_output_internal.h"

namespace meshvale::interchange::surface_detail {
Failure::Failure(SurfaceStatus status, SurfaceDiagnostic diagnostic) noexcept
    : status_(status), diagnostic_(diagnostic) {}
SurfaceStatus Failure::Status() const noexcept { return status_; }
const SurfaceDiagnostic& Failure::Diagnostic() const noexcept {
  return diagnostic_;
}
const char* Failure::what() const noexcept {
  return "supplied surface operation refused";
}
[[noreturn]] void Fail(SurfaceStatus status, SurfaceStage stage,
                       SurfaceCode code, SurfaceSubject subject,
                       std::optional<std::uint64_t> index,
                       std::optional<std::uint64_t> secondary,
                       std::optional<std::uint32_t> component) {
  throw Failure(status, {stage, code, subject, index, secondary, component});
}
std::size_t Size(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::ptrdiff_t>::max()) ||
      value > std::numeric_limits<std::size_t>::max())
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kConfigurationExtent);
  return static_cast<std::size_t>(value);
}
std::uint64_t AddCount(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kPlan,
         SurfaceCode::kSizeOverflow);
  return a + b;
}
std::uint64_t Product(std::uint64_t a, std::uint64_t b) {
  if (a && b > std::numeric_limits<std::uint64_t>::max() / a)
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kPlan,
         SurfaceCode::kSizeOverflow);
  return a * b;
}
FactsState::FactsState(const gltf_detail::AllocationContext& a)
    : allocation(a),
      delegates(gltf_detail::Allocator<SurfaceDelegateView>(a)),
      profiles(gltf_detail::Allocator<SurfaceDelegateProfileView>(a)),
      uv(gltf_detail::Allocator<SurfaceNumericSummary>(a)) {}
Member::Member(const gltf_detail::AllocationContext& allocation)
    : path(gltf_detail::Allocator<char>(allocation)),
      bytes(gltf_detail::Allocator<std::byte>(allocation)) {}
UvPlan::UvPlan(const gltf_detail::AllocationContext& allocation,
               SurfaceUvBinding selected)
    : binding(selected),
      prepared(gltf_detail::Allocator<std::uint32_t>(allocation)),
      source_rows(gltf_detail::Allocator<std::uint64_t>(allocation)) {}
Primitive::Primitive(const gltf_detail::AllocationContext& allocation)
    : triangles(gltf_detail::Allocator<std::uint64_t>(allocation)),
      uv_accessors(gltf_detail::Allocator<std::uint64_t>(allocation)) {}
CatalogDefinition::CatalogDefinition(
    const gltf_detail::AllocationContext& allocation)
    : kind(gltf_detail::Allocator<char>(allocation)),
      raw_json(gltf_detail::Allocator<char>(allocation)) {}
CatalogImage::CatalogImage(const gltf_detail::AllocationContext& allocation)
    : mime(gltf_detail::Allocator<char>(allocation)),
      bytes(gltf_detail::Allocator<std::byte>(allocation)),
      ordinary_path(gltf_detail::Allocator<char>(allocation)) {}
OutputPayload::OutputPayload(const gltf_detail::AllocationContext& a,
                             const SurfaceEncodeRequest& r)
    : original(r.original),
      triangles(r.triangles),
      encoding(std::visit(
          [](const auto& value) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>)
              return SurfaceEncoding::kInvalid;
            else
              return value.encoding;
          },
          r.destination)),
      representation(r.representation),
      coordinates(std::visit(
          [](const auto& value) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>)
              return SurfaceCoordinates::kInvalid;
            else
              return value.coordinates;
          },
          r.destination)),
      narrowing(r.numeric.positions),
      position_bound_bits(
          std::bit_cast<std::uint64_t>(r.numeric.max_position_linf_error)),
      uv_bound_bits(gltf_detail::Allocator<std::uint64_t>(a)),
      unit(gltf_detail::Allocator<char>(a)),
      ascii_name(gltf_detail::Allocator<char>(a)),
      binary_header(gltf_detail::Allocator<std::byte>(a)),
      unused_acknowledged(r.acknowledge_unused_vertices),
      channels(gltf_detail::Allocator<SurfaceChannelRule>(a)),
      uv_bindings(gltf_detail::Allocator<SurfaceUvBinding>(a)),
      uv_views(gltf_detail::Allocator<SurfaceUvView>(a)),
      uv(gltf_detail::Allocator<UvPlan>(a)),
      position_words(gltf_detail::Allocator<std::uint64_t>(a)),
      used_vertices(gltf_detail::Allocator<std::uint64_t>(a)),
      primitives(gltf_detail::Allocator<Primitive>(a)),
      primitive_views(gltf_detail::Allocator<SurfacePrimitiveView>(a)),
      triangle_sources(gltf_detail::Allocator<std::uint64_t>(a)),
      triangle_faces(gltf_detail::Allocator<std::uint64_t>(a)),
      corner_sources(gltf_detail::Allocator<std::uint64_t>(a)),
      corner_corners(gltf_detail::Allocator<std::uint64_t>(a)),
      vertex_sources(gltf_detail::Allocator<std::uint64_t>(a)),
      vertex_offsets(gltf_detail::Allocator<std::uint64_t>(a)),
      vertex_members(gltf_detail::Allocator<std::uint64_t>(a)),
      face_offsets(gltf_detail::Allocator<std::uint64_t>(a)),
      face_members(gltf_detail::Allocator<std::uint64_t>(a)),
      corner_offsets(gltf_detail::Allocator<std::uint64_t>(a)),
      corner_members(gltf_detail::Allocator<std::uint64_t>(a)),
      material_bindings(gltf_detail::Allocator<SurfaceMaterialBinding>(a)),
      catalog_uv(gltf_detail::Allocator<SurfaceCatalogUvBinding>(a)),
      material_factors(gltf_detail::Allocator<SurfaceMaterialFactorView>(a)),
      definitions(gltf_detail::Allocator<CatalogDefinition>(a)),
      images(gltf_detail::Allocator<CatalogImage>(a)),
      storage(gltf_detail::Allocator<Member>(a)),
      members(gltf_detail::Allocator<SurfaceMemberView>(a)) {
  if (const auto* gltf = std::get_if<GltfSurfaceOptions>(&r.destination)) {
    if (gltf->catalog) catalog.emplace(*gltf->catalog);
  }
}
OutputState::OutputState(const gltf_detail::AllocationContext& a)
    : allocation(a), facts(a) {}
InspectionPayload::InspectionPayload(const gltf_detail::AllocationContext& a,
                                     const SurfaceOutput& value)
    : expected(value),
      storage(gltf_detail::Allocator<Member>(a)),
      members(gltf_detail::Allocator<SurfaceMemberView>(a)) {}
InspectionState::InspectionState(const gltf_detail::AllocationContext& a)
    : allocation(a), facts(a) {}
SurfaceOutput Access::Output(std::shared_ptr<const OutputState> state) {
  return SurfaceOutput(std::move(state));
}
SurfaceOutputInspection Access::Inspection(
    std::shared_ptr<const InspectionState> state) {
  return SurfaceOutputInspection(std::move(state));
}
SurfaceFacts Access::Facts(const std::shared_ptr<const OutputState>& state) {
  return SurfaceFacts(std::shared_ptr<const FactsState>(state, &state->facts));
}
SurfaceFacts Access::Facts(
    const std::shared_ptr<const InspectionState>& state) {
  return SurfaceFacts(std::shared_ptr<const FactsState>(state, &state->facts));
}
const OutputState& Access::State(const SurfaceOutput& output) noexcept {
  return *output.state_;
}
Run::Run(const geometry::ExecutionContext& context,
         const gltf_detail::AllocationContext& a,
         const SurfaceCountLimits& caps, const SurfaceWorkPolicy& work,
         FactsState& observations, std::stop_token token,
         const TestOptions& options)
    : execution(context),
      allocation(a),
      limits(caps),
      policy(work),
      facts(observations),
      stop(token),
      test(options) {}
void Run::Check(std::uint64_t completed) const {
  if (test.checkpoint) test.checkpoint(stage, completed, test.checkpoint_state);
  if (stop.stop_requested())
    Fail(SurfaceStatus::kCanceled, stage, SurfaceCode::kStopRequested);
  allocation.Checkpoint();
}
void Run::Bound(std::uint64_t actual, std::uint64_t limit,
                SurfaceCode code) const {
  if (actual > limit) Fail(SurfaceStatus::kLimitExceeded, stage, code);
}
void Run::Pair(std::uint64_t count) {
  facts.work.source_scalar_pairs =
      AddCount(facts.work.source_scalar_pairs, count);
}
std::uint64_t Run::Enter(SurfaceDelegateKind, std::optional<std::uint64_t>,
                         bool) {
  // Reserve the reached record before native entry: return-time accounting
  // cannot lose a real result to a subsequent container growth request.
  facts.delegates.reserve(facts.delegates.size() + 1);
  if (next_ordinal == std::numeric_limits<std::uint64_t>::max())
    Fail(SurfaceStatus::kLimitExceeded, stage, SurfaceCode::kSizeOverflow);
  return next_ordinal++;
}
SurfaceDelegateProfileFacts Run::Profile(
    SurfaceDelegateKind kind, std::uint64_t ordinal,
    std::optional<std::uint64_t> primitive,
    std::optional<std::uint32_t> component) {
  facts.profiles.reserve(facts.profiles.size() + 1);
  const auto observed = ObserveNumericProfile();
  facts.profiles.push_back(
      {stage, kind, ordinal, primitive, component, observed});
  if (observed.status != SurfaceDelegateProfileStatus::kEstablishedBaseline)
    Fail(SurfaceStatus::kUnsupported, stage, SurfaceCode::kNumericProfile);
  Check();
  return observed;
}
namespace {
// Only these ordinary worker interruptions participate in anchor arbitration.
// A real launch exception or unexpected worker exception is never routed here.
bool IsAnchorInterruption(const std::exception_ptr& failure) {
  try {
    std::rethrow_exception(failure);
  } catch (const Failure& actual) {
    return (actual.Status() == SurfaceStatus::kCanceled &&
            actual.Diagnostic().code == SurfaceCode::kStopRequested) ||
           (actual.Status() == SurfaceStatus::kLimitExceeded &&
            actual.Diagnostic().code == SurfaceCode::kCountLimit);
  } catch (const gltf_detail::AdmissionFailure& actual) {
    return actual.Code() == gltf_detail::AdmissionCode::kCanceled;
  } catch (...) {
    return false;
  }
}
void BulkImpl(Run& run, std::uint64_t items, std::uint64_t bytes_per_item,
              RangeOperation operation, void* state,
              std::exception_ptr* anchor_interruption) {
  if (!items) return;
  const auto bytes = Product(items, bytes_per_item);
  const bool partition = bytes >= run.policy.surface.parallel_min_bytes;
  const auto per_job =
      partition ? std::max(UINT64_C(1),
                           run.policy.surface.partition_bytes / bytes_per_item)
                : items;
  const auto jobs = items / per_job + (items % per_job != 0);
  run.Bound(jobs, run.policy.surface.max_jobs);
  Vector<JobObservation> observed{
      gltf_detail::Allocator<JobObservation>(run.allocation)};
  observed.resize(Size(jobs));
  struct Team {
    Run* run;
    std::uint64_t items, per_job;
    RangeOperation operation;
    void* state;
    std::span<JobObservation> observed;
  } team{&run, items, per_job, operation, state, observed};
  auto enter = [](void* opaque, std::size_t ordinal) noexcept {
    auto& t = *static_cast<Team*>(opaque);
    auto& result = t.observed[ordinal];
    result.entered = true;
    try {
      t.run->Check();
      const auto first = static_cast<std::uint64_t>(ordinal) * t.per_job;
      const auto last = first + std::min(t.per_job, t.items - first);
      t.operation(t.state, first, last, result);
    } catch (...) {
      result.failure = std::current_exception();
    }
  };
  run.facts.execution.planned_jobs =
      AddCount(run.facts.execution.planned_jobs, jobs);
  auto lease = geometry::TryReserveWorkers(
      run.execution,
      jobs > 1 ? std::min(Size(jobs), run.execution.WorkerBudget()) : 0);
  gltf_detail::OperationWorkerStats workers;
  std::exception_ptr launch_failure;
  if (lease.Count() >= kMinimumParallelWorkers) {
    try {
      gltf_detail::RunOperationJobs(run.allocation, lease, Size(jobs), enter,
                                    &team, run.test.execution, workers);
    } catch (...) {
      launch_failure = std::current_exception();
    }
  } else {
    for (std::size_t ordinal = 0; ordinal < observed.size(); ++ordinal) {
      enter(&team, ordinal);
      ++run.facts.execution.caller_jobs;
      if (observed[ordinal].failure) break;
    }
  }
  run.facts.execution.launched_workers =
      AddCount(run.facts.execution.launched_workers, workers.launched);
  run.facts.execution.joined_workers =
      AddCount(run.facts.execution.joined_workers, workers.joined);
  for (const auto& result : observed) {
    auto& w = run.facts.work;
    w.source_scalar_pairs =
        AddCount(w.source_scalar_pairs, result.work.source_scalar_pairs);
    w.descriptor_byte_pairs =
        AddCount(w.descriptor_byte_pairs, result.work.descriptor_byte_pairs);
    w.relationship_steps =
        AddCount(w.relationship_steps, result.work.relationship_steps);
    w.coincidence_comparisons = AddCount(w.coincidence_comparisons,
                                         result.work.coincidence_comparisons);
  }
  if (launch_failure) std::rethrow_exception(launch_failure);
  // Unexpected worker failures dominate normal expected results after joins.
  for (const auto& result : observed)
    if (result.failure) {
      try {
        std::rethrow_exception(result.failure);
      } catch (const Failure&) {
      } catch (const gltf_detail::AdmissionFailure&) {
      } catch (...) {
        std::rethrow_exception(result.failure);
      }
    }
  for (const auto& result : observed)
    if (result.failure) {
      // This point is reached only after real joins, work reduction and the
      // original launch/unexpected-worker exception arbitration above.
      if (anchor_interruption && IsAnchorInterruption(result.failure)) {
        *anchor_interruption = result.failure;
        return;
      }
      std::rethrow_exception(result.failure);
    }
}
}  // namespace
void Bulk(Run& run, std::uint64_t items, std::uint64_t bytes_per_item,
          RangeOperation operation, void* state) {
  BulkImpl(run, items, bytes_per_item, operation, state, nullptr);
}
namespace {
constexpr std::uint64_t kScalar64Bytes = sizeof(std::uint64_t);
constexpr std::uint64_t kByteBits = 8;
constexpr std::uint64_t kUnsignedByteMask = UINT64_C(0xff);
constexpr std::size_t kStlHeaderBytes = 80;
constexpr std::size_t kSignedMagnitudeBit = 63;
constexpr std::string_view kTexcoordSemantic = "TEXCOORD";
constexpr std::string_view kImagePathPrefix = "images/image-";
constexpr std::string_view kPngPathSuffix = ".png", kJpegPathSuffix = ".jpg";
constexpr std::string_view kDerivedNormalName = "normal";
constexpr std::string_view kDerivedNormalSemantic = "normal";
constexpr std::size_t kExactJsonDepthMaximum =
    static_cast<std::size_t>(std::numeric_limits<int>::max());

void Positive(std::uint64_t value) {
  if (!value)
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kRequiredFieldZero);
  (void)Size(value);
}
std::uint64_t ConfigAdd(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kConfigurationExtent);
  const auto result = a + b;
  (void)Size(result);
  return result;
}
void Extent(bool valid) {
  if (!valid)
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kEncodingLimitsMismatch);
}
void Validate(const geometry::MeshMaterializationWorkPolicy& w) {
  for (auto v : {w.parallel_min_bytes, w.partition_bytes,
                 w.cancellation_block_scalars, w.max_jobs})
    Positive(v);
}
void Validate(const geometry::MeshMaterializationLimits& c) {
  for (auto v :
       {c.max_vertices, c.max_faces, c.max_corners, c.max_face_offsets,
        c.max_attributes, c.max_attribute_scalars, c.max_attribute_offsets,
        c.max_presence_bytes, c.max_metadata_entries, c.max_descriptor_bytes,
        c.max_new_requested_bytes})
    Positive(v);
}
void Validate(const geometry::MeshInspectionLimits& c) {
  Positive(c.max_issues);
  Positive(c.max_new_requested_bytes);
}
void Validate(const SurfaceCountLimits& c) {
  for (auto v : {c.max_vertices,
                 c.max_faces,
                 c.max_corners,
                 c.max_face_offsets,
                 c.max_attributes,
                 c.max_attribute_scalars,
                 c.max_attribute_bytes,
                 c.max_attribute_offsets,
                 c.max_presence_bytes,
                 c.max_metadata_entries,
                 c.max_descriptor_bytes,
                 c.max_wire_primitives,
                 c.max_wire_vertices,
                 c.max_wire_corners,
                 c.max_map_entries,
                 c.max_members,
                 c.max_member_bytes,
                 c.max_total_member_bytes,
                 c.max_resources,
                 c.max_resource_bytes,
                 c.max_json_bytes,
                 c.max_binary_bytes,
                 c.max_capsule_json_bytes,
                 c.max_capsule_binary_bytes,
                 c.max_material_bindings,
                 c.max_numeric_token_bytes,
                 c.max_json_nodes,
                 c.max_json_depth,
                 c.max_numeric_integer_bits,
                 c.max_coincidence_comparisons,
                 c.max_relationship_steps})
    Positive(v);
  if (c.max_json_depth > kExactJsonDepthMaximum ||
      c.max_json_nodes > gltf_detail::MaximumJsonCaptureNodes())
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kConfigurationExtent);
}
bool Zero(const GlbLimits& c) {
  return !c.max_container_bytes && !c.max_chunks && !c.max_requested_bytes;
}
bool Zero(const GlbWorkPolicy& w) {
  return !w.parallel_min_bytes && !w.partition_bytes;
}
bool Zero(const GltfDirectTranslationLimits& c) {
  return !c.max_node_rows && !c.max_mesh_users && !c.max_channel_references &&
         !c.max_distinct_samplers && !c.max_keys_per_sampler &&
         !c.max_total_keys && !c.max_relationship_steps &&
         !c.max_metadata_bytes && !c.max_decoded_bytes &&
         !c.max_new_requested_bytes;
}
bool Zero(const GltfDirectTranslationWorkPolicy& w) {
  return !w.parallel_min_keys && !w.partition_keys &&
         !w.cancellation_block_keys && !w.max_jobs;
}
bool Zero(const GltfWorkPolicy& w) {
  return !w.parallel_min_resource_bytes && !w.resource_partition_bytes &&
         !w.parallel_min_channel_components && !w.channel_partition_elements &&
         Zero(w.direct_translation);
}
bool Zero(const StlWorkPolicy& w) {
  return !w.cancellation_block_bytes && !w.cancellation_block_tokens &&
         !w.cancellation_block_facets && !w.geometry.parallel_min_bytes &&
         !w.geometry.partition_bytes &&
         !w.geometry.cancellation_block_scalars && !w.geometry.max_jobs;
}
void Validate(const GlbLimits& c, const GlbWorkPolicy& w) {
  Positive(c.max_container_bytes);
  Positive(c.max_chunks);
  Positive(c.max_requested_bytes);
  Positive(w.parallel_min_bytes);
  Positive(w.partition_bytes);
}
void Validate(const StlWorkPolicy& w) {
  Positive(w.cancellation_block_bytes);
  Positive(w.cancellation_block_tokens);
  Positive(w.cancellation_block_facets);
  Validate(w.geometry);
}
void Validate(const StlWriteLimits& c) {
  for (auto v :
       {c.max_output_bytes, c.max_facets, c.max_name_bytes, c.max_line_bytes,
        c.max_token_bytes, c.max_codec_new_requested_bytes,
        c.max_total_new_requested_bytes})
    Positive(v);
  Validate(c.inspection);
  Extent(ConfigAdd(c.max_codec_new_requested_bytes,
                   c.inspection.max_new_requested_bytes) <=
         c.max_total_new_requested_bytes);
}
void Validate(const StlReadLimits& c) {
  for (auto v :
       {c.max_source_bytes, c.max_facets, c.max_name_bytes, c.max_line_bytes,
        c.max_token_bytes, c.max_codec_new_requested_bytes,
        c.max_total_new_requested_bytes})
    Positive(v);
  Validate(c.materialization);
  Validate(c.inspection);
  Extent(ConfigAdd(ConfigAdd(c.max_codec_new_requested_bytes,
                             c.materialization.max_new_requested_bytes),
                   c.inspection.max_new_requested_bytes) <=
         c.max_total_new_requested_bytes);
}
void Validate(const GltfReadLimits& c) {
  for (auto v :
       {c.max_json_bytes, c.max_resource_bytes,
        c.max_owned_source_resource_bytes, c.max_document_nodes,
        c.max_document_depth, c.max_decoded_string_bytes,
        c.max_numeric_token_bytes, c.max_decimal_coefficient_digits,
        c.max_decimal_exponent_digits, c.max_table_rows, c.max_graph_edges,
        c.max_instances, c.max_accessor_elements, c.max_projection_bytes})
    Positive(v);
  Extent(c.max_document_depth <= kExactJsonDepthMaximum &&
         c.max_document_nodes <= gltf_detail::MaximumJsonCaptureNodes());
}
void BoundBits(std::uint64_t bits) {
  if (!Finite64(bits) ||
      ((bits >> kSignedMagnitudeBit) && (bits & kBinary64MagnitudeMask)))
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kInvalidBound);
}
}  // namespace
void ValidateEncodeConfiguration(const SurfaceEncodeRequest& r,
                                 const SurfaceEncodeLimits& caps,
                                 const SurfaceWorkPolicy& work) {
  const auto* gltf = std::get_if<GltfSurfaceOptions>(&r.destination);
  const auto* stl = std::get_if<StlSurfaceOptions>(&r.destination);
  if ((!gltf && !stl) ||
      (gltf && gltf->encoding != SurfaceEncoding::kGltfJson &&
       gltf->encoding != SurfaceEncoding::kGlb) ||
      (stl && stl->encoding != SurfaceEncoding::kStlAscii &&
       stl->encoding != SurfaceEncoding::kStlBinary) ||
      (r.representation != SurfaceRepresentation::kStrictBare &&
       r.representation != SurfaceRepresentation::kAnnotatedBundleV1) ||
      (r.numeric.positions != SurfaceNarrowing::kExact32 &&
       r.numeric.positions != SurfaceNarrowing::kBoundedRne32 &&
       r.numeric.positions != SurfaceNarrowing::kExact64))
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kUnknownEnum);
  Extent(r.numeric.positions != SurfaceNarrowing::kExact64 ||
         (stl && stl->encoding == SurfaceEncoding::kStlAscii));
  const auto coordinates = gltf ? gltf->coordinates : stl->coordinates;
  if (coordinates != SurfaceCoordinates::kDeclaredMetersRightHandedYUp &&
      coordinates != SurfaceCoordinates::kStlUnitsUnspecified)
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kUnknownEnum);
  for (const auto& rule : r.channels)
    if (rule.role != SurfaceChannelRole::kBundleOnly &&
        rule.role != SurfaceChannelRole::kUv &&
        rule.role != SurfaceChannelRole::kStlCornerNormal &&
        rule.role != SurfaceChannelRole::kMaterialId)
      Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
           SurfaceCode::kUnknownEnum);
  Validate(caps.counts);
  Positive(caps.max_new_requested_bytes);
  Positive(caps.max_total_requested_allowance);
  Validate(work.surface);
  auto allowance = caps.max_new_requested_bytes;
  if (gltf) {
    const auto* d = std::get_if<GltfSurfaceEncodeLimits>(&caps.delegates);
    Extent(d != nullptr);
    if (gltf->encoding == SurfaceEncoding::kGlb) {
      Validate(d->container, work.container);
      allowance = ConfigAdd(allowance, d->container.max_requested_bytes);
    }
  } else {
    const auto* d = std::get_if<StlSurfaceEncodeLimits>(&caps.delegates);
    Extent(d != nullptr);
    Validate(d->derived_mesh);
    Validate(d->codec);
    Validate(work.stl);
    allowance =
        ConfigAdd(ConfigAdd(allowance, d->derived_mesh.max_new_requested_bytes),
                  d->codec.max_total_new_requested_bytes);
  }
  if (allowance > caps.max_total_requested_allowance)
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kAllowanceTooSmall);
  BoundBits(std::bit_cast<std::uint64_t>(r.numeric.max_position_linf_error));
  for (auto v : r.numeric.max_uv_component_errors)
    BoundBits(std::bit_cast<std::uint64_t>(v));
  Extent(r.numeric.max_uv_component_errors.size() ==
         (gltf ? gltf->uv.size() : 0));
  if (gltf) {
    const auto& d = std::get<GltfSurfaceEncodeLimits>(caps.delegates);
    if (gltf->encoding == SurfaceEncoding::kGltfJson)
      Extent(Zero(d.container) && Zero(work.container));
    Extent(Zero(work.gltf) && Zero(work.stl));
  } else {
    Extent(Zero(work.gltf) && Zero(work.container));
  }
}
void ValidateInspectConfiguration(SurfaceEncoding encoding,
                                  const SurfaceInspectLimits& caps,
                                  const SurfaceWorkPolicy& work) {
  Validate(caps.counts);
  Positive(caps.max_new_requested_bytes);
  Positive(caps.max_total_requested_allowance);
  Validate(work.surface);
  auto allowance = caps.max_new_requested_bytes;
  if (encoding == SurfaceEncoding::kGltfJson ||
      encoding == SurfaceEncoding::kGlb) {
    const auto* d = std::get_if<GltfSurfaceInspectLimits>(&caps.delegates);
    Extent(d != nullptr);
    Validate(d->read.source);
    Positive(d->read.max_new_requested_bytes);
    Positive(d->extraction.max_components);
    Positive(d->extraction.max_triangle_corners);
    Positive(d->extraction.max_output_requested_bytes);
    for (auto v : {work.gltf.parallel_min_resource_bytes,
                   work.gltf.resource_partition_bytes,
                   work.gltf.parallel_min_channel_components,
                   work.gltf.channel_partition_elements})
      Positive(v);
    allowance = ConfigAdd(ConfigAdd(allowance, d->read.max_new_requested_bytes),
                          d->extraction.max_output_requested_bytes);
    if (encoding == SurfaceEncoding::kGlb) {
      Validate(d->container, work.container);
      allowance = ConfigAdd(allowance, d->container.max_requested_bytes);
    }
  } else {
    const auto* d = std::get_if<StlSurfaceInspectLimits>(&caps.delegates);
    Extent(d != nullptr);
    Validate(d->codec);
    Validate(work.stl);
    allowance = ConfigAdd(allowance, d->codec.max_total_new_requested_bytes);
  }
  if (allowance > caps.max_total_requested_allowance)
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kAllowanceTooSmall);
  if (encoding == SurfaceEncoding::kGltfJson ||
      encoding == SurfaceEncoding::kGlb) {
    const auto& d = std::get<GltfSurfaceInspectLimits>(caps.delegates);
    Extent(Zero(d.extraction.direct_translation) &&
           Zero(work.gltf.direct_translation) && Zero(work.stl));
    if (encoding == SurfaceEncoding::kGltfJson)
      Extent(Zero(d.container) && Zero(work.container));
  } else {
    Extent(Zero(work.gltf) && Zero(work.container));
  }
}
namespace {
std::uint64_t ScalarCount(const geometry::ScalarReadView& values) {
  return std::visit(
      [](auto span) { return static_cast<std::uint64_t>(span.size()); },
      values);
}
std::uint64_t ScalarWidth(const geometry::ScalarReadView& values) {
  return std::visit(
      [](auto span) {
        using T = typename decltype(span)::value_type;
        return static_cast<std::uint64_t>(sizeof(T));
      },
      values);
}
std::uint64_t ScalarBits(const geometry::ScalarReadView& values,
                         std::uint64_t at) {
  return std::visit(
      [&](auto span) -> std::uint64_t {
        using T = typename decltype(span)::value_type;
        if constexpr (std::is_same_v<T, float>)
          return std::bit_cast<std::uint32_t>(span[Size(at)]);
        else if constexpr (std::is_same_v<T, double>)
          return std::bit_cast<std::uint64_t>(span[Size(at)]);
        else if constexpr (std::is_same_v<T, std::int32_t>)
          return std::bit_cast<std::uint32_t>(span[Size(at)]);
        else
          return span[Size(at)];
      },
      values);
}
std::uint64_t NumericBits(const geometry::AttributeReadView& attribute,
                          std::uint64_t scalar) {
  if (attribute.values.index() ==
      static_cast<std::size_t>(SurfaceScalar::kFloat32))
    return Widen32(
        static_cast<std::uint32_t>(ScalarBits(attribute.values, scalar)));
  if (attribute.values.index() ==
      static_cast<std::size_t>(SurfaceScalar::kFloat64))
    return ScalarBits(attribute.values, scalar);
  Fail(SurfaceStatus::kUnsupported, SurfaceStage::kDisposition,
       SurfaceCode::kChannelShape, SurfaceSubject::kAttribute);
}
std::uint64_t DomainRows(geometry::MeshReadView mesh,
                         geometry::AttributeDomain domain) {
  switch (domain) {
    case geometry::AttributeDomain::vertex:
      return mesh.positions.size() / kPositionComponents;
    case geometry::AttributeDomain::face:
      return mesh.face_offsets.size() - 1;
    case geometry::AttributeDomain::corner:
      return mesh.corner_vertices.size();
  }
  Fail(SurfaceStatus::kInvalidStorage, SurfaceStage::kSource,
       SurfaceCode::kInvalidSourceStorage);
}
void Relationship(Run& run, std::uint64_t count = 1) {
  const auto next = AddCount(run.facts.work.relationship_steps, count);
  run.Bound(next, run.limits.max_relationship_steps);
  run.facts.work.relationship_steps = next;
}
void EqualBytes(std::string_view a, std::string_view b, Run& run) {
  if (a.size() != b.size())
    Fail(SurfaceStatus::kInvalidStorage, run.stage,
         SurfaceCode::kSourceCorrespondence);
  for (std::size_t i = 0; i < a.size(); ++i) {
    run.Check(i);
    const bool equal =
        static_cast<unsigned char>(a[i]) == static_cast<unsigned char>(b[i]);
    ++run.facts.work.descriptor_byte_pairs;
    if (!equal)
      Fail(SurfaceStatus::kInvalidStorage, run.stage,
           SurfaceCode::kSourceCorrespondence);
  }
}
void EqualWord(std::uint64_t a, std::uint64_t b, Run& run,
               SurfaceSubject subject, std::uint64_t index) {
  run.Pair();
  if (a != b)
    Fail(SurfaceStatus::kInvalidStorage, run.stage,
         SurfaceCode::kSourceCorrespondence, subject, index);
}
void Utf8Label(std::string_view text, Run& run) {
  constexpr unsigned kAsciiLimit = 0x80, kContinuationLow = 0x80,
                     kContinuationHigh = 0xbf;
  constexpr unsigned kTwoLow = 0xc2, kTwoHigh = 0xdf, kThreeLow = 0xe0,
                     kThreeHigh = 0xef, kFourLow = 0xf0, kFourHigh = 0xf4;
  constexpr unsigned kUtf8MinThreeByteSecond = 0xa0, kUtf8SurrogateLead = 0xed,
                     kUtf8SurrogateSecondUpper = 0x9f,
                     kUtf8MinFourByteSecond = 0x90,
                     kUtf8MaxUnicodeSecond = 0x8f;
  std::size_t i = 0;
  while (i < text.size()) {
    run.Check(i);
    const auto first = static_cast<unsigned char>(text[i++]);
    if (first < kAsciiLimit) continue;
    unsigned remaining = 0, lower = kContinuationLow, upper = kContinuationHigh;
    if (first >= kTwoLow && first <= kTwoHigh)
      remaining = 1;
    else if (first >= kThreeLow && first <= kThreeHigh) {
      remaining = 2;
      if (first == kThreeLow) lower = kUtf8MinThreeByteSecond;
      if (first == kUtf8SurrogateLead) upper = kUtf8SurrogateSecondUpper;
    } else if (first >= kFourLow && first <= kFourHigh) {
      remaining = 3;
      if (first == kFourLow) lower = kUtf8MinFourByteSecond;
      if (first == kFourHigh) upper = kUtf8MaxUnicodeSecond;
    }
    if (!remaining || remaining > text.size() - i)
      Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kUnits);
    for (unsigned j = 0; j < remaining; ++j) {
      run.Check(i);
      const auto next = static_cast<unsigned char>(text[i++]);
      if (next < (j ? kContinuationLow : lower) ||
          next > (j ? kContinuationHigh : upper))
        Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kUnits);
    }
  }
}
}  // namespace
SurfaceMeshCounts MeshCounts(geometry::MeshReadView mesh, Run& run,
                             MeshRole role) {
  SurfaceMeshCounts c;
  c.vertices = mesh.positions.size() / kPositionComponents;
  c.faces = mesh.face_offsets.size() - 1;
  c.corners = mesh.corner_vertices.size();
  c.face_offsets = mesh.face_offsets.size();
  c.attributes = mesh.attributes.size();
  const auto count_code = role == MeshRole::kTriangulated
                              ? SurfaceCode::kTriangleCount
                              : SurfaceCode::kSourceCount;
  run.Bound(c.vertices, run.limits.max_vertices, count_code);
  run.Bound(c.faces, run.limits.max_faces, count_code);
  run.Bound(c.corners, run.limits.max_corners, count_code);
  run.Bound(c.face_offsets, run.limits.max_face_offsets, count_code);
  run.Bound(c.attributes, run.limits.max_attributes, count_code);
  for (std::size_t i = 0; i < mesh.attributes.size(); ++i) {
    run.Check(i);
    const auto& a = mesh.attributes[i];
    c.attribute_scalars = AddCount(c.attribute_scalars, ScalarCount(a.values));
    c.attribute_bytes =
        AddCount(c.attribute_bytes,
                 Product(ScalarCount(a.values), ScalarWidth(a.values)));
    if (a.offsets)
      c.attribute_offsets = AddCount(c.attribute_offsets, a.offsets->size());
    if (a.present)
      c.presence_bytes = AddCount(c.presence_bytes, a.present->size());
    c.metadata_entries = AddCount(c.metadata_entries, a.metadata.size());
    c.descriptor_bytes = AddCount(c.descriptor_bytes,
                                  AddCount(a.name.size(), a.semantic.size()));
    for (const auto& entry : a.metadata) {
      run.Check();
      c.descriptor_bytes = AddCount(
          c.descriptor_bytes, AddCount(entry.key.size(), entry.value.size()));
    }
    run.Bound(c.attribute_scalars, run.limits.max_attribute_scalars,
              count_code);
    run.Bound(c.attribute_bytes, run.limits.max_attribute_bytes, count_code);
    run.Bound(c.attribute_offsets, run.limits.max_attribute_offsets,
              count_code);
    run.Bound(c.presence_bytes, run.limits.max_presence_bytes, count_code);
    run.Bound(c.metadata_entries, run.limits.max_metadata_entries, count_code);
    run.Bound(c.descriptor_bytes, run.limits.max_descriptor_bytes,
              SurfaceCode::kDescriptorLimit);
  }
  return c;
}
void ValidateCorrespondence(OutputPayload& output, Run& run) {
  run.stage = SurfaceStage::kCorrespondence;
  const auto original = output.original.Source().View();
  const auto triangles = output.triangles.Mesh().View();
  const auto c = MeshCounts(triangles, run, MeshRole::kTriangulated);
  run.facts.triangles = c;
  const auto& source = *run.facts.original;
  if (c.vertices != source.vertices ||
      c.corners != Product(c.faces, kTriangleCorners) ||
      c.attributes != source.attributes)
    Fail(SurfaceStatus::kInvalidStorage, run.stage,
         SurfaceCode::kSourceCorrespondence);
  const auto faces = output.triangles.FaceSources();
  const auto corners = output.triangles.CornerSources();
  const auto offsets = output.triangles.FaceOutputOffsets();
  run.Bound(AddCount(AddCount(faces.size(), corners.size()), offsets.size()),
            run.limits.max_map_entries, SurfaceCode::kMapLimit);
  if (faces.size() != c.faces || corners.size() != c.corners ||
      offsets.size() != source.faces + 1 || offsets.front() != 0 ||
      offsets.back() != c.faces)
    Fail(SurfaceStatus::kInvalidStorage, run.stage,
         SurfaceCode::kSourceCorrespondence);
  for (std::uint64_t f = 0; f < source.faces; ++f) {
    run.Check(f);
    if (offsets[Size(f)] > offsets[Size(f + 1)] ||
        offsets[Size(f + 1)] > faces.size())
      Fail(SurfaceStatus::kInvalidStorage, run.stage,
           SurfaceCode::kSourceCorrespondence, SurfaceSubject::kFace, f);
    for (auto t = offsets[Size(f)]; t < offsets[Size(f + 1)]; ++t) {
      Relationship(run);
      EqualWord(faces[Size(t)], f, run, SurfaceSubject::kFace, t);
    }
  }
  for (std::uint64_t t = 0; t < c.faces; ++t) {
    run.Check(t);
    EqualWord(triangles.face_offsets[Size(t)], Product(t, kTriangleCorners),
              run, SurfaceSubject::kFace, t);
    if (faces[Size(t)] >= source.faces)
      Fail(SurfaceStatus::kInvalidStorage, run.stage,
           SurfaceCode::kSourceCorrespondence, SurfaceSubject::kFace, t);
  }
  EqualWord(triangles.face_offsets.back(), c.corners, run,
            SurfaceSubject::kFace, c.faces);
  // This temporary is charged before construction and lives through the full
  // coverage check. Repeated triangle references are legal; omitted original
  // corners are not. Empty original faces legitimately own no corner rows.
  Vector<std::uint8_t> covered{
      gltf_detail::Allocator<std::uint8_t>(run.allocation)};
  covered.resize(Size(source.corners), 0);
  for (std::uint64_t corner = 0; corner < c.corners; ++corner) {
    run.Check(corner);
    const auto old = corners[Size(corner)],
               f = faces[Size(corner / kTriangleCorners)];
    Relationship(run);
    if (old < original.face_offsets[Size(f)] ||
        old >= original.face_offsets[Size(f + 1)])
      Fail(SurfaceStatus::kInvalidStorage, run.stage,
           SurfaceCode::kSourceCorrespondence, SurfaceSubject::kCorner, corner);
    covered[Size(old)] = 1;
    EqualWord(triangles.corner_vertices[Size(corner)],
              original.corner_vertices[Size(old)], run, SurfaceSubject::kCorner,
              corner);
  }
  struct Coverage {
    Run* run;
    std::span<const std::uint8_t> covered;
  } coverage{&run, covered};
  Bulk(
      run, covered.size(), sizeof(std::uint8_t),
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation&) {
        const auto& state = *static_cast<Coverage*>(opaque);
        for (auto i = first; i < last; ++i) {
          state.run->Check(i);
          if (!state.covered[Size(i)])
            Fail(SurfaceStatus::kInvalidStorage, state.run->stage,
                 SurfaceCode::kSourceCorrespondence, SurfaceSubject::kCorner,
                 i);
        }
      },
      &coverage);
  struct Positions {
    Run* run;
    geometry::MeshReadView source, target;
  } positions{&run, original, triangles};
  Bulk(
      run, original.positions.size(), kScalar64Bytes,
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation& observation) {
        auto& p = *static_cast<Positions*>(opaque);
        for (auto i = first; i < last; ++i) {
          p.run->Check(i);
          const auto a =
              std::bit_cast<std::uint64_t>(p.source.positions[Size(i)]);
          const auto b =
              std::bit_cast<std::uint64_t>(p.target.positions[Size(i)]);
          ++observation.work.source_scalar_pairs;
          if (a != b)
            Fail(SurfaceStatus::kInvalidStorage, SurfaceStage::kCorrespondence,
                 SurfaceCode::kSourceCorrespondence, SurfaceSubject::kVertex,
                 i / kPositionComponents, {},
                 static_cast<std::uint32_t>(i % kPositionComponents));
        }
      },
      &positions);
  for (std::size_t ordinal = 0; ordinal < original.attributes.size();
       ++ordinal) {
    run.Check(ordinal);
    const auto& a = original.attributes[ordinal];
    const auto& b = triangles.attributes[ordinal];
    EqualBytes(a.name, b.name, run);
    EqualBytes(a.semantic, b.semantic, run);
    if (a.domain != b.domain || a.set_index != b.set_index ||
        a.components != b.components || a.values.index() != b.values.index() ||
        a.offsets.has_value() != b.offsets.has_value() ||
        a.present.has_value() != b.present.has_value() ||
        a.metadata.size() != b.metadata.size())
      Fail(SurfaceStatus::kInvalidStorage, run.stage,
           SurfaceCode::kSourceCorrespondence, SurfaceSubject::kAttribute,
           ordinal);
    for (std::size_t m = 0; m < a.metadata.size(); ++m) {
      EqualBytes(a.metadata[m].key, b.metadata[m].key, run);
      EqualBytes(a.metadata[m].value, b.metadata[m].value, run);
    }
    const auto rows = DomainRows(triangles, b.domain);
    for (std::uint64_t row = 0; row < rows; ++row) {
      run.Check(row);
      const auto old = b.domain == geometry::AttributeDomain::vertex ? row
                       : b.domain == geometry::AttributeDomain::face
                           ? faces[Size(row)]
                           : corners[Size(row)];
      Relationship(run);
      const auto a_begin =
          a.offsets ? (*a.offsets)[Size(old)] : Product(old, a.components);
      const auto a_end = a.offsets ? (*a.offsets)[Size(old + 1)]
                                   : AddCount(a_begin, a.components);
      const auto b_begin =
          b.offsets ? (*b.offsets)[Size(row)] : Product(row, b.components);
      const auto b_end = b.offsets ? (*b.offsets)[Size(row + 1)]
                                   : AddCount(b_begin, b.components);
      EqualWord(a_end - a_begin, b_end - b_begin, run,
                SurfaceSubject::kAttribute, ordinal);
      if (a.present)
        EqualWord((*a.present)[Size(old)], (*b.present)[Size(row)], run,
                  SurfaceSubject::kAttribute, ordinal);
      for (auto j = UINT64_C(0); j < a_end - a_begin; ++j) {
        run.Check(j);
        EqualWord(ScalarBits(a.values, a_begin + j),
                  ScalarBits(b.values, b_begin + j), run,
                  SurfaceSubject::kAttribute, ordinal);
      }
    }
  }
}
void ValidateDisposition(OutputPayload& output, const SurfaceEncodeRequest& r,
                         Run& run) {
  run.stage = SurfaceStage::kDisposition;
  const auto mesh = output.original.Source().View();
  const auto* gltf = std::get_if<GltfSurfaceOptions>(&r.destination);
  const auto* stl = std::get_if<StlSurfaceOptions>(&r.destination);
  if (r.channels.size() != mesh.attributes.size())
    Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kChannelCoverage);
  if (!r.acknowledge_triangle_conversion || !r.acknowledge_corner_expansion)
    Fail(SurfaceStatus::kUnsupported, run.stage,
         SurfaceCode::kLossNotAcknowledged);
  if ((gltf && gltf->coordinates !=
                   SurfaceCoordinates::kDeclaredMetersRightHandedYUp) ||
      (stl && stl->coordinates != SurfaceCoordinates::kStlUnitsUnspecified))
    Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kUnits);
  const bool bare = output.representation == SurfaceRepresentation::kStrictBare;
  std::optional<std::uint64_t> normal;
  std::uint64_t uv_count = 0;
  for (std::size_t i = 0; i < r.channels.size(); ++i) {
    run.Check(i);
    const auto& rule = r.channels[i];
    const auto& a = mesh.attributes[i];
    if (rule.source_attribute != i)
      Fail(SurfaceStatus::kUnsupported, run.stage,
           SurfaceCode::kChannelCoverage, SurfaceSubject::kAttribute, i);
    if (bare && (!a.metadata.empty() || a.offsets || a.present))
      Fail(SurfaceStatus::kUnsupported, run.stage,
           SurfaceCode::kBareUnrepresented, SurfaceSubject::kAttribute, i);
    switch (rule.role) {
      case SurfaceChannelRole::kBundleOnly:
        if (bare)
          Fail(SurfaceStatus::kUnsupported, run.stage,
               SurfaceCode::kBareUnrepresented, SurfaceSubject::kAttribute, i);
        if (!rule.acknowledge_ordinary_view_loss)
          Fail(SurfaceStatus::kUnsupported, run.stage,
               SurfaceCode::kLossNotAcknowledged, SurfaceSubject::kAttribute,
               i);
        break;
      case SurfaceChannelRole::kUv:
        if (!gltf || a.components != kUvComponents ||
            a.semantic != kTexcoordSemantic || !a.set_index || a.offsets ||
            a.present ||
            (a.domain != geometry::AttributeDomain::vertex &&
             a.domain != geometry::AttributeDomain::corner) ||
            (a.values.index() !=
                 static_cast<std::size_t>(SurfaceScalar::kFloat32) &&
             a.values.index() !=
                 static_cast<std::size_t>(SurfaceScalar::kFloat64)))
          Fail(SurfaceStatus::kUnsupported, run.stage,
               SurfaceCode::kChannelShape, SurfaceSubject::kAttribute, i);
        ++uv_count;
        break;
      case SurfaceChannelRole::kStlCornerNormal:
        if (!stl || normal || a.domain != geometry::AttributeDomain::corner ||
            a.components != kPositionComponents || a.set_index || a.offsets ||
            a.present ||
            (a.values.index() !=
                 static_cast<std::size_t>(SurfaceScalar::kFloat32) &&
             a.values.index() !=
                 static_cast<std::size_t>(SurfaceScalar::kFloat64)))
          Fail(SurfaceStatus::kUnsupported, run.stage,
               SurfaceCode::kChannelShape, SurfaceSubject::kAttribute, i);
        normal = i;
        break;
      case SurfaceChannelRole::kMaterialId:
        if (!gltf || output.material_attribute ||
            a.domain != geometry::AttributeDomain::face || a.components != 1 ||
            a.offsets || a.present || a.set_index ||
            a.values.index() != static_cast<std::size_t>(SurfaceScalar::kInt32))
          Fail(SurfaceStatus::kUnsupported, run.stage,
               SurfaceCode::kChannelShape, SurfaceSubject::kAttribute, i);
        if (bare)
          Fail(SurfaceStatus::kUnsupported, run.stage,
               SurfaceCode::kBareUnrepresented, SurfaceSubject::kAttribute, i);
        output.material_attribute = i;
        break;
      default:
        Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kChannelRole,
             SurfaceSubject::kAttribute, i);
    }
  }
  if (gltf) {
    if (gltf->uv.size() != uv_count)
      Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kUvMap);
    for (std::size_t i = 0; i < gltf->uv.size(); ++i) {
      run.Check(i);
      const auto& b = gltf->uv[i];
      if (b.source_attribute >= mesh.attributes.size() || b.wire_set != i ||
          (i && b.source_set <= gltf->uv[i - 1].source_set) ||
          r.channels[Size(b.source_attribute)].role !=
              SurfaceChannelRole::kUv ||
          mesh.attributes[Size(b.source_attribute)].set_index != b.source_set)
        Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kUvMap,
             SurfaceSubject::kUvSet, i);
      if (bare && b.source_set != b.wire_set)
        Fail(SurfaceStatus::kUnsupported, run.stage,
             SurfaceCode::kBareUnrepresented, SurfaceSubject::kUvSet, i);
      output.uv_views.push_back({b.source_attribute, b.source_set, b.wire_set});
    }
    if (!gltf->catalog &&
        (output.material_attribute || !gltf->materials.empty() ||
         !gltf->catalog_uv.empty()))
      Fail(SurfaceStatus::kUnsupported, run.stage,
           SurfaceCode::kMaterialBinding);
    if (gltf->catalog && !output.material_attribute)
      Fail(SurfaceStatus::kUnsupported, run.stage,
           SurfaceCode::kMaterialBinding);
  } else {
    if (!normal || !stl->corner_normal_attribute)
      Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kNormalAbsent);
    if (normal != stl->corner_normal_attribute)
      Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kChannelRole,
           SurfaceSubject::kAttribute, *normal);
    run.Bound(stl->declared_unit.size(), run.limits.max_descriptor_bytes,
              SurfaceCode::kDescriptorLimit);
    Utf8Label(stl->declared_unit, run);
    if ((stl->encoding == SurfaceEncoding::kStlBinary &&
         (stl->binary_header.size() != kStlHeaderBytes ||
          !stl->ascii_name.empty())) ||
        (stl->encoding == SurfaceEncoding::kStlAscii &&
         !stl->binary_header.empty()))
      Fail(SurfaceStatus::kUnsupported, run.stage,
           SurfaceCode::kChannelMetadata);
    for (unsigned char c : stl->ascii_name)
      if (c == 0 || c == '\n' || c == '\r')
        Fail(SurfaceStatus::kUnsupported, run.stage,
             SurfaceCode::kChannelMetadata);
    run.Bound(stl->ascii_name.size(), run.limits.max_descriptor_bytes,
              SurfaceCode::kDescriptorLimit);
    const auto target = output.triangles.Mesh().View();
    const auto& a = target.attributes[Size(*normal)];
    for (std::uint64_t triangle = 0; triangle < run.facts.triangles->faces;
         ++triangle) {
      for (std::uint64_t corner = 0; corner < kTriangleCorners; ++corner) {
        run.Check(Product(triangle, kTriangleCorners) + corner);
        for (std::uint32_t component = 0; component < kPositionComponents;
             ++component) {
          const auto first = Product(Product(triangle, kTriangleCorners),
                                     kPositionComponents) +
                             component;
          const auto other = first + Product(corner, kPositionComponents);
          const auto first_bits = ScalarBits(a.values, first),
                     other_bits = ScalarBits(a.values, other);
          run.Pair();
          if (first_bits != other_bits)
            Fail(SurfaceStatus::kUnsupported, run.stage,
                 SurfaceCode::kNormalNotConstant, SurfaceSubject::kCorner,
                 output.triangles.CornerSources()[Size(
                     Product(triangle, kTriangleCorners) + corner)],
                 *normal, component);
          const auto bits = NumericBits(a, other);
          if (!Finite64(bits))
            Fail(SurfaceStatus::kUnsupported, run.stage,
                 SurfaceCode::kNonfinite, SurfaceSubject::kAttribute, *normal,
                 {}, component);
          if (stl->encoding == SurfaceEncoding::kStlBinary) {
            try {
              (void)Round32(bits, SurfaceNarrowing::kExact32);
            } catch (const Failure& failure) {
              auto diagnostic = failure.Diagnostic();
              diagnostic.stage = run.stage;
              diagnostic.subject = SurfaceSubject::kAttribute;
              diagnostic.index = *normal;
              diagnostic.component = component;
              throw Failure(failure.Status(), diagnostic);
            }
          }
        }
      }
    }
  }
  // Copy variable caller storage only after its whole physical extent and
  // selected role/metadata checks, never during the constant-time owner seed.
  output.channels.assign(r.channels.begin(), r.channels.end());
  if (gltf) {
    output.uv_bindings.assign(gltf->uv.begin(), gltf->uv.end());
    output.uv_bound_bits.reserve(r.numeric.max_uv_component_errors.size());
    for (auto bound : r.numeric.max_uv_component_errors)
      output.uv_bound_bits.push_back(std::bit_cast<std::uint64_t>(bound));
  } else {
    output.unit.assign(stl->declared_unit);
    output.ascii_name.assign(stl->ascii_name);
    output.binary_header.assign(stl->binary_header.begin(),
                                stl->binary_header.end());
  }
}
namespace {
std::int32_t MaterialId(const OutputPayload& output, std::uint64_t triangle) {
  const auto source = output.original.Source().View();
  const auto face = output.triangles.FaceSources()[Size(triangle)];
  const auto& a = source.attributes[Size(*output.material_attribute)];
  return std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(ScalarBits(a.values, face)));
}
void BuildCsr(std::span<const std::uint64_t> map, std::uint64_t rows,
              Vector<std::uint64_t>& offsets, Vector<std::uint64_t>& members,
              Run& run) {
  offsets.resize(Size(AddCount(rows, 1)), 0);
  for (std::uint64_t i = 0; i < map.size(); ++i) {
    run.Check(i);
    Relationship(run);
    if (map[Size(i)] >= rows)
      Fail(SurfaceStatus::kInvalidStorage, run.stage,
           SurfaceCode::kSourceCorrespondence);
    ++offsets[Size(map[Size(i)] + 1)];
  }
  for (std::size_t i = 1; i < offsets.size(); ++i) {
    run.Check(i);
    offsets[i] = AddCount(offsets[i], offsets[i - 1]);
  }
  members.resize(map.size());
  Vector<std::uint64_t> cursor(
      offsets, gltf_detail::Allocator<std::uint64_t>(run.allocation));
  for (std::uint64_t i = 0; i < map.size(); ++i) {
    run.Check(i);
    Relationship(run);
    members[Size(cursor[Size(map[Size(i)])]++)] = i;
  }
}
}  // namespace
void BuildPlan(OutputPayload& output, const SurfaceEncodeRequest& r, Run& run) {
  run.stage = SurfaceStage::kPlan;
  const auto source = output.original.Source().View();
  const auto target = output.triangles.Mesh().View();
  const auto& c = *run.facts.original;
  const auto& t = *run.facts.triangles;
  run.Bound(t.corners, run.limits.max_wire_vertices);
  run.Bound(t.corners, run.limits.max_wire_corners);
  const auto geometry_maps =
      AddCount(AddCount(output.triangles.FaceSources().size(),
                        output.triangles.CornerSources().size()),
               output.triangles.FaceOutputOffsets().size());
  const auto forward = AddCount(Product(t.faces, 2), Product(t.corners, 3));
  const auto inverse_offsets =
      AddCount(AddCount(AddCount(c.vertices, c.faces), c.corners), 3);
  const auto inverse_members = AddCount(Product(t.corners, 2), t.faces);
  run.Bound(AddCount(AddCount(geometry_maps, forward),
                     AddCount(inverse_offsets, inverse_members)),
            run.limits.max_map_entries, SurfaceCode::kMapLimit);
  if (t.faces) {
    if (output.material_attribute) {
      for (std::uint64_t i = 0; i < output.material_bindings.size(); ++i) {
        run.Bound(AddCount(output.primitives.size(), 1),
                  run.limits.max_wire_primitives);
        Primitive primitive(run.allocation);
        primitive.view.material = i;
        output.primitives.push_back(std::move(primitive));
      }
    } else {
      run.Bound(1, run.limits.max_wire_primitives);
      output.primitives.emplace_back(run.allocation);
    }
  }
  for (std::uint64_t triangle = 0; triangle < t.faces; ++triangle) {
    run.Check(triangle);
    std::uint64_t group = 0;
    if (output.material_attribute) {
      const auto id = MaterialId(output, triangle);
      for (; group < output.material_bindings.size(); ++group) {
        Relationship(run);
        if (output.material_bindings[Size(group)].source_id == id) break;
      }
      if (group == output.material_bindings.size())
        Fail(SurfaceStatus::kUnsupported, run.stage,
             SurfaceCode::kMaterialBinding, SurfaceSubject::kFace,
             output.triangles.FaceSources()[Size(triangle)]);
    }
    output.primitives[Size(group)].triangles.push_back(triangle);
  }
  Vector<std::uint8_t> used{
      gltf_detail::Allocator<std::uint8_t>(run.allocation)};
  used.resize(Size(c.vertices), 0);
  for (std::uint64_t p = 0; p < output.primitives.size(); ++p) {
    auto& primitive = output.primitives[Size(p)];
    primitive.view.ordinal = p;
    primitive.view.first_wire_triangle = output.triangle_sources.size();
    primitive.view.first_wire_vertex = output.vertex_sources.size();
    primitive.view.wire_triangles = primitive.triangles.size();
    primitive.view.wire_vertices =
        Product(primitive.triangles.size(), kTriangleCorners);
    if ((output.encoding == SurfaceEncoding::kGltfJson ||
         output.encoding == SurfaceEncoding::kGlb) &&
        primitive.view.wire_vertices > kUnsigned32WireMaximum)
      Fail(SurfaceStatus::kLimitExceeded, run.stage, SurfaceCode::kOutputLimit,
           SurfaceSubject::kPrimitive, p);
    for (auto triangle : primitive.triangles) {
      run.Check(triangle);
      Relationship(run);
      output.triangle_sources.push_back(triangle);
      output.triangle_faces.push_back(
          output.triangles.FaceSources()[Size(triangle)]);
      for (std::uint64_t j = 0; j < kTriangleCorners; ++j) {
        const auto corner = Product(triangle, kTriangleCorners) + j;
        Relationship(run);
        output.corner_sources.push_back(corner);
        output.corner_corners.push_back(
            output.triangles.CornerSources()[Size(corner)]);
        const auto vertex = target.corner_vertices[Size(corner)];
        output.vertex_sources.push_back(vertex);
        used[Size(vertex)] = 1;
      }
    }
    output.primitive_views.push_back(primitive.view);
  }
  for (std::uint64_t vertex = 0; vertex < c.vertices; ++vertex) {
    run.Check(vertex);
    if (used[Size(vertex)])
      output.used_vertices.push_back(vertex);
    else if (!r.acknowledge_unused_vertices)
      Fail(SurfaceStatus::kUnsupported, run.stage,
           SurfaceCode::kLossNotAcknowledged, SurfaceSubject::kVertex, vertex);
  }
  BuildCsr(output.vertex_sources, c.vertices, output.vertex_offsets,
           output.vertex_members, run);
  BuildCsr(output.triangle_faces, c.faces, output.face_offsets,
           output.face_members, run);
  BuildCsr(output.corner_corners, c.corners, output.corner_offsets,
           output.corner_members, run);
  for (const auto& binding : output.uv_bindings) {
    const auto& a = source.attributes[Size(binding.source_attribute)];
    const auto rows = DomainRows(source, a.domain);
    UvPlan uv(run.allocation, binding);
    uv.prepared.resize(Size(Product(rows, kUvComponents)), 0);
    Vector<std::uint8_t> present{
        gltf_detail::Allocator<std::uint8_t>(run.allocation)};
    present.resize(Size(rows), 0);
    for (std::size_t wire = 0; wire < output.vertex_sources.size(); ++wire) {
      run.Check(wire);
      Relationship(run);
      present[Size(a.domain == geometry::AttributeDomain::vertex
                       ? output.vertex_sources[wire]
                       : output.corner_corners[wire])] = 1;
    }
    for (std::uint64_t row = 0; row < rows; ++row) {
      run.Check(row);
      if (present[Size(row)]) uv.source_rows.push_back(row);
    }
    output.uv.push_back(std::move(uv));
  }
  output.position_words.resize(Size(Product(c.vertices, kPositionComponents)),
                               0);
}
namespace {
struct NumericCell {
  SurfaceDyadicError error;
  bool changed = false;
  bool complete = false;
};
void Retire(SurfaceNumericSummary& summary, std::span<const NumericCell> cells,
            std::span<const std::uint64_t> rows, std::uint32_t components,
            std::optional<std::uint64_t> attribute, Run& run) {
  for (std::size_t i = 0; i < cells.size(); ++i) {
    if (!cells[i].complete) continue;
    run.Check(i);
    ++summary.examined_values;
    summary.changed_values += cells[i].changed;
    if (CompareError(cells[i].error, summary.max_absolute_error) > 0) {
      summary.max_absolute_error = cells[i].error;
      summary.source_attribute = attribute;
      summary.source_row = rows[i / components];
      summary.component = static_cast<std::uint32_t>(i % components);
    }
  }
}
struct CoincidenceRecord {
  std::array<std::uint64_t, kPositionComponents> words;
  std::uint64_t vertex;
};
struct ComparisonPermit {
  std::atomic<std::uint64_t> used;
  std::uint64_t limit;
  void Admit(SurfaceStage stage) {
    auto old = used.load(std::memory_order_relaxed);
    for (;;) {
      if (old >= limit)
        Fail(SurfaceStatus::kLimitExceeded, stage, SurfaceCode::kCountLimit);
      if (used.compare_exchange_weak(old, old + 1, std::memory_order_relaxed))
        return;
    }
  }
};
bool RecordLess(const CoincidenceRecord& a,
                const CoincidenceRecord& b) noexcept {
  if (a.words < b.words) return true;
  if (b.words < a.words) return false;
  return a.vertex < b.vertex;
}
}  // namespace
void CheckSurfaceCoincidence(const geometry::MeshInspection& original,
                             std::span<const std::uint64_t> used_vertices,
                             std::span<const std::uint64_t> selected_words,
                             SurfaceEncoding encoding, Run& run) {
  Vector<CoincidenceRecord> records{
      gltf_detail::Allocator<CoincidenceRecord>(run.allocation)};
  Vector<CoincidenceRecord> scratch{
      gltf_detail::Allocator<CoincidenceRecord>(run.allocation)};
  records.resize(used_vertices.size());
  scratch.resize(records.size());
  const bool ascii = encoding == SurfaceEncoding::kStlAscii;
  for (std::size_t i = 0; i < records.size(); ++i) {
    run.Check(i);
    records[i].vertex = used_vertices[i];
    for (std::size_t c = 0; c < kPositionComponents; ++c) {
      const auto value = selected_words[Size(
          Product(records[i].vertex, kPositionComponents) + c)];
      records[i].words[c] = ascii
                                ? Equality64(value)
                                : Equality32(static_cast<std::uint32_t>(value));
    }
  }
  ComparisonPermit permit{run.facts.work.coincidence_comparisons,
                          run.limits.max_coincidence_comparisons};
  for (std::uint64_t width = 1; width < records.size();) {
    const auto block =
        std::min(Product(width, 2), static_cast<std::uint64_t>(records.size()));
    const auto blocks = records.size() / block + (records.size() % block != 0);
    struct Merge {
      Run* run;
      ComparisonPermit* permit;
      std::span<const CoincidenceRecord> source;
      std::span<CoincidenceRecord> target;
      std::uint64_t width, block;
    } state{&run, &permit, records, scratch, width, block};
    Bulk(
        run, blocks, Product(block, sizeof(CoincidenceRecord)),
        [](void* opaque, std::uint64_t first, std::uint64_t last,
           JobObservation& work) {
          auto& s = *static_cast<Merge*>(opaque);
          for (auto i = first; i < last; ++i) {
            const auto begin = Product(i, s.block);
            const auto middle =
                begin +
                std::min(s.width,
                         static_cast<std::uint64_t>(s.source.size()) - begin);
            const auto end =
                begin +
                std::min(s.block,
                         static_cast<std::uint64_t>(s.source.size()) - begin);
            auto a = begin, b = middle;
            for (auto out = begin; out < end; ++out) {
              s.run->Check(out);
              bool left;
              if (b == end)
                left = true;
              else if (a == middle)
                left = false;
              else {
                const auto& first_record =
                    s.source[static_cast<std::size_t>(a)];
                const auto& second = s.source[static_cast<std::size_t>(b)];
                s.permit->Admit(s.run->stage);
                left = RecordLess(first_record, second);
                ++work.work.coincidence_comparisons;
              }
              s.target[Size(out)] = s.source[Size(left ? a++ : b++)];
            }
          }
        },
        &state);
    records.swap(scratch);
    if (width > records.size() / 2) break;
    width *= 2;
  }
  // Group discovery depends on the completed key sort. Each independent
  // anchor comparison is then admitted through the same executor as sorting.
  struct AnchorPair {
    std::uint64_t anchor, candidate;
  };
  Vector<AnchorPair> pairs{gltf_detail::Allocator<AnchorPair>(run.allocation)};
  pairs.reserve(records.size());
  std::size_t anchor = 0;
  for (std::size_t i = 1; i < records.size(); ++i) {
    run.Check(i);
    permit.Admit(run.stage);
    const bool equal = records[i].words == records[anchor].words;
    ++run.facts.work.coincidence_comparisons;
    if (equal)
      pairs.push_back({records[anchor].vertex, records[i].vertex});
    else
      anchor = i;
  }
  Vector<std::uint8_t> different{
      gltf_detail::Allocator<std::uint8_t>(run.allocation)};
  different.resize(pairs.size(), 0);
  struct Anchors {
    Run* run;
    ComparisonPermit* permit;
    geometry::MeshReadView source;
    std::span<const AnchorPair> pairs;
    std::span<std::uint8_t> different;
  } comparisons{&run, &permit, original.Source().View(), pairs, different};
  std::exception_ptr interruption;
  BulkImpl(
      run, pairs.size(), 2 * kPositionComponents * kScalar64Bytes,
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation& observation) {
        auto& state = *static_cast<Anchors*>(opaque);
        for (auto i = first; i < last; ++i) {
          state.run->Check(i);
          if (kPositionComponents > std::numeric_limits<std::uint64_t>::max() -
                                        observation.work.source_scalar_pairs)
            Fail(SurfaceStatus::kLimitExceeded, state.run->stage,
                 SurfaceCode::kSizeOverflow);
          const auto pair = state.pairs[Size(i)];
          state.permit->Admit(state.run->stage);
          const bool same = [&]() noexcept {
            bool equal = true;
            for (std::size_t c = 0; c < kPositionComponents; ++c) {
              const auto a = std::bit_cast<std::uint64_t>(
                  state.source.positions[static_cast<std::size_t>(
                      pair.anchor * kPositionComponents + c)]);
              const auto b = std::bit_cast<std::uint64_t>(
                  state.source.positions[static_cast<std::size_t>(
                      pair.candidate * kPositionComponents + c)]);
              equal = (Equality64(a) == Equality64(b)) && equal;
            }
            return equal;
          }();
          observation.work.source_scalar_pairs += kPositionComponents;
          ++observation.work.coincidence_comparisons;
          state.different[Size(i)] = !same;
        }
      },
      &comparisons, &interruption);
  // Fold only completed bad flags after joins, including expected partial
  // work. A zero flag makes no completion/prefix claim. Once interrupted,
  // finish this bounded metadata reduction without another source predicate
  // or fallible operation; otherwise a later stop could erase a known defect.
  std::optional<std::pair<std::uint64_t, std::uint64_t>> earliest;
  for (std::size_t i = 0; i < pairs.size(); ++i) {
    if (!interruption) {
      try {
        run.Check(i);
      } catch (...) {
        const auto actual = std::current_exception();
        if (!IsAnchorInterruption(actual)) throw;
        interruption = actual;
      }
    }
    if (!different[i]) continue;
    const std::pair candidate{pairs[i].anchor, pairs[i].candidate};
    if (!earliest || candidate < *earliest) earliest = candidate;
  }
  if (earliest)
    Fail(SurfaceStatus::kUnsupported, run.stage, SurfaceCode::kNewCoincidence,
         SurfaceSubject::kVertex, earliest->first, earliest->second);
  if (interruption) std::rethrow_exception(interruption);
}
namespace {
void NumericTriangles(OutputPayload& output, Run& run) {
  const auto source = output.original.Source().View();
  const auto mesh = output.triangles.Mesh().View();
  for (std::uint64_t triangle = 0; triangle < run.facts.triangles->faces;
       ++triangle) {
    run.Check(triangle);
    std::array<std::uint64_t, kTrianglePositionScalars> original{}, decoded{};
    for (std::size_t c = 0; c < kTriangleCorners; ++c) {
      const auto vertex =
          mesh.corner_vertices[Size(Product(triangle, kTriangleCorners) + c)];
      for (std::size_t component = 0; component < kPositionComponents;
           ++component) {
        const auto i = Product(vertex, kPositionComponents) + component;
        original[c * kPositionComponents + component] =
            std::bit_cast<std::uint64_t>(source.positions[Size(i)]);
        const auto target = output.position_words[Size(i)];
        decoded[c * kPositionComponents + component] =
            output.encoding == SurfaceEncoding::kStlAscii
                ? target
                : Widen32(static_cast<std::uint32_t>(target));
        run.Pair();
      }
    }
    const auto issue = TriangleIssue(original, decoded, run.allocation,
                                     run.limits.max_numeric_integer_bits);
    if (issue != SurfaceCode::kNone)
      Fail(SurfaceStatus::kUnsupported, run.stage, issue, SurfaceSubject::kFace,
           output.triangles.FaceSources()[Size(triangle)], triangle);
  }
}
}  // namespace
void PrepareNumeric(OutputPayload& output, const SurfaceEncodeRequest&,
                    Run& run) {
  run.stage = SurfaceStage::kNumeric;
  run.facts.positions.emplace();
  Vector<NumericCell> cells{
      gltf_detail::Allocator<NumericCell>(run.allocation)};
  cells.resize(Size(Product(output.used_vertices.size(), kPositionComponents)));
  struct Positions {
    OutputPayload* output;
    Run* run;
    std::span<NumericCell> cells;
  } state{&output, &run, cells};
  std::exception_ptr failed;
  try {
    Bulk(
        run, cells.size(), kScalar64Bytes,
        [](void* opaque, std::uint64_t first, std::uint64_t last,
           JobObservation& observation) {
          auto& s = *static_cast<Positions*>(opaque);
          const auto source = s.output->original.Source().View();
          for (auto i = first; i < last; ++i) {
            s.run->Check(i);
            const auto row =
                s.output->used_vertices[Size(i / kPositionComponents)];
            const auto component = i % kPositionComponents,
                       at = Product(row, kPositionComponents) + component;
            const auto original =
                std::bit_cast<std::uint64_t>(source.positions[Size(at)]);
            try {
              const auto target =
                  s.output->narrowing == SurfaceNarrowing::kExact64
                      ? original
                      : Round32(original, s.output->narrowing);
              const auto decoded =
                  s.output->narrowing == SurfaceNarrowing::kExact64
                      ? original
                      : Widen32(static_cast<std::uint32_t>(target));
              ++observation.work.source_scalar_pairs;
              const auto error =
                  AbsoluteError(original, decoded, s.run->allocation,
                                s.run->limits.max_numeric_integer_bits);
              if (!WithinBound(error, s.output->position_bound_bits,
                               s.run->allocation,
                               s.run->limits.max_numeric_integer_bits))
                Fail(SurfaceStatus::kUnsupported, s.run->stage,
                     SurfaceCode::kErrorBound);
              s.output->position_words[Size(at)] =
                  s.output->encoding == SurfaceEncoding::kStlAscii ? decoded
                                                                   : target;
              s.cells[Size(i)] = {error, original != decoded, true};
            } catch (const Failure& failure) {
              auto d = failure.Diagnostic();
              d.stage = s.run->stage;
              d.subject = SurfaceSubject::kVertex;
              d.index = row;
              d.component = static_cast<std::uint32_t>(component);
              throw Failure(failure.Status(), d);
            }
          }
        },
        &state);
  } catch (...) {
    failed = std::current_exception();
  }
  // Reduction is actual bounded work. A reduction stop cannot claim the
  // remaining provisional cells as retired facts.
  try {
    Retire(*run.facts.positions, cells, output.used_vertices,
           kPositionComponents, {}, run);
  } catch (const Failure&) {
    if (!failed) throw;
  } catch (const gltf_detail::AdmissionFailure&) {
    if (!failed) throw;
  }
  if (failed) std::rethrow_exception(failed);
  CheckSurfaceCoincidence(output.original, output.used_vertices,
                          output.position_words, output.encoding, run);
  NumericTriangles(output, run);
  run.facts.positions->complete = true;
  const auto source = output.original.Source().View();
  for (std::size_t field = 0; field < output.uv.size(); ++field) {
    auto& uv = output.uv[field];
    run.facts.uv.emplace_back();
    auto& summary = run.facts.uv.back();
    summary.source_attribute = uv.binding.source_attribute;
    Vector<NumericCell> values{
        gltf_detail::Allocator<NumericCell>(run.allocation)};
    values.resize(Size(Product(uv.source_rows.size(), kUvComponents)));
    const auto& a = source.attributes[Size(uv.binding.source_attribute)];
    struct UvValues {
      OutputPayload* output;
      UvPlan* uv;
      Run* run;
      geometry::AttributeReadView attribute;
      std::size_t field;
      std::span<NumericCell> values;
    } uv_state{&output, &uv, &run, a, field, values};
    std::exception_ptr failure;
    try {
      Bulk(
          run, values.size(), kScalar64Bytes,
          [](void* opaque, std::uint64_t first, std::uint64_t last,
             JobObservation& observation) {
            auto& s = *static_cast<UvValues*>(opaque);
            for (auto i = first; i < last; ++i) {
              s.run->Check(i);
              const auto row = s.uv->source_rows[Size(i / kUvComponents)],
                         component = i % kUvComponents;
              const auto at = Product(row, kUvComponents) + component;
              try {
                const auto original = NumericBits(s.attribute, at);
                const auto target = Round32(original, s.output->narrowing);
                const auto decoded = Widen32(target);
                ++observation.work.source_scalar_pairs;
                const auto error =
                    AbsoluteError(original, decoded, s.run->allocation,
                                  s.run->limits.max_numeric_integer_bits);
                if (!WithinBound(error, s.output->uv_bound_bits[s.field],
                                 s.run->allocation,
                                 s.run->limits.max_numeric_integer_bits))
                  Fail(SurfaceStatus::kUnsupported, s.run->stage,
                       SurfaceCode::kErrorBound);
                s.uv->prepared[Size(at)] = target;
                s.values[Size(i)] = {error, original != decoded, true};
              } catch (const Failure& failed) {
                auto d = failed.Diagnostic();
                d.stage = s.run->stage;
                d.subject = SurfaceSubject::kAttribute;
                d.index = s.uv->binding.source_attribute;
                d.secondary_index = row;
                d.component = static_cast<std::uint32_t>(component);
                throw Failure(failed.Status(), d);
              }
            }
          },
          &uv_state);
    } catch (...) {
      failure = std::current_exception();
    }
    try {
      Retire(summary, values, uv.source_rows, kUvComponents,
             uv.binding.source_attribute, run);
    } catch (const Failure&) {
      if (!failure) throw;
    } catch (const gltf_detail::AdmissionFailure&) {
      if (!failure) throw;
    }
    if (failure) std::rethrow_exception(failure);
    summary.complete = true;
  }
}
void RefreshViews(OutputPayload& output, Run& run) {
  run.Bound(output.storage.size(), run.limits.max_members,
            SurfaceCode::kOutputLimit);
  std::uint64_t total = 0;
  for (auto& member : output.storage) {
    run.Check();
    run.Bound(member.bytes.size(), run.limits.max_member_bytes,
              SurfaceCode::kOutputLimit);
    total = AddCount(total, member.bytes.size());
    run.Bound(total, run.limits.max_total_member_bytes,
              SurfaceCode::kOutputLimit);
    member.digest = Digest(member.bytes, run);
  }
  output.members.clear();
  output.members.reserve(output.storage.size());
  for (const auto& member : output.storage)
    output.members.push_back(
        {member.path, member.kind, member.bytes, member.digest});
}
void RefreshViews(InspectionPayload& output, Run& run) {
  output.members.clear();
  output.members.reserve(output.storage.size());
  for (auto& member : output.storage) {
    run.Check();
    member.digest = Digest(member.bytes, run);
    output.members.push_back(
        {member.path, member.kind, member.bytes, member.digest});
  }
}
const SurfaceMemberView& MemberByKind(
    std::span<const SurfaceMemberView> members, SurfaceMemberKind kind) {
  const SurfaceMemberView* selected = nullptr;
  for (const auto& member : members)
    if (member.kind == kind) {
      if (selected)
        Fail(SurfaceStatus::kVerificationFailed, SurfaceStage::kCapture,
             SurfaceCode::kMemberInventory);
      selected = &member;
    }
  if (!selected)
    Fail(SurfaceStatus::kVerificationFailed, SurfaceStage::kCapture,
         SurfaceCode::kMemberInventory);
  return *selected;
}
namespace {
namespace key {
constexpr std::string_view kName = "name", kPbr = "pbrMetallicRoughness";
constexpr std::string_view kAlphaMode = "alphaMode",
                           kDoubleSided = "doubleSided";
constexpr std::string_view kBaseFactor = "baseColorFactor",
                           kMetallic = "metallicFactor";
constexpr std::string_view kRoughness = "roughnessFactor",
                           kBaseTexture = "baseColorTexture";
constexpr std::string_view kIndex = "index", kTexCoord = "texCoord",
                           kSampler = "sampler", kSource = "source";
constexpr std::string_view kMag = "magFilter", kMin = "minFilter",
                           kWrapS = "wrapS", kWrapT = "wrapT";
constexpr std::string_view kUri = "uri", kMime = "mimeType",
                           kBufferView = "bufferView";
constexpr std::string_view kBuffer = "buffer", kOffset = "byteOffset",
                           kLength = "byteLength";
constexpr std::string_view kMaterials = "materials", kTextures = "textures",
                           kImages = "images";
constexpr std::string_view kSamplers = "samplers", kViews = "bufferViews",
                           kBuffers = "buffers";
constexpr std::string_view kExtensionsUsed = "extensionsUsed",
                           kExtensionsRequired = "extensionsRequired";
constexpr std::string_view kExtensions = "extensions", kExtras = "extras";
constexpr std::string_view kOpaque = "OPAQUE";
constexpr std::string_view kMaterialKind = "material", kTextureKind = "texture",
                           kImageKind = "image";
constexpr std::string_view kSamplerKind = "sampler", kViewKind = "buffer-view",
                           kBufferKind = "buffer";
constexpr std::string_view kPng = "image/png", kJpeg = "image/jpeg";
constexpr std::string_view kPngData = "data:image/png;base64,",
                           kJpegData = "data:image/jpeg;base64,";
}  // namespace key
constexpr std::uint32_t kFloat32One = UINT32_C(0x3f800000);
constexpr std::size_t kBaseColorComponents = 4;
constexpr std::size_t kMetallicComponent = kBaseColorComponents;
constexpr std::size_t kRoughnessComponent = kMetallicComponent + 1;
constexpr std::array<std::string_view, 4> kMaterialKeys{
    key::kName, key::kPbr, key::kAlphaMode, key::kDoubleSided};
constexpr std::array<std::string_view, 4> kPbrKeys{
    key::kBaseFactor, key::kMetallic, key::kRoughness, key::kBaseTexture};
constexpr std::array<std::string_view, 2> kTextureInfoKeys{key::kIndex,
                                                           key::kTexCoord};
constexpr std::array<std::string_view, 3> kTextureKeys{
    key::kName, key::kSampler, key::kSource};
constexpr std::array<std::string_view, 5> kSamplerKeys{
    key::kName, key::kMag, key::kMin, key::kWrapS, key::kWrapT};
constexpr std::array<std::string_view, 4> kImageKeys{
    key::kName, key::kUri, key::kMime, key::kBufferView};
constexpr std::array<std::string_view, 4> kViewKeys{key::kName, key::kBuffer,
                                                    key::kOffset, key::kLength};
constexpr std::array<std::string_view, 3> kBufferKeys{key::kName, key::kUri,
                                                      key::kLength};
constexpr std::array<std::uint64_t, 2> kMagFilters{9728, 9729};
constexpr std::array<std::uint64_t, 6> kMinFilters{9728, 9729, 9984,
                                                   9985, 9986, 9987};
constexpr std::array<std::uint64_t, 3> kWrapModes{33071, 33648, 10497};
constexpr std::array<std::uint8_t, 8> kPngSignature{137, 80, 78, 71,
                                                    13,  10, 26, 10};
constexpr std::array<std::uint8_t, 3> kJpegSignature{255, 216, 255};
using Json = gltf_detail::JsonSnapshot;
using JsonKind = gltf_detail::JsonKind;
[[noreturn]] void CatalogFail(Run& run,
                              SurfaceCode code = SurfaceCode::kCatalogField) {
  Fail(SurfaceStatus::kUnsupported, run.stage, code);
}
std::optional<std::size_t> Field(const Json& doc, std::size_t node,
                                 std::string_view name, Run& run) {
  for (auto child = doc.FirstChild(node); child;
       child = doc.NextSibling(*child)) {
    run.Check();
    Relationship(run);
    if (doc.Key(*child) == name) return child;
  }
  return {};
}
std::uint64_t JsonUnsigned(const Json& doc, std::size_t node, Run& run) {
  if (doc.Kind(node) != JsonKind::kNumber) CatalogFail(run);
  run.Bound(doc.Number(node).size(), run.limits.max_numeric_token_bytes);
  try {
    return gltf_detail::ExactUnsigned(doc.Number(node));
  } catch (const gltf_detail::GltfFailure&) {
    CatalogFail(run);
  }
}
std::size_t TableNode(const Json& doc, std::string_view table,
                      std::uint64_t ordinal, Run& run) {
  const auto array = Field(doc, 0, table, run);
  if (!array || doc.Kind(*array) != JsonKind::kArray) CatalogFail(run);
  auto child = doc.FirstChild(*array);
  for (std::uint64_t i = 0; i < ordinal && child; ++i) {
    run.Check(i);
    Relationship(run);
    child = doc.NextSibling(*child);
  }
  if (!child || doc.Kind(*child) != JsonKind::kObject) CatalogFail(run);
  return *child;
}
void Whitelist(const Json& doc, std::size_t node,
               std::span<const std::string_view> allowed, Run& run) {
  if (doc.Kind(node) != JsonKind::kObject) CatalogFail(run);
  for (auto child = doc.FirstChild(node); child;
       child = doc.NextSibling(*child)) {
    run.Check();
    Relationship(run);
    const auto name = doc.Key(*child);
    if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
      CatalogFail(run, name == key::kExtensions || name == key::kExtras
                           ? SurfaceCode::kCatalogExtension
                           : SurfaceCode::kCatalogField);
    if (name == key::kName && doc.Kind(*child) != JsonKind::kString)
      CatalogFail(run);
  }
}
struct RawRange {
  std::size_t first = 0, last = 0;
};
Vector<RawRange> LocateRawRanges(const Json& doc, Run& run) {
  run.Bound(doc.Original().size(), run.limits.max_json_bytes);
  run.Bound(doc.NodeCount(), run.limits.max_json_nodes);
  Vector<RawRange> ranges{gltf_detail::Allocator<RawRange>(run.allocation)};
  ranges.resize(doc.NodeCount());
  struct Frame {
    std::size_t node;
    bool started = false;
    std::optional<std::size_t> child;
  };
  Vector<Frame> stack{gltf_detail::Allocator<Frame>(run.allocation)};
  stack.push_back({0, false, {}});
  const auto text = doc.Original();
  std::size_t cursor = 0;
  auto space = [&]() {
    while (cursor < text.size() &&
           (text[cursor] == ' ' || text[cursor] == '\t' ||
            text[cursor] == '\n' || text[cursor] == '\r')) {
      run.Check(cursor);
      ++cursor;
    }
  };
  auto quoted = [&]() {
    if (cursor >= text.size() || text[cursor] != '"') CatalogFail(run);
    ++cursor;
    while (cursor < text.size()) {
      run.Check(cursor);
      const auto ch = text[cursor++];
      if (ch == '"') return;
      if (ch == '\\') {
        if (cursor == text.size()) CatalogFail(run);
        ++cursor;
      }
    }
    CatalogFail(run);
  };
  while (!stack.empty()) {
    run.Check(cursor);
    run.Bound(stack.size(), run.limits.max_json_depth);
    auto& frame = stack.back();
    const auto kind = doc.Kind(frame.node);
    space();
    if (!frame.started) {
      ranges[frame.node].first = cursor;
      frame.started = true;
      if (kind == JsonKind::kObject || kind == JsonKind::kArray) {
        if (cursor >= text.size() ||
            text[cursor] != (kind == JsonKind::kObject ? '{' : '['))
          CatalogFail(run);
        ++cursor;
        frame.child = doc.FirstChild(frame.node);
      } else {
        if (kind == JsonKind::kString)
          quoted();
        else
          while (cursor < text.size() && text[cursor] != ',' &&
                 text[cursor] != ']' && text[cursor] != '}' &&
                 text[cursor] != ' ' && text[cursor] != '\t' &&
                 text[cursor] != '\n' && text[cursor] != '\r') {
            run.Check(cursor);
            ++cursor;
          }
        ranges[frame.node].last = cursor;
        stack.pop_back();
        continue;
      }
    }
    space();
    if (!frame.child) {
      if (cursor >= text.size() ||
          text[cursor] != (kind == JsonKind::kObject ? '}' : ']'))
        CatalogFail(run);
      ranges[frame.node].last = ++cursor;
      stack.pop_back();
      continue;
    }
    if (cursor < text.size() && text[cursor] == ',') {
      ++cursor;
      space();
    }
    if (kind == JsonKind::kObject) {
      quoted();
      space();
      if (cursor >= text.size() || text[cursor++] != ':') CatalogFail(run);
      space();
    }
    const auto child = *frame.child;
    frame.child = doc.NextSibling(child);
    stack.push_back({child, false, {}});
  }
  space();
  if (cursor != text.size()) CatalogFail(run);
  return ranges;
}
}  // namespace
SurfaceCoreFactorFacts CollectMaterialFactors(const Json& doc,
                                              std::size_t material,
                                              std::uint64_t ordinal,
                                              SurfaceDelegateKind kind,
                                              Run& run) {
  std::array<std::optional<std::size_t>, kSurfaceCoreFactorComponents> nodes{};
  if (const auto pbr = Field(doc, material, key::kPbr, run)) {
    if (const auto base = Field(doc, *pbr, key::kBaseFactor, run)) {
      if (doc.Kind(*base) != JsonKind::kArray) CatalogFail(run);
      auto child = doc.FirstChild(*base);
      for (std::size_t c = 0; c < kBaseColorComponents; ++c) {
        if (!child) CatalogFail(run);
        nodes[c] = child;
        child = doc.NextSibling(*child);
      }
      if (child) CatalogFail(run);
    }
    nodes[kMetallicComponent] = Field(doc, *pbr, key::kMetallic, run);
    nodes[kRoughnessComponent] = Field(doc, *pbr, key::kRoughness, run);
  }
  SurfaceCoreFactorFacts initial{};
  initial.material = ordinal;
  const auto call = run.Enter(kind, {}, true);
  run.facts.delegates.push_back({run.stage, kind, call, {}, initial});
  auto& actual =
      std::get<SurfaceCoreFactorFacts>(run.facts.delegates.back().facts);
  for (std::uint32_t c = 0; c < nodes.size(); ++c) {
    run.Check(c);
    actual.authored[c] = nodes[c].has_value();
    if (nodes[c]) {
      const auto node = *nodes[c];
      if (doc.Kind(node) != JsonKind::kNumber) CatalogFail(run);
      const auto token = doc.Number(node);
      run.Bound(token.size(), run.limits.max_numeric_token_bytes);
      try {
        if (gltf_detail::CompareDecimal(token, "0") < 0 ||
            gltf_detail::CompareDecimal(token, "1") > 0)
          CatalogFail(run);
      } catch (const gltf_detail::GltfFailure&) {
        CatalogFail(run);
      }
      (void)run.Profile(kind, call, {}, c);
      ++actual.lexical_calls;
      try {
        actual.core_f32[c] = gltf_detail::ReadFloatBits(doc, node);
        actual.observed[c] = true;
      } catch (const gltf_detail::GltfFailure& failure) {
        actual.failure = failure.Diagnostic();
        Fail(kind == SurfaceDelegateKind::kSourceMaterialFactors
                 ? SurfaceStatus::kUnsupported
                 : SurfaceStatus::kVerificationFailed,
             run.stage, SurfaceCode::kGltfDelegate, SurfaceSubject::kMaterial,
             ordinal, {}, c);
      }
    } else {
      actual.core_f32[c] = kFloat32One;
      actual.observed[c] = true;
    }
  }
  actual.complete = true;
  return actual;
}
template <class Container>
void CopyCatalogBytes(Container& target,
                      std::span<const typename Container::value_type> source,
                      CatalogCopyKind kind, Run& run) {
  using Element = typename Container::value_type;
  static_assert(sizeof(Element) == sizeof(std::byte));
  run.Check();
  target.resize(source.size());
  struct Copy {
    Run* run;
    std::span<Element> target;
    std::span<const Element> source;
    CatalogCopyKind kind;
  } copy{&run, {target.data(), target.size()}, source, kind};
  Bulk(
      run, source.size(), sizeof(Element),
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation&) {
        auto& s = *static_cast<Copy*>(opaque);
        for (auto i = first; i < last; ++i) {
          s.run->Check(i - first);
          s.target[Size(i)] = s.source[Size(i)];
          if (s.run->test.catalog_copy_progress)
            s.run->test.catalog_copy_progress(s.kind, i - first + 1,
                                              s.run->test.catalog_copy_state);
        }
        s.run->Check(last - first);
      },
      &copy);
}
void BuildCatalog(OutputPayload& output, const SurfaceEncodeRequest& request,
                  Run& run) {
  run.stage = SurfaceStage::kCatalog;
  if (!output.catalog) return;
  const auto& options = std::get<GltfSurfaceOptions>(request.destination);
  run.Bound(options.materials.size(), run.limits.max_material_bindings);
  run.Bound(options.catalog_uv.size(), run.limits.max_material_bindings);
  output.material_bindings.assign(options.materials.begin(),
                                  options.materials.end());
  output.catalog_uv.assign(options.catalog_uv.begin(),
                           options.catalog_uv.end());
  const auto& doc = gltf_detail::Access::State(*output.catalog).document;
  const auto ranges = LocateRawRanges(doc, run);
  for (const auto name : {key::kExtensionsUsed, key::kExtensionsRequired})
    if (const auto field = Field(doc, 0, name, run))
      if (doc.Kind(*field) != JsonKind::kArray || doc.FirstChild(*field))
        CatalogFail(run, SurfaceCode::kCatalogExtension);
  run.Bound(output.material_bindings.size(), run.limits.max_material_bindings);
  run.Bound(output.catalog_uv.size(), run.limits.max_material_bindings);
  Vector<std::uint8_t> seen{
      gltf_detail::Allocator<std::uint8_t>(run.allocation)};
  Vector<std::uint8_t> uv_seen{
      gltf_detail::Allocator<std::uint8_t>(run.allocation)};
  seen.resize(output.material_bindings.size(), 0);
  uv_seen.resize(output.catalog_uv.size(), 0);
  Vector<SurfaceMaterialBinding> used{
      gltf_detail::Allocator<SurfaceMaterialBinding>(run.allocation)};
  for (std::size_t i = 0; i < output.material_bindings.size(); ++i)
    for (std::size_t j = 0; j < i; ++j) {
      run.Check();
      Relationship(run);
      if (output.material_bindings[i].source_id ==
          output.material_bindings[j].source_id)
        CatalogFail(run, SurfaceCode::kMaterialBinding);
    }
  for (std::size_t i = 0; i < output.catalog_uv.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      run.Check();
      Relationship(run);
      if (output.catalog_uv[i].catalog_set == output.catalog_uv[j].catalog_set)
        CatalogFail(run, SurfaceCode::kUvMap);
    }
    bool exists = false;
    for (const auto& binding : output.uv_bindings) {
      Relationship(run);
      exists = exists || binding.source_set == output.catalog_uv[i].source_set;
    }
    if (!exists) CatalogFail(run, SurfaceCode::kUvReference);
  }
  for (std::uint64_t t = 0; t < run.facts.triangles->faces; ++t) {
    run.Check(t);
    const auto id = MaterialId(output, t);
    std::size_t at = 0;
    for (; at < output.material_bindings.size(); ++at) {
      Relationship(run);
      if (output.material_bindings[at].source_id == id) break;
    }
    if (at == output.material_bindings.size())
      CatalogFail(run, SurfaceCode::kMaterialBinding);
    if (!seen[at]) {
      seen[at] = 1;
      used.push_back(output.material_bindings[at]);
    }
  }
  if (std::find(seen.begin(), seen.end(), std::uint8_t{0}) != seen.end())
    CatalogFail(run, SurfaceCode::kMaterialBinding);
  output.material_bindings = std::move(used);
  auto find_definition =
      [&](std::string_view kind, std::uint64_t source,
          std::optional<std::uint64_t> wire) -> std::optional<std::size_t> {
    for (std::size_t i = 0; i < output.definitions.size(); ++i) {
      run.Check(i);
      Relationship(run);
      const auto& d = output.definitions[i];
      if (d.kind == kind && d.source == source && d.wire == wire) return i;
    }
    return {};
  };
  auto add_definition = [&](std::string_view kind, std::uint64_t source,
                            std::optional<std::uint64_t> wire,
                            std::size_t node) -> std::size_t {
    if (const auto existing = find_definition(kind, source, wire))
      return *existing;
    std::uint64_t count = 0;
    for (const auto& d : output.definitions)
      if (d.kind == kind) ++count;
    run.Bound(AddCount(count, 1), run.limits.max_material_bindings);
    CatalogDefinition definition(run.allocation);
    definition.kind.assign(kind);
    definition.source = source;
    definition.wire = wire;
    const auto range = ranges[node];
    const auto raw =
        doc.Original().substr(range.first, range.last - range.first);
    CopyCatalogBytes(definition.raw_json,
                     std::span<const char>(raw.data(), raw.size()),
                     CatalogCopyKind::kDefinition, run);
    output.definitions.push_back(std::move(definition));
    return output.definitions.size() - 1;
  };
  auto mapped_uv = [&](std::uint64_t catalog_set) -> std::uint32_t {
    for (std::size_t i = 0; i < output.catalog_uv.size(); ++i) {
      run.Check(i);
      Relationship(run);
      if (output.catalog_uv[i].catalog_set != catalog_set) continue;
      uv_seen[i] = 1;
      for (const auto& uv : output.uv_bindings) {
        Relationship(run);
        if (uv.source_set == output.catalog_uv[i].source_set)
          return uv.wire_set;
      }
    }
    CatalogFail(run, SurfaceCode::kUvReference);
  };
  auto add_sampler = [&](std::uint64_t sampler) -> std::uint64_t {
    std::uint64_t wire = 0;
    for (const auto& d : output.definitions)
      if (d.kind == key::kSamplerKind) {
        Relationship(run);
        if (d.source == sampler) return *d.wire;
        ++wire;
      }
    const auto node = TableNode(doc, key::kSamplers, sampler, run);
    Whitelist(doc, node, kSamplerKeys, run);
    for (const auto name : {key::kMag, key::kMin, key::kWrapS, key::kWrapT})
      if (const auto field = Field(doc, node, name, run)) {
        const auto value = JsonUnsigned(doc, *field, run);
        const auto allowed =
            name == key::kMag   ? std::span<const std::uint64_t>{kMagFilters}
            : name == key::kMin ? std::span<const std::uint64_t>{kMinFilters}
                                : std::span<const std::uint64_t>{kWrapModes};
        if (std::find(allowed.begin(), allowed.end(), value) == allowed.end())
          CatalogFail(run);
      }
    (void)add_definition(key::kSamplerKind, sampler, wire, node);
    return wire;
  };
  auto add_image = [&](std::uint64_t image) -> std::uint64_t {
    for (const auto& old : output.images) {
      Relationship(run);
      if (old.source == image) return old.wire;
    }
    run.Bound(AddCount(output.images.size(), 1), run.limits.max_resources);
    const auto node = TableNode(doc, key::kImages, image, run);
    Whitelist(doc, node, kImageKeys, run);
    const auto uri = Field(doc, node, key::kUri, run),
               view = Field(doc, node, key::kBufferView, run);
    if (uri.has_value() == view.has_value() ||
        (uri && doc.Kind(*uri) != JsonKind::kString))
      CatalogFail(run, SurfaceCode::kResourceBinding);
    const auto resources = output.catalog->Resources();
    std::optional<std::size_t> resource;
    for (std::size_t i = 0; i < resources.size(); ++i) {
      run.Check(i);
      Relationship(run);
      if (resources[i].kind == GltfResourceKind::kImage &&
          resources[i].table_index == image) {
        if (resource) CatalogFail(run, SurfaceCode::kResourceBinding);
        resource = i;
      }
    }
    if (!resource) CatalogFail(run, SurfaceCode::kResourceBinding);
    const auto& actual = resources[*resource];
    std::string_view mime;
    if (const auto field = Field(doc, node, key::kMime, run)) {
      if (doc.Kind(*field) != JsonKind::kString)
        CatalogFail(run, SurfaceCode::kResourceMedia);
      mime = doc.String(*field);
    }
    if (uri && doc.String(*uri).starts_with(key::kPngData)) {
      if (!mime.empty() && mime != key::kPng)
        CatalogFail(run, SurfaceCode::kResourceMedia);
      mime = key::kPng;
    } else if (uri && doc.String(*uri).starts_with(key::kJpegData)) {
      if (!mime.empty() && mime != key::kJpeg)
        CatalogFail(run, SurfaceCode::kResourceMedia);
      mime = key::kJpeg;
    }
    if (mime != key::kPng && mime != key::kJpeg)
      CatalogFail(run, SurfaceCode::kResourceMedia);
    const auto signature = mime == key::kPng
                               ? std::span<const std::uint8_t>{kPngSignature}
                               : std::span<const std::uint8_t>{kJpegSignature};
    if (actual.bytes.size() < signature.size())
      CatalogFail(run, SurfaceCode::kResourceMedia);
    for (std::size_t i = 0; i < signature.size(); ++i) {
      run.Check(i);
      if (std::to_integer<std::uint8_t>(actual.bytes[i]) != signature[i])
        CatalogFail(run, SurfaceCode::kResourceMedia);
    }
    if (view) {
      const auto view_index = JsonUnsigned(doc, *view, run);
      const auto view_node = TableNode(doc, key::kViews, view_index, run);
      Whitelist(doc, view_node, kViewKeys, run);
      const auto buffer_field = Field(doc, view_node, key::kBuffer, run);
      const auto length_field = Field(doc, view_node, key::kLength, run);
      if (!buffer_field || !length_field)
        CatalogFail(run, SurfaceCode::kResourceBinding);
      const auto buffer = JsonUnsigned(doc, *buffer_field, run);
      const auto buffer_node = TableNode(doc, key::kBuffers, buffer, run);
      Whitelist(doc, buffer_node, kBufferKeys, run);
      if (actual.origin != GltfResourceOrigin::kBufferView ||
          actual.buffer_index != buffer ||
          actual.buffer_view_index != view_index ||
          JsonUnsigned(doc, *length_field, run) != actual.bytes.size())
        CatalogFail(run, SurfaceCode::kResourceBinding);
      if (const auto offset = Field(doc, view_node, key::kOffset, run))
        (void)JsonUnsigned(doc, *offset, run);
      (void)add_definition(key::kViewKind, view_index, {}, view_node);
      (void)add_definition(key::kBufferKind, buffer, {}, buffer_node);
    } else if (actual.origin == GltfResourceOrigin::kBufferView)
      CatalogFail(run, SurfaceCode::kResourceBinding);
    std::uint64_t bytes = actual.bytes.size();
    for (const auto& old : output.images)
      bytes = AddCount(bytes, old.bytes.size());
    run.Bound(bytes, run.limits.max_resource_bytes);
    CatalogImage selected(run.allocation);
    selected.source = image;
    selected.resource = *resource;
    selected.table = actual.table_index;
    selected.wire = output.images.size();
    selected.kind = actual.kind;
    selected.origin = actual.origin;
    selected.buffer = actual.buffer_index;
    selected.buffer_view = actual.buffer_view_index;
    selected.mime.assign(mime);
    CopyCatalogBytes(
        selected.bytes,
        std::span<const std::byte>(actual.bytes.data(), actual.bytes.size()),
        CatalogCopyKind::kImage, run);
    if (output.encoding == SurfaceEncoding::kGltfJson) {
      selected.ordinary_path.assign(kImagePathPrefix);
      AppendUnsigned(selected.ordinary_path, selected.wire);
      selected.ordinary_path.append(mime == key::kPng ? kPngPathSuffix
                                                      : kJpegPathSuffix);
    }
    const auto wire = selected.wire;
    output.images.push_back(std::move(selected));
    (void)add_definition(key::kImageKind, image, wire, node);
    return wire;
  };
  auto add_texture = [&](std::uint64_t texture) -> std::uint64_t {
    std::uint64_t wire = 0;
    for (const auto& d : output.definitions)
      if (d.kind == key::kTextureKind) {
        Relationship(run);
        if (d.source == texture) return *d.wire;
        ++wire;
      }
    const auto node = TableNode(doc, key::kTextures, texture, run);
    Whitelist(doc, node, kTextureKeys, run);
    const auto source = Field(doc, node, key::kSource, run);
    if (!source) CatalogFail(run, SurfaceCode::kResourceBinding);
    (void)add_image(JsonUnsigned(doc, *source, run));
    if (const auto sampler = Field(doc, node, key::kSampler, run))
      (void)add_sampler(JsonUnsigned(doc, *sampler, run));
    (void)add_definition(key::kTextureKind, texture, wire, node);
    return wire;
  };
  for (std::uint64_t wire = 0; wire < output.material_bindings.size(); ++wire) {
    run.Check(wire);
    const auto material = output.material_bindings[Size(wire)].catalog_material;
    const auto node = TableNode(doc, key::kMaterials, material, run);
    Whitelist(doc, node, kMaterialKeys, run);
    if (const auto alpha = Field(doc, node, key::kAlphaMode, run))
      if (doc.Kind(*alpha) != JsonKind::kString ||
          doc.String(*alpha) != key::kOpaque)
        CatalogFail(run);
    if (const auto sided = Field(doc, node, key::kDoubleSided, run))
      if (doc.Kind(*sided) != JsonKind::kBool || doc.Boolean(*sided))
        CatalogFail(run);
    if (const auto pbr = Field(doc, node, key::kPbr, run)) {
      Whitelist(doc, *pbr, kPbrKeys, run);
      if (const auto texture = Field(doc, *pbr, key::kBaseTexture, run)) {
        Whitelist(doc, *texture, kTextureInfoKeys, run);
        const auto index = Field(doc, *texture, key::kIndex, run);
        if (!index) CatalogFail(run);
        const auto coord = Field(doc, *texture, key::kTexCoord, run);
        (void)mapped_uv(coord ? JsonUnsigned(doc, *coord, run) : 0);
        (void)add_texture(JsonUnsigned(doc, *index, run));
      }
    }
    std::optional<std::array<std::uint32_t, kSurfaceCoreFactorComponents>>
        factors;
    for (const auto& old : output.definitions)
      if (old.kind == key::kMaterialKind && old.source == material) {
        Relationship(run);
        factors = old.source_factors;
        break;
      }
    if (!factors)
      factors = CollectMaterialFactors(
                    doc, node, material,
                    SurfaceDelegateKind::kSourceMaterialFactors, run)
                    .core_f32;
    const auto added = add_definition(key::kMaterialKind, material, wire, node);
    output.definitions[added].source_factors = factors;
  }
  if (std::find(uv_seen.begin(), uv_seen.end(), std::uint8_t{0}) !=
      uv_seen.end())
    CatalogFail(run, SurfaceCode::kUvMap);
  output.catalog_digest = Digest(
      std::as_bytes(std::span{doc.Original().data(), doc.Original().size()}),
      run);
}
namespace {
constexpr std::string_view kAssetKey = "asset", kVersionKey = "version",
                           kGltfVersion = "2.0";
constexpr std::string_view kSceneKey = "scene", kScenesKey = "scenes",
                           kNodesKey = "nodes";
constexpr std::string_view kMeshesKey = "meshes", kMeshKey = "mesh",
                           kPrimitivesKey = "primitives";
constexpr std::string_view kAttributesKey = "attributes",
                           kPositionSemantic = "POSITION";
constexpr std::string_view kTexcoordPrefix = "TEXCOORD_",
                           kIndicesKey = "indices", kModeKey = "mode";
constexpr std::string_view kMaterialKey = "material",
                           kAccessorsKey = "accessors";
constexpr std::string_view kComponentKey = "componentType", kCountKey = "count",
                           kTypeKey = "type";
constexpr std::string_view kMinBoundKey = "min", kMaxBoundKey = "max";
constexpr std::string_view kVec3 = "VEC3", kVec2 = "VEC2",
                           kScalarShape = "SCALAR";
constexpr std::uint64_t kGltfFloatComponent = 5126,
                        kGltfUnsigned32Component = 5125;
constexpr std::uint64_t kGltfTrianglesMode = 4, kNativeAlignment = 4;
constexpr char kHexAlphabet[] = "0123456789abcdef";

class JsonText {
 public:
  JsonText(Run& run, std::uint64_t cap)
      : run_(run),
        cap_(cap),
        text_(gltf_detail::Allocator<char>(run.allocation)) {}
  void Raw(std::string_view text) {
    run_.Bound(AddCount(text_.size(), text.size()), cap_,
               SurfaceCode::kOutputLimit);
    for (char c : text) {
      run_.Check(text_.size());
      text_.push_back(c);
    }
  }
  void Quoted(std::string_view text) {
    constexpr unsigned kJsonControlLimit = 0x20;
    constexpr std::size_t kJsonUnicodeEscapeBytes = 6;
    constexpr unsigned kJsonHexDigitBits = 4, kJsonHexDigitMask = 0xf;
    Raw("\"");
    for (unsigned char c : text) {
      if (c == '"' || c == '\\') {
        Raw("\\");
        const char ch = static_cast<char>(c);
        Raw({&ch, 1});
      } else if (c < kJsonControlLimit) {
        std::array<char, kJsonUnicodeEscapeBytes> escape{
            '\\',
            'u',
            '0',
            '0',
            kHexAlphabet[c >> kJsonHexDigitBits],
            kHexAlphabet[c & kJsonHexDigitMask]};
        Raw({escape.data(), escape.size()});
      } else {
        const char ch = static_cast<char>(c);
        Raw({&ch, 1});
      }
    }
    Raw("\"");
  }
  void Unsigned(std::uint64_t value) {
    String token{gltf_detail::Allocator<char>(run_.allocation)};
    AppendUnsigned(token, value);
    run_.Bound(token.size(), run_.limits.max_numeric_token_bytes);
    Raw(token);
  }
  void Decimal(std::uint64_t bits) {
    String token{gltf_detail::Allocator<char>(run_.allocation)};
    AppendDecimal64(token, bits, run_.allocation,
                    run_.limits.max_numeric_integer_bits,
                    run_.limits.max_numeric_token_bytes);
    Raw(token);
  }
  void Key(std::string_view name, bool& first) {
    if (!first) Raw(",");
    first = false;
    Quoted(name);
    Raw(":");
  }
  [[nodiscard]] std::string_view View() const noexcept { return text_; }

 private:
  Run& run_;
  std::uint64_t cap_;
  String text_;
};
void CopyBytes(Vector<std::byte>& target, std::span<const std::byte> source,
               Run& run) {
  target.resize(source.size());
  struct Copy {
    Run* run;
    std::span<std::byte> target;
    std::span<const std::byte> source;
  } copy{&run, target, source};
  Bulk(
      run, source.size(), 1,
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation&) {
        auto& s = *static_cast<Copy*>(opaque);
        for (auto i = first; i < last; ++i) {
          s.run->Check(i);
          s.target[Size(i)] = s.source[Size(i)];
        }
      },
      &copy);
}
void Put32(std::span<std::byte> target, std::uint64_t offset,
           std::uint32_t value) noexcept {
  for (std::size_t byte = 0; byte < sizeof(value); ++byte)
    target[static_cast<std::size_t>(offset) + byte] = static_cast<std::byte>(
        (value >> (byte * kByteBits)) & kUnsignedByteMask);
}
std::uint64_t AlignNative(std::uint64_t value) {
  const auto remainder = value % kNativeAlignment;
  return remainder ? AddCount(value, kNativeAlignment - remainder) : value;
}
bool LessFinite(std::uint64_t a, std::uint64_t b) noexcept {
  // Finite total ordering preserves -0 minimum and +0 maximum.
  const auto first = (a & kBinary64SignMask) ? ~a : (a | kBinary64SignMask);
  const auto second = (b & kBinary64SignMask) ? ~b : (b | kBinary64SignMask);
  return first < second;
}
std::uint64_t WireDefinition(const OutputPayload& output, std::string_view kind,
                             std::uint64_t source, Run& run) {
  for (const auto& d : output.definitions) {
    run.Check();
    Relationship(run);
    if (d.kind == kind && d.source == source && d.wire) return *d.wire;
  }
  CatalogFail(run, SurfaceCode::kMaterialReference);
}
std::uint32_t WireUv(const OutputPayload& output, std::uint64_t catalog,
                     Run& run) {
  for (const auto& alias : output.catalog_uv) {
    Relationship(run);
    if (alias.catalog_set == catalog)
      for (const auto& uv : output.uv_bindings) {
        Relationship(run);
        if (uv.source_set == alias.source_set) return uv.wire_set;
      }
  }
  CatalogFail(run, SurfaceCode::kUvReference);
}
void CopyJsonField(JsonText& json, const Json& doc, std::size_t object,
                   std::string_view name, bool& first, Run& run) {
  if (const auto field = Field(doc, object, name, run)) {
    json.Key(name, first);
    switch (doc.Kind(*field)) {
      case JsonKind::kString:
        json.Quoted(doc.String(*field));
        break;
      case JsonKind::kNumber:
        run.Bound(doc.Number(*field).size(),
                  run.limits.max_numeric_token_bytes);
        json.Raw(doc.Number(*field));
        break;
      case JsonKind::kBool:
        json.Raw(doc.Boolean(*field) ? "true" : "false");
        break;
      default:
        CatalogFail(run);
    }
  }
}
struct WireBlock {
  std::uint64_t offset, bytes, rows, component;
  std::uint32_t components;
  std::optional<std::uint64_t> primitive, uv;
  bool indices = false;
};
}  // namespace
void EncodeGltf(OutputPayload& output, const SurfaceEncodeRequest&,
                const SurfaceEncodeLimits& limits, Run& run) {
  run.stage = SurfaceStage::kEncode;
  Vector<WireBlock> blocks{gltf_detail::Allocator<WireBlock>(run.allocation)};
  std::uint64_t binary_bytes = 0;
  auto add_block = [&](std::uint64_t rows, std::uint32_t components,
                       std::uint64_t primitive, std::optional<std::uint64_t> uv,
                       bool indices) {
    const auto offset = AlignNative(binary_bytes);
    const auto bytes =
        Product(Product(rows, components), sizeof(std::uint32_t));
    binary_bytes = AddCount(offset, bytes);
    run.Bound(binary_bytes, run.limits.max_binary_bytes,
              SurfaceCode::kOutputLimit);
    blocks.push_back({offset, bytes, rows,
                      indices ? kGltfUnsigned32Component : kGltfFloatComponent,
                      components, primitive, uv, indices});
    return blocks.size() - 1;
  };
  for (std::uint64_t p = 0; p < output.primitives.size(); ++p) {
    auto& primitive = output.primitives[Size(p)];
    primitive.position_accessor = add_block(primitive.view.wire_vertices,
                                            kPositionComponents, p, {}, false);
    for (std::uint64_t uv = 0; uv < output.uv.size(); ++uv)
      primitive.uv_accessors.push_back(
          add_block(primitive.view.wire_vertices, kUvComponents, p, uv, false));
    primitive.index_accessor =
        add_block(primitive.view.wire_vertices, 1, p, {}, true);
  }
  if (output.encoding == SurfaceEncoding::kGlb)
    for (auto& image : output.images) {
      image.bin_offset = AlignNative(binary_bytes);
      binary_bytes = AddCount(*image.bin_offset, image.bytes.size());
      run.Bound(binary_bytes, run.limits.max_binary_bytes,
                SurfaceCode::kOutputLimit);
    }
  Vector<std::byte> binary{gltf_detail::Allocator<std::byte>(run.allocation)};
  binary.resize(Size(binary_bytes), std::byte{0});
  for (const auto& block : blocks) {
    struct Fill {
      Run* run;
      OutputPayload* output;
      const WireBlock* block;
      std::span<std::byte> binary;
    } fill{&run, &output, &block, binary};
    Bulk(
        run, Product(block.rows, block.components), sizeof(std::uint32_t),
        [](void* opaque, std::uint64_t first, std::uint64_t last,
           JobObservation&) {
          auto& s = *static_cast<Fill*>(opaque);
          const auto& b = *s.block;
          const auto& p = s.output->primitives[Size(*b.primitive)];
          const auto original = s.output->original.Source().View();
          for (auto i = first; i < last; ++i) {
            s.run->Check(i);
            std::uint32_t word;
            const auto row = i / b.components, component = i % b.components;
            const auto wire = AddCount(p.view.first_wire_vertex, row);
            if (b.indices)
              word = static_cast<std::uint32_t>(row);
            else if (!b.uv)
              word = static_cast<std::uint32_t>(s.output->position_words[Size(
                  Product(s.output->vertex_sources[Size(wire)],
                          kPositionComponents) +
                  component)]);
            else {
              const auto& uv = s.output->uv[Size(*b.uv)];
              const auto& a =
                  original.attributes[Size(uv.binding.source_attribute)];
              const auto source = a.domain == geometry::AttributeDomain::vertex
                                      ? s.output->vertex_sources[Size(wire)]
                                      : s.output->corner_corners[Size(wire)];
              word =
                  uv.prepared[Size(Product(source, kUvComponents) + component)];
            }
            Put32(s.binary, AddCount(b.offset, Product(i, sizeof(word))), word);
          }
        },
        &fill);
  }
  if (output.encoding == SurfaceEncoding::kGlb)
    for (const auto& image : output.images)
      for (std::uint64_t i = 0; i < image.bytes.size(); ++i) {
        run.Check(i);
        binary[Size(AddCount(*image.bin_offset, i))] = image.bytes[Size(i)];
      }
  JsonText json(
      run, std::min(run.limits.max_json_bytes, run.limits.max_member_bytes));
  json.Raw("{");
  bool root = true;
  json.Key(kAssetKey, root);
  json.Raw("{");
  bool asset = true;
  json.Key(kVersionKey, asset);
  json.Quoted(kGltfVersion);
  json.Raw("}");
  json.Key(kScenesKey, root);
  json.Raw("[{");
  bool scene = true;
  if (!output.primitives.empty()) {
    json.Key(kNodesKey, scene);
    json.Raw("[");
    for (std::uint64_t i = 0; i < output.primitives.size(); ++i) {
      if (i) json.Raw(",");
      json.Unsigned(i);
    }
    json.Raw("]");
  }
  json.Raw("}]");
  json.Key(kSceneKey, root);
  json.Unsigned(0);
  if (!output.primitives.empty()) {
    json.Key(kNodesKey, root);
    json.Raw("[");
    for (std::uint64_t i = 0; i < output.primitives.size(); ++i) {
      if (i) json.Raw(",");
      json.Raw("{");
      bool first = true;
      json.Key(kMeshKey, first);
      json.Unsigned(i);
      json.Raw("}");
    }
    json.Raw("]");
    json.Key(kMeshesKey, root);
    json.Raw("[");
    for (std::size_t i = 0; i < output.primitives.size(); ++i) {
      const auto& p = output.primitives[i];
      if (i) json.Raw(",");
      json.Raw("{");
      bool mesh = true;
      json.Key(kPrimitivesKey, mesh);
      json.Raw("[{");
      bool primitive = true;
      json.Key(kAttributesKey, primitive);
      json.Raw("{");
      bool attribute = true;
      json.Key(kPositionSemantic, attribute);
      json.Unsigned(*p.position_accessor);
      for (std::size_t uv = 0; uv < p.uv_accessors.size(); ++uv) {
        String semantic{gltf_detail::Allocator<char>(run.allocation)};
        semantic.assign(kTexcoordPrefix);
        AppendUnsigned(semantic, uv);
        json.Key(semantic, attribute);
        json.Unsigned(p.uv_accessors[uv]);
      }
      json.Raw("}");
      json.Key(kIndicesKey, primitive);
      json.Unsigned(*p.index_accessor);
      json.Key(kModeKey, primitive);
      json.Unsigned(kGltfTrianglesMode);
      if (p.view.material) {
        json.Key(kMaterialKey, primitive);
        json.Unsigned(*p.view.material);
      }
      json.Raw("}]}");
    }
    json.Raw("]");
  }
  if (binary_bytes) {
    json.Key(key::kBuffers, root);
    json.Raw("[{");
    bool first = true;
    json.Key(key::kLength, first);
    json.Unsigned(binary_bytes);
    if (output.encoding == SurfaceEncoding::kGltfJson) {
      json.Key(key::kUri, first);
      json.Quoted(kGeometryBufferPath);
    }
    json.Raw("}]");
    json.Key(key::kViews, root);
    json.Raw("[");
    bool initial = true;
    auto view = [&](std::uint64_t offset, std::uint64_t bytes) {
      if (!initial) json.Raw(",");
      initial = false;
      json.Raw("{");
      bool field = true;
      json.Key(key::kBuffer, field);
      json.Unsigned(0);
      json.Key(key::kOffset, field);
      json.Unsigned(offset);
      json.Key(key::kLength, field);
      json.Unsigned(bytes);
      json.Raw("}");
    };
    for (const auto& block : blocks) view(block.offset, block.bytes);
    if (output.encoding == SurfaceEncoding::kGlb)
      for (const auto& image : output.images)
        view(*image.bin_offset, image.bytes.size());
    json.Raw("]");
  }
  if (!blocks.empty()) {
    json.Key(kAccessorsKey, root);
    json.Raw("[");
    for (std::size_t i = 0; i < blocks.size(); ++i) {
      const auto& block = blocks[i];
      if (i) json.Raw(",");
      json.Raw("{");
      bool field = true;
      json.Key(key::kBufferView, field);
      json.Unsigned(i);
      json.Key(key::kOffset, field);
      json.Unsigned(0);
      json.Key(kComponentKey, field);
      json.Unsigned(block.component);
      json.Key(kCountKey, field);
      json.Unsigned(block.rows);
      json.Key(kTypeKey, field);
      json.Quoted(block.indices ? kScalarShape : block.uv ? kVec2 : kVec3);
      if (!block.indices && !block.uv) {
        std::array<std::uint64_t, kPositionComponents> minimum{}, maximum{};
        const auto& p = output.primitives[Size(*block.primitive)];
        for (std::uint64_t row = 0; row < block.rows; ++row)
          for (std::size_t c = 0; c < kPositionComponents; ++c) {
            run.Check(row);
            const auto source = output.vertex_sources[Size(
                AddCount(p.view.first_wire_vertex, row))];
            const auto word =
                Widen32(static_cast<std::uint32_t>(output.position_words[Size(
                    Product(source, kPositionComponents) + c)]));
            if (!row || LessFinite(word, minimum[c])) minimum[c] = word;
            if (!row || LessFinite(maximum[c], word)) maximum[c] = word;
          }
        for (bool upper : {false, true}) {
          json.Key(upper ? kMaxBoundKey : kMinBoundKey, field);
          json.Raw("[");
          for (std::size_t c = 0; c < kPositionComponents; ++c) {
            if (c) json.Raw(",");
            json.Decimal(upper ? maximum[c] : minimum[c]);
          }
          json.Raw("]");
        }
      }
      json.Raw("}");
    }
    json.Raw("]");
  }
  if (output.catalog) {
    const auto& doc = gltf_detail::Access::State(*output.catalog).document;
    if (!output.material_bindings.empty()) {
      json.Key(key::kMaterials, root);
      json.Raw("[");
      for (std::size_t wire = 0; wire < output.material_bindings.size();
           ++wire) {
        if (wire) json.Raw(",");
        json.Raw("{");
        bool field = true;
        const auto node =
            TableNode(doc, key::kMaterials,
                      output.material_bindings[wire].catalog_material, run);
        CopyJsonField(json, doc, node, key::kName, field, run);
        CopyJsonField(json, doc, node, key::kAlphaMode, field, run);
        CopyJsonField(json, doc, node, key::kDoubleSided, field, run);
        if (const auto pbr = Field(doc, node, key::kPbr, run)) {
          json.Key(key::kPbr, field);
          json.Raw("{");
          bool pbr_first = true;
          if (const auto base = Field(doc, *pbr, key::kBaseFactor, run)) {
            json.Key(key::kBaseFactor, pbr_first);
            json.Raw("[");
            bool first = true;
            for (auto child = doc.FirstChild(*base); child;
                 child = doc.NextSibling(*child)) {
              if (!first) json.Raw(",");
              first = false;
              json.Raw(doc.Number(*child));
            }
            json.Raw("]");
          }
          CopyJsonField(json, doc, *pbr, key::kMetallic, pbr_first, run);
          CopyJsonField(json, doc, *pbr, key::kRoughness, pbr_first, run);
          if (const auto texture = Field(doc, *pbr, key::kBaseTexture, run)) {
            json.Key(key::kBaseTexture, pbr_first);
            json.Raw("{");
            bool info = true;
            const auto index = *Field(doc, *texture, key::kIndex, run);
            const auto coord = Field(doc, *texture, key::kTexCoord, run);
            json.Key(key::kIndex, info);
            json.Unsigned(WireDefinition(output, key::kTextureKind,
                                         JsonUnsigned(doc, index, run), run));
            json.Key(key::kTexCoord, info);
            json.Unsigned(WireUv(
                output, coord ? JsonUnsigned(doc, *coord, run) : 0, run));
            json.Raw("}");
          }
          json.Raw("}");
        }
        json.Raw("}");
      }
      json.Raw("]");
    }
    for (const auto kind : {key::kTextureKind, key::kSamplerKind}) {
      std::uint64_t count = 0;
      for (const auto& d : output.definitions)
        if (d.kind == kind) ++count;
      if (!count) continue;
      json.Key(kind == key::kTextureKind ? key::kTextures : key::kSamplers,
               root);
      json.Raw("[");
      for (std::uint64_t wire = 0; wire < count; ++wire) {
        const CatalogDefinition* selected = nullptr;
        for (const auto& d : output.definitions)
          if (d.kind == kind && d.wire == wire) {
            selected = &d;
            break;
          }
        if (!selected) CatalogFail(run);
        if (wire) json.Raw(",");
        if (kind == key::kSamplerKind) {
          json.Raw(selected->raw_json);
          continue;
        }
        const auto node = TableNode(doc, key::kTextures, selected->source, run);
        json.Raw("{");
        bool field = true;
        CopyJsonField(json, doc, node, key::kName, field, run);
        json.Key(key::kSource, field);
        json.Unsigned(WireDefinition(
            output, key::kImageKind,
            JsonUnsigned(doc, *Field(doc, node, key::kSource, run), run), run));
        if (const auto sampler = Field(doc, node, key::kSampler, run)) {
          json.Key(key::kSampler, field);
          json.Unsigned(WireDefinition(output, key::kSamplerKind,
                                       JsonUnsigned(doc, *sampler, run), run));
        }
        json.Raw("}");
      }
      json.Raw("]");
    }
    if (!output.images.empty()) {
      json.Key(key::kImages, root);
      json.Raw("[");
      for (std::size_t i = 0; i < output.images.size(); ++i) {
        const auto& image = output.images[i];
        if (i) json.Raw(",");
        json.Raw("{");
        bool field = true;
        const auto node = TableNode(doc, key::kImages, image.source, run);
        CopyJsonField(json, doc, node, key::kName, field, run);
        json.Key(key::kMime, field);
        json.Quoted(image.mime);
        if (output.encoding == SurfaceEncoding::kGltfJson) {
          json.Key(key::kUri, field);
          json.Quoted(image.ordinary_path);
        } else {
          json.Key(key::kBufferView, field);
          json.Unsigned(AddCount(blocks.size(), i));
        }
        json.Raw("}");
      }
      json.Raw("]");
    }
  }
  json.Raw("}");
  const auto capsule_members =
      output.representation == SurfaceRepresentation::kAnnotatedBundleV1
          ? UINT64_C(2)
          : UINT64_C(0);
  std::uint64_t ordinary_members = 1, ordinary_bytes = 0;
  if (output.encoding == SurfaceEncoding::kGltfJson) {
    ordinary_members = AddCount(AddCount(ordinary_members, !binary.empty()),
                                output.images.size());
    ordinary_bytes = AddCount(json.View().size(), binary.size());
    run.Bound(binary.size(), run.limits.max_member_bytes,
              SurfaceCode::kOutputLimit);
    for (const auto& image : output.images) {
      run.Bound(image.bytes.size(), run.limits.max_member_bytes,
                SurfaceCode::kOutputLimit);
      ordinary_bytes = AddCount(ordinary_bytes, image.bytes.size());
    }
  } else {
    constexpr std::uint64_t kContainerHeaderBytes = 3 * sizeof(std::uint32_t);
    constexpr std::uint64_t kChunkHeaderBytes = 2 * sizeof(std::uint32_t);
    ordinary_bytes =
        AddCount(AddCount(kContainerHeaderBytes, kChunkHeaderBytes),
                 AlignNative(json.View().size()));
    if (!binary.empty())
      ordinary_bytes =
          AddCount(ordinary_bytes,
                   AddCount(kChunkHeaderBytes, AlignNative(binary.size())));
    run.Bound(ordinary_bytes, run.limits.max_member_bytes,
              SurfaceCode::kOutputLimit);
  }
  run.Bound(AddCount(ordinary_members, capsule_members), run.limits.max_members,
            SurfaceCode::kOutputLimit);
  run.Bound(ordinary_bytes, run.limits.max_total_member_bytes,
            SurfaceCode::kOutputLimit);
  if (output.encoding == SurfaceEncoding::kGltfJson) {
    Member main(run.allocation);
    main.path.assign(kMainGltfPath);
    main.kind = SurfaceMemberKind::kGltf;
    CopyBytes(main.bytes,
              std::as_bytes(std::span{json.View().data(), json.View().size()}),
              run);
    output.storage.push_back(std::move(main));
    if (!binary.empty()) {
      Member buffer(run.allocation);
      buffer.path.assign(kGeometryBufferPath);
      buffer.kind = SurfaceMemberKind::kGeometryBuffer;
      buffer.bytes = std::move(binary);
      output.storage.push_back(std::move(buffer));
    }
    for (const auto& image : output.images) {
      Member member(run.allocation);
      member.path.assign(image.ordinary_path);
      member.kind = SurfaceMemberKind::kImage;
      CopyBytes(member.bytes, image.bytes, run);
      output.storage.push_back(std::move(member));
    }
  } else {
    std::array<GlbChunk, 2> chunks{
        {{kGlbJsonChunkType,
          std::as_bytes(std::span{json.View().data(), json.View().size()})},
         {kGlbBinChunkType, binary}}};
    const auto ordinal = run.Enter(SurfaceDelegateKind::kGlbWrite, {}, false);
    auto result = WriteGlbContainer(
        std::span<const GlbChunk>{chunks}.first(binary.empty() ? 1 : 2),
        std::get<GltfSurfaceEncodeLimits>(limits.delegates).container,
        run.policy.container, run.execution, run.stop);
    run.facts.delegates.push_back(
        {run.stage,
         SurfaceDelegateKind::kGlbWrite,
         ordinal,
         {},
         SurfaceGlbFacts{result.status, result.error_byte_offset,
                         result.workers_used}});
    if (result.status != GlbStatus::kSuccess || !result.container) {
      if (result.status == GlbStatus::kCanceled)
        Fail(SurfaceStatus::kCanceled, run.stage, SurfaceCode::kGlbDelegate);
      if (result.status == GlbStatus::kBudgetExceeded)
        Fail(SurfaceStatus::kBudgetExceeded, run.stage,
             SurfaceCode::kGlbDelegate);
      if (result.status == GlbStatus::kLimitExceeded)
        Fail(SurfaceStatus::kLimitExceeded, run.stage,
             SurfaceCode::kGlbDelegate);
      Fail(SurfaceStatus::kDelegateFailure, run.stage,
           SurfaceCode::kGlbDelegate);
    }
    Member main(run.allocation);
    main.path.assign(kMainGlbPath);
    main.kind = SurfaceMemberKind::kGlb;
    CopyBytes(main.bytes, result.container.Bytes(), run);
    output.storage.push_back(std::move(main));
  }
  RefreshViews(output, run);
}
void EncodeStl(OutputPayload& output, const SurfaceEncodeRequest& request,
               const SurfaceEncodeLimits& limits, Run& run) {
  run.stage = SurfaceStage::kEncode;
  const auto& options = std::get<StlSurfaceOptions>(request.destination);
  const auto& caps = std::get<StlSurfaceEncodeLimits>(limits.delegates);
  const auto original = output.original.Source().View();
  Vector<double> positions{gltf_detail::Allocator<double>(run.allocation)};
  Vector<double> normals{gltf_detail::Allocator<double>(run.allocation)};
  Vector<geometry::index_t> offsets{
      gltf_detail::Allocator<geometry::index_t>(run.allocation)};
  Vector<geometry::index_t> corners{
      gltf_detail::Allocator<geometry::index_t>(run.allocation)};
  positions.resize(
      Size(Product(output.vertex_sources.size(), kPositionComponents)));
  normals.resize(
      Size(Product(output.triangle_sources.size(), kPositionComponents)));
  offsets.resize(Size(AddCount(output.triangle_sources.size(), 1)));
  corners.resize(output.vertex_sources.size());
  struct Derived {
    OutputPayload* output;
    Run* run;
    std::span<double> positions;
    std::span<geometry::index_t> corners;
  } derived{&output, &run, positions, corners};
  Bulk(
      run, output.vertex_sources.size(),
      Product(kPositionComponents, kScalar64Bytes),
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation&) {
        auto& s = *static_cast<Derived*>(opaque);
        for (auto row = first; row < last; ++row) {
          s.run->Check(row);
          const auto source = s.output->vertex_sources[Size(row)];
          s.corners[Size(row)] = row;
          for (std::uint64_t c = 0; c < kPositionComponents; ++c) {
            const auto target = s.output->position_words[Size(
                Product(source, kPositionComponents) + c)];
            s.positions[Size(Product(row, kPositionComponents) + c)] =
                std::bit_cast<double>(
                    s.output->encoding == SurfaceEncoding::kStlAscii
                        ? target
                        : Widen32(static_cast<std::uint32_t>(target)));
          }
        }
      },
      &derived);
  const auto& selected =
      original.attributes[Size(*options.corner_normal_attribute)];
  for (std::uint64_t row = 0; row < output.triangle_sources.size(); ++row) {
    run.Check(row);
    offsets[Size(row)] = Product(row, kTriangleCorners);
    const auto original_corner =
        output.corner_corners[Size(Product(row, kTriangleCorners))];
    for (std::uint64_t c = 0; c < kPositionComponents; ++c)
      normals[Size(Product(row, kPositionComponents) + c)] =
          std::bit_cast<double>(NumericBits(
              selected, Product(original_corner, kPositionComponents) + c));
  }
  offsets.back() = corners.size();
  const std::array<geometry::AttributeMaterializationInput, 1> attributes{
      {{geometry::AttributeDomain::face,
        kDerivedNormalName,
        kDerivedNormalSemantic,
        {},
        kPositionComponents,
        std::span<const double>{normals},
        {},
        {},
        {}}}};
  const geometry::MeshMaterializationInput input{
      std::span<const double>{positions},
      geometry::PolygonTopologyInput{offsets, corners}, attributes};
  const auto materialization_ordinal =
      run.Enter(SurfaceDelegateKind::kDerivedMaterialization, {}, false);
  auto materialized = geometry::MaterializeMesh(
      input, caps.derived_mesh, run.policy.surface, run.execution, run.stop);
  run.facts.delegates.push_back(
      {run.stage,
       SurfaceDelegateKind::kDerivedMaterialization,
       materialization_ordinal,
       {},
       SurfaceGeometryFacts{
           materialized.status, materialized.diagnostic, materialized.execution,
           materialized.mesh
               ? std::optional<std::uint64_t>{materialized.mesh
                                                  ->NewRequestedBytes()}
               : std::nullopt}});
  if (materialized.status != geometry::MaterializationStatus::kSuccess ||
      !materialized.mesh) {
    switch (materialized.status) {
      case geometry::MaterializationStatus::kLimitExceeded:
        Fail(SurfaceStatus::kLimitExceeded, run.stage,
             SurfaceCode::kGeometryDelegate);
      case geometry::MaterializationStatus::kBudgetExceeded:
        Fail(SurfaceStatus::kBudgetExceeded, run.stage,
             SurfaceCode::kGeometryDelegate);
      case geometry::MaterializationStatus::kCanceled:
        Fail(SurfaceStatus::kCanceled, run.stage,
             SurfaceCode::kGeometryDelegate);
      case geometry::MaterializationStatus::kUnsupportedAllocationProfile:
        Fail(SurfaceStatus::kUnsupported, run.stage,
             SurfaceCode::kGeometryDelegate);
      default:
        Fail(SurfaceStatus::kDelegateFailure, run.stage,
             SurfaceCode::kGeometryDelegate);
    }
  }
  run.Check();
  StlWriteOptions codec;
  codec.encoding = output.encoding == SurfaceEncoding::kStlAscii
                       ? StlEncoding::kAscii
                       : StlEncoding::kBinary;
  codec.binary_header = options.binary_header;
  codec.ascii_name = options.ascii_name;
  codec.face_normal_attribute = 0;
  codec.acknowledge_corner_expansion = true;
  const auto codec_ordinal =
      run.Enter(SurfaceDelegateKind::kStlWrite, {}, false);
  auto written =
      WriteStl(*materialized.mesh, codec, caps.codec, run.policy.stl, run.stop);
  SurfaceStlFacts facts{
      written.status, written.diagnostic, written.execution, {}, {}};
  if (written.output) {
    facts.requested_bytes_at_return = written.output->NewRequestedBytes();
    facts.write_facts = written.output->Facts();
  }
  run.facts.delegates.push_back(
      {run.stage, SurfaceDelegateKind::kStlWrite, codec_ordinal, {}, facts});
  if (written.status != StlStatus::kSuccess || !written.output) {
    if (written.status == StlStatus::kLimitExceeded)
      Fail(SurfaceStatus::kLimitExceeded, run.stage, SurfaceCode::kStlDelegate);
    if (written.status == StlStatus::kBudgetExceeded)
      Fail(SurfaceStatus::kBudgetExceeded, run.stage,
           SurfaceCode::kStlDelegate);
    if (written.status == StlStatus::kCanceled)
      Fail(SurfaceStatus::kCanceled, run.stage, SurfaceCode::kStlDelegate);
    Fail(SurfaceStatus::kDelegateFailure, run.stage, SurfaceCode::kStlDelegate);
  }
  run.Bound(written.output->Bytes().size(), run.limits.max_member_bytes,
            SurfaceCode::kOutputLimit);
  Member main(run.allocation);
  main.path.assign(kMainStlPath);
  main.kind = SurfaceMemberKind::kStl;
  CopyBytes(main.bytes, written.output->Bytes(), run);
  output.storage.push_back(std::move(main));
  RefreshViews(output, run);
}
Vector<SurfaceMaterialFactorView> ObserveWireCatalog(
    const OutputPayload& output, const gltf_detail::JsonSnapshot& actual,
    Run& run) {
  Vector<SurfaceMaterialFactorView> observed{
      gltf_detail::Allocator<SurfaceMaterialFactorView>(run.allocation)};
  auto wire_fail = [&]() -> void {
    Fail(SurfaceStatus::kVerificationFailed, run.stage,
         SurfaceCode::kMaterialReference);
  };
  auto table_count = [&](std::string_view name) -> std::uint64_t {
    const auto array = Field(actual, 0, name, run);
    if (!array) return 0;
    if (actual.Kind(*array) != JsonKind::kArray || !actual.FirstChild(*array))
      wire_fail();
    std::uint64_t count = 0;
    for (auto child = actual.FirstChild(*array); child;
         child = actual.NextSibling(*child)) {
      run.Check();
      Relationship(run);
      ++count;
    }
    return count;
  };
  auto equal_name = [&](const Json& source, std::size_t a, std::size_t b) {
    const auto first = Field(source, a, key::kName, run),
               second = Field(actual, b, key::kName, run);
    if (first.has_value() != second.has_value()) wire_fail();
    if (first) {
      if (actual.Kind(*second) != JsonKind::kString) wire_fail();
      const auto x = source.String(*first), y = actual.String(*second);
      if (x.size() != y.size()) wire_fail();
      for (std::size_t i = 0; i < x.size(); ++i) {
        run.Check(i);
        const bool equal = static_cast<unsigned char>(x[i]) ==
                           static_cast<unsigned char>(y[i]);
        ++run.facts.work.descriptor_byte_pairs;
        if (!equal) wire_fail();
      }
    }
  };
  const auto materials = table_count(key::kMaterials);
  if (materials != output.material_bindings.size()) wire_fail();
  std::uint64_t textures = 0, samplers = 0;
  for (const auto& d : output.definitions) {
    if (d.kind == key::kTextureKind)
      ++textures;
    else if (d.kind == key::kSamplerKind)
      ++samplers;
  }
  if (table_count(key::kTextures) != textures ||
      table_count(key::kSamplers) != samplers ||
      table_count(key::kImages) != output.images.size())
    wire_fail();
  if (!output.catalog) return observed;
  const auto& source = gltf_detail::Access::State(*output.catalog).document;
  for (std::uint64_t wire = 0; wire < materials; ++wire) {
    run.Check(wire);
    const auto original = output.material_bindings[Size(wire)].catalog_material;
    const auto a = TableNode(source, key::kMaterials, original, run),
               b = TableNode(actual, key::kMaterials, wire, run);
    Whitelist(actual, b, kMaterialKeys, run);
    equal_name(source, a, b);
    if (const auto alpha = Field(actual, b, key::kAlphaMode, run))
      if (actual.Kind(*alpha) != JsonKind::kString ||
          actual.String(*alpha) != key::kOpaque)
        wire_fail();
    if (const auto sided = Field(actual, b, key::kDoubleSided, run))
      if (actual.Kind(*sided) != JsonKind::kBool || actual.Boolean(*sided))
        wire_fail();
    const auto pbr = Field(actual, b, key::kPbr, run),
               original_pbr = Field(source, a, key::kPbr, run);
    if (pbr) Whitelist(actual, *pbr, kPbrKeys, run);
    std::optional<std::size_t> texture;
    if (pbr) texture = Field(actual, *pbr, key::kBaseTexture, run);
    std::optional<std::size_t> original_texture;
    if (original_pbr)
      original_texture = Field(source, *original_pbr, key::kBaseTexture, run);
    if (texture.has_value() != original_texture.has_value()) wire_fail();
    if (texture) {
      Whitelist(actual, *texture, kTextureInfoKeys, run);
      const auto index = Field(actual, *texture, key::kIndex, run),
                 coord = Field(actual, *texture, key::kTexCoord, run);
      const auto source_index =
          Field(source, *original_texture, key::kIndex, run);
      const auto source_coord =
          Field(source, *original_texture, key::kTexCoord, run);
      if (!index || !source_index ||
          JsonUnsigned(actual, *index, run) !=
              WireDefinition(output, key::kTextureKind,
                             JsonUnsigned(source, *source_index, run), run) ||
          (coord ? JsonUnsigned(actual, *coord, run) : 0) !=
              WireUv(
                  output,
                  source_coord ? JsonUnsigned(source, *source_coord, run) : 0,
                  run))
        wire_fail();
    }
    const CatalogDefinition* definition = nullptr;
    for (const auto& d : output.definitions)
      if (d.kind == key::kMaterialKind && d.wire == wire) {
        definition = &d;
        break;
      }
    if (!definition || !definition->source_factors) wire_fail();
    const auto factors = CollectMaterialFactors(
        actual, b, wire, SurfaceDelegateKind::kWireMaterialFactors, run);
    for (std::size_t c = 0; c < kSurfaceCoreFactorComponents; ++c) {
      run.Pair();
      if (factors.core_f32[c] != (*definition->source_factors)[c]) wire_fail();
    }
    observed.push_back(
        {original, wire, *definition->source_factors, factors.core_f32});
  }
  for (const auto kind : {key::kTextureKind, key::kSamplerKind}) {
    const auto table =
        kind == key::kTextureKind ? key::kTextures : key::kSamplers;
    for (const auto& d : output.definitions)
      if (d.kind == kind) {
        const auto a = TableNode(source, table, d.source, run),
                   b = TableNode(actual, table, *d.wire, run);
        Whitelist(actual, b,
                  kind == key::kTextureKind
                      ? std::span<const std::string_view>{kTextureKeys}
                      : std::span<const std::string_view>{kSamplerKeys},
                  run);
        equal_name(source, a, b);
        if (kind == key::kTextureKind) {
          const auto image = Field(actual, b, key::kSource, run),
                     original_image = Field(source, a, key::kSource, run);
          if (!image || !original_image ||
              JsonUnsigned(actual, *image, run) !=
                  WireDefinition(output, key::kImageKind,
                                 JsonUnsigned(source, *original_image, run),
                                 run))
            wire_fail();
          const auto sampler = Field(actual, b, key::kSampler, run),
                     original_sampler = Field(source, a, key::kSampler, run);
          if (sampler.has_value() != original_sampler.has_value()) wire_fail();
          if (sampler &&
              JsonUnsigned(actual, *sampler, run) !=
                  WireDefinition(output, key::kSamplerKind,
                                 JsonUnsigned(source, *original_sampler, run),
                                 run))
            wire_fail();
        } else {
          for (const auto name :
               {key::kMag, key::kMin, key::kWrapS, key::kWrapT}) {
            const auto first = Field(source, a, name, run),
                       second = Field(actual, b, name, run);
            if (first.has_value() != second.has_value()) wire_fail();
            if (first && JsonUnsigned(source, *first, run) !=
                             JsonUnsigned(actual, *second, run))
              wire_fail();
          }
        }
      }
  }
  for (const auto& image : output.images) {
    const auto a = TableNode(source, key::kImages, image.source, run),
               b = TableNode(actual, key::kImages, image.wire, run);
    Whitelist(actual, b, kImageKeys, run);
    equal_name(source, a, b);
    const auto mime = Field(actual, b, key::kMime, run);
    if (!mime || actual.Kind(*mime) != JsonKind::kString ||
        actual.String(*mime) != image.mime)
      wire_fail();
    const auto uri = Field(actual, b, key::kUri, run),
               view = Field(actual, b, key::kBufferView, run);
    if (output.encoding == SurfaceEncoding::kGltfJson) {
      if (!uri || view || actual.Kind(*uri) != JsonKind::kString ||
          actual.String(*uri) != image.ordinary_path)
        wire_fail();
    } else if (uri || !view)
      wire_fail();
  }
  return observed;
}
namespace {
SurfaceWireCounts CompletedWireCounts(const OutputPayload& output) {
  SurfaceWireCounts c;
  c.primitives = output.primitives.size();
  c.vertices = output.vertex_sources.size();
  c.triangles = output.triangle_sources.size();
  c.corners = output.corner_sources.size();
  c.map_entries = AddCount(AddCount(output.triangles.FaceSources().size(),
                                    output.triangles.CornerSources().size()),
                           output.triangles.FaceOutputOffsets().size());
  for (auto size : {output.triangle_sources.size(),
                    output.triangle_faces.size(), output.corner_sources.size(),
                    output.corner_corners.size(), output.vertex_sources.size(),
                    output.vertex_offsets.size(), output.vertex_members.size(),
                    output.face_offsets.size(), output.face_members.size(),
                    output.corner_offsets.size(), output.corner_members.size()})
    c.map_entries = AddCount(c.map_entries, size);
  c.materials = output.material_bindings.size();
  c.images = output.images.size();
  for (const auto& d : output.definitions) {
    if (d.kind == key::kTextureKind)
      ++c.textures;
    else if (d.kind == key::kSamplerKind)
      ++c.samplers;
  }
  c.members = output.members.size();
  for (const auto& member : output.members)
    c.member_bytes = AddCount(c.member_bytes, member.bytes.size());
  return c;
}
SurfaceDiagnostic AdmissionDiagnostic(gltf_detail::AdmissionCode code,
                                      SurfaceStage stage) {
  return {stage,
          code == gltf_detail::AdmissionCode::kBudgetExceeded
              ? SurfaceCode::kRequestBudget
          : code == gltf_detail::AdmissionCode::kCanceled
              ? SurfaceCode::kStopRequested
          : code == gltf_detail::AdmissionCode::kSizeLimit
              ? SurfaceCode::kSizeOverflow
              : SurfaceCode::kInvalidSourceStorage,
          SurfaceSubject::kNone,
          {},
          {},
          {}};
}
SurfaceStatus AdmissionStatus(gltf_detail::AdmissionCode code) {
  switch (code) {
    case gltf_detail::AdmissionCode::kBudgetExceeded:
      return SurfaceStatus::kBudgetExceeded;
    case gltf_detail::AdmissionCode::kCanceled:
      return SurfaceStatus::kCanceled;
    case gltf_detail::AdmissionCode::kSizeLimit:
      return SurfaceStatus::kLimitExceeded;
    case gltf_detail::AdmissionCode::kInvalidObject:
      return SurfaceStatus::kInvalidStorage;
  }
  return SurfaceStatus::kInvalidStorage;
}
}  // namespace
SurfaceEncodeResult EncodeForTest(const SurfaceEncodeRequest& request,
                                  const SurfaceEncodeLimits& limits,
                                  const SurfaceWorkPolicy& work,
                                  std::stop_token stop,
                                  const TestOptions& test) {
  SurfaceEncodeResult result;
  std::shared_ptr<OutputState> state;
  auto reached = SurfaceStage::kConfiguration;
  try {
    ValidateEncodeConfiguration(request, limits, work);
    reached = SurfaceStage::kSource;
    if (SurfaceNumericSupport() != SurfaceNumericProfile::kIeeeBinary32Binary64)
      Fail(SurfaceStatus::kUnsupported, reached, SurfaceCode::kNumericProfile);
    if (!request.original.IsStorageValid() ||
        !request.triangles.Inspection().IsStorageValid())
      Fail(SurfaceStatus::kInvalidStorage, reached,
           SurfaceCode::kInvalidSourceStorage);
    const auto context = request.original.Source().ShareExecutionContext();
    if (!geometry::SharesExecutionBudget(
            context, request.triangles.ShareExecutionContext()))
      Fail(SurfaceStatus::kInvalidStorage, reached,
           SurfaceCode::kForeignLedger);
    if (const auto* gltf =
            std::get_if<GltfSurfaceOptions>(&request.destination))
      if (gltf->catalog && !gltf->catalog->SharesExecutionBudget(context))
        Fail(SurfaceStatus::kInvalidStorage, reached,
             SurfaceCode::kForeignLedger);
    if (stop.stop_requested())
      Fail(SurfaceStatus::kCanceled, reached, SurfaceCode::kStopRequested);
    const gltf_detail::AllocationContext root(context, stop, test.allocation);
    const auto allocation =
        root.WithRequestLimit(Size(limits.max_new_requested_bytes));
    state = std::allocate_shared<OutputState>(
        gltf_detail::Allocator<OutputState>(allocation), allocation);
    state->payload.emplace(allocation, request);
    Run run(context, allocation, limits.counts, work, state->facts, stop, test);
    try {
      auto& output = *state->payload;
      state->facts.original = MeshCounts(request.original.Source().View(), run);
      ValidateCorrespondence(output, run);
      ValidateDisposition(output, request, run);
      BuildCatalog(output, request, run);
      BuildPlan(output, request, run);
      PrepareNumeric(output, request, run);
      if (output.encoding == SurfaceEncoding::kGltfJson ||
          output.encoding == SurfaceEncoding::kGlb)
        EncodeGltf(output, request, limits, run);
      else
        EncodeStl(output, request, limits, run);
      VerifyEncodedNumeric(output, run);
      if (output.representation == SurfaceRepresentation::kAnnotatedBundleV1)
        BuildCapsule(output, run);
      RefreshViews(output, run);
      state->facts.wire = CompletedWireCounts(output);
      run.Check();
      result.status = SurfaceStatus::kSuccess;
      result.diagnostic = {SurfaceStage::kComplete,
                           SurfaceCode::kNone,
                           SurfaceSubject::kNone,
                           {},
                           {},
                           {}};
      result.output = Access::Output(state);
      result.facts = Access::Facts(state);
      return result;
    } catch (...) {
      reached = run.stage;
      throw;
    }
  } catch (const Failure& failure) {
    result.status = failure.Status();
    result.diagnostic = failure.Diagnostic();
    if (result.status == SurfaceStatus::kLimitExceeded &&
        result.diagnostic.code == SurfaceCode::kCountLimit &&
        result.diagnostic.stage == SurfaceStage::kNumeric)
      result.diagnostic.stage = reached;
  } catch (const gltf_detail::AdmissionFailure& failure) {
    const auto code = SurfaceAdmissionCode(failure);
    result.status = AdmissionStatus(code);
    result.diagnostic = AdmissionDiagnostic(code, reached);
  }
  if (state) {
    state->payload.reset();
    result.facts = Access::Facts(state);
  }
  return result;
}
}  // namespace meshvale::interchange::surface_detail

namespace meshvale::interchange {
SurfaceFacts::SurfaceFacts(
    std::shared_ptr<const surface_detail::FactsState> state) noexcept
    : state_(std::move(state)) {}
SurfaceFacts::SurfaceFacts(const SurfaceFacts&) noexcept = default;
SurfaceFacts& SurfaceFacts::operator=(const SurfaceFacts&) noexcept = default;
SurfaceFacts::~SurfaceFacts() noexcept = default;
std::optional<SurfaceMeshCounts> SurfaceFacts::OriginalCounts() const noexcept {
  return state_->original;
}
std::optional<SurfaceMeshCounts> SurfaceFacts::TriangleCounts() const noexcept {
  return state_->triangles;
}
std::optional<SurfaceWireCounts> SurfaceFacts::WireCounts() const noexcept {
  return state_->wire;
}
std::span<const SurfaceDelegateView> SurfaceFacts::Delegates() const noexcept {
  return state_->delegates;
}
std::optional<SurfaceNumericSummary> SurfaceFacts::PositionNumeric()
    const noexcept {
  return state_->positions;
}
std::span<const SurfaceNumericSummary> SurfaceFacts::UvNumeric()
    const noexcept {
  return state_->uv;
}
geometry::MaterializationExecutionStats SurfaceFacts::Execution()
    const noexcept {
  return state_->execution;
}
SurfaceWorkCounts SurfaceFacts::ExaminedWork() const noexcept {
  return state_->work;
}
std::span<const SurfaceDelegateProfileView>
SurfaceFacts::NumericDelegateProfiles() const noexcept {
  return state_->profiles;
}
std::uint64_t SurfaceFacts::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}

SurfaceOutput::SurfaceOutput(
    std::shared_ptr<const surface_detail::OutputState> state) noexcept
    : state_(std::move(state)) {}
SurfaceOutput::SurfaceOutput(const SurfaceOutput&) noexcept = default;
SurfaceOutput& SurfaceOutput::operator=(const SurfaceOutput&) noexcept =
    default;
SurfaceOutput::~SurfaceOutput() noexcept = default;
SurfaceEncoding SurfaceOutput::Encoding() const noexcept {
  return state_->payload->encoding;
}
SurfaceRepresentation SurfaceOutput::Representation() const noexcept {
  return state_->payload->representation;
}
std::span<const SurfaceMemberView> SurfaceOutput::Members() const noexcept {
  return state_->payload->members;
}
std::span<const SurfacePrimitiveView> SurfaceOutput::Primitives()
    const noexcept {
  return state_->payload->primitive_views;
}
std::span<const SurfaceUvView> SurfaceOutput::UvMapping() const noexcept {
  return state_->payload->uv_views;
}
std::span<const SurfaceMaterialFactorView> SurfaceOutput::MaterialFactors()
    const noexcept {
  return state_->payload->material_factors;
}
std::span<const std::uint64_t> SurfaceOutput::WireTriangleFaces()
    const noexcept {
  return state_->payload->triangle_faces;
}
std::span<const std::uint64_t> SurfaceOutput::WireTriangleSources()
    const noexcept {
  return state_->payload->triangle_sources;
}
std::span<const std::uint64_t> SurfaceOutput::WireCornerCorners()
    const noexcept {
  return state_->payload->corner_corners;
}
std::span<const std::uint64_t> SurfaceOutput::WireCornerSources()
    const noexcept {
  return state_->payload->corner_sources;
}
std::span<const std::uint64_t> SurfaceOutput::WireVertexSources()
    const noexcept {
  return state_->payload->vertex_sources;
}
std::span<const std::uint64_t> SurfaceOutput::SourceVertexOffsets()
    const noexcept {
  return state_->payload->vertex_offsets;
}
std::span<const std::uint64_t> SurfaceOutput::SourceVertexWireMembers()
    const noexcept {
  return state_->payload->vertex_members;
}
std::span<const std::uint64_t> SurfaceOutput::SourceFaceOffsets()
    const noexcept {
  return state_->payload->face_offsets;
}
std::span<const std::uint64_t> SurfaceOutput::SourceFaceWireTriangles()
    const noexcept {
  return state_->payload->face_members;
}
std::span<const std::uint64_t> SurfaceOutput::SourceCornerOffsets()
    const noexcept {
  return state_->payload->corner_offsets;
}
std::span<const std::uint64_t> SurfaceOutput::SourceCornerWireCorners()
    const noexcept {
  return state_->payload->corner_members;
}
const geometry::MeshInspection& SurfaceOutput::Original() const noexcept {
  return state_->payload->original;
}
const geometry::ChargedTriangulation& SurfaceOutput::Triangles()
    const noexcept {
  return state_->payload->triangles;
}
SurfaceFacts SurfaceOutput::Facts() const noexcept {
  return surface_detail::Access::Facts(state_);
}
std::uint64_t SurfaceOutput::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}

SurfaceOutputInspection::SurfaceOutputInspection(
    std::shared_ptr<const surface_detail::InspectionState> state) noexcept
    : state_(std::move(state)) {}
SurfaceOutputInspection::SurfaceOutputInspection(
    const SurfaceOutputInspection&) noexcept = default;
SurfaceOutputInspection& SurfaceOutputInspection::operator=(
    const SurfaceOutputInspection&) noexcept = default;
SurfaceOutputInspection::~SurfaceOutputInspection() noexcept = default;
const SurfaceOutput& SurfaceOutputInspection::Expected() const noexcept {
  return state_->payload->expected;
}
std::span<const SurfaceMemberView> SurfaceOutputInspection::CapturedMembers()
    const noexcept {
  return state_->payload->members;
}
SurfaceFacts SurfaceOutputInspection::Facts() const noexcept {
  return surface_detail::Access::Facts(state_);
}
std::uint64_t SurfaceOutputInspection::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
SurfaceEncodeResult EncodeSurface(const SurfaceEncodeRequest& request,
                                  const SurfaceEncodeLimits& limits,
                                  const SurfaceWorkPolicy& work,
                                  std::stop_token stop) {
  return surface_detail::EncodeForTest(request, limits, work, stop, {});
}
SurfaceInspectionResult InspectSurfaceOutput(
    const SurfaceOutput& expected,
    std::span<const SuppliedSurfaceMember> members,
    const SurfaceInspectLimits& limits, const SurfaceWorkPolicy& work,
    std::stop_token stop) {
  return surface_detail::InspectForTest(expected, members, limits, work, stop,
                                        {});
}
}  // namespace meshvale::interchange
