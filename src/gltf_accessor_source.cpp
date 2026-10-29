// SPDX-License-Identifier: Apache-2.0
#include "gltf_accessor_source.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

#include "gltf_metadata.h"

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr std::uint64_t kByteComponent = 5120;
constexpr std::uint64_t kUnsignedByteComponent = 5121;
constexpr std::uint64_t kShortComponent = 5122;
constexpr std::uint64_t kUnsignedShortComponent = 5123;
constexpr std::uint64_t kUnsignedIntComponent = 5125;
constexpr std::uint64_t kFloatComponent = 5126;
constexpr std::uint64_t kVertexTarget = 34962;
constexpr std::uint64_t kIndexTarget = 34963;
constexpr std::uint32_t kBoundsRule = 312;
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
constexpr std::string_view kScalarShape = "SCALAR";
constexpr std::string_view kVec2Shape = "VEC2";
constexpr std::string_view kVec3Shape = "VEC3";
constexpr std::string_view kVec4Shape = "VEC4";
constexpr std::string_view kMat2Shape = "MAT2";
constexpr std::string_view kMat3Shape = "MAT3";
constexpr std::string_view kMat4Shape = "MAT4";
void Reference(const AccessorResolveWork* work) {
  if (work) work->reference(work->state);
}
bool KeyEquals(std::string_view a, std::string_view b,
               const AccessorResolveWork* work) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (work) work->byte_visit(work->state);
    if (a[i] != b[i]) return false;
  }
  return true;
}
std::optional<std::size_t> Member(const JsonSnapshot& doc, std::size_t object,
                                  std::string_view key,
                                  const AccessorResolveWork* work) {
  if (doc.Kind(object) != JsonKind::kObject) return {};
  for (auto n = doc.FirstChild(object); n; n = doc.NextSibling(*n)) {
    Reference(work);
    if (KeyEquals(doc.Key(*n), key, work)) return n;
  }
  return {};
}
std::optional<std::size_t> Child(const JsonSnapshot& doc, std::size_t array,
                                 std::uint64_t index,
                                 const AccessorResolveWork* work) {
  for (auto n = doc.FirstChild(array); n; n = doc.NextSibling(*n)) {
    Reference(work);
    if (index == 0) return n;
    --index;
  }
  return {};
}
std::size_t Host(std::uint64_t value) {
  if (value > std::numeric_limits<std::size_t>::max())
    Fail(GltfStatus::kUnsupportedProfile,
         GltfDiagnosticCode::kUnsupportedNumberRepresentation, {}, kLimitRule);
  return static_cast<std::size_t>(value);
}
std::size_t Required(const JsonSnapshot& doc, std::size_t object,
                     std::string_view name, const AccessorResolveWork* work) {
  const auto value = Member(doc, object, name, work);
  if (!value)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired);
  return *value;
}
std::uint64_t OptionalUnsigned(const JsonSnapshot& doc, std::size_t object,
                               std::string_view name,
                               const AccessorResolveWork* work) {
  const auto value = Member(doc, object, name, work);
  return value ? ReadExactUnsigned(doc, *value) : 0;
}
std::size_t TableRow(const JsonSnapshot& doc, std::string_view name,
                     std::uint64_t index, const AccessorResolveWork* work) {
  const auto value = Child(doc, Required(doc, 0, name, work), index, work);
  if (!value) Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
  return *value;
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
AccessorByteView ResolveView(const AssetState& asset, std::uint64_t index,
                             const AccessorResolveWork* work) {
  const auto& doc = asset.document;
  const auto row = TableRow(doc, kBufferViews, index, work);
  const auto resource_index =
      ReadExactUnsigned(doc, Required(doc, row, kBuffer, work));
  const GltfResourceView* resource = nullptr;
  for (const auto& entry : asset.resources) {
    Reference(work);
    if (entry.kind == GltfResourceKind::kBuffer &&
        entry.table_index == resource_index) {
      resource = &entry;
      break;
    }
  }
  if (!resource)
    Fail(GltfStatus::kResourceBindingMismatch,
         GltfDiagnosticCode::kMissingResource);
  const auto offset = OptionalUnsigned(doc, row, kByteOffset, work);
  const auto size =
      ReadExactUnsigned(doc, Required(doc, row, kByteLength, work));
  if (offset > resource->logical_bytes ||
      size > resource->logical_bytes - offset ||
      offset > resource->bytes.size() || size > resource->bytes.size() - offset)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kResourceExtent);
  auto target = ViewTarget::kAbsent;
  if (const auto member = Member(doc, row, kTarget, work)) {
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
          Host(OptionalUnsigned(doc, row, kByteStride, work)),
          target};
}
}  // namespace
AccessorSource ResolveAccessorSource(const AssetState& asset,
                                     std::uint64_t index, AccessorRole role,
                                     const AccessorResolveWork* work) {
  const auto& doc = asset.document;
  const auto row = TableRow(doc, kAccessors, index, work);
  AccessorSource source;
  source.index = index;
  source.component_type = Component(
      ReadExactUnsigned(doc, Required(doc, row, kComponentType, work)));
  source.shape = Shape(doc.String(Required(doc, row, kType, work)));
  source.count = Host(ReadExactUnsigned(doc, Required(doc, row, kCount, work)));
  source.role = role;
  source.base_offset = Host(OptionalUnsigned(doc, row, kByteOffset, work));
  if (const auto normalized = Member(doc, row, kNormalized, work))
    source.normalized = doc.Boolean(*normalized);
  if (const auto view = Member(doc, row, kBufferView, work))
    source.base = ResolveView(asset, ReadExactUnsigned(doc, *view), work);
  if (const auto sparse = Member(doc, row, kSparse, work)) {
    const auto indices = Required(doc, *sparse, kIndices, work);
    const auto values = Required(doc, *sparse, kValues, work);
    source.sparse = AccessorSparseSource{
        Host(ReadExactUnsigned(doc, Required(doc, *sparse, kCount, work))),
        ResolveView(
            asset,
            ReadExactUnsigned(doc, Required(doc, indices, kBufferView, work)),
            work),
        Host(OptionalUnsigned(doc, indices, kByteOffset, work)),
        Component(ReadExactUnsigned(
            doc, Required(doc, indices, kComponentType, work))),
        ResolveView(
            asset,
            ReadExactUnsigned(doc, Required(doc, values, kBufferView, work)),
            work),
        Host(OptionalUnsigned(doc, values, kByteOffset, work))};
  }
  return source;
}
OwnedRawValues ReadAccessorBound(
    const JsonSnapshot& doc, std::size_t bound, const AccessorSource& source,
    const AllocationContext& allocation,
    GltfDeformationNumericObservation* observation) {
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
            value = std::bit_cast<float>(
                ReadFloatBitsObserved(doc, *child, observation));
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
}  // namespace meshvale::interchange::gltf_detail
