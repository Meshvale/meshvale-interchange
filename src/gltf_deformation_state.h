// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_STATE_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_STATE_H_

#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_deformation.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "gltf_accessor.h"
#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_deformation_detail {
struct AccessorStorage {
  AccessorStorage(gltf_detail::AccessorSource source,
                  const gltf_detail::AllocationContext& decoded_allocation);
  gltf_detail::AccessorSource source;
  gltf_detail::DecodedAccessor decoded;
  std::optional<gltf_detail::OwnedRawValues> minimum, maximum;
  bool unconstrained_float_words = false;
};
// Mutable only while the validator builds it, then shared as const. Every view
// below borrows this storage or the actual retained source, never caller input.
struct State {
  State(const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
        const GltfExtractionRequest& request,
        const gltf_detail::AllocationContext& allocation,
        const gltf_detail::AllocationContext& descriptors,
        const gltf_detail::AllocationContext& decoded);
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  GltfAsset asset;
  GltfPrimitiveHandle primitive;
  GltfTriangleRequest triangles;
  GltfValueRequest values;
  GltfDeformationPolicy policy;
  gltf_detail::AllocationContext allocation;
  gltf_detail::AllocationContext descriptors;
  gltf_detail::AllocationContext decoded;
  gltf_detail::OwnedVector<AccessorStorage> accessor_storage;
  gltf_detail::OwnedVector<GltfDeformationAccessorView> accessors;
  gltf_detail::OwnedVector<GltfDeformationAccessorUseView> uses;
  gltf_detail::OwnedVector<GltfInfluenceSetView> influences;
  gltf_detail::OwnedVector<gltf_detail::OwnedVector<std::uint64_t>>
      joint_storage;
  gltf_detail::OwnedVector<GltfSkinView> skins;
  gltf_detail::OwnedVector<gltf_detail::OwnedVector<std::uint64_t>> ancestors;
  gltf_detail::OwnedVector<gltf_detail::OwnedVector<std::uint64_t>>
      joint_ancestors;
  gltf_detail::OwnedVector<GltfSkinUserView> users;
  gltf_detail::OwnedVector<GltfMorphChannelView> morph_channels;
  gltf_detail::OwnedVector<GltfMorphPrimitiveView> morphs;
  gltf_detail::OwnedVector<gltf_detail::OwnedVector<GltfRawNumberView>>
      defaults_storage;
  gltf_detail::OwnedVector<GltfMorphWeightDefaultView> defaults;
  gltf_detail::OwnedVector<GltfDeformationAnimationView> animations;
  gltf_detail::OwnedVector<gltf_detail::OwnedVector<char>> declaration_names;
  gltf_detail::OwnedVector<GltfDeformationRowLocalDeclaration> declarations;
  GltfWeightSumSummary weight_sums;
  GltfDeformationObservation facts;
};
struct ValidationResult {
  // Pending private storage. Extraction finishes its source facade before
  // setting complete and publishing a const proof.
  std::shared_ptr<State> proof;
  GltfDiagnostic diagnostic;
  GltfDeformationObservation observation;
};
void DeformationReference(void* state);
struct ReferenceWork {
  GltfDeformationObservation& observation;
  const GltfDeformationPolicy& policy;
  const gltf_detail::AllocationContext& allocation;
};
void DeformationPreludeReference(void* work);
void ValidateDeformationPreflight(const GltfExtractionRequest& request);
struct Access {
  // Finish private mutable storage only after charging final source-owner
  // validation; complete and immutable publication belong to this seam.
  [[nodiscard]] static GltfDeformationProof MakeProof(
      std::shared_ptr<State> state);
  [[nodiscard]] static const State& Get(
      const GltfDeformationProof& proof) noexcept;
};
// The supplied allocator is the already admitted complete extraction request.
// All additional owners retain it and the original execution ledger. No new
// context, nested WithRequestLimit or detached proof construction is permitted.
[[nodiscard]] ValidationResult ValidateDeformation(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
    const GltfExtractionRequest& request,
    const gltf_detail::AllocationContext& complete_allocation,
    const GltfExtractionLimits& limits,
    const gltf_detail::NativeExecutionOptions* execution,
    bool pending_facade = false,
    const GltfDeformationObservation* preflight = nullptr);
}  // namespace meshvale::interchange::gltf_deformation_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_STATE_H_
