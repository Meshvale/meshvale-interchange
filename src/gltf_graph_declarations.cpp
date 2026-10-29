// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "gltf_metadata.h"

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr std::string_view kNodes = "nodes";
constexpr std::string_view kMeshes = "meshes";
constexpr std::string_view kSkins = "skins";
constexpr std::string_view kAccessors = "accessors";
constexpr std::string_view kAnimations = "animations";
constexpr std::string_view kPrimitives = "primitives";
constexpr std::string_view kAttributes = "attributes";
constexpr std::string_view kTargets = "targets";
constexpr std::string_view kWeights = "weights";
constexpr std::string_view kJoints = "joints";
constexpr std::string_view kSkeleton = "skeleton";
constexpr std::string_view kInverseBindMatrices = "inverseBindMatrices";
constexpr std::string_view kType = "type";
constexpr std::string_view kMatrixShape = "MAT4";
constexpr std::string_view kComponentType = "componentType";
constexpr std::string_view kNormalized = "normalized";
constexpr std::string_view kCount = "count";
constexpr std::string_view kChannels = "channels";
constexpr std::string_view kTarget = "target";
constexpr std::string_view kNode = "node";
constexpr std::string_view kPath = "path";
constexpr std::string_view kMatrix = "matrix";
constexpr std::uint64_t kFloatComponent = 5126;
constexpr std::array<std::string_view, 3> kCoreMorphAttributes = {
    "POSITION", "NORMAL", "TANGENT"};
[[noreturn]] void RelationshipFailure() {
  Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kGraphRelationship);
}
std::uint64_t Integer(const JsonSnapshot& doc, std::size_t node,
                      std::string_view field) {
  return ReadExactUnsigned(doc, *FindMember(doc, node, field));
}
std::size_t Row(const JsonSnapshot& doc, std::string_view table,
                std::uint64_t index) {
  const auto rows = FindMember(doc, 0, table);
  const auto row = rows ? ChildAt(doc, *rows, index) : std::nullopt;
  if (!row) RelationshipFailure();
  return *row;
}
bool Ancestor(const AssetState& state, std::uint64_t candidate,
              std::uint64_t node) {
  for (std::optional<std::uint64_t> current = node; current;
       current = state.nodes[*current].parent) {
    state.allocation.Checkpoint();
    if (*current == candidate) return true;
  }
  return false;
}
bool InScene(const AssetState& state, const GltfSceneView& scene,
             std::uint64_t node) {
  for (const auto root : scene.roots)
    if (Ancestor(state, root, node)) return true;
  return false;
}
void Skins(const AssetState& state) {
  const auto& doc = state.document;
  const auto skins = FindMember(doc, 0, kSkins);
  if (!skins) return;
  std::uint64_t skin_index = 0;
  for (auto skin = doc.FirstChild(*skins); skin;
       skin = doc.NextSibling(*skin), ++skin_index) {
    state.allocation.Checkpoint();
    const auto joints = *FindMember(doc, *skin, kJoints);
    auto common =
        std::optional(ReadExactUnsigned(doc, *doc.FirstChild(joints)));
    while (common) {
      bool covers = true;
      for (auto joint = doc.FirstChild(joints); joint;
           joint = doc.NextSibling(*joint))
        if (!Ancestor(state, *common, ReadExactUnsigned(doc, *joint))) {
          covers = false;
          break;
        }
      if (covers) break;
      common = state.nodes[*common].parent;
    }
    if (!common) RelationshipFailure();
    if (const auto skeleton = FindMember(doc, *skin, kSkeleton))
      if (!Ancestor(state, ReadExactUnsigned(doc, *skeleton), *common))
        RelationshipFailure();
    for (const auto& node : state.nodes) {
      state.allocation.Checkpoint();
      if (node.skin != skin_index) continue;
      for (const auto& scene : state.scenes)
        if (InScene(state, scene, node.table_index) &&
            !InScene(state, scene, *common))
          RelationshipFailure();
    }
    if (const auto inverse = FindMember(doc, *skin, kInverseBindMatrices)) {
      const auto accessor =
          Row(doc, kAccessors, ReadExactUnsigned(doc, *inverse));
      const auto normalized = FindMember(doc, accessor, kNormalized);
      if (doc.String(*FindMember(doc, accessor, kType)) != kMatrixShape ||
          Integer(doc, accessor, kComponentType) != kFloatComponent ||
          (normalized && doc.Boolean(*normalized)) ||
          Integer(doc, accessor, kCount) < ChildCount(doc, joints))
        RelationshipFailure();
    }
  }
}
void Morphs(const AssetState& state) {
  const auto& doc = state.document;
  const auto node_table = FindMember(doc, 0, kNodes);
  const auto meshes = FindMember(doc, 0, kMeshes);
  if (!meshes) return;
  std::uint64_t mesh_index = 0;
  for (auto mesh = doc.FirstChild(*meshes); mesh;
       mesh = doc.NextSibling(*mesh), ++mesh_index) {
    std::optional<std::uint64_t> target_count;
    const auto primitives = *FindMember(doc, *mesh, kPrimitives);
    for (auto primitive = doc.FirstChild(primitives); primitive;
         primitive = doc.NextSibling(*primitive)) {
      state.allocation.Checkpoint();
      const auto targets = FindMember(doc, *primitive, kTargets);
      const auto count = targets ? ChildCount(doc, *targets) : 0;
      if (!targets && state.read_request.admission_profile ==
                          GltfAdmissionProfile::kRetainedCoreDeformationV1)
        continue;
      if (target_count && *target_count != count) RelationshipFailure();
      target_count = count;
      if (!targets) continue;
      const auto attributes = *FindMember(doc, *primitive, kAttributes);
      for (auto target = doc.FirstChild(*targets); target;
           target = doc.NextSibling(*target))
        for (auto channel = doc.FirstChild(*target); channel;
             channel = doc.NextSibling(*channel)) {
          state.allocation.Checkpoint();
          const auto semantic = doc.Key(*channel);
          const auto base = FindMember(doc, attributes, semantic);
          bool core = false;
          for (const auto candidate : kCoreMorphAttributes)
            core = core || candidate == semantic;
          if (!base) {
            if (core || state.read_request.admission_profile ==
                            GltfAdmissionProfile::kRetainedCoreDeformationV1)
              RelationshipFailure();
            continue;
          }
          const auto base_accessor =
              Row(doc, kAccessors, ReadExactUnsigned(doc, *base));
          const auto delta_accessor =
              Row(doc, kAccessors, ReadExactUnsigned(doc, *channel));
          if (Integer(doc, base_accessor, kCount) !=
              Integer(doc, delta_accessor, kCount))
            RelationshipFailure();
        }
    }
    if (const auto weights = FindMember(doc, *mesh, kWeights))
      if (ChildCount(doc, *weights) != target_count.value_or(0))
        RelationshipFailure();
    for (const auto& node : state.nodes) {
      state.allocation.Checkpoint();
      if (node.mesh != mesh_index) continue;
      const auto row = *ChildAt(doc, *node_table, node.table_index);
      if (const auto weights = FindMember(doc, row, kWeights))
        if (ChildCount(doc, *weights) != target_count.value_or(0))
          RelationshipFailure();
    }
  }
}
void Animations(const AssetState& state) {
  const auto& doc = state.document;
  const auto animations = FindMember(doc, 0, kAnimations);
  if (!animations) return;
  using Destination = std::pair<std::uint64_t, std::string_view>;
  OwnedVector<Destination> destinations{
      Allocator<Destination>(state.allocation)};
  for (auto animation = doc.FirstChild(*animations); animation;
       animation = doc.NextSibling(*animation)) {
    destinations.clear();
    const auto channels = *FindMember(doc, *animation, kChannels);
    for (auto channel = doc.FirstChild(channels); channel;
         channel = doc.NextSibling(*channel)) {
      state.allocation.Checkpoint();
      const auto target = *FindMember(doc, *channel, kTarget);
      const auto target_node = FindMember(doc, target, kNode);
      if (!target_node) continue;
      const auto node_index = ReadExactUnsigned(doc, *target_node);
      const auto node = Row(doc, kNodes, node_index);
      const auto path = doc.String(*FindMember(doc, target, kPath));
      if (FindMember(doc, node, kMatrix) &&
          (state.read_request.admission_profile !=
               GltfAdmissionProfile::kRetainedCoreDeformationV1 ||
           path != kWeights))
        RelationshipFailure();
      const Destination destination(node_index, path);
      for (const auto& previous : destinations)
        if (previous == destination) RelationshipFailure();
      destinations.push_back(destination);
    }
  }
}
}  // namespace
void CheckDeclaredGraphRelationships(const AssetState& state) {
  Skins(state);
  Morphs(state);
  Animations(state);
}
}  // namespace meshvale::interchange::gltf_detail
