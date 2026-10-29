// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/gltf_materialization.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
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
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_accessor.h"
#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_deformation_read.h"
#include "gltf_deformation_state.h"
#include "gltf_materialization_test_internal.h"
#include "gltf_numerical_profile.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_materialization_detail {
namespace {
template <class T>
using Vector = std::vector<T, gltf_detail::Allocator<T>>;
constexpr std::string_view kPositionSemantic = "POSITION";
constexpr char kRejectionMessage[] = "glTF materialization rejected";
constexpr std::size_t kPositionComponentCount = 3;
constexpr std::size_t kTriangleCornerCount = 3;
constexpr std::string_view kMorphSemantic = "gltf.morph.delta";
constexpr std::string_view kJointsPrefix = "JOINTS_";
constexpr std::string_view kWeightsPrefix = "WEIGHTS_";
constexpr std::string_view kJointIndicesSemantic = "joint_indices";
constexpr std::string_view kJointWeightsSemantic = "joint_weights";
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr std::size_t kHexDigitsPerByte = 2;
constexpr unsigned kHexNibbleBits = 4;
constexpr unsigned kHexNibbleMask = 0x0f;
constexpr std::string_view kAssociationRoot = "gltf/";
constexpr std::string_view kAssociationBase = "/base/";
constexpr std::string_view kAssociationMorph = "/morph/";
constexpr std::string_view kAssociationSeparator = "/";
constexpr std::string_view kAssociationLengthSeparator = ":";
constexpr std::size_t kDecimalIndexCapacity =
    std::numeric_limits<std::uint64_t>::digits10 + 1;

std::size_t CheckedAdd(std::size_t left, std::size_t right) {
  if (right > std::numeric_limits<std::size_t>::max() - left)
    throw gltf_detail::AdmissionFailure(gltf_detail::AdmissionCode::kSizeLimit);
  return left + right;
}
Vector<char> AssociationName(std::uint64_t mesh, std::uint64_t primitive,
                             std::optional<std::uint64_t> target,
                             std::string_view semantic,
                             const gltf_detail::AllocationContext& allocation) {
  if (semantic.size() >
      std::numeric_limits<std::size_t>::max() / kHexDigitsPerByte)
    throw gltf_detail::AdmissionFailure(gltf_detail::AdmissionCode::kSizeLimit);
  Vector<char> name{gltf_detail::Allocator<char>(allocation)};
  auto text = [&](std::string_view value) {
    (void)CheckedAdd(name.size(), value.size());
    for (const auto byte : value) {
      allocation.Checkpoint();
      name.push_back(byte);
    }
  };
  auto number = [&](std::uint64_t value) {
    std::array<char, kDecimalIndexCapacity> digits{};
    const auto encoded =
        std::to_chars(digits.data(), digits.data() + digits.size(), value);
    text(
        {digits.data(), static_cast<std::size_t>(encoded.ptr - digits.data())});
  };
  text(kAssociationRoot);
  number(mesh);
  text(kAssociationSeparator);
  number(primitive);
  if (target) {
    text(kAssociationMorph);
    number(*target);
    text(kAssociationSeparator);
  } else
    text(kAssociationBase);
  number(semantic.size());
  text(kAssociationLengthSeparator);
  (void)CheckedAdd(name.size(), semantic.size() * kHexDigitsPerByte);
  for (const unsigned char byte : semantic) {
    allocation.Checkpoint();
    name.push_back(kHexDigits[byte >> kHexNibbleBits]);
    name.push_back(kHexDigits[byte & kHexNibbleMask]);
  }
  return name;
}

class Failure : public std::exception {
 public:
  Failure(GltfMaterializationStatus status,
          GltfMaterializationDiagnostic diagnostic) noexcept
      : status_(status), diagnostic_(diagnostic) {}
  [[nodiscard]] GltfMaterializationStatus Status() const noexcept {
    return status_;
  }
  [[nodiscard]] const GltfMaterializationDiagnostic& Diagnostic()
      const noexcept {
    return diagnostic_;
  }
  [[nodiscard]] const char* what() const noexcept override {
    return kRejectionMessage;
  }

 private:
  GltfMaterializationStatus status_;
  GltfMaterializationDiagnostic diagnostic_;
};
[[noreturn]] void Reject(GltfMaterializationStatus status,
                         GltfMaterializationDiagnosticCode code,
                         std::optional<std::uint64_t> channel = std::nullopt) {
  GltfMaterializationDiagnostic diagnostic;
  diagnostic.code = code;
  diagnostic.source_channel = channel;
  throw Failure(status, diagnostic);
}
void ValidateConfiguration(
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work) {
  for (const auto value :
       {limits.max_channels, limits.max_bridge_new_requested_bytes,
        limits.materialization.max_vertices, limits.materialization.max_faces,
        limits.materialization.max_corners,
        limits.materialization.max_face_offsets,
        limits.materialization.max_attributes,
        limits.materialization.max_attribute_scalars,
        limits.materialization.max_attribute_offsets,
        limits.materialization.max_presence_bytes,
        limits.materialization.max_metadata_entries,
        limits.materialization.max_descriptor_bytes,
        limits.materialization.max_new_requested_bytes,
        limits.inspection.max_issues, limits.inspection.max_new_requested_bytes,
        work.parallel_min_bytes, work.partition_bytes,
        work.cancellation_block_scalars, work.max_jobs}) {
    if (value == 0)
      Reject(GltfMaterializationStatus::kInvalidConfiguration,
             GltfMaterializationDiagnosticCode::kRequiredFieldZero);
    if (value > std::numeric_limits<std::size_t>::max())
      Reject(GltfMaterializationStatus::kInvalidConfiguration,
             GltfMaterializationDiagnosticCode::kConfigurationExtent);
  }
}
template <class T>
constexpr GltfComponentType ComponentType() {
  if constexpr (std::is_same_v<T, std::int8_t>)
    return GltfComponentType::kInt8;
  else if constexpr (std::is_same_v<T, std::uint8_t>)
    return GltfComponentType::kUint8;
  else if constexpr (std::is_same_v<T, std::int16_t>)
    return GltfComponentType::kInt16;
  else if constexpr (std::is_same_v<T, std::uint16_t>)
    return GltfComponentType::kUint16;
  else if constexpr (std::is_same_v<T, std::uint32_t>)
    return GltfComponentType::kUint32;
  else {
    static_assert(std::is_same_v<T, float>);
    return GltfComponentType::kFloat32;
  }
}
std::uint32_t Components(const GltfChannelView& channel,
                         std::uint64_t ordinal) {
  switch (channel.shape) {
    case GltfAccessorShape::kScalar:
    case GltfAccessorShape::kVec2:
    case GltfAccessorShape::kVec3:
    case GltfAccessorShape::kVec4:
      break;
    default:
      Reject(GltfMaterializationStatus::kSourceContractFailure,
             GltfMaterializationDiagnosticCode::kSourceDescriptor, ordinal);
  }
  const auto layout = gltf_detail::DescribeAccessorLayout(
      channel.component_type, channel.shape);
  return static_cast<std::uint32_t>(layout.rows * layout.columns);
}
geometry::ScalarIngress Ingress(const GltfChannelView& channel,
                                std::uint32_t components,
                                std::uint64_t ordinal) {
  return std::visit(
      [&](auto values) -> geometry::ScalarIngress {
        using Value = typename decltype(values)::value_type;
        if (channel.component_type != ComponentType<Value>() ||
            channel.element_count >
                std::numeric_limits<std::size_t>::max() / components ||
            values.size() != channel.element_count * components)
          Reject(GltfMaterializationStatus::kSourceContractFailure,
                 GltfMaterializationDiagnosticCode::kSourceDescriptor, ordinal);
        return values;
      },
      channel.raw);
}
GltfMaterializationEncoding Encoding(GltfComponentType type) {
  return type == GltfComponentType::kInt8 || type == GltfComponentType::kInt16
             ? GltfMaterializationEncoding::kSignedIntegerWidened
             : GltfMaterializationEncoding::kExact;
}
void RetainFailure(GltfMaterializationResult& result,
                   const std::optional<GltfDeformationObservation>& observed) {
  if (!observed) return;
  auto observation = *observed;
  observation.complete = false;
  switch (result.status) {
    case GltfMaterializationStatus::kCanceled:
      observation.status = GltfStatus::kCanceled;
      observation.diagnostic.code = GltfDiagnosticCode::kCanceled;
      break;
    case GltfMaterializationStatus::kBudgetExceeded:
      observation.status = GltfStatus::kBudgetExceeded;
      observation.diagnostic.code = GltfDiagnosticCode::kPayloadBudget;
      break;
    case GltfMaterializationStatus::kInvalidConfiguration:
      observation.status = GltfStatus::kInvalidLimits;
      observation.diagnostic.code = GltfDiagnosticCode::kInvalidRequest;
      break;
    case GltfMaterializationStatus::kLimitExceeded:
      observation.status = GltfStatus::kUnsupportedProfile;
      observation.diagnostic.code = GltfDiagnosticCode::kLimitExceeded;
      break;
    default:
      observation.status = GltfStatus::kUnsupportedProfile;
      observation.diagnostic.code = GltfDiagnosticCode::kInvalidSelection;
      break;
  }
  observation.diagnostic.status = observation.status;
  result.deformation = observation;
}

// Merge only actual samples from this comparison's joined jobs. Null caller
// partition is the real partition-zero ordering tie, not a missing sample.
void MergeNumeric(GltfDeformationNumericObservation& to,
                  const GltfDeformationNumericObservation& from) {
  to.guard_calls = CheckedAdd(to.guard_calls, from.guard_calls);
  to.established_calls =
      CheckedAdd(to.established_calls, from.established_calls);
  to.exact_blocks = CheckedAdd(to.exact_blocks, from.exact_blocks);
  if (from.compiled_profile) to.compiled_profile = from.compiled_profile;
  auto precedes = [](auto partition, bool caller, auto previous,
                     bool previous_caller) {
    return partition.value_or(0) < previous.value_or(0) ||
           (partition.value_or(0) == previous.value_or(0) && caller &&
            !previous_caller);
  };
  if (from.exception_masks.required_units) {
    const auto units = to.exception_masks.required_units.value_or(0) |
                       *from.exception_masks.required_units;
    if (!to.exception_masks.required_units ||
        precedes(from.exception_masks.sample_partition,
                 from.exception_masks.sample_caller.value_or(false),
                 to.exception_masks.sample_partition,
                 to.exception_masks.sample_caller.value_or(false)))
      to.exception_masks = from.exception_masks;
    to.exception_masks.required_units = units;
  }
  if (from.refusal &&
      (!to.refusal || precedes(from.refusal->partition, from.refusal->caller,
                               to.refusal->partition, to.refusal->caller)))
    to.refusal = from.refusal;
}
struct CanonicalPartial {
  std::exception_ptr exception;
  std::optional<GltfMaterializationDiagnostic> mismatch;
  std::optional<GltfDiagnostic> numeric_failure;
  std::optional<gltf_detail::AdmissionCode> admission;
  GltfDeformationNumericObservation numeric;
  std::uint64_t scalars = 0, relationships = 0;
};
// b093's Geometry capture/inspection delegate uses integer/memcpy widening and
// bitwise finite checks. This independent comparison has its own genuine plan
// and guards the actual caller/worker before any float-to-double operation.
void VerifyCanonical(const geometry::MeshMaterializationInput& input,
                     geometry::MeshReadView actual,
                     const geometry::ExecutionContext& context,
                     const gltf_detail::AllocationContext& allocation,
                     const GltfDeformationPolicy& policy,
                     GltfDeformationObservation& observation,
                     const gltf_detail::NativeExecutionOptions* testing) {
  const auto positions = std::get<std::span<const float>>(input.positions);
  if (actual.positions.size() != positions.size() ||
      actual.attributes.size() != input.attributes.size())
    Reject(GltfMaterializationStatus::kSourceContractFailure,
           GltfMaterializationDiagnosticCode::kDeformation);
  Vector<std::size_t> offsets{gltf_detail::Allocator<std::size_t>(allocation)};
  offsets.reserve(CheckedAdd(input.attributes.size(), 2));
  offsets.push_back(0);
  offsets.push_back(positions.size());
  auto bytes = gltf_detail::CheckedProduct(positions.size(),
                                           sizeof(float) + sizeof(double));
  auto text_equal = [&](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (i % policy.work.cancellation_block_bytes == 0)
        allocation.Checkpoint();
      bytes = CheckedAdd(bytes, 2);
      if (a[i] != b[i]) return false;
    }
    return true;
  };
  for (std::size_t ordinal = 0; ordinal < input.attributes.size(); ++ordinal) {
    allocation.Checkpoint();
    if (observation.counts.relationship_steps ==
        policy.limits.max_relationship_steps)
      Reject(GltfMaterializationStatus::kLimitExceeded,
             GltfMaterializationDiagnosticCode::kDeformation);
    ++observation.counts.relationship_steps;
    const auto& expected = input.attributes[ordinal];
    const auto& output = actual.attributes[ordinal];
    if (!text_equal(expected.name, output.name) ||
        !text_equal(expected.semantic, output.semantic) ||
        expected.domain != output.domain ||
        expected.set_index != output.set_index ||
        expected.components != output.components || output.offsets ||
        output.present || !output.metadata.empty())
      Reject(GltfMaterializationStatus::kSourceContractFailure,
             GltfMaterializationDiagnosticCode::kDeformation);
    std::visit(
        [&](auto source) {
          using Span = decltype(source);
          if constexpr (std::is_same_v<Span, std::monostate>)
            Reject(GltfMaterializationStatus::kSourceContractFailure,
                   GltfMaterializationDiagnosticCode::kDeformation);
          else {
            using Source = typename Span::value_type;
            using Destination =
                std::conditional_t<std::is_same_v<Source, std::int8_t> ||
                                       std::is_same_v<Source, std::int16_t>,
                                   std::int32_t, Source>;
            const auto* values =
                std::get_if<std::span<const Destination>>(&output.values);
            if (!values || values->size() != source.size())
              Reject(GltfMaterializationStatus::kSourceContractFailure,
                     GltfMaterializationDiagnosticCode::kDeformation);
            offsets.push_back(CheckedAdd(offsets.back(), source.size()));
            bytes = CheckedAdd(
                bytes,
                gltf_detail::CheckedProduct(
                    source.size(), sizeof(Source) + sizeof(Destination)));
          }
        },
        expected.values);
  }
  const auto scalars = offsets.back();
  if (scalars > policy.limits.max_scalar_values)
    Reject(GltfMaterializationStatus::kLimitExceeded,
           GltfMaterializationDiagnosticCode::kDeformation);
  if (!scalars) return;
  const auto streams = offsets.size() - 1;
  const auto scalar_partition =
      static_cast<std::size_t>(policy.work.partition_scalars);
  const auto useful_partition =
      scalar_partition > streams ? scalar_partition - streams : 1;
  const auto scalar_work =
      CheckedAdd(scalars, observation.counts.relationship_steps);
  const auto scalar_jobs = 1 + (scalar_work - 1) / useful_partition;
  const auto byte_jobs =
      bytes ? 1 + (bytes - 1) /
                      static_cast<std::size_t>(policy.work.partition_bytes)
            : 1;
  const auto jobs = std::min<std::size_t>(
      {scalars, static_cast<std::size_t>(policy.work.max_jobs),
       std::max(scalar_jobs, byte_jobs)});
  const auto width = 1 + (scalars - 1) / jobs;
  const auto actual_jobs = 1 + (scalars - 1) / width;
  const auto relationship_bound =
      gltf_detail::CheckedProduct(actual_jobs, streams);
  if (relationship_bound > policy.limits.max_relationship_steps -
                               observation.counts.relationship_steps)
    Reject(GltfMaterializationStatus::kLimitExceeded,
           GltfMaterializationDiagnosticCode::kDeformation);
  Vector<CanonicalPartial> partials(
      actual_jobs, CanonicalPartial{},
      gltf_detail::Allocator<CanonicalPartial>(allocation));
  const auto planned_work = CheckedAdd(scalar_work, relationship_bound);
  auto lease = (planned_work >= policy.work.parallel_min_scalars ||
                bytes >= policy.work.parallel_min_bytes)
                   ? geometry::TryReserveWorkers(context, actual_jobs)
                   : geometry::WorkerLease();
  const bool caller = lease.Count() == 0;
  struct Job {
    const geometry::MeshMaterializationInput& input;
    geometry::MeshReadView actual;
    std::span<const std::size_t> offsets;
    std::span<CanonicalPartial> partials;
    const gltf_detail::AllocationContext& allocation;
    std::size_t width;
    bool caller;
  } job{input, actual, offsets, partials, allocation, width, caller};
  auto execute = [](void* state, std::size_t ordinal) noexcept {
    auto& j = *static_cast<Job*>(state);
    auto& partial = j.partials[ordinal];
    try {
      if (!gltf_detail::EstablishGltfNumericalProfile(partial.numeric, ordinal,
                                                      j.caller)) {
        partial.numeric_failure =
            GltfDiagnostic{GltfStatus::kUnsupportedProfile,
                           GltfDiagnosticCode::kNumericalProfile,
                           {},
                           {},
                           {},
                           {},
                           0};
        return;
      }
      const auto first = ordinal * j.width;
      const auto last = first + std::min(j.width, j.offsets.back() - first);
      for (std::size_t stream = 0; stream + 1 < j.offsets.size(); ++stream) {
        j.allocation.Checkpoint();
        ++partial.relationships;
        const auto begin = std::max(first, j.offsets[stream]);
        const auto end = std::min(last, j.offsets[stream + 1]);
        if (begin >= end) continue;
        auto compare = [&](auto source, auto destination) {
          using Source = typename decltype(source)::value_type;
          using Destination = typename decltype(destination)::value_type;
          for (auto i = begin - j.offsets[stream]; i < end - j.offsets[stream];
               ++i) {
            j.allocation.Checkpoint();
            ++partial.scalars;
            bool equal;
            if constexpr (std::is_same_v<Source, float> &&
                          std::is_same_v<Destination, double>)
              equal = std::bit_cast<std::uint64_t>(
                          static_cast<double>(source[i])) ==
                      std::bit_cast<std::uint64_t>(destination[i]);
            else if constexpr (std::is_same_v<Source, float>)
              equal = std::bit_cast<std::uint32_t>(source[i]) ==
                      std::bit_cast<std::uint32_t>(destination[i]);
            else if constexpr (std::is_same_v<Source, double>)
              equal = std::bit_cast<std::uint64_t>(source[i]) ==
                      std::bit_cast<std::uint64_t>(destination[i]);
            else
              equal = static_cast<Destination>(source[i]) == destination[i];
            if (!equal) {
              GltfMaterializationDiagnostic diagnostic;
              diagnostic.code = GltfMaterializationDiagnosticCode::kDeformation;
              diagnostic.stage = GltfMaterializationStage::kInspection;
              partial.mismatch = diagnostic;
              return false;
            }
          }
          return true;
        };
        if (!stream) {
          if (!compare(std::get<std::span<const float>>(j.input.positions),
                       j.actual.positions))
            return;
        } else {
          bool equal = true;
          std::visit(
              [&](auto source) {
                using Span = decltype(source);
                if constexpr (!std::is_same_v<Span, std::monostate>) {
                  using Source = typename Span::value_type;
                  using Destination = std::conditional_t<
                      std::is_same_v<Source, std::int8_t> ||
                          std::is_same_v<Source, std::int16_t>,
                      std::int32_t, Source>;
                  equal = compare(source,
                                  std::get<std::span<const Destination>>(
                                      j.actual.attributes[stream - 1].values));
                }
              },
              j.input.attributes[stream - 1].values);
          if (!equal) return;
        }
      }
    } catch (const gltf_detail::AdmissionFailure& failure) {
      partial.admission = failure.Code();
    } catch (...) {
      partial.exception = std::current_exception();
    }
  };
  gltf_detail::NativeExecutionOptions defaults;
  const auto& execution = testing ? *testing : defaults;
  gltf_detail::OperationWorkerStats workers;
  std::exception_ptr launch_failure;
  observation.execution.planned_jobs = actual_jobs;
  if (lease.Count()) {
    try {
      gltf_detail::RunOperationJobs(allocation, lease, actual_jobs, execute,
                                    &job, execution, workers);
    } catch (...) {
      launch_failure = std::current_exception();
    }
  } else
    for (std::size_t i = 0; i < actual_jobs; ++i) {
      ++observation.execution.caller_jobs;
      if (execution.observer && execution.observer->job_entered)
        execution.observer->job_entered(i, execution.observer->state);
      execute(&job, i);
    }
  observation.execution.launched_workers = workers.launched;
  observation.execution.joined_workers = workers.joined;
  for (const auto& partial : partials) {
    MergeNumeric(*observation.numeric, partial.numeric);
    observation.counts.scalar_checks =
        CheckedAdd(observation.counts.scalar_checks, partial.scalars);
    observation.counts.relationship_steps = CheckedAdd(
        observation.counts.relationship_steps, partial.relationships);
  }
  if (launch_failure) std::rethrow_exception(launch_failure);
  for (const auto& partial : partials)
    if (partial.exception) std::rethrow_exception(partial.exception);
  for (const auto& partial : partials)
    if (partial.mismatch)
      throw Failure(GltfMaterializationStatus::kSourceContractFailure,
                    *partial.mismatch);
  for (const auto& partial : partials)
    if (partial.numeric_failure) {
      observation.phase = GltfDeformationPhase::kNumericGate;
      throw gltf_detail::GltfFailure(*partial.numeric_failure);
    }
  for (const auto& partial : partials)
    if (partial.admission)
      throw gltf_detail::AdmissionFailure(*partial.admission);
}
GltfMaterializationResult DelegatedFailure(
    GltfMaterializationStage stage, geometry::MaterializationStatus status,
    const geometry::MaterializationDiagnostic& diagnostic,
    const GltfMaterializationExecution& execution) {
  GltfMaterializationResult result;
  result.status = GltfMaterializationStatus::kGeometryFailure;
  result.diagnostic.stage = stage;
  result.diagnostic.code = GltfMaterializationDiagnosticCode::kGeometry;
  result.diagnostic.geometry_status = status;
  result.diagnostic.geometry_diagnostic = diagnostic;
  result.execution = execution;
  return result;
}
}  // namespace

struct PrimitiveState {
  PrimitiveState(const gltf_detail::AllocationContext& operation,
                 const GltfPrimitiveExtraction& source,
                 const geometry::MeshInspection& inspected,
                 Vector<GltfMaterializedChannel> channel_mappings,
                 Vector<Vector<char>> association_names,
                 Vector<GltfDeformationAssociationView> source_associations,
                 std::optional<GltfDeformationObservation> observation)
      : allocation(operation),
        extraction(source),
        asset(gltf_detail::Access::ExtractionAsset(source)),
        primitive(gltf_detail::Access::ExtractionPrimitive(source)),
        inspection(inspected),
        mappings(std::move(channel_mappings)),
        names(std::move(association_names)),
        associations(std::move(source_associations)),
        deformation(std::move(observation)) {}
  gltf_detail::AllocationContext allocation;
  GltfPrimitiveExtraction extraction;
  GltfAsset asset;
  GltfPrimitiveHandle primitive;
  geometry::MeshInspection inspection;
  Vector<GltfMaterializedChannel> mappings;
  Vector<Vector<char>> names;
  Vector<GltfDeformationAssociationView> associations;
  std::optional<GltfDeformationObservation> deformation;
};
struct Access {
  static GltfMaterializedPrimitive Make(
      std::shared_ptr<const PrimitiveState> state) noexcept {
    return GltfMaterializedPrimitive(std::move(state));
  }
};

GltfMaterializationResult TestMaterializeGltfPrimitive(
    const GltfPrimitiveExtraction& extraction,
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop,
    const TestHooks& hooks, const GltfDeformationPolicy* deformation) {
  GltfMaterializationExecution execution;
  std::optional<GltfDeformationObservation> observation;
  auto stage = GltfMaterializationStage::kBridge;
  try {
    ValidateConfiguration(limits, work);
    const auto* proof = extraction.Deformation();
    if ((proof != nullptr) != (deformation != nullptr))
      Reject(GltfMaterializationStatus::kInvalidConfiguration,
             GltfMaterializationDiagnosticCode::kDeformationPolicyRequired);
    if (deformation) {
      gltf_detail::ValidateDeformationConfiguration(*deformation);
      observation.emplace();
      observation->work = deformation->work;
      observation->effective_request_cap =
          std::min(deformation->limits.max_new_requested_bytes,
                   limits.max_bridge_new_requested_bytes);
      observation->phase = GltfDeformationPhase::kNumericGate;
      observation->numeric.emplace();
      if (!gltf_detail::EstablishGltfNumericalProfile(*observation->numeric, {},
                                                      true))
        gltf_detail::Fail(GltfStatus::kUnsupportedProfile,
                          GltfDiagnosticCode::kNumericalProfile);
      observation->phase = GltfDeformationPhase::kAssociation;
      observation->predicates.canonical_association = false;
    }
    const auto channels = extraction.Channels();
    if (channels.size() > limits.max_channels)
      Reject(GltfMaterializationStatus::kLimitExceeded,
             GltfMaterializationDiagnosticCode::kChannelLimit);
    const auto triangles = extraction.TriangleIndices();
    if (channels.empty() || triangles.size() % kTriangleCornerCount != 0 ||
        triangles.size() != extraction.SourceCorners().size())
      Reject(GltfMaterializationStatus::kSourceContractFailure,
             GltfMaterializationDiagnosticCode::kSourceDescriptor);
    const auto& context = gltf_detail::Access::ExtractionContext(extraction);
    gltf_detail::AllocationContext allocation(context, stop, hooks.allocation);
    allocation.Checkpoint();
    allocation = allocation.WithRequestLimit(
        static_cast<std::size_t>(limits.max_bridge_new_requested_bytes));
    auto descriptors = allocation;
    auto decoded = allocation;
    std::optional<gltf_detail::AllocationContext> deformation_scope;
    if (deformation) {
      const auto live = allocation.OperationActiveBytes();
      if (live >= limits.max_bridge_new_requested_bytes)
        throw gltf_detail::AdmissionFailure(
            gltf_detail::AdmissionCode::kSizeLimit);
      observation->effective_request_cap =
          std::min(deformation->limits.max_new_requested_bytes,
                   limits.max_bridge_new_requested_bytes - live);
      auto phase = allocation.WithAdditionalRequestLimit(
          static_cast<std::size_t>(observation->effective_request_cap));
      deformation_scope = phase;
      descriptors = phase.WithAdditionalRequestLimit(
          static_cast<std::size_t>(deformation->limits.max_descriptor_bytes));
      decoded = phase.WithAdditionalRequestLimit(
          static_cast<std::size_t>(deformation->limits.max_decoded_bytes));
      // The original three independently owned caps remain unchanged. Their
      // checked sum is representable; every delegate uses the same real ledger.
      (void)CheckedAdd(
          CheckedAdd(
              static_cast<std::size_t>(limits.max_bridge_new_requested_bytes),
              static_cast<std::size_t>(
                  limits.materialization.max_new_requested_bytes)),
          static_cast<std::size_t>(limits.inspection.max_new_requested_bytes));
    }
    Vector<geometry::AttributeMaterializationInput> attributes{
        gltf_detail::Allocator<geometry::AttributeMaterializationInput>(
            descriptors)};
    Vector<GltfMaterializedChannel> mappings{
        gltf_detail::Allocator<GltfMaterializedChannel>(descriptors)};
    Vector<Vector<char>> names{
        gltf_detail::Allocator<Vector<char>>(descriptors)};
    Vector<GltfDeformationAssociationView> associations{
        gltf_detail::Allocator<GltfDeformationAssociationView>(descriptors)};
    Vector<Vector<float>> morph_values{
        gltf_detail::Allocator<Vector<float>>(descriptors)};
    if (channels.size() > mappings.max_size() ||
        channels.size() - 1 > attributes.max_size())
      Reject(GltfMaterializationStatus::kLimitExceeded,
             GltfMaterializationDiagnosticCode::kBridgeRequestLimit);
    mappings.reserve(channels.size());
    attributes.reserve(channels.size() - 1);
    geometry::MeshMaterializationInput input;
    input.topology = geometry::TriangleTopologyInput{triangles};
    const auto vertex_count = channels.front().element_count;
    bool position_seen = false;
    for (std::size_t ordinal = 0; ordinal < channels.size(); ++ordinal) {
      if (ordinal % work.cancellation_block_scalars == 0)
        allocation.Checkpoint();
      const auto& channel = channels[ordinal];
      if (channel.element_count != vertex_count || channel.semantic.empty())
        Reject(GltfMaterializationStatus::kSourceContractFailure,
               GltfMaterializationDiagnosticCode::kSourceDescriptor, ordinal);
      const auto components =
          proof ? static_cast<std::uint32_t>(
                      gltf_detail::DescribeAccessorLayout(
                          channel.component_type, channel.shape)
                          .rows *
                      gltf_detail::DescribeAccessorLayout(
                          channel.component_type, channel.shape)
                          .columns)
                : Components(channel, ordinal);
      auto values = Ingress(channel, components, ordinal);
      GltfMaterializedChannel mapping;
      mapping.source_channel = ordinal;
      if (channel.semantic == kPositionSemantic) {
        if (position_seen || components != kPositionComponentCount ||
            channel.component_type != GltfComponentType::kFloat32 ||
            channel.normalized)
          Reject(GltfMaterializationStatus::kSourceContractFailure,
                 GltfMaterializationDiagnosticCode::kSourceDescriptor, ordinal);
        input.positions = std::get<std::span<const float>>(values);
        position_seen = true;
        mapping.destination = GltfMaterializationDestination::kPositions;
        mapping.encoding = GltfMaterializationEncoding::kFloatPositionWidened;
      } else {
        if (channel.set_index &&
            *channel.set_index > std::numeric_limits<std::uint32_t>::max())
          Reject(GltfMaterializationStatus::kUnsupportedRepresentation,
                 GltfMaterializationDiagnosticCode::kSetIndexRepresentation,
                 ordinal);
        geometry::AttributeMaterializationInput attribute;
        attribute.domain = geometry::AttributeDomain::vertex;
        attribute.name = channel.semantic;
        attribute.semantic = channel.semantic;
        if (proof) {
          names.push_back(AssociationName(proof->Primitive().MeshIndex(),
                                          proof->Primitive().PrimitiveIndex(),
                                          {}, channel.semantic, descriptors));
          attribute.name = {names.back().data(), names.back().size()};
          if (channel.semantic.starts_with(kJointsPrefix))
            attribute.semantic = kJointIndicesSemantic;
          if (channel.semantic.starts_with(kWeightsPrefix))
            attribute.semantic = kJointWeightsSemantic;
        }
        if (channel.set_index)
          attribute.set_index = static_cast<std::uint32_t>(*channel.set_index);
        attribute.components = components;
        attribute.values = values;
        mapping.destination = GltfMaterializationDestination::kVertexAttribute;
        mapping.attribute = attributes.size();
        mapping.encoding = Encoding(channel.component_type);
        attributes.push_back(attribute);
      }
      mappings.push_back(mapping);
      if (proof) {
        associations.push_back(
            {proof->Primitive().MeshIndex(),
             proof->Primitive().PrimitiveIndex(),
             channel.accessor_index,
             channel.semantic,
             channel.semantic == kPositionSemantic
                 ? std::string_view{}
                 : std::string_view(names.back().data(), names.back().size()),
             {},
             channel.set_index,
             mapping.destination == GltfMaterializationDestination::kPositions
                 ? GltfDeformationDestination::kCanonicalPositions
                 : GltfDeformationDestination::kCanonicalVertexAttribute,
             mapping.attribute,
             channel.semantic == kPositionSemantic
                 ? GltfDeformationTransferEncoding::kFloatPositionWidened
             : mapping.encoding ==
                     GltfMaterializationEncoding::kSignedIntegerWidened
                 ? GltfDeformationTransferEncoding::kSignedIntegerWidened
                 : GltfDeformationTransferEncoding::kExact});
      }
      if (hooks.after_descriptor)
        hooks.after_descriptor(ordinal, hooks.descriptor_state);
    }
    if (!position_seen)
      Reject(GltfMaterializationStatus::kSourceContractFailure,
             GltfMaterializationDiagnosticCode::kSourceDescriptor);
    if (proof) {
      const auto& catalog = gltf_deformation_detail::Access::Get(*proof);
      for (const auto& morph : catalog.morph_channels) {
        allocation.Checkpoint();
        if (morph.mesh != proof->Primitive().MeshIndex() ||
            morph.primitive != proof->Primitive().PrimitiveIndex())
          continue;
        const gltf_deformation_detail::AccessorStorage* source = nullptr;
        for (const auto& value : catalog.accessor_storage) {
          allocation.Checkpoint();
          if (value.source.index == morph.delta_accessor) source = &value;
        }
        if (!source)
          Reject(GltfMaterializationStatus::kSourceContractFailure,
                 GltfMaterializationDiagnosticCode::kDeformation);
        names.push_back(AssociationName(morph.mesh, morph.primitive,
                                        morph.target, morph.semantic,
                                        descriptors));
        geometry::AttributeMaterializationInput attribute;
        attribute.domain = geometry::AttributeDomain::vertex;
        attribute.name = {names.back().data(), names.back().size()};
        attribute.semantic = kMorphSemantic;
        if (morph.set) {
          if (*morph.set > std::numeric_limits<std::uint32_t>::max())
            Reject(GltfMaterializationStatus::kUnsupportedRepresentation,
                   GltfMaterializationDiagnosticCode::kSetIndexRepresentation);
          attribute.set_index = static_cast<std::uint32_t>(*morph.set);
        }
        const auto layout = gltf_detail::DescribeAccessorLayout(
            source->source.component_type, source->source.shape);
        attribute.components =
            static_cast<std::uint32_t>(layout.rows * layout.columns);
        if (source->decoded.float_words) {
          morph_values.emplace_back(gltf_detail::Allocator<float>(decoded));
          auto& values = morph_values.back();
          const auto& words =
              std::get<gltf_detail::ChargedValues<std::uint32_t>>(
                  source->decoded.raw);
          values.resize(words.size());
          for (std::size_t i = 0; i < words.size(); ++i) {
            decoded.Checkpoint();
            values[i] = std::bit_cast<float>(words[i]);
          }
          attribute.values = std::span<const float>(values);
        } else
          attribute.values = std::visit(
              [](const auto& values) -> geometry::ScalarIngress {
                return std::span(values);
              },
              source->decoded.raw);
        associations.push_back(
            {morph.mesh, morph.primitive, morph.delta_accessor, morph.semantic,
             attribute.name, morph.target, morph.set,
             GltfDeformationDestination::kCanonicalVertexAttribute,
             attributes.size(),
             Encoding(source->source.component_type) ==
                     GltfMaterializationEncoding::kSignedIntegerWidened
                 ? GltfDeformationTransferEncoding::kSignedIntegerWidened
                 : GltfDeformationTransferEncoding::kExact});
        attributes.push_back(attribute);
      }
      if (associations.size() > deformation->limits.max_role_edges ||
          attributes.size() + 1 > limits.max_channels)
        Reject(GltfMaterializationStatus::kLimitExceeded,
               GltfMaterializationDiagnosticCode::kChannelLimit);
    }
    input.attributes = attributes;
    allocation.Checkpoint();
    stage = GltfMaterializationStage::kCapture;
    auto captured = geometry::MaterializeMesh(input, limits.materialization,
                                              work, context, stop);
    execution.capture = captured.execution;
    if (captured.status != geometry::MaterializationStatus::kSuccess) {
      auto failure = DelegatedFailure(stage, captured.status,
                                      captured.diagnostic, execution);
      RetainFailure(failure, observation);
      return failure;
    }
    stage = GltfMaterializationStage::kInspection;
    auto inspected = geometry::InspectMaterializedMesh(
        *captured.mesh, limits.inspection, work, stop);
    execution.inspection = inspected.execution;
    if (inspected.status != geometry::MaterializationStatus::kSuccess) {
      auto failure = DelegatedFailure(stage, inspected.status,
                                      inspected.diagnostic, execution);
      RetainFailure(failure, observation);
      return failure;
    }
    if (!inspected.inspection->IsStorageValid()) {
      auto result = DelegatedFailure(stage, inspected.status,
                                     inspected.diagnostic, execution);
      result.status = GltfMaterializationStatus::kInvalidStorage;
      result.diagnostic.code =
          GltfMaterializationDiagnosticCode::kStorageIssues;
      result.diagnostic.storage_issue = inspected.inspection->Issues().front();
      RetainFailure(result, observation);
      return result;
    }
    if (observation)
      VerifyCanonical(input, captured.mesh->View(), context, descriptors,
                      *deformation, *observation, hooks.comparison_execution);
    stage = GltfMaterializationStage::kPublication;
    auto state = std::allocate_shared<PrimitiveState>(
        gltf_detail::Allocator<PrimitiveState>(descriptors), allocation,
        extraction, *inspected.inspection, std::move(mappings),
        std::move(names), std::move(associations), observation);
    if (observation) {
      observation->predicates.canonical_association = true;
      observation->new_requested_bytes =
          deformation_scope->OperationActiveBytes();
      observation->phase = GltfDeformationPhase::kComplete;
      observation->status = GltfStatus::kAccepted;
      observation->diagnostic = gltf_detail::AcceptedDiagnostic();
      observation->complete = true;
      state->deformation = observation;
    }
    allocation.Checkpoint();
    GltfMaterializationResult result;
    result.status = GltfMaterializationStatus::kSuccess;
    result.primitive = Access::Make(std::move(state));
    result.diagnostic.stage = stage;
    result.execution = execution;
    result.deformation = observation;
    return result;
  } catch (const gltf_detail::GltfFailure& failure) {
    GltfMaterializationResult result;
    result.status = failure.Diagnostic().status == GltfStatus::kInvalidLimits
                        ? GltfMaterializationStatus::kInvalidConfiguration
                        : GltfMaterializationStatus::kUnsupportedRepresentation;
    result.diagnostic.stage = stage;
    result.diagnostic.code = GltfMaterializationDiagnosticCode::kDeformation;
    result.execution = execution;
    if (observation) {
      observation->complete = false;
      observation->status = failure.Diagnostic().status;
      observation->diagnostic = failure.Diagnostic();
      result.deformation = observation;
    }
    return result;
  } catch (const Failure& failure) {
    GltfMaterializationResult result;
    result.status = failure.Status();
    result.diagnostic = failure.Diagnostic();
    result.execution = execution;
    RetainFailure(result, observation);
    return result;
  } catch (const gltf_detail::AdmissionFailure& failure) {
    GltfMaterializationResult result;
    result.diagnostic.stage = stage;
    result.execution = execution;
    switch (failure.Code()) {
      case gltf_detail::AdmissionCode::kSizeLimit:
        result.status = GltfMaterializationStatus::kLimitExceeded;
        result.diagnostic.code =
            GltfMaterializationDiagnosticCode::kBridgeRequestLimit;
        break;
      case gltf_detail::AdmissionCode::kBudgetExceeded:
        result.status = GltfMaterializationStatus::kBudgetExceeded;
        result.diagnostic.code =
            GltfMaterializationDiagnosticCode::kRequestBudget;
        break;
      case gltf_detail::AdmissionCode::kCanceled:
        result.status = GltfMaterializationStatus::kCanceled;
        result.diagnostic.code =
            GltfMaterializationDiagnosticCode::kStopRequested;
        break;
      case gltf_detail::AdmissionCode::kInvalidObject:
        throw;
    }
    RetainFailure(result, observation);
    return result;
  }
}
}  // namespace meshvale::interchange::gltf_materialization_detail

namespace meshvale::interchange {
GltfMaterializedPrimitive::GltfMaterializedPrimitive(
    std::shared_ptr<const gltf_materialization_detail::PrimitiveState>
        state) noexcept
    : owner_(std::move(state)) {}
GltfMaterializedPrimitive::GltfMaterializedPrimitive(
    const GltfMaterializedPrimitive&) noexcept = default;
GltfMaterializedPrimitive& GltfMaterializedPrimitive::operator=(
    const GltfMaterializedPrimitive&) noexcept = default;
GltfMaterializedPrimitive::~GltfMaterializedPrimitive() noexcept = default;
const geometry::MaterializedMesh& GltfMaterializedPrimitive::Mesh()
    const noexcept {
  return owner_->inspection.Source();
}
const geometry::MeshInspection& GltfMaterializedPrimitive::Inspection()
    const noexcept {
  return owner_->inspection;
}
const GltfPrimitiveExtraction& GltfMaterializedPrimitive::SourceExtraction()
    const noexcept {
  return owner_->extraction;
}
const GltfAsset& GltfMaterializedPrimitive::SourceAsset() const noexcept {
  return owner_->asset;
}
const GltfPrimitiveHandle& GltfMaterializedPrimitive::SourcePrimitive()
    const noexcept {
  return owner_->primitive;
}
std::span<const GltfMaterializedChannel>
GltfMaterializedPrimitive::ChannelMappings() const noexcept {
  return owner_->mappings;
}
GltfMaterializationRequestedBytes GltfMaterializedPrimitive::NewRequestedBytes()
    const noexcept {
  return {owner_->allocation.OperationActiveBytes(),
          owner_->inspection.Source().NewRequestedBytes(),
          owner_->inspection.NewRequestedBytes()};
}
std::span<const GltfDeformationAssociationView>
GltfMaterializedPrimitive::DeformationAssociations() const noexcept {
  return owner_->associations;
}
const GltfDeformationObservation* GltfMaterializedPrimitive::DeformationFacts()
    const noexcept {
  return owner_->deformation ? &*owner_->deformation : nullptr;
}
GltfMaterializationResult MaterializeGltfPrimitive(
    const GltfPrimitiveExtraction& extraction,
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop) {
  return gltf_materialization_detail::TestMaterializeGltfPrimitive(
      extraction, limits, work, stop, {});
}
GltfMaterializationResult MaterializeGltfPrimitive(
    const GltfPrimitiveExtraction& extraction,
    const GltfDeformationPolicy& deformation,
    const GltfMaterializationLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop) {
  return gltf_materialization_detail::TestMaterializeGltfPrimitive(
      extraction, limits, work, stop, {}, &deformation);
}
}  // namespace meshvale::interchange
