// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "gltf_metadata.h"

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr std::string_view kNodes = "nodes";
constexpr std::string_view kMeshes = "meshes";
constexpr std::string_view kScenes = "scenes";
constexpr std::string_view kScene = "scene";
constexpr std::string_view kChildren = "children";
constexpr std::string_view kMesh = "mesh";
constexpr std::string_view kSkin = "skin";
constexpr std::string_view kSkins = "skins";
constexpr std::string_view kName = "name";
constexpr std::string_view kPrimitives = "primitives";
constexpr std::string_view kMode = "mode";
constexpr std::string_view kAttributes = "attributes";
constexpr std::string_view kTargets = "targets";
constexpr std::string_view kWeights = "weights";
constexpr std::string_view kExtensionsPathSuffix = "/extensions";
constexpr std::string_view kAnimationPath = "path";
constexpr std::string_view kAnimations = "animations";
constexpr std::string_view kChannels = "channels";
constexpr std::string_view kTarget = "target";
constexpr std::string_view kNode = "node";
constexpr std::string_view kSamplers = "samplers";
constexpr std::string_view kSampler = "sampler";
constexpr std::string_view kInput = "input";
constexpr std::string_view kOutput = "output";
constexpr std::string_view kAccessors = "accessors";
constexpr std::string_view kIndices = "indices";
constexpr std::string_view kMaterial = "material";
constexpr std::string_view kMaterials = "materials";
constexpr std::string_view kJoints = "joints";
constexpr std::string_view kSkeleton = "skeleton";
constexpr std::string_view kInverseBindMatrices = "inverseBindMatrices";
constexpr std::string_view kCamera = "camera";
constexpr std::string_view kCameras = "cameras";
constexpr std::string_view kTextures = "textures";
constexpr std::string_view kImages = "images";
constexpr std::string_view kSource = "source";
constexpr std::string_view kTextureIndex = "index";
constexpr std::string_view kPbrMetallicRoughness = "pbrMetallicRoughness";
constexpr std::array<std::string_view, 5> kTextureRoles = {
    "baseColorTexture", "metallicRoughnessTexture", "normalTexture",
    "occlusionTexture", "emissiveTexture"};
constexpr std::string_view kBufferView = "bufferView";
constexpr std::string_view kBufferViews = "bufferViews";
constexpr std::string_view kBuffer = "buffer";
constexpr std::string_view kBuffers = "buffers";
constexpr std::string_view kByteOffset = "byteOffset";
constexpr std::string_view kByteLength = "byteLength";
constexpr std::string_view kSparse = "sparse";
constexpr std::string_view kCount = "count";
constexpr std::string_view kValues = "values";
constexpr std::string_view kMatrix = "matrix";
constexpr std::array<std::string_view, 3> kTrs = {"translation", "rotation",
                                                  "scale"};
constexpr std::array<std::size_t, 3> kTrsOffsets = {
    GltfLocalTransformLayout::kTranslationOffset,
    GltfLocalTransformLayout::kRotationOffset,
    GltfLocalTransformLayout::kScaleOffset};
constexpr std::array<std::uint8_t, 3> kTrsMasks = {
    GltfLocalTransformLayout::kAuthoredTranslation,
    GltfLocalTransformLayout::kAuthoredRotation,
    GltfLocalTransformLayout::kAuthoredScale};
constexpr std::uint64_t kTrianglesMode = 4;
constexpr std::uint64_t kFanMode = 6;
constexpr std::string_view kJointPrefix = "JOINTS_";
constexpr std::string_view kWeightPrefix = "WEIGHTS_";
constexpr std::string_view kPosition = "POSITION";
constexpr std::string_view kNormal = "NORMAL";
constexpr std::string_view kTangent = "TANGENT";
constexpr std::string_view kTexcoordPrefix = "TEXCOORD_";
constexpr std::string_view kColorPrefix = "COLOR_";
constexpr std::string_view kCustomPrefix = "_";
std::optional<std::uint64_t> Index(const JsonSnapshot& doc, std::size_t node,
                                   std::string_view key) {
  const auto member = FindMember(doc, node, key);
  return member ? std::optional(ReadExactUnsigned(doc, *member)) : std::nullopt;
}
std::size_t TableRow(const JsonSnapshot& doc, std::string_view table,
                     std::uint64_t index) {
  const auto root = FindMember(doc, 0, table);
  const auto row = root ? ChildAt(doc, *root, index) : std::nullopt;
  if (!row) Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
  return *row;
}
void Reference(const JsonSnapshot& doc, std::size_t node, std::string_view key,
               std::string_view table) {
  const auto index = Index(doc, node, key);
  if (index) (void)TableRow(doc, table, *index);
}
void ReferenceArray(const JsonSnapshot& doc, std::size_t node,
                    std::string_view key, std::string_view table) {
  const auto array = FindMember(doc, node, key);
  if (!array) return;
  for (auto child = doc.FirstReferenceChild(*array); child;
       child = doc.NextReferenceSibling(*child))
    (void)TableRow(doc, table, ReadExactUnsigned(doc, *child));
}
void PrimitiveReferences(const JsonSnapshot& doc, std::size_t primitive) {
  Reference(doc, primitive, kIndices, kAccessors);
  Reference(doc, primitive, kMaterial, kMaterials);
  const auto attributes = FindMember(doc, primitive, kAttributes);
  for (auto child = doc.FirstReferenceChild(*attributes); child;
       child = doc.NextReferenceSibling(*child))
    (void)TableRow(doc, kAccessors, ReadExactUnsigned(doc, *child));
  const auto targets = FindMember(doc, primitive, kTargets);
  if (targets)
    for (auto target = doc.FirstReferenceChild(*targets); target;
         target = doc.NextReferenceSibling(*target))
      for (auto child = doc.FirstReferenceChild(*target); child;
           child = doc.NextReferenceSibling(*child))
        (void)TableRow(doc, kAccessors, ReadExactUnsigned(doc, *child));
}
void OtherReferences(AssetState& state) {
  const auto& doc = state.document;
  Reference(doc, 0, kScene, kScenes);
  const auto accessors = FindMember(doc, 0, kAccessors);
  if (accessors)
    for (auto row = doc.FirstReferenceChild(*accessors); row;
         row = doc.NextReferenceSibling(*row)) {
      state.allocation.Checkpoint();
      Reference(doc, *row, kBufferView, kBufferViews);
      const auto sparse = FindMember(doc, *row, kSparse);
      if (!sparse) continue;
      if (*Index(doc, *sparse, kCount) > *Index(doc, *row, kCount))
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
      Reference(doc, *FindMember(doc, *sparse, kIndices), kBufferView,
                kBufferViews);
      Reference(doc, *FindMember(doc, *sparse, kValues), kBufferView,
                kBufferViews);
    }
  const auto views = FindMember(doc, 0, kBufferViews);
  if (views)
    for (auto row = doc.FirstReferenceChild(*views); row;
         row = doc.NextReferenceSibling(*row)) {
      state.allocation.Checkpoint();
      const auto buffer = TableRow(doc, kBuffers, *Index(doc, *row, kBuffer));
      const auto bytes = *Index(doc, buffer, kByteLength);
      const auto offset = Index(doc, *row, kByteOffset).value_or(0);
      const auto extent = *Index(doc, *row, kByteLength);
      if (offset > bytes || extent > bytes - offset)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout);
    }
  const auto skins = FindMember(doc, 0, kSkins);
  if (skins)
    for (auto row = doc.FirstReferenceChild(*skins); row;
         row = doc.NextReferenceSibling(*row)) {
      state.allocation.Checkpoint();
      Reference(doc, *row, kSkeleton, kNodes);
      Reference(doc, *row, kInverseBindMatrices, kAccessors);
      ReferenceArray(doc, *row, kJoints, kNodes);
    }
  const auto animations = FindMember(doc, 0, kAnimations);
  if (animations)
    for (auto animation = doc.FirstReferenceChild(*animations); animation;
         animation = doc.NextReferenceSibling(*animation)) {
      const auto samplers = *FindMember(doc, *animation, kSamplers);
      for (auto row = doc.FirstReferenceChild(samplers); row;
           row = doc.NextReferenceSibling(*row)) {
        state.allocation.Checkpoint();
        Reference(doc, *row, kInput, kAccessors);
        Reference(doc, *row, kOutput, kAccessors);
      }
      const auto channels = *FindMember(doc, *animation, kChannels);
      for (auto row = doc.FirstReferenceChild(channels); row;
           row = doc.NextReferenceSibling(*row)) {
        const auto sampler = *Index(doc, *row, kSampler);
        if (!ChildAt(doc, samplers, sampler))
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
        Reference(doc, *FindMember(doc, *row, kTarget), kNode, kNodes);
      }
    }
  const auto textures = FindMember(doc, 0, kTextures);
  if (textures)
    for (auto row = doc.FirstReferenceChild(*textures); row;
         row = doc.NextReferenceSibling(*row)) {
      Reference(doc, *row, kSource, kImages);
      Reference(doc, *row, kSampler, kSamplers);
    }
  const auto materials = FindMember(doc, 0, kMaterials);
  if (materials)
    for (auto row = doc.FirstReferenceChild(*materials); row;
         row = doc.NextReferenceSibling(*row)) {
      const auto pbr = FindMember(doc, *row, kPbrMetallicRoughness);
      for (const auto key : kTextureRoles) {
        const auto direct = FindMember(doc, *row, key);
        const auto nested = pbr ? FindMember(doc, *pbr, key) : std::nullopt;
        if (direct) Reference(doc, *direct, kTextureIndex, kTextures);
        if (nested) Reference(doc, *nested, kTextureIndex, kTextures);
      }
    }
}
GltfCapabilityReason Capability(const AssetState& state,
                                const PrimitiveRecord& primitive) {
  const auto& doc = state.document;
  const auto mode =
      Index(doc, primitive.json_node, kMode).value_or(kTrianglesMode);
  if (mode < kTrianglesMode || mode > kFanMode)
    return GltfCapabilityReason::kPrimitiveMode;
  const auto attributes = *FindMember(doc, primitive.json_node, kAttributes);
  bool unknown = false;
  for (auto channel = doc.FirstReferenceChild(attributes); channel;
       channel = doc.NextReferenceSibling(*channel)) {
    const auto key = doc.Key(*channel);
    if (key.starts_with(kJointPrefix) || key.starts_with(kWeightPrefix))
      return GltfCapabilityReason::kJointWeightChannels;
    if (key != kPosition && key != kNormal && key != kTangent &&
        !key.starts_with(kTexcoordPrefix) && !key.starts_with(kColorPrefix) &&
        !key.starts_with(kCustomPrefix))
      unknown = true;
  }
  const auto mesh = TableRow(doc, kMeshes, primitive.mesh_index);
  if (FindMember(doc, primitive.json_node, kTargets) ||
      FindMember(doc, mesh, kWeights))
    return GltfCapabilityReason::kMorphBinding;
  const auto animation_table = FindMember(doc, 0, kAnimations);
  bool animated = false;
  bool extension = false;
  for (std::size_t node = 0; node < state.guard_rules.size(); ++node) {
    state.allocation.Checkpoint();
    doc.ReferenceVisit();
    if (GuardPath(state.guard_rules[node]).ends_with(kExtensionsPathSuffix) &&
        ChildCount(doc, node) != 0)
      extension = true;
  }
  const auto node_table = FindMember(doc, 0, kNodes);
  // The first reason has profile priority independent of authored use order.
  for (std::size_t use = 0; use < state.nodes.size(); ++use) {
    doc.ReferenceVisit();
    if (state.nodes[use].mesh == primitive.mesh_index &&
        FindMember(doc, *ChildAt(doc, *node_table, use), kWeights))
      return GltfCapabilityReason::kMorphBinding;
  }
  for (std::size_t use = 0; use < state.nodes.size(); ++use) {
    doc.ReferenceVisit();
    if (state.nodes[use].mesh != primitive.mesh_index) continue;
    if (state.nodes[use].skin) return GltfCapabilityReason::kSkinBinding;
    for (std::optional<std::uint64_t> ancestor = use; ancestor;
         ancestor = state.nodes[*ancestor].parent) {
      doc.ReferenceVisit();
      if (animation_table)
        for (auto animation = doc.FirstReferenceChild(*animation_table);
             animation; animation = doc.NextReferenceSibling(*animation)) {
          const auto channels = *FindMember(doc, *animation, kChannels);
          for (auto channel = doc.FirstReferenceChild(channels); channel;
               channel = doc.NextReferenceSibling(*channel)) {
            const auto target = *FindMember(doc, *channel, kTarget);
            if (Index(doc, target, kNode) != ancestor) continue;
            const auto path =
                doc.String(*FindMember(doc, target, kAnimationPath));
            const bool transform =
                std::find(kTrs.begin(), kTrs.end(), path) != kTrs.end();
            if (transform || (path == kWeights && *ancestor == use))
              animated = true;
          }
        }
    }
  }
  if (animated) return GltfCapabilityReason::kAnimationBinding;
  if (extension) return GltfCapabilityReason::kOptionalExtensionEffect;
  if (unknown) return GltfCapabilityReason::kUnknownCoreRole;
  return GltfCapabilityReason::kBinaryAdmissionPending;
}
}  // namespace
void BuildGraph(AssetState& state) {
  const auto& doc = state.document;
  OtherReferences(state);
  const auto node_table = FindMember(doc, 0, kNodes);
  const auto count = node_table ? ChildCount(doc, *node_table) : 0;
  if (count > state.limits.max_table_rows)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded);
  state.nodes.reserve(static_cast<std::size_t>(count));
  state.children.reserve(static_cast<std::size_t>(count));
  std::uint64_t edges = 0;
  // Every valid DOM node is below NodeCount; keep the cursor payload
  // initialized even when the table or its next row is absent.
  const auto missing_source_row = doc.NodeCount();
  auto source_row =
      node_table
          ? doc.FirstReferenceChild(*node_table).value_or(missing_source_row)
          : missing_source_row;
  for (std::uint64_t index = 0; index < count;
       ++index, source_row = doc.NextReferenceSibling(source_row)
                                 .value_or(missing_source_row)) {
    doc.ReferenceVisit();
    state.allocation.Checkpoint();
    if (source_row == missing_source_row)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
    const auto source = source_row;
    Reference(doc, source, kMesh, kMeshes);
    Reference(doc, source, kSkin, kSkins);
    Reference(doc, source, kCamera, kCameras);
    ReferenceArray(doc, source, kChildren, kNodes);
    state.children.emplace_back(Allocator<std::uint64_t>(state.allocation));
    const auto children = FindMember(doc, source, kChildren);
    if (children)
      for (auto child = doc.FirstReferenceChild(*children); child;
           child = doc.NextReferenceSibling(*child)) {
        if (edges == state.limits.max_graph_edges)
          Fail(GltfStatus::kUnsupportedProfile,
               GltfDiagnosticCode::kLimitExceeded);
        ++edges;
        state.children.back().push_back(ReadExactUnsigned(doc, *child));
      }
    GltfNodeView node{index,
                      {},
                      {},
                      state.children.back(),
                      Index(doc, source, kMesh),
                      Index(doc, source, kSkin),
                      GltfTransformKind::kDefaultTrs,
                      {},
                      0};
    const auto name = FindMember(doc, source, kName);
    if (name) node.name = doc.String(*name);
    constexpr auto kUnitTransformBits = std::bit_cast<std::uint32_t>(1.0f);
    node.local_transform_bits[GltfLocalTransformLayout::kRotationWSlot] =
        kUnitTransformBits;
    for (std::size_t component = 0;
         component < GltfLocalTransformLayout::kScaleComponents; ++component)
      node.local_transform_bits[GltfLocalTransformLayout::kScaleOffset +
                                component] = kUnitTransformBits;
    const auto matrix = FindMember(doc, source, kMatrix);
    if (matrix) {
      node.transform_kind = GltfTransformKind::kMatrix;
      node.authored_transform_mask = GltfLocalTransformLayout::kAuthoredMatrix;
      for (std::uint64_t i = 0; i < node.local_transform_bits.size(); ++i)
        node.local_transform_bits[i] =
            ProjectFloatBits(state, *ChildAt(JsonSnapshot(doc), *matrix, i));
    } else {
      for (std::size_t field = 0; field < kTrs.size(); ++field) {
        const auto array = FindMember(doc, source, kTrs[field]);
        if (!array) continue;
        node.transform_kind = GltfTransformKind::kTrs;
        node.authored_transform_mask |= kTrsMasks[field];
        std::size_t at = kTrsOffsets[field];
        for (auto value = doc.FirstChild(*array); value;
             value = doc.NextSibling(*value))
          node.local_transform_bits[at++] = ProjectFloatBits(state, *value);
      }
    }
    state.nodes.push_back(node);
  }
  for (const auto& node : state.nodes) {
    doc.ReferenceVisit();
    for (const auto child : node.children) {
      doc.ReferenceVisit();
      if (state.nodes[child].parent)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kGraphRelationship);
      state.nodes[child].parent = node.table_index;
    }
  }
  for (const auto& node : state.nodes) {
    doc.ReferenceVisit();
    state.allocation.Checkpoint();
    auto ancestor = node.parent;
    std::uint64_t walked = 0;
    while (ancestor) {
      doc.ReferenceVisit();
      if (++walked > count)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kGraphRelationship);
      ancestor = state.nodes[*ancestor].parent;
    }
  }
  const auto mesh_table = FindMember(doc, 0, kMeshes);
  if (mesh_table) {
    std::uint64_t mesh_index = 0;
    for (auto mesh = doc.FirstReferenceChild(*mesh_table); mesh;
         mesh = doc.NextReferenceSibling(*mesh), ++mesh_index) {
      const auto primitives = *FindMember(doc, *mesh, kPrimitives);
      std::uint64_t primitive_index = 0;
      for (auto primitive = doc.FirstReferenceChild(primitives); primitive;
           primitive = doc.NextReferenceSibling(*primitive),
                ++primitive_index) {
        state.allocation.Checkpoint();
        PrimitiveReferences(doc, *primitive);
        if (state.primitives.size() >= state.limits.max_table_rows)
          Fail(GltfStatus::kUnsupportedProfile,
               GltfDiagnosticCode::kLimitExceeded);
        state.primitives.push_back(
            {mesh_index, primitive_index, *primitive,
             GltfCapabilityReason::kBinaryAdmissionPending});
      }
    }
  }
  for (auto& primitive : state.primitives) {
    doc.ReferenceVisit();
    primitive.capability = Capability(state, primitive);
  }
  const auto scene_table = FindMember(doc, 0, kScenes);
  if (!scene_table) return;
  const auto scene_count = ChildCount(doc, *scene_table);
  state.roots.reserve(static_cast<std::size_t>(scene_count));
  state.scenes.reserve(static_cast<std::size_t>(scene_count));
  OwnedVector<std::uint64_t> stack{Allocator<std::uint64_t>(state.allocation)};
  std::uint64_t scene_index = 0;
  for (auto scene = doc.FirstReferenceChild(*scene_table); scene;
       scene = doc.NextReferenceSibling(*scene), ++scene_index) {
    state.roots.emplace_back(Allocator<std::uint64_t>(state.allocation));
    ReferenceArray(doc, *scene, kNodes, kNodes);
    const auto roots = FindMember(doc, *scene, kNodes);
    if (roots)
      for (auto node = doc.FirstReferenceChild(*roots); node;
           node = doc.NextReferenceSibling(*node)) {
        const auto index = ReadExactUnsigned(doc, *node);
        if (state.nodes[index].parent)
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kGraphRelationship);
        state.roots.back().push_back(index);
      }
    const auto name = FindMember(doc, *scene, kName);
    state.scenes.push_back({scene_index,
                            name ? doc.String(*name) : std::string_view{},
                            state.roots.back()});
    stack.assign(state.roots.back().rbegin(), state.roots.back().rend());
    while (!stack.empty()) {
      state.allocation.Checkpoint();
      const auto node = stack.back();
      doc.ReferenceVisit();
      stack.pop_back();
      const auto& view = state.nodes[node];
      if (view.mesh)
        for (std::size_t ordinal = 0; ordinal < state.primitives.size();
             ++ordinal) {
          doc.ReferenceVisit();
          if (state.primitives[ordinal].mesh_index == *view.mesh) {
            if (state.instances.size() >= state.limits.max_instances)
              Fail(GltfStatus::kUnsupportedProfile,
                   GltfDiagnosticCode::kLimitExceeded);
            state.instances.push_back({scene_index, node, ordinal});
          }
        }
      for (auto child = view.children.rbegin(); child != view.children.rend();
           ++child) {
        doc.ReferenceVisit();
        stack.push_back(*child);
      }
    }
  }
}
}  // namespace meshvale::interchange::gltf_detail
