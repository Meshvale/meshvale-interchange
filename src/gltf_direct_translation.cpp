// SPDX-License-Identifier: Apache-2.0
#include "gltf_direct_translation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "gltf_json_snapshot.h"
#include "gltf_metadata.h"
#include "meshvale/geometry/execution.h"

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr std::uint32_t kMembershipRule = 315;
constexpr std::uint32_t kSampleRule = 316;
constexpr std::uint32_t kLimitRule = 317;
constexpr std::size_t kFloatBytes = sizeof(std::uint32_t);
constexpr std::size_t kTranslationComponents = kDirectTranslationComponents;
constexpr std::uint64_t kFloatComponent = 5126;
constexpr std::uint64_t kTrianglesMode = 4;
constexpr std::uint64_t kFanMode = 6;
constexpr std::uint32_t kSignBit = 0x80000000U;
constexpr std::uint32_t kMagnitudeBits = 0x7fffffffU;
constexpr std::uint32_t kExponentBits = 0x7f800000U;
constexpr std::size_t kByteBits = 8;
constexpr std::size_t kMinimumWorkers = 2;
constexpr std::size_t kUserMetadataBytes = 16;
constexpr std::size_t kChannelMetadataBytes = 64;
constexpr std::size_t kSamplerMetadataBytes = 64;
constexpr std::size_t kDecodedKeyBytes =
    kFloatBytes * (1 + kTranslationComponents);
constexpr std::uint8_t kAnimationRole = 1;
constexpr std::uint8_t kVertexRole = 2;
constexpr std::uint8_t kIndexRole = 4;
constexpr std::uint8_t kSkinRole = 8;
constexpr std::uint8_t kImageRole = 16;
constexpr std::uint8_t kSparseIndexRole = 32;
constexpr std::uint8_t kSparseValueRole = 64;
constexpr std::size_t kMissing = std::numeric_limits<std::size_t>::max();
constexpr std::string_view kAccessors = "accessors";
constexpr std::string_view kBufferViews = "bufferViews";
constexpr std::string_view kBuffers = "buffers";
constexpr std::string_view kBufferView = "bufferView";
constexpr std::string_view kBuffer = "buffer";
constexpr std::string_view kByteOffset = "byteOffset";
constexpr std::string_view kByteLength = "byteLength";
constexpr std::string_view kByteStride = "byteStride";
constexpr std::string_view kTarget = "target";
constexpr std::string_view kTargets = "targets";
constexpr std::string_view kNormalized = "normalized";
constexpr std::string_view kComponentType = "componentType";
constexpr std::string_view kType = "type";
constexpr std::string_view kCount = "count";
constexpr std::string_view kSparse = "sparse";
constexpr std::string_view kIndices = "indices";
constexpr std::string_view kValues = "values";
constexpr std::string_view kMinimum = "min";
constexpr std::string_view kMaximum = "max";
constexpr std::string_view kScalar = "SCALAR";
constexpr std::string_view kVector = "VEC3";
constexpr std::string_view kMeshes = "meshes";
constexpr std::string_view kPrimitives = "primitives";
constexpr std::string_view kAttributes = "attributes";
constexpr std::string_view kMode = "mode";
constexpr std::string_view kWeights = "weights";
constexpr std::string_view kNodes = "nodes";
constexpr std::string_view kSkins = "skins";
constexpr std::string_view kInverseBindMatrices = "inverseBindMatrices";
constexpr std::string_view kImages = "images";
constexpr std::string_view kAnimations = "animations";
constexpr std::string_view kChannels = "channels";
constexpr std::string_view kSamplers = "samplers";
constexpr std::string_view kSampler = "sampler";
constexpr std::string_view kNode = "node";
constexpr std::string_view kPath = "path";
constexpr std::string_view kTranslation = "translation";
constexpr std::string_view kInput = "input";
constexpr std::string_view kOutput = "output";
constexpr std::string_view kInterpolation = "interpolation";
constexpr std::string_view kLinear = "LINEAR";
constexpr std::string_view kStep = "STEP";
constexpr std::string_view kPosition = "POSITION";
constexpr std::string_view kNormal = "NORMAL";
constexpr std::string_view kTangent = "TANGENT";
constexpr std::string_view kTexcoordPrefix = "TEXCOORD_";
constexpr std::string_view kColorPrefix = "COLOR_";
constexpr std::string_view kCustomPrefix = "_";
constexpr std::string_view kJointPrefix = "JOINTS_";
constexpr std::string_view kWeightPrefix = "WEIGHTS_";
constexpr std::string_view kExtensionSuffix = "/extensions";

std::size_t Host(std::uint64_t value) {
  if (value > std::numeric_limits<std::size_t>::max())
    Fail(GltfStatus::kInvalidLimits, GltfDiagnosticCode::kLimitNotRepresentable,
         {}, kLimitRule);
  return static_cast<std::size_t>(value);
}
std::size_t Add(std::size_t left, std::size_t right) {
  if (right > std::numeric_limits<std::size_t>::max() - left)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow, {},
         kLimitRule);
  return left + right;
}
void Limit(bool exceeded) {
  if (exceeded)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded,
         {}, kLimitRule);
}
void Unsupported(std::uint32_t rule = kMembershipRule) {
  Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kUnsupportedEnum,
       {}, rule);
}
std::size_t Required(const JsonSnapshot& doc, std::size_t object,
                     std::string_view key) {
  const auto node = FindMember(doc, object, key);
  if (!node)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired, {},
         kSampleRule);
  return *node;
}
std::uint64_t Unsigned(const JsonSnapshot& doc, std::size_t object,
                       std::string_view key, std::uint64_t fallback = 0) {
  const auto node = FindMember(doc, object, key);
  return node ? ReadExactUnsigned(doc, *node) : fallback;
}
bool EqualBits(std::uint32_t left, std::uint32_t right) noexcept {
  return left == right ||
         (!(left & kMagnitudeBits) && !(right & kMagnitudeBits));
}
bool Finite(std::uint32_t value) noexcept {
  return (value & kExponentBits) != kExponentBits;
}
std::uint32_t Ordered(std::uint32_t value) noexcept {
  if (!(value & kMagnitudeBits)) value = 0;
  return value & kSignBit ? ~value : value ^ kSignBit;
}

struct Tables {
  Tables(const AssetState& source, const AllocationContext& scope)
      : asset(source),
        allocation(scope),
        accessors(Allocator<std::size_t>(scope)),
        views(Allocator<std::size_t>(scope)),
        roles(Allocator<std::uint8_t>(scope)),
        buffers(Allocator<const GltfResourceView*>(scope)) {
    Index(kAccessors, accessors);
    Index(kBufferViews, views);
    roles.resize(views.size());
    const auto table = FindMember(asset.document, 0, kBuffers);
    buffers.resize(table ? Host(ChildCount(asset.document, *table)) : 0);
    for (const auto& resource : asset.resources) {
      allocation.Checkpoint();
      if (resource.kind == GltfResourceKind::kBuffer) {
        if (resource.table_index >= buffers.size())
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
        buffers[Host(resource.table_index)] = &resource;
      }
    }
  }
  void Index(std::string_view key, OwnedVector<std::size_t>& output) const {
    const auto& doc = asset.document;
    if (const auto table = FindMember(doc, 0, key)) {
      output.reserve(Host(ChildCount(doc, *table)));
      for (auto node = doc.FirstChild(*table); node;
           node = doc.NextSibling(*node)) {
        allocation.Checkpoint();
        output.push_back(*node);
      }
    }
  }
  std::size_t Accessor(std::uint64_t index) const {
    if (index >= accessors.size())
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
    return accessors[Host(index)];
  }
  std::size_t View(std::uint64_t index) const {
    if (index >= views.size())
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
    return views[Host(index)];
  }
  void MarkView(std::uint64_t index, std::uint8_t role) {
    (void)View(index);
    roles[Host(index)] |= role;
  }
  void MarkAccessor(std::size_t reference, std::uint8_t role) {
    allocation.Checkpoint();
    const auto& doc = asset.document;
    const auto row = Accessor(ReadExactUnsigned(doc, reference));
    if (const auto view = FindMember(doc, row, kBufferView))
      MarkView(ReadExactUnsigned(doc, *view), role);
  }
  void MarkAttributes(std::size_t object) {
    const auto& doc = asset.document;
    for (auto member = doc.FirstChild(object); member;
         member = doc.NextSibling(*member))
      MarkAccessor(*member, kVertexRole);
  }
  void InventoryRoles() {
    const auto& doc = asset.document;
    if (const auto meshes = FindMember(doc, 0, kMeshes))
      for (auto mesh = doc.FirstChild(*meshes); mesh;
           mesh = doc.NextSibling(*mesh)) {
        allocation.Checkpoint();
        const auto primitives = Required(doc, *mesh, kPrimitives);
        for (auto primitive = doc.FirstChild(primitives); primitive;
             primitive = doc.NextSibling(*primitive)) {
          allocation.Checkpoint();
          MarkAttributes(Required(doc, *primitive, kAttributes));
          if (const auto indices = FindMember(doc, *primitive, kIndices))
            MarkAccessor(*indices, kIndexRole);
          if (const auto targets = FindMember(doc, *primitive, kTargets))
            for (auto target = doc.FirstChild(*targets); target;
                 target = doc.NextSibling(*target)) {
              allocation.Checkpoint();
              MarkAttributes(*target);
            }
        }
      }
    if (const auto skins = FindMember(doc, 0, kSkins))
      for (auto skin = doc.FirstChild(*skins); skin;
           skin = doc.NextSibling(*skin)) {
        allocation.Checkpoint();
        if (const auto matrices = FindMember(doc, *skin, kInverseBindMatrices))
          MarkAccessor(*matrices, kSkinRole);
      }
    if (const auto animations = FindMember(doc, 0, kAnimations))
      for (auto animation = doc.FirstChild(*animations); animation;
           animation = doc.NextSibling(*animation)) {
        allocation.Checkpoint();
        const auto samplers = Required(doc, *animation, kSamplers);
        for (auto sampler = doc.FirstChild(samplers); sampler;
             sampler = doc.NextSibling(*sampler)) {
          allocation.Checkpoint();
          MarkAccessor(Required(doc, *sampler, kInput), kAnimationRole);
          MarkAccessor(Required(doc, *sampler, kOutput), kAnimationRole);
        }
      }
    if (const auto images = FindMember(doc, 0, kImages))
      for (auto image = doc.FirstChild(*images); image;
           image = doc.NextSibling(*image)) {
        allocation.Checkpoint();
        if (const auto view = FindMember(doc, *image, kBufferView))
          MarkView(ReadExactUnsigned(doc, *view), kImageRole);
      }
    for (const auto row : accessors) {
      allocation.Checkpoint();
      if (const auto sparse = FindMember(doc, row, kSparse)) {
        const auto indices = Required(doc, *sparse, kIndices);
        const auto values = Required(doc, *sparse, kValues);
        MarkView(ReadExactUnsigned(doc, Required(doc, indices, kBufferView)),
                 kSparseIndexRole);
        MarkView(ReadExactUnsigned(doc, Required(doc, values, kBufferView)),
                 kSparseValueRole);
      }
    }
  }
  DirectTranslationAccessor Dense(std::uint64_t index,
                                  std::size_t components) const {
    const auto& doc = asset.document;
    const auto row = Accessor(index);
    if (FindMember(doc, row, kSparse)) Unsupported(kSampleRule);
    if (Unsigned(doc, row, kComponentType) != kFloatComponent ||
        doc.String(Required(doc, row, kType)) !=
            (components == 1 ? kScalar : kVector) ||
        (FindMember(doc, row, kNormalized) &&
         doc.Boolean(*FindMember(doc, row, kNormalized))))
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldType, {},
           kSampleRule);
    const auto count = Unsigned(doc, row, kCount);
    if (!count)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange, {},
           kSampleRule);
    const auto base = FindMember(doc, row, kBufferView);
    if (!base) Unsupported(kSampleRule);
    const auto view_index = ReadExactUnsigned(doc, *base);
    const auto view = View(view_index);
    if (FindMember(doc, view, kByteStride) || FindMember(doc, view, kTarget) ||
        roles[Host(view_index)] != kAnimationRole)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout, {},
           kSampleRule);
    const auto buffer_index = Unsigned(doc, view, kBuffer);
    if (buffer_index >= buffers.size() || !buffers[Host(buffer_index)])
      Fail(GltfStatus::kResourceBindingMismatch,
           GltfDiagnosticCode::kMissingResource);
    const auto& resource = *buffers[Host(buffer_index)];
    const auto view_offset = Host(Unsigned(doc, view, kByteOffset));
    const auto view_size = Host(Unsigned(doc, view, kByteLength));
    const auto offset = Host(Unsigned(doc, row, kByteOffset));
    const auto extent =
        CheckedProduct(Host(count), CheckedProduct(components, kFloatBytes));
    if (view_offset % kFloatBytes || offset % kFloatBytes ||
        view_offset > resource.logical_bytes ||
        view_size > resource.logical_bytes - view_offset ||
        view_offset > resource.bytes.size() ||
        view_size > resource.bytes.size() - view_offset || offset > view_size ||
        extent > view_size - offset)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kResourceExtent, {},
           kSampleRule);
    const auto start = Add(view_offset, offset);
    return {index,
            view_index,
            buffer_index,
            start,
            count,
            components,
            resource.bytes.subspan(start, extent)};
  }
  const AssetState& asset;
  const AllocationContext& allocation;
  OwnedVector<std::size_t> accessors;
  OwnedVector<std::size_t> views;
  OwnedVector<std::uint8_t> roles;
  OwnedVector<const GltfResourceView*> buffers;
};

void MetadataLimit(const DirectTranslationState& proof,
                   const GltfDirectTranslationLimits& limits,
                   std::size_t users = 0, std::size_t samplers = 0) {
  auto bytes = proof.membership.size();
  bytes = Add(bytes, CheckedProduct(Add(proof.users.size(), users),
                                    kUserMetadataBytes));
  bytes = Add(bytes, CheckedProduct(Host(proof.facts.scanned_channels),
                                    kChannelMetadataBytes));
  bytes = Add(bytes, CheckedProduct(Add(proof.samplers.size(), samplers),
                                    kSamplerMetadataBytes));
  Limit(bytes > limits.max_metadata_bytes);
}
void Membership(DirectTranslationState& proof,
                const GltfDirectTranslationLimits& limits) {
  const auto& asset = *proof.asset;
  const auto& doc = asset.document;
  const auto& allocation = proof.allocation;
  Limit(asset.nodes.size() > limits.max_node_rows);
  Limit(asset.nodes.size() > limits.max_metadata_bytes);
  proof.membership.resize(asset.nodes.size());
  MetadataLimit(proof, limits);
  const auto nodes = FindMember(doc, 0, kNodes);
  auto node_row = nodes ? doc.FirstChild(*nodes) : std::optional<std::size_t>{};
  for (std::size_t node = 0; node < asset.nodes.size(); ++node) {
    allocation.Checkpoint();
    ++proof.facts.node_rows;
    if (asset.nodes[node].mesh == proof.primitive.mesh_index) {
      ++proof.facts.mesh_users;
      Limit(proof.facts.mesh_users > limits.max_mesh_users);
      if (asset.nodes[node].skin || FindMember(doc, *node_row, kWeights))
        Unsupported();
      proof.membership[node] |= GltfDirectTranslationProofView::kMeshUser;
      MetadataLimit(proof, limits, 1);
      proof.users.push_back(node);
      MetadataLimit(proof, limits);
    }
    node_row = doc.NextSibling(*node_row);
  }
  for (const auto user : proof.users)
    for (auto parent = asset.nodes[Host(user)].parent; parent;
         parent = asset.nodes[Host(*parent)].parent) {
      allocation.Checkpoint();
      ++proof.facts.relationship_steps;
      Limit(proof.facts.relationship_steps > limits.max_relationship_steps);
      proof.membership[Host(*parent)] |=
          GltfDirectTranslationProofView::kStrictAncestor;
    }
  const auto mode =
      Unsigned(doc, proof.primitive.json_node, kMode, kTrianglesMode);
  if (mode < kTrianglesMode || mode > kFanMode) Unsupported();
  const auto attributes = Required(doc, proof.primitive.json_node, kAttributes);
  for (auto member = doc.FirstChild(attributes); member;
       member = doc.NextSibling(*member)) {
    allocation.Checkpoint();
    const auto key = doc.Key(*member);
    if (key.starts_with(kJointPrefix) || key.starts_with(kWeightPrefix) ||
        (key != kPosition && key != kNormal && key != kTangent &&
         !key.starts_with(kTexcoordPrefix) && !key.starts_with(kColorPrefix) &&
         !key.starts_with(kCustomPrefix)))
      Unsupported();
  }
  const auto meshes = Required(doc, 0, kMeshes);
  const auto mesh = *ChildAt(doc, meshes, proof.primitive.mesh_index);
  if (FindMember(doc, proof.primitive.json_node, kTargets) ||
      FindMember(doc, mesh, kWeights))
    Unsupported();
  for (std::size_t node = 0; node < asset.guard_rules.size(); ++node) {
    allocation.Checkpoint();
    if (GuardPath(asset.guard_rules[node]).ends_with(kExtensionSuffix) &&
        ChildCount(doc, node))
      Unsupported();
  }
}

std::array<std::uint32_t, kTranslationComponents> Bound(
    const JsonSnapshot& doc, std::size_t accessor, std::string_view key,
    std::size_t components, bool required, bool& present,
    std::span<GltfSourceSpan> locations) {
  std::array<std::uint32_t, kTranslationComponents> bits = {};
  const auto member = FindMember(doc, accessor, key);
  present = member.has_value();
  if (!member) {
    if (required)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired, {},
           kSampleRule);
    return bits;
  }
  if (doc.Kind(*member) != JsonKind::kArray ||
      ChildCount(doc, *member) != components)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreArrayLength, {},
         kSampleRule);
  auto value = doc.FirstChild(*member);
  for (std::size_t component = 0; component < components; ++component) {
    const auto token = doc.Number(*value);
    locations[component] = {
        static_cast<std::uint64_t>(token.data() - doc.Original().data()),
        token.size()};
    bits[component] = ReadFloatBits(doc, *value);
    value = doc.NextSibling(*value);
  }
  return bits;
}

std::size_t Inventory(DirectTranslationState& proof, const Tables& tables,
                      const GltfDirectTranslationLimits& limits) {
  const auto& doc = proof.asset->document;
  const auto& allocation = proof.allocation;
  OwnedVector<std::size_t> sampler_nodes{Allocator<std::size_t>(allocation)};
  OwnedVector<std::size_t> slots{Allocator<std::size_t>(allocation)};
  const auto animations = FindMember(doc, 0, kAnimations);
  if (!animations) return 0;
  std::size_t total_keys = 0;
  std::uint64_t animation_index = 0;
  for (auto animation = doc.FirstChild(*animations); animation;
       animation = doc.NextSibling(*animation), ++animation_index) {
    allocation.Checkpoint();
    sampler_nodes.clear();
    const auto samplers = Required(doc, *animation, kSamplers);
    for (auto sampler = doc.FirstChild(samplers); sampler;
         sampler = doc.NextSibling(*sampler)) {
      allocation.Checkpoint();
      sampler_nodes.push_back(*sampler);
    }
    slots.assign(sampler_nodes.size(), kMissing);
    const auto channels = Required(doc, *animation, kChannels);
    std::uint64_t channel_index = 0;
    for (auto channel = doc.FirstChild(channels); channel;
         channel = doc.NextSibling(*channel), ++channel_index) {
      allocation.Checkpoint();
      ++proof.facts.scanned_channels;
      Limit(proof.facts.scanned_channels > limits.max_channel_references);
      MetadataLimit(proof, limits);
      const auto target = Required(doc, *channel, kTarget);
      const auto node_member = FindMember(doc, target, kNode);
      if (!node_member) continue;
      const auto node_index = ReadExactUnsigned(doc, *node_member);
      if (node_index >= proof.membership.size())
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
      const auto flags = proof.membership[Host(node_index)];
      if (!flags) continue;
      ++proof.facts.affected_channels;
      if (flags & GltfDirectTranslationProofView::kStrictAncestor)
        Unsupported();
      if (doc.String(Required(doc, target, kPath)) != kTranslation)
        Unsupported();
      if (proof.asset->nodes[Host(node_index)].transform_kind ==
          GltfTransformKind::kMatrix)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kGraphRelationship, {},
             kMembershipRule);
      const auto sampler_index = Unsigned(doc, *channel, kSampler);
      if (sampler_index >= sampler_nodes.size())
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
      const auto sampler_node = sampler_nodes[Host(sampler_index)];
      const auto interpolation_member =
          FindMember(doc, sampler_node, kInterpolation);
      const auto interpolation =
          interpolation_member ? doc.String(*interpolation_member) : kLinear;
      if (interpolation != kLinear && interpolation != kStep) Unsupported();
      auto& slot = slots[Host(sampler_index)];
      if (slot == kMissing) {
        Limit(proof.samplers.size() >= limits.max_distinct_samplers);
        DirectTranslationSampler sampler;
        sampler.animation_index = animation_index;
        sampler.sampler_index = sampler_index;
        sampler.first_channel = channel_index;
        sampler.input = tables.Dense(Unsigned(doc, sampler_node, kInput), 1);
        sampler.output = tables.Dense(Unsigned(doc, sampler_node, kOutput),
                                      kTranslationComponents);
        if (sampler.input.count != sampler.output.count)
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreArrayLength, {},
               kSampleRule);
        Limit(sampler.input.count > limits.max_keys_per_sampler);
        total_keys = Add(total_keys, Host(sampler.input.count));
        Limit(total_keys > limits.max_total_keys ||
              CheckedProduct(total_keys, kDecodedKeyBytes) >
                  limits.max_decoded_bytes);
        bool present = false;
        const auto input = tables.Accessor(sampler.input.accessor_index);
        sampler.declared_first_time =
            Bound(doc, input, kMinimum, 1, true, present,
                  std::span(&sampler.declared_first_span, 1))[0];
        sampler.declared_last_time =
            Bound(doc, input, kMaximum, 1, true, present,
                  std::span(&sampler.declared_last_span, 1))[0];
        const auto output = tables.Accessor(sampler.output.accessor_index);
        sampler.minimum =
            Bound(doc, output, kMinimum, kTranslationComponents, false,
                  sampler.has_minimum, sampler.minimum_spans);
        sampler.maximum =
            Bound(doc, output, kMaximum, kTranslationComponents, false,
                  sampler.has_maximum, sampler.maximum_spans);
        slot = proof.samplers.size();
        MetadataLimit(proof, limits, 0, 1);
        proof.samplers.push_back(sampler);
        ++proof.facts.distinct_samplers;
        MetadataLimit(proof, limits);
      }
      const auto& sampler = proof.samplers[slot];
      proof.tracks.push_back({animation_index, channel_index, sampler_index,
                              node_index, sampler.input.accessor_index,
                              sampler.output.accessor_index,
                              sampler.input.count,
                              interpolation == kLinear
                                  ? GltfDirectTranslationInterpolation::kLinear
                                  : GltfDirectTranslationInterpolation::kStep});
      MetadataLimit(proof, limits);
    }
  }
  return total_keys;
}

struct Segment {
  Segment(std::size_t sampler_index, std::size_t begin, std::size_t end,
          std::size_t partition_index)
      : sampler(sampler_index),
        first(begin),
        last(end),
        partition(partition_index) {}
  std::size_t sampler;
  std::size_t first;
  std::size_t last;
  std::size_t partition;
  std::size_t next = kMissing;
  std::uint64_t keys = 0;
  std::uint64_t bytes = 0;
  std::optional<GltfDiagnostic> diagnostic;
  std::size_t failed_key = kMissing;
  std::size_t failed_component = kMissing;
  std::exception_ptr exception;
  std::array<std::uint32_t, kTranslationComponents> minimum = {};
  std::array<std::uint32_t, kTranslationComponents> maximum = {};
  std::uint32_t first_time = 0;
  std::uint32_t last_time = 0;
  bool has_values = false;
};
GltfDiagnostic SampleDiagnostic(const DirectTranslationAccessor& source,
                                std::size_t key, std::size_t component) {
  const auto offset =
      Add(Host(source.resource_offset),
          CheckedProduct(Add(CheckedProduct(key, source.components), component),
                         kFloatBytes));
  return {GltfStatus::kMalformed,
          GltfDiagnosticCode::kBinaryValue,
          {},
          GltfResourceKind::kBuffer,
          source.buffer_index,
          GltfSourceSpan{offset, kFloatBytes},
          kSampleRule};
}
std::uint32_t Load(const DirectTranslationAccessor& source, std::size_t key,
                   std::size_t component, Segment& actual) {
  const auto offset = CheckedProduct(
      Add(CheckedProduct(key, source.components), component), kFloatBytes);
  std::uint32_t bits = 0;
  for (std::size_t byte = 0; byte < kFloatBytes; ++byte)
    bits |= std::uint32_t{std::to_integer<std::uint8_t>(
                source.bytes[offset + byte])}
            << (byte * kByteBits);
  actual.bytes += kFloatBytes;
  return bits;
}
void RecordFailure(Segment& segment, std::size_t key, std::size_t component,
                   const DirectTranslationAccessor& source) {
  if (segment.diagnostic) return;
  segment.failed_key = key;
  segment.failed_component = component;
  segment.diagnostic =
      SampleDiagnostic(source, key, component ? component - 1 : 0);
}
void ExamineSegment(DirectTranslationState& proof, Segment& segment) {
  const auto& sampler = proof.samplers[segment.sampler];
  const auto cadence = Host(proof.facts.work.cancellation_block_keys);
  std::uint32_t previous = 0;
  bool previous_valid = false;
  for (auto key = segment.first; key < segment.last; ++key) {
    if ((key - segment.first) % cadence == 0) proof.allocation.Checkpoint();
    ++segment.keys;
    const auto time = Load(sampler.input, key, 0, segment);
    if (key == segment.first && key) {
      previous = Load(sampler.input, key - 1, 0, segment);
      previous_valid = Finite(previous);
    }
    const bool time_valid =
        Finite(time) && (!(time & kSignBit) || !(time & kMagnitudeBits)) &&
        (!key || (previous_valid && Ordered(previous) < Ordered(time)));
    previous = time;
    previous_valid = Finite(time);
    if (!time_valid) {
      RecordFailure(segment, key, 0, sampler.input);
      continue;
    }
    if (key == segment.first) segment.first_time = time;
    segment.last_time = time;
    std::array<std::uint32_t, kTranslationComponents> values = {};
    bool valid = true;
    for (std::size_t component = 0; component < kTranslationComponents;
         ++component) {
      values[component] = Load(sampler.output, key, component, segment);
      if (!Finite(values[component])) {
        RecordFailure(segment, key, component + 1, sampler.output);
        valid = false;
        break;
      }
    }
    if (!valid) continue;
    for (std::size_t component = 0; component < kTranslationComponents;
         ++component) {
      if (!segment.has_values ||
          Ordered(values[component]) < Ordered(segment.minimum[component]))
        segment.minimum[component] = values[component];
      if (!segment.has_values ||
          Ordered(values[component]) > Ordered(segment.maximum[component]))
        segment.maximum[component] = values[component];
    }
    segment.has_values = true;
  }
}

void CheckSamples(DirectTranslationState& proof, std::size_t total_keys,
                  const NativeExecutionOptions* supplied_execution) {
  if (!total_keys) return;
  const auto& scope = proof.allocation;
  const auto partition_keys = Host(proof.facts.work.partition_keys);
  const auto partitions =
      total_keys / partition_keys + (total_keys % partition_keys != 0);
  const auto jobs = std::min(partitions, Host(proof.facts.work.max_jobs));
  proof.facts.execution.planned_jobs = jobs;
  OwnedVector<Segment> segments{Allocator<Segment>(scope)};
  OwnedVector<std::size_t> heads(jobs, kMissing, Allocator<std::size_t>(scope));
  auto tails = heads;
  std::size_t global = 0;
  for (std::size_t sampler = 0; sampler < proof.samplers.size(); ++sampler) {
    const auto count = Host(proof.samplers[sampler].input.count);
    for (std::size_t key = 0; key < count;) {
      scope.Checkpoint();
      const auto length =
          std::min(count - key, partition_keys - global % partition_keys);
      segments.push_back(
          Segment{sampler, key, key + length, global / partition_keys});
      const auto index = segments.size() - 1;
      const auto job = segments[index].partition % jobs;
      if (tails[job] == kMissing)
        heads[job] = index;
      else
        segments[tails[job]].next = index;
      tails[job] = index;
      key += length;
      global += length;
    }
  }
  struct Job {
    DirectTranslationState& proof;
    OwnedVector<Segment>& segments;
    const OwnedVector<std::size_t>& heads;
  } work{proof, segments, heads};
  auto invoke = [](void* opaque, std::size_t ordinal) noexcept {
    auto& job = *static_cast<Job*>(opaque);
    for (auto index = job.heads[ordinal]; index != kMissing;
         index = job.segments[index].next) {
      auto& segment = job.segments[index];
      try {
        ExamineSegment(job.proof, segment);
      } catch (const AdmissionFailure& failure) {
        if (failure.Code() == AdmissionCode::kInvalidObject) {
          segment.exception = std::current_exception();
        } else {
          const bool canceled = failure.Code() == AdmissionCode::kCanceled;
          segment.diagnostic =
              GltfDiagnostic{canceled ? GltfStatus::kCanceled
                             : failure.Code() == AdmissionCode::kBudgetExceeded
                                 ? GltfStatus::kBudgetExceeded
                                 : GltfStatus::kUnsupportedProfile,
                             canceled ? GltfDiagnosticCode::kCanceled
                             : failure.Code() == AdmissionCode::kBudgetExceeded
                                 ? GltfDiagnosticCode::kPayloadBudget
                                 : GltfDiagnosticCode::kLimitExceeded,
                             {},
                             {},
                             {},
                             {},
                             kLimitRule};
        }
      } catch (...) {
        segment.exception = std::current_exception();
      }
    }
  };
  const bool eligible = total_keys >= proof.facts.work.parallel_min_keys &&
                        jobs >= kMinimumWorkers;
  auto lease =
      geometry::TryReserveWorkers(proof.asset->context, eligible ? jobs : 0);
  OperationWorkerStats actual;
  std::exception_ptr launch_failure;
  if (lease.Count() >= kMinimumWorkers) {
    const NativeExecutionOptions default_execution;
    try {
      RunOperationJobs(
          scope, lease, jobs, invoke, &work,
          supplied_execution ? *supplied_execution : default_execution, actual);
    } catch (...) {
      launch_failure = std::current_exception();
    }
  } else {
    for (std::size_t job = 0; job < jobs; ++job) {
      ++proof.facts.execution.caller_jobs;
      invoke(&work, job);
    }
  }
  proof.facts.execution.launched_workers = actual.launched;
  proof.facts.execution.joined_workers = actual.joined;
  for (const auto& segment : segments) {
    proof.facts.keys_checked += segment.keys;
    proof.facts.decoded_bytes_checked += segment.bytes;
  }
  if (launch_failure) std::rethrow_exception(launch_failure);
  for (const auto& segment : segments)
    if (segment.exception) std::rethrow_exception(segment.exception);
  // Interruption is an actual schedule outcome, not an earliest-data-error
  // certificate. Every planned callback has joined before any selection.
  for (const auto& segment : segments)
    if (segment.diagnostic &&
        segment.diagnostic->status != GltfStatus::kMalformed)
      throw GltfFailure(*segment.diagnostic);
  for (const auto& segment : segments)
    if (segment.diagnostic) throw GltfFailure(*segment.diagnostic);
  // Ordered segments are (animation, earliest referring channel, key) because
  // samplers are admitted at their first affected channel, never by hash order.
  OwnedVector<std::array<std::uint32_t, kTranslationComponents>> minima{
      Allocator<std::array<std::uint32_t, kTranslationComponents>>(scope)};
  auto maxima = minima;
  minima.resize(proof.samplers.size());
  maxima.resize(proof.samplers.size());
  for (const auto& segment : segments) {
    auto& sampler = proof.samplers[segment.sampler];
    if (!segment.first) {
      sampler.first_time = segment.first_time;
      minima[segment.sampler] = segment.minimum;
      maxima[segment.sampler] = segment.maximum;
    }
    sampler.last_time = segment.last_time;
    for (std::size_t component = 0; component < kTranslationComponents;
         ++component) {
      if (Ordered(segment.minimum[component]) <
          Ordered(minima[segment.sampler][component]))
        minima[segment.sampler][component] = segment.minimum[component];
      if (Ordered(segment.maximum[component]) >
          Ordered(maxima[segment.sampler][component]))
        maxima[segment.sampler][component] = segment.maximum[component];
    }
  }
  for (std::size_t index = 0; index < proof.samplers.size(); ++index) {
    scope.Checkpoint();
    auto& sampler = proof.samplers[index];
    if (!EqualBits(sampler.first_time, sampler.declared_first_time))
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue,
           sampler.declared_first_span, kSampleRule);
    if (!EqualBits(sampler.last_time, sampler.declared_last_time))
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue,
           sampler.declared_last_span, kSampleRule);
    for (std::size_t component = 0; component < kTranslationComponents;
         ++component) {
      if (sampler.has_minimum &&
          !EqualBits(sampler.minimum[component], minima[index][component]))
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue,
             sampler.minimum_spans[component], kSampleRule);
      if (sampler.has_maximum &&
          !EqualBits(sampler.maximum[component], maxima[index][component]))
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue,
             sampler.maximum_spans[component], kSampleRule);
    }
    sampler.minimum = minima[index];
    sampler.maximum = maxima[index];
  }
}
}  // namespace

DirectTranslationState::DirectTranslationState(
    std::shared_ptr<const AssetState> source, PrimitiveRecord selected,
    const AllocationContext& scope)
    : asset(std::move(source)),
      primitive(selected),
      allocation(scope),
      users(Allocator<std::uint64_t>(scope)),
      membership(Allocator<std::uint8_t>(scope)),
      tracks(Allocator<GltfDirectTranslationTrackView>(scope)),
      samplers(Allocator<DirectTranslationSampler>(scope)) {}

void ValidateDirectTranslationConfiguration(
    const GltfDirectTranslationLimits& limits,
    const GltfDirectTranslationWorkPolicy& work) {
  for (const auto value :
       {limits.max_node_rows, limits.max_mesh_users,
        limits.max_channel_references, limits.max_distinct_samplers,
        limits.max_keys_per_sampler, limits.max_total_keys,
        limits.max_relationship_steps, limits.max_metadata_bytes,
        limits.max_decoded_bytes, limits.max_new_requested_bytes,
        work.parallel_min_keys, work.partition_keys,
        work.cancellation_block_keys, work.max_jobs}) {
    if (!value)
      Fail(GltfStatus::kInvalidLimits, GltfDiagnosticCode::kZeroLimit, {},
           kLimitRule);
    (void)Host(value);
  }
}
DirectTranslationResult ValidateDirectTranslation(
    std::shared_ptr<const AssetState> asset, const PrimitiveRecord& primitive,
    const GltfDirectTranslationLimits& limits, const AllocationContext& root,
    std::size_t owner_allowance, const NativeExecutionOptions* execution) {
  DirectTranslationResult result{{}, {}, AcceptedDiagnostic()};
  std::optional<AllocationContext> scope;
  std::shared_ptr<DirectTranslationState> proof;
  const auto effective =
      std::min(owner_allowance, Host(limits.max_new_requested_bytes));
  try {
    scope.emplace(root.WithRequestLimit(effective));
    result.observation.emplace();
    result.observation->effective_request_cap = effective;
    result.observation->work = asset->work_policy.direct_translation;
    proof = std::allocate_shared<DirectTranslationState>(
        Allocator<DirectTranslationState>(*scope), std::move(asset), primitive,
        *scope);
    proof->facts = *result.observation;
    Membership(*proof, limits);
    Tables tables(*proof->asset, *scope);
    tables.InventoryRoles();
    const auto keys = Inventory(*proof, tables, limits);
    CheckSamples(*proof, keys, execution);
    scope->Checkpoint();
    proof->facts.complete = true;
    proof->facts.new_requested_bytes = scope->OperationActiveBytes();
    result.observation = proof->facts;
    result.proof = std::move(proof);
    return result;
  } catch (const GltfFailure& failure) {
    result.diagnostic = failure.Diagnostic();
  } catch (const AdmissionFailure& failure) {
    if (failure.Code() == AdmissionCode::kInvalidObject) throw;
    const bool canceled = failure.Code() == AdmissionCode::kCanceled;
    const bool budget = failure.Code() == AdmissionCode::kBudgetExceeded;
    result.diagnostic = {canceled ? GltfStatus::kCanceled
                         : budget ? GltfStatus::kBudgetExceeded
                                  : GltfStatus::kUnsupportedProfile,
                         canceled ? GltfDiagnosticCode::kCanceled
                         : budget ? GltfDiagnosticCode::kPayloadBudget
                                  : GltfDiagnosticCode::kLimitExceeded,
                         {},
                         {},
                         {},
                         {},
                         kLimitRule};
  }
  if (result.observation) {
    if (proof) result.observation = proof->facts;
    result.observation->status = result.diagnostic.status;
    result.observation->diagnostic = result.diagnostic;
    result.observation->complete = false;
    result.observation->new_requested_bytes = scope->OperationActiveBytes();
  }
  return result;
}
}  // namespace meshvale::interchange::gltf_detail
