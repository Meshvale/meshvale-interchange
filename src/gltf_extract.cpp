// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

#include "gltf_accessor.h"
#include "gltf_asset_state.h"
#include "gltf_direct_translation.h"
#include "gltf_metadata.h"
#include "gltf_operation_workers.h"
#include "gltf_workflow_test_internal.h"

namespace meshvale::interchange::gltf_detail {
namespace {
// Khronos glTF 2.0 core identifiers and roles, not parser enum ordinals.
constexpr std::uint64_t kByteComponent = 5120;
constexpr std::uint64_t kUnsignedByteComponent = 5121;
constexpr std::uint64_t kShortComponent = 5122;
constexpr std::uint64_t kUnsignedShortComponent = 5123;
constexpr std::uint64_t kUnsignedIntComponent = 5125;
constexpr std::uint64_t kFloatComponent = 5126;
constexpr std::uint64_t kVertexTarget = 34962;
constexpr std::uint64_t kIndexTarget = 34963;
constexpr std::uint64_t kTriangles = 4;
constexpr std::uint64_t kTriangleStrip = 5;
constexpr std::uint64_t kTriangleFan = 6;
constexpr std::size_t kTriangleCorners = 3;
constexpr std::size_t kPositionComponents = 3;
constexpr std::size_t kTangentComponents = 4;
// Processing-profile allowance for binary32 authored unit directions: a
// power-of-two bound reproducible in binary64, comfortably above the rounding
// error of three binary32 components. Values are preserved, never normalized.
constexpr double kMaximumSquaredUnitDirectionError = 0x1p-16;
constexpr std::uint32_t kSelectionRule = 310;
constexpr std::uint32_t kRoleRule = 311;
constexpr std::uint32_t kBoundsRule = 312;
constexpr std::uint32_t kTopologyRule = 313;
constexpr std::uint32_t kLimitRule = 314;
constexpr std::string_view kAccessors = "accessors";
constexpr std::string_view kBufferViews = "bufferViews";
constexpr std::string_view kBufferView = "bufferView";
constexpr std::string_view kBuffer = "buffer";
constexpr std::string_view kByteOffset = "byteOffset";
constexpr std::string_view kByteLength = "byteLength";
constexpr std::string_view kByteStride = "byteStride";
constexpr std::string_view kTarget = "target";
constexpr std::string_view kComponentType = "componentType";
constexpr std::string_view kType = "type";
constexpr std::string_view kCount = "count";
constexpr std::string_view kNormalized = "normalized";
constexpr std::string_view kSparse = "sparse";
constexpr std::string_view kIndices = "indices";
constexpr std::string_view kValues = "values";
constexpr std::string_view kMinimum = "min";
constexpr std::string_view kMaximum = "max";
constexpr std::string_view kAttributes = "attributes";
constexpr std::string_view kMeshes = "meshes";
constexpr std::string_view kPrimitives = "primitives";
constexpr std::string_view kTargets = "targets";
constexpr std::string_view kSkins = "skins";
constexpr std::string_view kInverseBindMatrices = "inverseBindMatrices";
constexpr std::string_view kAnimations = "animations";
constexpr std::string_view kSamplers = "samplers";
constexpr std::string_view kInput = "input";
constexpr std::string_view kOutput = "output";
constexpr std::string_view kImages = "images";
constexpr std::string_view kMode = "mode";
constexpr std::string_view kMaterial = "material";
constexpr std::string_view kPosition = "POSITION";
constexpr std::string_view kNormal = "NORMAL";
constexpr std::string_view kTangent = "TANGENT";
constexpr std::string_view kTexcoordPrefix = "TEXCOORD_";
constexpr std::string_view kColorPrefix = "COLOR_";
constexpr std::string_view kJointsPrefix = "JOINTS_";
constexpr std::string_view kWeightsPrefix = "WEIGHTS_";
constexpr std::string_view kScalarShape = "SCALAR";
constexpr std::string_view kVec2Shape = "VEC2";
constexpr std::string_view kVec3Shape = "VEC3";
constexpr std::string_view kVec4Shape = "VEC4";
constexpr std::string_view kMat2Shape = "MAT2";
constexpr std::string_view kMat3Shape = "MAT3";
constexpr std::string_view kMat4Shape = "MAT4";

std::size_t Host(std::uint64_t number) {
  if (number > std::numeric_limits<std::size_t>::max())
    Fail(GltfStatus::kUnsupportedProfile,
         GltfDiagnosticCode::kUnsupportedNumberRepresentation, {}, kLimitRule);
  return static_cast<std::size_t>(number);
}
std::size_t Add(std::size_t first, std::size_t second) {
  if (second > std::numeric_limits<std::size_t>::max() - first)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow, {},
         kLimitRule);
  return first + second;
}
std::size_t Product(std::size_t first, std::size_t second) {
  if (second && first > std::numeric_limits<std::size_t>::max() / second)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow, {},
         kLimitRule);
  return first * second;
}
std::size_t Required(const JsonSnapshot& document, std::size_t object,
                     std::string_view name) {
  auto member = FindMember(document, object, name);
  if (!member)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired);
  return *member;
}
std::uint64_t OptionalUnsigned(const JsonSnapshot& document, std::size_t object,
                               std::string_view name,
                               std::uint64_t absent = 0) {
  const auto member = FindMember(document, object, name);
  return member ? ReadExactUnsigned(document, *member) : absent;
}
std::size_t TableRow(const JsonSnapshot& document, std::string_view table,
                     std::uint64_t index) {
  auto row = ChildAt(document, Required(document, 0, table), index);
  if (!row) Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
  return *row;
}
GltfComponentType Component(std::uint64_t identifier) {
  switch (identifier) {
    case kByteComponent:
      return GltfComponentType::kInt8;
    case kUnsignedByteComponent:
      return GltfComponentType::kUint8;
    case kShortComponent:
      return GltfComponentType::kInt16;
    case kUnsignedShortComponent:
      return GltfComponentType::kUint16;
    case kUnsignedIntComponent:
      return GltfComponentType::kUint32;
    case kFloatComponent:
      return GltfComponentType::kFloat32;
    default:
      Fail(GltfStatus::kUnsupportedProfile,
           GltfDiagnosticCode::kUnsupportedEnum);
  }
}
GltfAccessorShape Shape(std::string_view name) {
  if (name == kScalarShape) return GltfAccessorShape::kScalar;
  if (name == kVec2Shape) return GltfAccessorShape::kVec2;
  if (name == kVec3Shape) return GltfAccessorShape::kVec3;
  if (name == kVec4Shape) return GltfAccessorShape::kVec4;
  if (name == kMat2Shape) return GltfAccessorShape::kMat2;
  if (name == kMat3Shape) return GltfAccessorShape::kMat3;
  if (name == kMat4Shape) return GltfAccessorShape::kMat4;
  Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kUnsupportedEnum);
}
AccessorByteView ResolveView(const AssetState& asset, std::uint64_t index) {
  const auto& doc = asset.document;
  const auto row = TableRow(doc, kBufferViews, index);
  const auto resource_index =
      ReadExactUnsigned(doc, Required(doc, row, kBuffer));
  const GltfResourceView* resource = nullptr;
  for (const auto& entry : asset.resources)
    if (entry.kind == GltfResourceKind::kBuffer &&
        entry.table_index == resource_index) {
      resource = &entry;
      break;
    }
  if (!resource)
    Fail(GltfStatus::kResourceBindingMismatch,
         GltfDiagnosticCode::kMissingResource);
  const auto offset = OptionalUnsigned(doc, row, kByteOffset);
  const auto size = ReadExactUnsigned(doc, Required(doc, row, kByteLength));
  if (offset > resource->logical_bytes ||
      size > resource->logical_bytes - offset ||
      offset > resource->bytes.size() || size > resource->bytes.size() - offset)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kResourceExtent);
  auto target = ViewTarget::kAbsent;
  if (const auto member = FindMember(doc, row, kTarget)) {
    const auto value = ReadExactUnsigned(doc, *member);
    if (value == kVertexTarget)
      target = ViewTarget::kVertex;
    else if (value == kIndexTarget)
      target = ViewTarget::kIndices;
    else
      Fail(GltfStatus::kUnsupportedProfile,
           GltfDiagnosticCode::kUnsupportedEnum);
  }
  return {index,
          resource_index,
          offset,
          resource->bytes.subspan(Host(offset), Host(size)),
          Host(OptionalUnsigned(doc, row, kByteStride)),
          target};
}
AccessorSource ResolveAccessor(const AssetState& asset, std::uint64_t index,
                               AccessorRole role) {
  const auto& doc = asset.document;
  const auto row = TableRow(doc, kAccessors, index);
  AccessorSource source;
  source.index = index;
  source.component_type =
      Component(ReadExactUnsigned(doc, Required(doc, row, kComponentType)));
  source.shape = Shape(doc.String(Required(doc, row, kType)));
  source.count = Host(ReadExactUnsigned(doc, Required(doc, row, kCount)));
  source.role = role;
  source.base_offset = Host(OptionalUnsigned(doc, row, kByteOffset));
  if (const auto normalized = FindMember(doc, row, kNormalized))
    source.normalized = doc.Boolean(*normalized);
  if (const auto view = FindMember(doc, row, kBufferView))
    source.base = ResolveView(asset, ReadExactUnsigned(doc, *view));
  if (const auto sparse = FindMember(doc, row, kSparse)) {
    const auto indices = Required(doc, *sparse, kIndices);
    const auto values = Required(doc, *sparse, kValues);
    source.sparse = AccessorSparseSource{
        Host(ReadExactUnsigned(doc, Required(doc, *sparse, kCount))),
        ResolveView(
            asset, ReadExactUnsigned(doc, Required(doc, indices, kBufferView))),
        Host(OptionalUnsigned(doc, indices, kByteOffset)),
        Component(
            ReadExactUnsigned(doc, Required(doc, indices, kComponentType))),
        ResolveView(asset,
                    ReadExactUnsigned(doc, Required(doc, values, kBufferView))),
        Host(OptionalUnsigned(doc, values, kByteOffset))};
  }
  return source;
}
std::optional<std::uint64_t> SetIndex(std::string_view semantic,
                                      std::string_view prefix) {
  if (!semantic.starts_with(prefix)) return {};
  const auto suffix = semantic.substr(prefix.size());
  if (suffix.empty() || (suffix.size() > 1 && suffix.front() == '0'))
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {}, kRoleRule);
  std::uint64_t index = 0;
  constexpr std::uint64_t kDecimalRadix = 10;
  for (const auto character : suffix) {
    if (character < '0' || character > '9')
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {},
           kRoleRule);
    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (index >
        (std::numeric_limits<std::uint64_t>::max() - digit) / kDecimalRadix)
      Fail(GltfStatus::kUnsupportedProfile,
           GltfDiagnosticCode::kUnsupportedNumberRepresentation);
    index = index * kDecimalRadix + digit;
  }
  return index;
}
std::optional<std::uint64_t> AdmitRole(const AccessorSource& source,
                                       std::string_view semantic) {
  const bool floating = source.component_type == GltfComponentType::kFloat32 &&
                        !source.normalized;
  const bool normalized_unsigned =
      source.normalized &&
      (source.component_type == GltfComponentType::kUint8 ||
       source.component_type == GltfComponentType::kUint16);
  bool accepted = false;
  std::optional<std::uint64_t> set;
  if (semantic == kPosition || semantic == kNormal)
    accepted = floating && source.shape == GltfAccessorShape::kVec3;
  else if (semantic == kTangent)
    accepted = floating && source.shape == GltfAccessorShape::kVec4;
  else if ((set = SetIndex(semantic, kTexcoordPrefix)))
    accepted = (floating || normalized_unsigned) &&
               source.shape == GltfAccessorShape::kVec2;
  else if ((set = SetIndex(semantic, kColorPrefix)))
    accepted = (floating || normalized_unsigned) &&
               (source.shape == GltfAccessorShape::kVec3 ||
                source.shape == GltfAccessorShape::kVec4);
  else if (semantic.starts_with(kJointsPrefix) ||
           semantic.starts_with(kWeightsPrefix))
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kIndexRole, {},
         kRoleRule);
  else if (semantic.starts_with('_')) {
    if (source.shape == GltfAccessorShape::kMat2 ||
        source.shape == GltfAccessorShape::kMat3 ||
        source.shape == GltfAccessorShape::kMat4)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kIndexRole, {},
           kRoleRule);
    accepted = source.component_type != GltfComponentType::kUint32 &&
               source.shape != GltfAccessorShape::kMat2 &&
               source.shape != GltfAccessorShape::kMat3 &&
               source.shape != GltfAccessorShape::kMat4 &&
               !(source.component_type == GltfComponentType::kFloat32 &&
                 source.normalized);
  } else
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kIndexRole, {},
         kRoleRule);
  if (!accepted)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {}, kRoleRule);
  return set;
}
void ValidateSets(const JsonSnapshot& doc, std::size_t attributes,
                  std::string_view prefix) {
  std::uint64_t count = 0, maximum = 0;
  for (auto child = doc.FirstChild(attributes); child;
       child = doc.NextSibling(*child)) {
    if (const auto set = SetIndex(doc.Key(*child), prefix)) {
      ++count;
      maximum = std::max(maximum, *set);
    }
  }
  if (count && maximum != count - 1)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {}, kRoleRule);
}
enum Usage : unsigned {
  kNoUse = 0,
  kVertexUse = 1,
  kIndexUse = 2,
  kGenericUse = 4,
  kSparseSelectorUse = 8,
  kSparseValueUse = 16,
  kImageUse = 32
};
struct ViewUsage {
  unsigned roles = kNoUse;
  std::size_t vertex_accessors = 0;
};
struct SemanticUsage {
  std::uint64_t accessor;
  std::string_view semantic;
};
struct RoleInventory {
  explicit RoleInventory(const AllocationContext& allocation)
      : accessor_roles(Allocator<unsigned>(allocation)),
        view_roles(Allocator<ViewUsage>(allocation)),
        semantics(Allocator<SemanticUsage>(allocation)) {}
  ChargedValues<unsigned> accessor_roles;
  ChargedValues<ViewUsage> view_roles;
  ChargedValues<SemanticUsage> semantics;
};
RoleInventory BuildRoleInventory(const AssetState& asset,
                                 const AllocationContext& allocation) {
  const auto& doc = asset.document;
  RoleInventory inventory(allocation);
  inventory.accessor_roles.resize(
      Host(ChildCount(doc, Required(doc, 0, kAccessors))));
  if (const auto views = FindMember(doc, 0, kBufferViews))
    inventory.view_roles.resize(Host(ChildCount(doc, *views)));
  auto mark_accessor = [&](std::size_t node, unsigned role) {
    const auto index = ReadExactUnsigned(doc, node);
    if (index >= inventory.accessor_roles.size())
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
    inventory.accessor_roles[Host(index)] |= role;
    return index;
  };
  auto mark_attributes = [&](std::size_t attributes) {
    for (auto child = doc.FirstChild(attributes); child;
         child = doc.NextSibling(*child)) {
      allocation.Checkpoint();
      const auto index = mark_accessor(*child, kVertexUse);
      inventory.semantics.push_back({index, doc.Key(*child)});
    }
  };
  if (const auto meshes = FindMember(doc, 0, kMeshes))
    for (auto mesh = doc.FirstChild(*meshes); mesh;
         mesh = doc.NextSibling(*mesh)) {
      const auto primitives = Required(doc, *mesh, kPrimitives);
      for (auto primitive = doc.FirstChild(primitives); primitive;
           primitive = doc.NextSibling(*primitive)) {
        allocation.Checkpoint();
        if (const auto indices = FindMember(doc, *primitive, kIndices))
          (void)mark_accessor(*indices, kIndexUse);
        if (const auto attributes = FindMember(doc, *primitive, kAttributes))
          mark_attributes(*attributes);
        if (const auto targets = FindMember(doc, *primitive, kTargets))
          for (auto target = doc.FirstChild(*targets); target;
               target = doc.NextSibling(*target))
            mark_attributes(*target);
      }
    }
  if (const auto skins = FindMember(doc, 0, kSkins))
    for (auto skin = doc.FirstChild(*skins); skin;
         skin = doc.NextSibling(*skin)) {
      allocation.Checkpoint();
      if (const auto matrices = FindMember(doc, *skin, kInverseBindMatrices))
        (void)mark_accessor(*matrices, kGenericUse);
    }
  if (const auto animations = FindMember(doc, 0, kAnimations))
    for (auto animation = doc.FirstChild(*animations); animation;
         animation = doc.NextSibling(*animation))
      if (const auto samplers = FindMember(doc, *animation, kSamplers))
        for (auto sampler = doc.FirstChild(*samplers); sampler;
             sampler = doc.NextSibling(*sampler)) {
          allocation.Checkpoint();
          for (const auto key : {kInput, kOutput})
            if (const auto accessor = FindMember(doc, *sampler, key))
              (void)mark_accessor(*accessor, kGenericUse);
        }
  auto view_usage = [&](std::size_t node) -> ViewUsage& {
    const auto index = ReadExactUnsigned(doc, node);
    if (index >= inventory.view_roles.size())
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
    return inventory.view_roles[Host(index)];
  };
  std::size_t index = 0;
  const auto accessors = Required(doc, 0, kAccessors);
  for (auto accessor = doc.FirstChild(accessors); accessor;
       accessor = doc.NextSibling(*accessor), ++index) {
    allocation.Checkpoint();
    if (const auto view = FindMember(doc, *accessor, kBufferView)) {
      auto& usage = view_usage(*view);
      usage.roles |= inventory.accessor_roles[index];
      if (inventory.accessor_roles[index] & kVertexUse)
        ++usage.vertex_accessors;
    }
    if (const auto sparse = FindMember(doc, *accessor, kSparse)) {
      const auto indices = Required(doc, *sparse, kIndices);
      const auto values = Required(doc, *sparse, kValues);
      view_usage(Required(doc, indices, kBufferView)).roles |=
          kSparseSelectorUse;
      view_usage(Required(doc, values, kBufferView)).roles |= kSparseValueUse;
    }
  }
  if (const auto images = FindMember(doc, 0, kImages))
    for (auto image = doc.FirstChild(*images); image;
         image = doc.NextSibling(*image)) {
      allocation.Checkpoint();
      if (const auto view = FindMember(doc, *image, kBufferView))
        view_usage(*view).roles |= kImageUse;
    }
  return inventory;
}
void ValidateSharedViewUses(const RoleInventory& inventory,
                            const AccessorSource& selected,
                            const AllocationContext& allocation) {
  const auto own_role =
      selected.role == AccessorRole::kIndices ? kIndexUse : kVertexUse;
  if (inventory.accessor_roles[Host(selected.index)] != own_role)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {}, kRoleRule);
  if (selected.role == AccessorRole::kVertex)
    for (const auto& usage : inventory.semantics) {
      allocation.Checkpoint();
      if (usage.accessor == selected.index)
        (void)AdmitRole(selected, usage.semantic);
    }
  if (selected.base) {
    const auto& usage = inventory.view_roles[Host(selected.base->index)];
    if (usage.roles != own_role ||
        (usage.vertex_accessors > 1 && !selected.base->stride))
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout, {},
           kRoleRule);
  }
  if (selected.sparse) {
    const auto selectors =
        inventory.view_roles[Host(selected.sparse->indices.index)].roles;
    const auto replacements =
        inventory.view_roles[Host(selected.sparse->values.index)].roles;
    constexpr unsigned kSparseRoles = kSparseSelectorUse | kSparseValueUse;
    if ((selectors & ~kSparseRoles) || (replacements & ~kSparseRoles))
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout, {},
           kRoleRule);
  }
}
struct ChannelStorage {
  ChannelStorage(const AllocationContext& allocation, AccessorSource descriptor,
                 std::string_view semantic_name)
      : source(std::move(descriptor)),
        semantic(semantic_name),
        values(allocation) {}
  AccessorSource source;
  std::string_view semantic;
  std::optional<std::uint64_t> set;
  DecodedAccessor values;
  std::optional<OwnedRawValues> minimum;
  std::optional<OwnedRawValues> maximum;
};
OwnedRawValues ReadBound(const JsonSnapshot& doc, std::size_t bound,
                         const AccessorSource& source,
                         const AllocationContext& allocation) {
  const auto layout =
      DescribeAccessorLayout(source.component_type, source.shape);
  const auto components = layout.rows * layout.columns;
  if (doc.Kind(bound) != JsonKind::kArray ||
      ChildCount(doc, bound) != components)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreArrayLength, {},
         kBoundsRule);
  auto values = MakeRawValues(source.component_type, allocation, components);
  std::visit(
      [&](auto& array) {
        using Value = typename std::remove_cvref_t<decltype(array)>::value_type;
        auto child = doc.FirstChild(bound);
        for (auto& value : array) {
          if constexpr (std::is_same_v<Value, float>) {
            value = std::bit_cast<float>(ReadFloatBits(doc, *child));
          } else if constexpr (std::is_signed_v<Value>) {
            const auto exact = ReadExactSigned(doc, *child);
            if (exact < std::numeric_limits<Value>::min() ||
                exact > std::numeric_limits<Value>::max())
              Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange, {},
                   kBoundsRule);
            value = static_cast<Value>(exact);
          } else {
            const auto exact = ReadExactUnsigned(doc, *child);
            if (exact > std::numeric_limits<Value>::max())
              Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange, {},
                   kBoundsRule);
            value = static_cast<Value>(exact);
          }
          child = doc.NextSibling(*child);
        }
      },
      values);
  return values;
}
void ValidateBounds(ChannelStorage& channel, const JsonSnapshot& doc,
                    const AllocationContext& allocation) {
  const auto node = TableRow(doc, kAccessors, channel.source.index);
  const auto minimum = FindMember(doc, node, kMinimum);
  const auto maximum = FindMember(doc, node, kMaximum);
  if (channel.semantic == kPosition && (!minimum || !maximum))
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired, {},
         kBoundsRule);
  if (minimum)
    channel.minimum = ReadBound(doc, *minimum, channel.source, allocation);
  if (maximum)
    channel.maximum = ReadBound(doc, *maximum, channel.source, allocation);
  const auto layout = DescribeAccessorLayout(channel.source.component_type,
                                             channel.source.shape);
  const auto components = layout.rows * layout.columns;
  std::visit(
      [&](const auto& raw) {
        using Array = std::remove_cvref_t<decltype(raw)>;
        for (std::size_t component = 0; component < components; ++component) {
          auto low = raw[component], high = low;
          for (std::size_t row = 1; row < channel.source.count; ++row) {
            allocation.Checkpoint();
            const auto value = raw[row * components + component];
            low = std::min(low, value);
            high = std::max(high, value);
          }
          if ((channel.minimum &&
               std::get<Array>(*channel.minimum)[component] != low) ||
              (channel.maximum &&
               std::get<Array>(*channel.maximum)[component] != high))
            Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue, {},
                 kBoundsRule);
        }
      },
      channel.values.raw);
}
GltfChannelView ChannelView(const ChannelStorage& channel) {
  const auto& source = channel.source;
  std::optional<GltfSparseProvenance> sparse;
  if (source.sparse)
    sparse = {source.sparse->indices.index, source.sparse->indices_offset,
              source.sparse->index_type, source.sparse->values.index,
              source.sparse->values_offset};
  return {
      channel.semantic,
      channel.set,
      source.index,
      source.component_type,
      source.shape,
      source.normalized,
      source.count,
      source.base ? std::optional(source.base->index) : std::nullopt,
      source.base_offset,
      channel.values.base_stride,
      channel.values.packed_sparse_step,
      sparse,
      channel.values.sparse_destinations,
      channel.minimum ? std::optional(RawView(*channel.minimum)) : std::nullopt,
      channel.maximum ? std::optional(RawView(*channel.maximum)) : std::nullopt,
      RawView(channel.values.raw),
      channel.values.normalized};
}
void DecodeChannel(ChannelStorage& channel, const AssetState& asset,
                   const RoleInventory& inventory,
                   const GltfExtractionRequest& request,
                   const GltfExtractionLimits& limits,
                   const AllocationContext& allocation, std::stop_token stop,
                   const NativeExecutionOptions* execution) {
  ValidateSharedViewUses(inventory, channel.source, allocation);
  auto decoded = DecodeAccessor(
      channel.source, request.values == GltfValueRequest::kRawAndNormalized,
      limits, asset.work_policy, asset.context, allocation, stop, nullptr,
      nullptr, execution);
  if (!decoded.values) throw GltfFailure(decoded.diagnostic);
  channel.values = std::move(*decoded.values);
  ValidateBounds(channel, asset.document, allocation);
  if (channel.semantic == kNormal || channel.semantic == kTangent) {
    const auto& raw = std::get<ChargedValues<float>>(channel.values.raw);
    const auto components =
        channel.semantic == kNormal ? kPositionComponents : kTangentComponents;
    for (std::size_t row = 0; row < channel.source.count; ++row) {
      allocation.Checkpoint();
      double square = 0;
      for (std::size_t component = 0; component < kPositionComponents;
           ++component) {
        const double value = raw[row * components + component];
        square += value * value;
      }
      if (std::abs(square - 1.0) > kMaximumSquaredUnitDirectionError)
        Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kBinaryValue,
             {}, kRoleRule);
    }
  }
  if (channel.semantic.starts_with(kColorPrefix) &&
      channel.source.component_type == GltfComponentType::kFloat32) {
    const auto& raw = std::get<ChargedValues<float>>(channel.values.raw);
    for (const auto value : raw) {
      allocation.Checkpoint();
      if (value < 0.0F || value > 1.0F)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue, {},
             kRoleRule);
    }
  }
  if (channel.semantic == kTangent) {
    const auto& raw = std::get<ChargedValues<float>>(channel.values.raw);
    for (std::size_t row = 0; row < channel.source.count; ++row) {
      allocation.Checkpoint();
      const auto sign = raw[row * kTangentComponents + kPositionComponents];
      if (sign != -1.0F && sign != 1.0F)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue, {},
             kRoleRule);
    }
  }
}
}  // namespace
struct ExtractionState {
  ExtractionState(std::shared_ptr<const AssetState> source,
                  const PrimitiveRecord& source_primitive,
                  const AllocationContext& operation)
      : asset(std::move(source)),
        primitive(source_primitive),
        allocation(operation),
        channel_storage(Allocator<ChannelStorage>(operation)),
        channels(Allocator<GltfChannelView>(operation)),
        triangles(Allocator<std::uint32_t>(operation)),
        corners(Allocator<GltfSourceCorner>(operation)) {}
  std::shared_ptr<const AssetState> asset;
  PrimitiveRecord primitive;
  AllocationContext allocation;
  std::shared_ptr<const DirectTranslationState> translation;
  std::uint64_t mode = 0;
  std::optional<std::uint64_t> material;
  ChargedValues<ChannelStorage> channel_storage;
  ChargedValues<GltfChannelView> channels;
  std::optional<ChannelStorage> indices;
  ChargedValues<std::uint32_t> triangles;
  ChargedValues<GltfSourceCorner> corners;
};
GltfPrimitiveExtraction Access::MakeExtraction(
    std::shared_ptr<const ExtractionState> state) {
  return GltfPrimitiveExtraction(std::move(state));
}
const DirectTranslationState* Access::ExtractionTranslation(
    const GltfPrimitiveExtraction& extraction) noexcept {
  return extraction.owner_->translation.get();
}
const geometry::ExecutionContext& Access::ExtractionContext(
    const GltfPrimitiveExtraction& extraction) noexcept {
  return extraction.owner_->asset->context;
}
GltfAsset Access::ExtractionAsset(const GltfPrimitiveExtraction& extraction) {
  return MakeAsset(extraction.owner_->asset);
}
GltfPrimitiveHandle Access::ExtractionPrimitive(
    const GltfPrimitiveExtraction& extraction) {
  return MakePrimitive(extraction.owner_->asset, extraction.owner_->primitive);
}
}  // namespace meshvale::interchange::gltf_detail

namespace meshvale::interchange {
GltfPrimitiveExtraction::GltfPrimitiveExtraction(
    std::shared_ptr<const gltf_detail::ExtractionState> owner)
    : owner_(std::move(owner)) {}
GltfPrimitiveExtraction::GltfPrimitiveExtraction(
    const GltfPrimitiveExtraction&) = default;
GltfPrimitiveExtraction& GltfPrimitiveExtraction::operator=(
    const GltfPrimitiveExtraction&) = default;
GltfPrimitiveExtraction::~GltfPrimitiveExtraction() = default;
std::uint64_t GltfPrimitiveExtraction::NewRequestedBytes() const noexcept {
  return owner_->allocation.OperationActiveBytes() +
         (owner_->translation
              ? owner_->translation->allocation.OperationActiveBytes()
              : 0);
}
GltfProcessingProfile GltfPrimitiveExtraction::ProcessingProfile()
    const noexcept {
  return owner_->translation
             ? GltfProcessingProfile::kDirectTranslationTrianglesV1
             : GltfProcessingProfile::kStaticLocalTrianglesV1;
}
std::optional<GltfDirectTranslationProofView>
GltfPrimitiveExtraction::DirectTranslation() const noexcept {
  if (!owner_->translation) return {};
  const auto& proof = *owner_->translation;
  return GltfDirectTranslationProofView{proof.users, proof.membership,
                                        proof.tracks};
}
const GltfDirectTranslationObservation*
GltfPrimitiveExtraction::DirectTranslationFacts() const noexcept {
  return owner_->translation ? &owner_->translation->facts : nullptr;
}
std::uint64_t GltfPrimitiveExtraction::SourceMode() const noexcept {
  return owner_->mode;
}
std::span<const std::uint32_t> GltfPrimitiveExtraction::TriangleIndices()
    const noexcept {
  return owner_->triangles;
}
std::span<const GltfSourceCorner> GltfPrimitiveExtraction::SourceCorners()
    const noexcept {
  return owner_->corners;
}
std::span<const GltfChannelView> GltfPrimitiveExtraction::Channels()
    const noexcept {
  return owner_->channels;
}
std::optional<GltfChannelView> GltfPrimitiveExtraction::SourceIndices()
    const noexcept {
  return owner_->indices
             ? std::optional(gltf_detail::ChannelView(*owner_->indices))
             : std::nullopt;
}
std::optional<std::uint64_t> GltfPrimitiveExtraction::MaterialIndex()
    const noexcept {
  return owner_->material;
}

namespace {
GltfExtractionResult ExtractPrimitive(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
    const GltfExtractionRequest& request, const GltfExtractionLimits& limits,
    std::stop_token stop, gltf_detail::AllocationBackend backend,
    const gltf_detail::NativeExecutionOptions* execution) {
  using namespace gltf_detail;
  std::optional<GltfDirectTranslationObservation> translation_observation;
  try {
    if ((request.triangles != GltfTriangleRequest::kRequireTriangles &&
         request.triangles != GltfTriangleRequest::kConvertStripOrFan) ||
        (request.values != GltfValueRequest::kRaw &&
         request.values != GltfValueRequest::kRawAndNormalized))
      Fail(GltfStatus::kInvalidRequest, GltfDiagnosticCode::kInvalidRequest, {},
           kSelectionRule);
    if (request.processing_profile !=
            GltfProcessingProfile::kStaticLocalTrianglesV1 &&
        request.processing_profile !=
            GltfProcessingProfile::kDirectTranslationTrianglesV1)
      Fail(GltfStatus::kInvalidRequest, GltfDiagnosticCode::kInvalidRequest, {},
           kSelectionRule);
    for (const auto limit : {limits.max_components, limits.max_triangle_corners,
                             limits.max_output_requested_bytes}) {
      if (!limit)
        Fail(GltfStatus::kInvalidLimits, GltfDiagnosticCode::kZeroLimit);
      if (limit > std::numeric_limits<std::size_t>::max())
        Fail(GltfStatus::kInvalidLimits,
             GltfDiagnosticCode::kLimitNotRepresentable);
    }
    const auto* record = Access::PrimitiveRecordFor(asset, primitive);
    if (!record)
      Fail(GltfStatus::kInvalidSelection,
           GltfDiagnosticCode::kInvalidSelection);
    const bool translated =
        request.processing_profile ==
        GltfProcessingProfile::kDirectTranslationTrianglesV1;
    if (!translated &&
        record->capability != GltfCapabilityReason::kBinaryAdmissionPending &&
        record->capability != GltfCapabilityReason::kReady)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kIndexRole, {},
           kSelectionRule);
    const auto& state = Access::State(asset);
    if (translated)
      ValidateDirectTranslationConfiguration(
          limits.direct_translation, state.work_policy.direct_translation);
    AllocationContext allocation(state.context, stop, backend);
    allocation.Checkpoint();
    std::shared_ptr<const DirectTranslationState> translation;
    auto ordinary_allowance = Host(limits.max_output_requested_bytes);
    if (translated) {
      auto validation = ValidateDirectTranslation(
          Access::Owner(asset), *record, limits.direct_translation,
          state.work_policy.direct_translation, allocation, ordinary_allowance,
          execution);
      translation_observation = validation.observation;
      if (!validation.proof)
        return {{}, validation.diagnostic, translation_observation};
      translation = std::move(validation.proof);
      const auto retained = translation->allocation.OperationActiveBytes();
      if (retained >= ordinary_allowance)
        Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kLimitExceeded, {}, kLimitRule);
      ordinary_allowance -= retained;
    }
    // Full actual request admission is supplied by the operation allocator,
    // including rebound shared control storage and replacement overlap.
    allocation = allocation.WithRequestLimit(ordinary_allowance);
    auto output = std::allocate_shared<ExtractionState>(
        Allocator<ExtractionState>(allocation), Access::Owner(asset), *record,
        allocation);
    output->translation = std::move(translation);
    const auto& doc = state.document;
    const auto primitive_node = record->json_node;
    output->mode = OptionalUnsigned(doc, primitive_node, kMode, kTriangles);
    if ((output->mode == kTriangles &&
         request.triangles != GltfTriangleRequest::kRequireTriangles) ||
        ((output->mode == kTriangleStrip || output->mode == kTriangleFan) &&
         request.triangles != GltfTriangleRequest::kConvertStripOrFan) ||
        (output->mode != kTriangles && output->mode != kTriangleStrip &&
         output->mode != kTriangleFan))
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kIndexRole, {},
           kTopologyRule);
    if (const auto material = FindMember(doc, primitive_node, kMaterial))
      output->material = ReadExactUnsigned(doc, *material);
    const auto attributes = Required(doc, primitive_node, kAttributes);
    const auto position = FindMember(doc, attributes, kPosition);
    if (!position)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired, {},
           kRoleRule);
    ValidateSets(doc, attributes, kTexcoordPrefix);
    ValidateSets(doc, attributes, kColorPrefix);
    const auto vertex_count =
        ResolveAccessor(state, ReadExactUnsigned(doc, *position),
                        AccessorRole::kVertex)
            .count;
    const auto inventory = BuildRoleInventory(state, allocation);
    std::size_t total_components = 0;
    output->channel_storage.reserve(Host(ChildCount(doc, attributes)));
    output->channels.reserve(Host(ChildCount(doc, attributes)));
    for (auto child = doc.FirstChild(attributes); child;
         child = doc.NextSibling(*child)) {
      allocation.Checkpoint();
      auto source = ResolveAccessor(state, ReadExactUnsigned(doc, *child),
                                    AccessorRole::kVertex);
      if (source.count != vertex_count)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {},
             kRoleRule);
      const auto layout =
          DescribeAccessorLayout(source.component_type, source.shape);
      total_components =
          Add(total_components,
              Product(source.count, layout.rows * layout.columns));
      if (total_components > limits.max_components)
        Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kLimitExceeded, {}, kLimitRule);
      const auto set = AdmitRole(source, doc.Key(*child));
      output->channel_storage.emplace_back(allocation, std::move(source),
                                           doc.Key(*child));
      auto& channel = output->channel_storage.back();
      channel.set = set;
      DecodeChannel(channel, state, inventory, request, limits, allocation,
                    stop, execution);
      output->channels.push_back(ChannelView(channel));
    }
    std::size_t stream_count = vertex_count;
    if (const auto index = FindMember(doc, primitive_node, kIndices)) {
      auto source = ResolveAccessor(state, ReadExactUnsigned(doc, *index),
                                    AccessorRole::kIndices);
      stream_count = source.count;
      total_components = Add(total_components, stream_count);
      if (total_components > limits.max_components)
        Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kLimitExceeded, {}, kLimitRule);
      output->indices.emplace(allocation, std::move(source), kIndices);
      DecodeChannel(*output->indices, state, inventory, request, limits,
                    allocation, stop, execution);
    } else if (vertex_count != 0 &&
               vertex_count - 1 > std::numeric_limits<std::uint32_t>::max()) {
      Fail(GltfStatus::kUnsupportedProfile,
           GltfDiagnosticCode::kUnsupportedNumberRepresentation, {},
           kTopologyRule);
    }
    if (stream_count < kTriangleCorners ||
        (output->mode == kTriangles && stream_count % kTriangleCorners))
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {},
           kTopologyRule);
    const auto corners = output->mode == kTriangles
                             ? stream_count
                             : Product(stream_count - 2, kTriangleCorners);
    if (corners > limits.max_triangle_corners)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded,
           {}, kLimitRule);
    auto row_at = [&](std::size_t corner) -> std::uint32_t {
      std::uint32_t row = static_cast<std::uint32_t>(corner);
      if (output->indices)
        row = std::visit(
            [&](const auto& raw) -> std::uint32_t {
              using Value =
                  typename std::remove_cvref_t<decltype(raw)>::value_type;
              if constexpr (std::is_integral_v<Value> &&
                            std::is_unsigned_v<Value>)
                return static_cast<std::uint32_t>(raw[corner]);
              else
                Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {},
                     kTopologyRule);
            },
            output->indices->values.raw);
      if (row >= vertex_count)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole, {},
             kTopologyRule);
      return row;
    };
    output->triangles.resize(corners);
    output->corners.resize(corners);
    for (std::size_t corner = 0; corner < corners; ++corner) {
      allocation.Checkpoint();
      auto source_corner = corner;
      if (output->mode != kTriangles) {
        const auto triangle = corner / kTriangleCorners;
        const auto local = corner % kTriangleCorners;
        if (output->mode == kTriangleFan)
          source_corner = local == 0 ? 0 : triangle + local;
        else
          source_corner =
              triangle + (local == 2 ? 2 : (triangle % 2 ? 1 - local : local));
      }
      const auto row = row_at(source_corner);
      output->triangles[corner] = row;
      output->corners[corner] = {source_corner, row};
    }
    allocation.Checkpoint();
    return {Access::MakeExtraction(std::move(output)), AcceptedDiagnostic(),
            translation_observation};
  } catch (const GltfFailure& failure) {
    return {{}, failure.Diagnostic(), translation_observation};
  } catch (const AdmissionFailure& failure) {
    GltfStatus status = GltfStatus::kUnsupportedProfile;
    GltfDiagnosticCode code = GltfDiagnosticCode::kLimitExceeded;
    switch (failure.Code()) {
      case AdmissionCode::kBudgetExceeded:
        status = GltfStatus::kBudgetExceeded;
        code = GltfDiagnosticCode::kPayloadBudget;
        break;
      case AdmissionCode::kCanceled:
        status = GltfStatus::kCanceled;
        code = GltfDiagnosticCode::kCanceled;
        break;
      case AdmissionCode::kSizeLimit:
        status = GltfStatus::kUnsupportedProfile;
        code = GltfDiagnosticCode::kLimitExceeded;
        break;
      case AdmissionCode::kInvalidObject:
        throw;
    }
    return {{},
            {status, code, {}, {}, {}, {}, kLimitRule},
            translation_observation};
  }
}
}  // namespace
GltfExtractionResult ExtractGltfPrimitive(const GltfAsset& asset,
                                          const GltfPrimitiveHandle& primitive,
                                          const GltfExtractionRequest& request,
                                          const GltfExtractionLimits& limits,
                                          std::stop_token stop) {
  return ExtractPrimitive(asset, primitive, request, limits, stop,
                          gltf_detail::DefaultAllocationBackend(), nullptr);
}
GltfExtractionResult ExtractGltfPrimitiveCapped(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
    const GltfExtractionRequest& request, const GltfExtractionLimits& limits,
    std::stop_token stop) {
  const gltf_detail::NativeExecutionOptions execution;
  return ExtractPrimitive(asset, primitive, request, limits, stop,
                          gltf_detail::DefaultAllocationBackend(), &execution);
}
namespace gltf_detail {
GltfExtractionResult TestExtractGltfPrimitiveCapped(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
    const GltfExtractionRequest& request, const GltfExtractionLimits& limits,
    std::stop_token stop, AllocationBackend allocation_backend,
    NativeWorkerBackend worker_backend,
    const OperationWorkerObserver* observer) {
  const NativeExecutionOptions execution{worker_backend, observer};
  return ExtractPrimitive(asset, primitive, request, limits, stop,
                          allocation_backend, &execution);
}
}  // namespace gltf_detail
}  // namespace meshvale::interchange
