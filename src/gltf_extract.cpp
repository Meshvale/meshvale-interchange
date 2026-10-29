// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_deformation.h>

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
#include "gltf_accessor_source.h"
#include "gltf_asset_state.h"
#include "gltf_deformation_state.h"
#include "gltf_direct_translation.h"
#include "gltf_metadata.h"
#include "gltf_operation_workers.h"
#include "gltf_workflow_test_internal.h"

namespace meshvale::interchange::gltf_detail {
namespace {
// Khronos glTF 2.0 core identifiers and roles, not parser enum ordinals.
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
void ValidateBounds(ChannelStorage& channel, const JsonSnapshot& doc,
                    const AllocationContext& allocation) {
  const auto node = TableRow(doc, kAccessors, channel.source.index);
  const auto minimum = FindMember(doc, node, kMinimum);
  const auto maximum = FindMember(doc, node, kMaximum);
  if (channel.semantic == kPosition && (!minimum || !maximum))
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired, {},
         kBoundsRule);
  if (minimum)
    channel.minimum =
        ReadAccessorBound(doc, *minimum, channel.source, allocation);
  if (maximum)
    channel.maximum =
        ReadAccessorBound(doc, *maximum, channel.source, allocation);
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

// The catalog has already admitted every use, including sparse source rows and
// shared/inactive users. The legacy channel facade still owns typed FLOAT
// values; form those by exact bit transfer, never by a second admission or
// numeric conversion. Both copies and their controls remain in the catalog's
// original decoded/descriptor children of this complete extraction request.
OwnedRawValues CopyCatalogValues(const OwnedRawValues& values, bool float_words,
                                 const AllocationContext& allocation) {
  if (float_words) {
    const auto& words = std::get<ChargedValues<std::uint32_t>>(values);
    ChargedValues<float> copy{Allocator<float>(allocation)};
    copy.resize(words.size());
    for (std::size_t i = 0; i < words.size(); ++i) {
      allocation.Checkpoint();
      copy[i] = std::bit_cast<float>(words[i]);
    }
    return copy;
  }
  return std::visit(
      [&](const auto& raw) -> OwnedRawValues {
        using Value = typename std::remove_cvref_t<decltype(raw)>::value_type;
        ChargedValues<Value> copy{Allocator<Value>(allocation)};
        copy.resize(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
          allocation.Checkpoint();
          copy[i] = raw[i];
        }
        return copy;
      },
      values);
}
const gltf_deformation_detail::AccessorStorage& CatalogAccessor(
    const gltf_deformation_detail::State& catalog, std::uint64_t index) {
  for (const auto& accessor : catalog.accessor_storage) {
    catalog.allocation.Checkpoint();
    if (accessor.source.index == index) return accessor;
  }
  Fail(GltfStatus::kInvalidSelection, GltfDiagnosticCode::kInvalidSelection, {},
       kSelectionRule);
}
void CopyCatalogChannel(ChannelStorage& channel,
                        const gltf_deformation_detail::AccessorStorage& source,
                        const AllocationContext& allocation) {
  channel.values.raw = CopyCatalogValues(
      source.decoded.raw, source.decoded.float_words, allocation);
  channel.values.base_stride = source.decoded.base_stride;
  channel.values.packed_sparse_step = source.decoded.packed_sparse_step;
  for (const auto destination : source.decoded.sparse_destinations) {
    allocation.Checkpoint();
    channel.values.sparse_destinations.push_back(destination);
  }
  for (const auto value : source.decoded.normalized) {
    allocation.Checkpoint();
    channel.values.normalized.push_back(value);
  }
  if (source.minimum)
    channel.minimum = CopyCatalogValues(*source.minimum, false, allocation);
  if (source.maximum)
    channel.maximum = CopyCatalogValues(*source.maximum, false, allocation);
}
}  // namespace
struct ExtractionState {
  ExtractionState(std::shared_ptr<const AssetState> source,
                  const PrimitiveRecord& source_primitive,
                  const AllocationContext& operation,
                  const AllocationContext& descriptors)
      : asset(std::move(source)),
        primitive(source_primitive),
        allocation(operation),
        channel_storage(Allocator<ChannelStorage>(descriptors)),
        channels(Allocator<GltfChannelView>(descriptors)),
        triangles(Allocator<std::uint32_t>(descriptors)),
        corners(Allocator<GltfSourceCorner>(descriptors)) {}
  std::shared_ptr<const AssetState> asset;
  PrimitiveRecord primitive;
  AllocationContext allocation;
  std::shared_ptr<const DirectTranslationState> translation;
  std::optional<GltfDeformationProof> deformation;
  std::optional<GltfDeformationObservation> deformation_facts;
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
  if (owner_->deformation)
    return GltfProcessingProfile::kDeformationRetainedTrianglesV1;
  return owner_->translation
             ? GltfProcessingProfile::kDirectTranslationTrianglesV1
             : GltfProcessingProfile::kStaticLocalTrianglesV1;
}
const GltfDeformationProof* GltfPrimitiveExtraction::Deformation()
    const noexcept {
  return owner_->deformation ? &*owner_->deformation : nullptr;
}
const GltfDeformationObservation* GltfPrimitiveExtraction::DeformationFacts()
    const noexcept {
  return owner_->deformation_facts ? &*owner_->deformation_facts : nullptr;
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
  std::optional<GltfDeformationObservation> deformation_observation;
  try {
    if ((request.triangles != GltfTriangleRequest::kRequireTriangles &&
         request.triangles != GltfTriangleRequest::kConvertStripOrFan) ||
        (request.values != GltfValueRequest::kRaw &&
         request.values != GltfValueRequest::kRawAndNormalized))
      Fail(GltfStatus::kInvalidRequest, GltfDiagnosticCode::kInvalidRequest, {},
           kSelectionRule);
    const bool deformation_requested =
        request.processing_profile ==
        GltfProcessingProfile::kDeformationRetainedTrianglesV1;
    const bool deformation_source =
        asset.AdmissionProfile() ==
        GltfAdmissionProfile::kRetainedCoreDeformationV1;
    if (deformation_requested != deformation_source)
      Fail(GltfStatus::kInvalidRequest,
           GltfDiagnosticCode::kAdmissionProfileMismatch, {}, kSelectionRule);
    if (request.processing_profile !=
            GltfProcessingProfile::kStaticLocalTrianglesV1 &&
        request.processing_profile !=
            GltfProcessingProfile::kDirectTranslationTrianglesV1 &&
        !deformation_requested)
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
    if (!translated && !deformation_requested &&
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
    std::optional<GltfDeformationProof> deformation;
    if (deformation_requested) {
      auto validation = gltf_deformation_detail::ValidateDeformation(
          asset, primitive, request, allocation, limits, execution);
      deformation_observation = validation.observation;
      if (!validation.proof)
        return {{},
                validation.diagnostic,
                translation_observation,
                deformation_observation};
      deformation = gltf_deformation_detail::Access::MakeProof(
          std::move(validation.proof));
    }
    const auto* catalog =
        deformation ? &gltf_deformation_detail::Access::Get(*deformation)
                    : nullptr;
    const auto descriptors = catalog ? catalog->descriptors : allocation;
    const auto decoded = catalog ? catalog->decoded : allocation;
    auto output = std::allocate_shared<ExtractionState>(
        Allocator<ExtractionState>(descriptors), Access::Owner(asset), *record,
        allocation, descriptors);
    output->translation = std::move(translation);
    output->deformation = std::move(deformation);
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
    const auto position_index = ReadExactUnsigned(doc, *position);
    const auto vertex_count =
        catalog ? CatalogAccessor(*catalog, position_index).source.count
                : ResolveAccessorSource(state, position_index,
                                        AccessorRole::kVertex)
                      .count;
    std::optional<RoleInventory> inventory;
    if (!catalog) inventory = BuildRoleInventory(state, allocation);
    std::size_t total_components = 0;
    output->channel_storage.reserve(Host(ChildCount(doc, attributes)));
    output->channels.reserve(Host(ChildCount(doc, attributes)));
    for (auto child = doc.FirstChild(attributes); child;
         child = doc.NextSibling(*child)) {
      allocation.Checkpoint();
      const auto index = ReadExactUnsigned(doc, *child);
      const auto* admitted =
          catalog ? &CatalogAccessor(*catalog, index) : nullptr;
      auto source =
          admitted ? admitted->source
                   : ResolveAccessorSource(state, index, AccessorRole::kVertex);
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
      const auto semantic = doc.Key(*child);
      auto set = catalog ? std::optional<std::uint64_t>{}
                         : AdmitRole(source, semantic);
      if (catalog)
        for (const auto prefix :
             {kTexcoordPrefix, kColorPrefix, kJointsPrefix, kWeightsPrefix})
          if (semantic.starts_with(prefix)) set = SetIndex(semantic, prefix);
      output->channel_storage.emplace_back(decoded, std::move(source),
                                           doc.Key(*child));
      auto& channel = output->channel_storage.back();
      channel.set = set;
      if (admitted)
        CopyCatalogChannel(channel, *admitted, decoded);
      else
        DecodeChannel(channel, state, *inventory, request, limits, allocation,
                      stop, execution);
      output->channels.push_back(ChannelView(channel));
    }
    std::size_t stream_count = vertex_count;
    if (const auto index = FindMember(doc, primitive_node, kIndices)) {
      const auto accessor_index = ReadExactUnsigned(doc, *index);
      const auto* admitted =
          catalog ? &CatalogAccessor(*catalog, accessor_index) : nullptr;
      auto source = admitted ? admitted->source
                             : ResolveAccessorSource(state, accessor_index,
                                                     AccessorRole::kIndices);
      stream_count = source.count;
      total_components = Add(total_components, stream_count);
      if (total_components > limits.max_components)
        Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kLimitExceeded, {}, kLimitRule);
      output->indices.emplace(decoded, std::move(source), kIndices);
      if (admitted)
        CopyCatalogChannel(*output->indices, *admitted, decoded);
      else
        DecodeChannel(*output->indices, state, *inventory, request, limits,
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
    if (deformation_observation) {
      deformation_observation->new_requested_bytes =
          output->deformation->NewRequestedBytes();
      output->deformation_facts = deformation_observation;
    }
    return {Access::MakeExtraction(std::move(output)), AcceptedDiagnostic(),
            translation_observation, deformation_observation};
  } catch (const GltfFailure& failure) {
    if (deformation_observation) {
      deformation_observation->complete = false;
      deformation_observation->status = failure.Diagnostic().status;
      deformation_observation->diagnostic = failure.Diagnostic();
    }
    return {{},
            failure.Diagnostic(),
            translation_observation,
            deformation_observation};
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
    const GltfDiagnostic diagnostic{status, code, {}, {}, {}, {}, kLimitRule};
    if (deformation_observation) {
      deformation_observation->complete = false;
      deformation_observation->status = status;
      deformation_observation->diagnostic = diagnostic;
    }
    return {{}, diagnostic, translation_observation, deformation_observation};
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
