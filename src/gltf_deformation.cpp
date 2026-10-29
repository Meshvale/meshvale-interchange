// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_deformation.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_deformation_read.h"
#include "gltf_deformation_state.h"

namespace meshvale::interchange::gltf_deformation_detail {
AccessorStorage::AccessorStorage(
    gltf_detail::AccessorSource descriptor,
    const gltf_detail::AllocationContext& decoded_allocation)
    : source(std::move(descriptor)), decoded(decoded_allocation) {}
State::State(const GltfAsset& source, const GltfPrimitiveHandle& selected,
             const GltfExtractionRequest& request,
             const gltf_detail::AllocationContext& operation,
             const gltf_detail::AllocationContext& descriptor_scope,
             const gltf_detail::AllocationContext& decoded_scope)
    : asset(source),
      primitive(selected),
      triangles(request.triangles),
      values(request.values),
      policy(request.deformation),
      allocation(operation),
      descriptors(descriptor_scope),
      decoded(decoded_scope),
      accessor_storage(gltf_detail::Allocator<AccessorStorage>(descriptors)),
      accessors(
          gltf_detail::Allocator<GltfDeformationAccessorView>(descriptors)),
      uses(gltf_detail::Allocator<GltfDeformationAccessorUseView>(descriptors)),
      influences(gltf_detail::Allocator<GltfInfluenceSetView>(descriptors)),
      joint_storage(
          gltf_detail::Allocator<gltf_detail::OwnedVector<std::uint64_t>>(
              descriptors)),
      skins(gltf_detail::Allocator<GltfSkinView>(descriptors)),
      ancestors(gltf_detail::Allocator<gltf_detail::OwnedVector<std::uint64_t>>(
          descriptors)),
      joint_ancestors(
          gltf_detail::Allocator<gltf_detail::OwnedVector<std::uint64_t>>(
              descriptors)),
      users(gltf_detail::Allocator<GltfSkinUserView>(descriptors)),
      morph_channels(gltf_detail::Allocator<GltfMorphChannelView>(descriptors)),
      morphs(gltf_detail::Allocator<GltfMorphPrimitiveView>(descriptors)),
      defaults_storage(
          gltf_detail::Allocator<gltf_detail::OwnedVector<GltfRawNumberView>>(
              descriptors)),
      defaults(gltf_detail::Allocator<GltfMorphWeightDefaultView>(descriptors)),
      animations(
          gltf_detail::Allocator<GltfDeformationAnimationView>(descriptors)),
      declaration_names(
          gltf_detail::Allocator<gltf_detail::OwnedVector<char>>(descriptors)),
      declarations(gltf_detail::Allocator<GltfDeformationRowLocalDeclaration>(
          descriptors)) {}
const State& Access::Get(const GltfDeformationProof& proof) noexcept {
  return *proof.owner_;
}
GltfDeformationProof Access::MakeProof(std::shared_ptr<const State> state) {
  if (!state || !state->facts.complete ||
      state->asset.AdmissionProfile() !=
          GltfAdmissionProfile::kRetainedCoreDeformationV1 ||
      state->facts.status != GltfStatus::kAccepted || !state->facts.numeric ||
      state->facts.numeric->established_calls == 0 ||
      !state->facts.numeric->compiled_profile ||
      state->facts.numeric->refusal ||
      state->facts.predicates.source_admission != true ||
      state->facts.predicates.influences != true ||
      state->facts.predicates.bindings != true ||
      state->facts.predicates.morphs != true ||
      state->facts.predicates.affecting_animation != true)
    gltf_detail::Fail(GltfStatus::kInvalidSelection,
                      GltfDiagnosticCode::kInvalidSelection);
  if (!state->allocation.SharesExecutionBudget(
          gltf_detail::Access::State(state->asset).allocation) ||
      (state->triangles != GltfTriangleRequest::kRequireTriangles &&
       state->triangles != GltfTriangleRequest::kConvertStripOrFan) ||
      (state->values != GltfValueRequest::kRaw &&
       state->values != GltfValueRequest::kRawAndNormalized))
    gltf_detail::Fail(GltfStatus::kInvalidSelection,
                      GltfDiagnosticCode::kInvalidSelection);
  if (!gltf_detail::Access::PrimitiveRecordFor(state->asset, state->primitive))
    gltf_detail::Fail(GltfStatus::kInvalidSelection,
                      GltfDiagnosticCode::kInvalidSelection);
  return GltfDeformationProof(std::move(state));
}
}  // namespace meshvale::interchange::gltf_deformation_detail

namespace meshvale::interchange {
GltfDeformationProof::GltfDeformationProof(
    std::shared_ptr<const gltf_deformation_detail::State> owner) noexcept
    : owner_(std::move(owner)) {}
GltfDeformationProof::GltfDeformationProof(
    const GltfDeformationProof&) noexcept = default;
GltfDeformationProof& GltfDeformationProof::operator=(
    const GltfDeformationProof&) noexcept = default;
GltfDeformationProof::~GltfDeformationProof() noexcept = default;
const GltfAsset& GltfDeformationProof::Asset() const noexcept {
  return owner_->asset;
}
const GltfPrimitiveHandle& GltfDeformationProof::Primitive() const noexcept {
  return owner_->primitive;
}
GltfAdmissionProfile GltfDeformationProof::AdmissionProfile() const noexcept {
  return owner_->asset.AdmissionProfile();
}
GltfProcessingProfile GltfDeformationProof::ProcessingProfile() const noexcept {
  return GltfProcessingProfile::kDeformationRetainedTrianglesV1;
}
GltfNormativeRevision GltfDeformationProof::NormativeRevision() const noexcept {
  return gltf_detail::kDeformationRevision;
}
GltfDeformationNumericProfile GltfDeformationProof::NumericProfile()
    const noexcept {
  return GltfDeformationNumericProfile::kRneGradualV1;
}
std::string_view GltfDeformationProof::QuaternionPolicy() const noexcept {
  return gltf_detail::kDeformationQuaternionPolicy;
}
GltfTriangleRequest GltfDeformationProof::Triangles() const noexcept {
  return owner_->triangles;
}
GltfValueRequest GltfDeformationProof::Values() const noexcept {
  return owner_->values;
}
const GltfDeformationPolicy& GltfDeformationProof::Policy() const noexcept {
  return owner_->policy;
}
std::span<const GltfDeformationAccessorView> GltfDeformationProof::Accessors()
    const noexcept {
  return owner_->accessors;
}
std::span<const GltfDeformationAccessorUseView>
GltfDeformationProof::AccessorUses() const noexcept {
  return owner_->uses;
}
std::span<const GltfInfluenceSetView> GltfDeformationProof::InfluenceSets()
    const noexcept {
  return owner_->influences;
}
std::span<const GltfSkinView> GltfDeformationProof::Skins() const noexcept {
  return owner_->skins;
}
std::span<const GltfSkinUserView> GltfDeformationProof::Users() const noexcept {
  return owner_->users;
}
std::span<const GltfMorphPrimitiveView> GltfDeformationProof::MorphPrimitives()
    const noexcept {
  return owner_->morphs;
}
std::span<const GltfMorphWeightDefaultView>
GltfDeformationProof::MorphDefaults() const noexcept {
  return owner_->defaults;
}
std::span<const GltfDeformationAnimationView> GltfDeformationProof::Animations()
    const noexcept {
  return owner_->animations;
}
const GltfWeightSumSummary& GltfDeformationProof::WeightSums() const noexcept {
  return owner_->weight_sums;
}
std::span<const GltfDeformationRowLocalDeclaration>
GltfDeformationProof::RowLocalDeclarations() const noexcept {
  return owner_->declarations;
}
const GltfDeformationObservation& GltfDeformationProof::Facts() const noexcept {
  return owner_->facts;
}
std::uint64_t GltfDeformationProof::NewRequestedBytes() const noexcept {
  return owner_->allocation.OperationActiveBytes();
}
bool GltfDeformationProof::SharesExecutionBudget(
    const geometry::ExecutionContext& context) const noexcept {
  return owner_->asset.SharesExecutionBudget(context);
}
}  // namespace meshvale::interchange
