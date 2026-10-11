// SPDX-License-Identifier: Apache-2.0
#include <utility>

#include "gltf_asset_state.h"
#include "meshvale/interchange/gltf.h"

namespace meshvale::interchange::gltf_detail {
AssetState::AssetState(const geometry::ExecutionContext& execution,
                       std::stop_token stop, const GltfReadLimits& read_limits,
                       const GltfWorkPolicy& policy, JsonSnapshot snapshot)
    : context(execution),
      allocation(execution, stop),
      limits(read_limits),
      work_policy(policy),
      document(std::move(snapshot)),
      resource_storage(Allocator<OwnedVector<std::byte>>(allocation)),
      resources(Allocator<GltfResourceView>(allocation)),
      raw_numbers(Allocator<GltfRawNumberView>(allocation)),
      children(Allocator<OwnedVector<std::uint64_t>>(allocation)),
      nodes(Allocator<GltfNodeView>(allocation)),
      roots(Allocator<OwnedVector<std::uint64_t>>(allocation)),
      scenes(Allocator<GltfSceneView>(allocation)),
      primitives(Allocator<PrimitiveRecord>(allocation)),
      instances(Allocator<InstanceRecord>(allocation)) {}
GltfFailure::GltfFailure(GltfDiagnostic diagnostic) noexcept
    : diagnostic_(diagnostic) {}
const GltfDiagnostic& GltfFailure::Diagnostic() const noexcept {
  return diagnostic_;
}
const char* GltfFailure::what() const noexcept {
  return "glTF admission failure";
}
void Fail(GltfStatus status, GltfDiagnosticCode code,
          std::optional<GltfSourceSpan> source, std::uint32_t rule_id) {
  throw GltfFailure({status, code, source, std::nullopt, std::nullopt,
                     std::nullopt, rule_id});
}
GltfDiagnostic AcceptedDiagnostic() noexcept {
  return {GltfStatus::kAccepted,
          GltfDiagnosticCode::kNone,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          0};
}
std::optional<std::size_t> FindMember(const JsonSnapshot& document,
                                      std::size_t object,
                                      std::string_view key) {
  if (document.Kind(object) != JsonKind::kObject) return std::nullopt;
  for (auto child = document.FirstChild(object); child;
       child = document.NextSibling(*child))
    if (document.Key(*child) == key) return child;
  return std::nullopt;
}
std::optional<std::size_t> ChildAt(const JsonSnapshot& document,
                                   std::size_t array, std::uint64_t ordinal) {
  if (document.Kind(array) != JsonKind::kArray) return std::nullopt;
  auto child = document.FirstChild(array);
  while (child && ordinal != 0) {
    child = document.NextSibling(*child);
    --ordinal;
  }
  return child;
}
std::uint64_t ChildCount(const JsonSnapshot& document, std::size_t node) {
  std::uint64_t count = 0;
  for (auto child = document.FirstChild(node); child;
       child = document.NextSibling(*child))
    ++count;
  return count;
}
const std::shared_ptr<const AssetState>& Access::Owner(
    const GltfAsset& asset) noexcept {
  return asset.owner_;
}
const std::shared_ptr<const AssetState>& Access::Owner(
    const GltfPrimitiveHandle& primitive) noexcept {
  return primitive.owner_;
}
const AssetState& Access::State(const GltfAsset& asset) {
  if (!asset.owner_)
    Fail(GltfStatus::kInvalidSelection, GltfDiagnosticCode::kInvalidSelection);
  return *asset.owner_;
}
const PrimitiveRecord* Access::PrimitiveRecordFor(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive) {
  if (asset.owner_ != primitive.owner_)
    Fail(GltfStatus::kForeignOwner, GltfDiagnosticCode::kForeignOwner);
  for (const auto& record : State(asset).primitives)
    if (record.mesh_index == primitive.mesh_ &&
        record.primitive_index == primitive.primitive_)
      return &record;
  return nullptr;
}
GltfAsset Access::MakeAsset(std::shared_ptr<const AssetState> state) {
  return GltfAsset(std::move(state));
}
GltfPrimitiveHandle Access::MakePrimitive(
    std::shared_ptr<const AssetState> state, const PrimitiveRecord& primitive) {
  return GltfPrimitiveHandle(std::move(state), primitive.mesh_index,
                             primitive.primitive_index);
}
GltfInstanceHandle Access::MakeInstance(std::shared_ptr<const AssetState> state,
                                        std::uint64_t ordinal) {
  return GltfInstanceHandle(std::move(state), ordinal);
}
}  // namespace meshvale::interchange::gltf_detail
namespace meshvale::interchange {
GltfAsset::GltfAsset(std::shared_ptr<const gltf_detail::AssetState> owner)
    : owner_(std::move(owner)) {}
GltfAsset::GltfAsset(const GltfAsset&) = default;
GltfAsset& GltfAsset::operator=(const GltfAsset&) = default;
GltfAsset::~GltfAsset() = default;
GltfAdmissionProfile GltfAsset::AdmissionProfile() const noexcept {
  return GltfAdmissionProfile::kRetainedCoreMetadataV1;
}
std::span<const std::byte> GltfAsset::OriginalJson() const noexcept {
  const auto original = owner_->document.Original();
  return std::as_bytes(std::span(original.data(), original.size()));
}
std::span<const GltfRawNumberView> GltfAsset::RawNumbers() const noexcept {
  return owner_->raw_numbers;
}
std::span<const GltfResourceView> GltfAsset::Resources() const noexcept {
  return owner_->resources;
}
std::span<const GltfNodeView> GltfAsset::Nodes() const noexcept {
  return owner_->nodes;
}
std::span<const GltfSceneView> GltfAsset::Scenes() const noexcept {
  return owner_->scenes;
}
std::uint64_t GltfAsset::PrimitiveCount() const noexcept {
  return owner_->primitives.size();
}
std::uint64_t GltfAsset::InstanceCount() const noexcept {
  return owner_->instances.size();
}
std::optional<GltfPrimitiveHandle> GltfAsset::Primitive(
    std::uint64_t ordinal) const {
  if (ordinal >= owner_->primitives.size()) return std::nullopt;
  return gltf_detail::Access::MakePrimitive(owner_,
                                            owner_->primitives[ordinal]);
}
std::optional<GltfInstanceHandle> GltfAsset::Instance(
    std::uint64_t ordinal) const {
  if (ordinal >= owner_->instances.size()) return std::nullopt;
  return gltf_detail::Access::MakeInstance(owner_, ordinal);
}
GltfPrimitiveCapability GltfAsset::Capability(
    const GltfPrimitiveHandle& primitive) const {
  constexpr auto kProfile = GltfProcessingProfile::kStaticLocalTrianglesV1;
  try {
    const auto* record =
        gltf_detail::Access::PrimitiveRecordFor(*this, primitive);
    return {kProfile, record ? record->capability
                             : GltfCapabilityReason::kInvalidSelection};
  } catch (const gltf_detail::GltfFailure& failure) {
    return {kProfile, failure.Diagnostic().status == GltfStatus::kForeignOwner
                          ? GltfCapabilityReason::kForeignOwner
                          : GltfCapabilityReason::kInvalidSelection};
  }
}
GltfPrimitiveHandle::GltfPrimitiveHandle(
    std::shared_ptr<const gltf_detail::AssetState> owner, std::uint64_t mesh,
    std::uint64_t primitive)
    : owner_(std::move(owner)), mesh_(mesh), primitive_(primitive) {}
GltfPrimitiveHandle::GltfPrimitiveHandle(const GltfPrimitiveHandle&) = default;
GltfPrimitiveHandle& GltfPrimitiveHandle::operator=(
    const GltfPrimitiveHandle&) = default;
GltfPrimitiveHandle::~GltfPrimitiveHandle() = default;
std::uint64_t GltfPrimitiveHandle::MeshIndex() const noexcept { return mesh_; }
std::uint64_t GltfPrimitiveHandle::PrimitiveIndex() const noexcept {
  return primitive_;
}
GltfInstanceHandle::GltfInstanceHandle(
    std::shared_ptr<const gltf_detail::AssetState> owner, std::uint64_t ordinal)
    : owner_(std::move(owner)), ordinal_(ordinal) {}
GltfInstanceHandle::GltfInstanceHandle(const GltfInstanceHandle&) = default;
GltfInstanceHandle& GltfInstanceHandle::operator=(const GltfInstanceHandle&) =
    default;
GltfInstanceHandle::~GltfInstanceHandle() = default;
std::uint64_t GltfInstanceHandle::SceneIndex() const noexcept {
  return owner_->instances[ordinal_].scene;
}
std::uint64_t GltfInstanceHandle::NodeIndex() const noexcept {
  return owner_->instances[ordinal_].node;
}
GltfPrimitiveHandle GltfInstanceHandle::Primitive() const {
  return gltf_detail::Access::MakePrimitive(
      owner_, owner_->primitives[owner_->instances[ordinal_].primitive]);
}
}  // namespace meshvale::interchange
