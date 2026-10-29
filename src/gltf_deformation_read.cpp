// SPDX-License-Identifier: Apache-2.0
#include "gltf_deformation_read.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "gltf_metadata.h"
#include "gltf_quaternion.h"

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr std::string_view kNodes = "nodes";
constexpr std::string_view kRotation = "rotation";
constexpr std::string_view kRotationMinimum = "-1";
constexpr std::string_view kRotationMaximum = "1";
GltfRawNumberView SourceNumber(const JsonSnapshot& doc, std::size_t value) {
  const auto lexeme = doc.Number(value);
  return {{static_cast<std::uint64_t>(lexeme.data() - doc.Original().data()),
           lexeme.size()},
          lexeme.find_first_of(".eE") == std::string_view::npos
              ? GltfJsonNumberKind::kIntegerSpelling
              : GltfJsonNumberKind::kDecimalSpelling,
          lexeme};
}
}  // namespace
void ValidateDeformationConfiguration(const GltfDeformationPolicy& policy) {
  const auto& l = policy.limits;
  const auto& w = policy.work;
  const std::array<std::uint64_t, 26> values = {l.max_node_rows,
                                                l.max_mesh_users,
                                                l.max_skin_rows,
                                                l.max_joint_references,
                                                l.max_influence_sets,
                                                l.max_lanes_per_vertex,
                                                l.max_morph_targets,
                                                l.max_morph_channels,
                                                l.max_animation_channels,
                                                l.max_samplers,
                                                l.max_keys,
                                                l.max_scalar_values,
                                                l.max_sparse_writes,
                                                l.max_role_edges,
                                                l.max_relationship_steps,
                                                l.max_descriptor_bytes,
                                                l.max_decoded_bytes,
                                                l.max_new_requested_bytes,
                                                w.parallel_min_scalars,
                                                w.partition_scalars,
                                                w.cancellation_block_scalars,
                                                w.parallel_min_bytes,
                                                w.partition_bytes,
                                                w.cancellation_block_bytes,
                                                w.max_jobs,
                                                w.max_relationship_block};
  constexpr auto kMaximumHostExtent =
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
  for (const auto value : values) {
    if (value == 0)
      Fail(GltfStatus::kInvalidLimits, GltfDiagnosticCode::kZeroLimit);
    if (value > kMaximumHostExtent)
      Fail(GltfStatus::kInvalidLimits,
           GltfDiagnosticCode::kLimitNotRepresentable);
  }
}
void AdmitDeformationRotations(AssetState& state) {
  auto& observation = *state.deformation_read;
  observation.phase = GltfDeformationPhase::kDeclarations;
  observation.quaternions.emplace();
  auto& summary = *observation.quaternions;
  summary.policy = kDeformationQuaternionPolicy;
  const auto& doc = state.document;
  const auto table = FindMember(doc, 0, kNodes);
  observation.counts.node_rows = state.nodes.size();
  if (state.nodes.size() > state.read_request.deformation.limits.max_node_rows)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded);
  state.node_rotation_storage = decltype(state.node_rotation_storage)(
      Allocator<std::array<GltfRawNumberView, kQuaternionComponents>>(
          state.deformation_descriptors));
  state.node_rotations = decltype(state.node_rotations)(
      Allocator<GltfNodeRotationSourceView>(state.deformation_descriptors));
  state.node_rotation_storage.resize(state.nodes.size());
  state.node_rotations.reserve(state.nodes.size());
  std::shared_ptr<QuaternionWorkspace> workspace;
  QuaternionWork work{state.deformation_descriptors,
                      state.read_request.deformation.work,
                      0,
                      0,
                      std::nullopt,
                      true};
  for (const auto& node : state.nodes) {
    state.allocation.Checkpoint();
    const auto row = *ChildAt(doc, *table, node.table_index);
    const auto rotation = FindMember(doc, row, kRotation);
    auto& source = state.node_rotation_storage[node.table_index];
    if (!rotation) {
      state.node_rotations.push_back({node.table_index, false, {}, {}});
      continue;
    }
    std::size_t component = 0;
    for (auto value = doc.FirstChild(*rotation); value;
         value = doc.NextSibling(*value), ++component) {
      state.allocation.Checkpoint();
      const auto number = SourceNumber(doc, *value);
      // Metadata already applies this exact range before any lexical delegate.
      // Keep the original tuple's source authority explicit at this seam.
      if (CompareDecimal(number.lexeme, kRotationMinimum) < 0 ||
          CompareDecimal(number.lexeme, kRotationMaximum) > 0)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange,
             number.source);
      source[component] = number;
    }
    const auto projected =
        std::span<const std::uint32_t, kQuaternionComponents>(
            node.local_transform_bits.data() +
                GltfLocalTransformLayout::kRotationOffset,
            kQuaternionComponents);
    state.node_rotations.push_back({node.table_index, true, source, projected});
    if (!workspace)
      workspace = std::allocate_shared<QuaternionWorkspace>(
          Allocator<QuaternionWorkspace>(state.deformation_descriptors));
    if (!observation.numeric) observation.numeric.emplace();
    const auto result = EvaluateNodeQuaternion(source, projected, *workspace,
                                               work, *observation.numeric);
    ++summary.node_values_checked;
    if (result.decision != QuaternionDecision::kAdmitted) {
      observation.location.user = node.table_index;
      const bool representation =
          result.decision == QuaternionDecision::kRepresentation;
      const bool numeric =
          result.decision == QuaternionDecision::kNumericRefusal;
      if (!numeric) {
        auto reason = GltfQuaternionSourceRefusalReason::kNonunit;
        if (representation)
          reason = GltfQuaternionSourceRefusalReason::kRepresentation;
        else if (result.decision == QuaternionDecision::kRange)
          reason = GltfQuaternionSourceRefusalReason::kRange;
        else if (result.decision == QuaternionDecision::kNonfinite)
          reason = GltfQuaternionSourceRefusalReason::kNonfinite;
        summary.first_refusal =
            GltfQuaternionSourceRefusal{GltfQuaternionRole::kNodeRotation,
                                        GltfQuaternionEncoding::kJsonDecimal,
                                        node.table_index,
                                        {},
                                        {},
                                        {},
                                        {},
                                        {},
                                        reason};
      }
      const auto code =
          result.decision == QuaternionDecision::kNumericRefusal
              ? GltfDiagnosticCode::kNumericalProfile
          : representation
              ? GltfDiagnosticCode::kUnsupportedNumberRepresentation
              : GltfDiagnosticCode::kCoreRange;
      if (result.decision == QuaternionDecision::kNumericRefusal)
        observation.phase = GltfDeformationPhase::kNumericGate;
      Fail(numeric || representation ? GltfStatus::kUnsupportedProfile
                                     : GltfStatus::kMalformed,
           code,
           result.component ? std::optional(source[*result.component].source)
                            : std::nullopt,
           result.rule ? static_cast<std::uint32_t>(*result.rule) : 0);
    }
    ++summary.rows_admitted;
  }
  summary.complete = true;
}
}  // namespace meshvale::interchange::gltf_detail
