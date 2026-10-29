// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_GLTF_DEFORMATION_H_
#define MESHVALE_INTERCHANGE_GLTF_DEFORMATION_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include "meshvale/geometry/attributes.h"
#include "meshvale/geometry/execution.h"
#include "meshvale/interchange/gltf.h"

// Retained deformation catalog and privately constructed proof. Spans and
// references borrow an alive, unreassigned visible owner; independent copies
// support concurrent reads, while mutation/reassignment needs synchronization.
// Copies/getters allocate nothing and never recompute admission or
// observations.
namespace meshvale::interchange {
namespace gltf_deformation_detail {
struct State;
struct Access;
}  // namespace gltf_deformation_detail
struct GltfFloat32BitWords {
  std::span<const std::byte> little_endian_scalar_words;
};
using GltfDeformationRawValues =
    std::variant<std::span<const std::int8_t>, std::span<const std::uint8_t>,
                 std::span<const std::int16_t>, std::span<const std::uint16_t>,
                 std::span<const std::uint32_t>, GltfFloat32BitWords>;
enum class GltfDeformationDerivedValues {
  kNotRequested = 0,
  kAvailable = 1,
  kUnconstrainedRawBits = 2
};
struct GltfDeformationAccessorView {
  std::uint64_t accessor;
  GltfComponentType component_type;
  GltfAccessorShape shape;
  bool normalized;
  std::uint64_t element_count;
  std::optional<std::uint64_t> base_buffer_view;
  std::uint64_t base_byte_offset;
  std::uint64_t base_byte_stride;
  std::uint64_t packed_sparse_step;
  std::optional<GltfSparseProvenance> sparse_source;
  std::span<const std::uint64_t> sparse_destinations;
  std::optional<GltfDeformationRawValues> exact_min;
  std::optional<GltfDeformationRawValues> exact_max;
  GltfDeformationRawValues raw;
  GltfDeformationDerivedValues derived_state;
  std::span<const double> derived_values;
};
enum class GltfDeformationAccessorRole {
  kBaseAttribute = 0,
  kJoints = 1,
  kWeights = 2,
  kInverseBindMatrices = 3,
  kMorphDelta = 4,
  kAnimationTimes = 5,
  kTranslationValues = 6,
  kRotationValues = 7,
  kScaleValues = 8,
  kMorphWeightValues = 9
};
struct GltfDeformationAccessorUseView {
  std::uint64_t accessor;
  GltfDeformationAccessorRole role;
  std::string_view semantic;
  std::optional<std::uint64_t> mesh, primitive, target, set, skin;
  std::optional<std::uint64_t> animation, sampler, channel, node;
};
struct GltfInfluenceSetView {
  std::uint64_t mesh, primitive, set;
  std::uint64_t joints_accessor, weights_accessor;
};
struct GltfSkinView {
  std::uint64_t skin;
  std::span<const std::uint64_t> joints;
  std::optional<std::uint64_t> skeleton;
  std::optional<std::uint64_t> inverse_bind_accessor;
};
struct GltfSkinUserView {
  std::uint64_t node, mesh, primitive;
  std::optional<std::uint64_t> skin;
  bool instantiated_as_skinned;
  bool reachable_from_any_scene;
  std::span<const std::uint64_t> ancestor_nodes;
  std::span<const std::uint64_t> joint_ancestor_nodes;
};
struct GltfMorphChannelView {
  std::uint64_t mesh, primitive, target;
  std::string_view semantic;
  std::optional<std::uint64_t> set;
  std::uint64_t base_accessor, delta_accessor;
};
struct GltfMorphPrimitiveView {
  std::uint64_t mesh, primitive;
  bool targets_authored;
  std::uint64_t effective_target_count;
  std::span<const GltfMorphChannelView> channels;
};
enum class GltfMorphWeightOwner { kMesh = 0, kNode = 1 };
struct GltfMorphWeightDefaultView {
  GltfMorphWeightOwner owner_kind;
  std::uint64_t owner_index;
  bool authored;
  std::span<const GltfRawNumberView> authored_values;
};
enum class GltfDeformationAnimationPath {
  kTranslation = 0,
  kRotation = 1,
  kScale = 2,
  kWeights = 3
};
enum class GltfDeformationInterpolation {
  kLinear = 0,
  kStep = 1,
  kCubicSpline = 2
};
struct GltfDeformationAnimationView {
  std::uint64_t animation, channel, sampler;
  std::optional<std::uint64_t> node;
  GltfDeformationAnimationPath path;
  GltfDeformationInterpolation interpolation;
  std::uint64_t input_accessor, output_accessor, key_count;
  // Positive target count for node-bound scalar weight streams. Zero only for
  // node-absent weights: width is unbound/not applicable, never an inferred
  // mesh association or an authored zero-width stream. All source rows remain
  // retained; node is null and affects_selected_deformation is false.
  std::uint64_t values_per_key;
  bool affects_selected_deformation;
};
enum class GltfDeformationDestination {
  kCanonicalPositions = 0,
  kCanonicalVertexAttribute = 1,
  kTypedSidecar = 2
};
enum class GltfDeformationTransferEncoding {
  kExact = 0,
  kFloatPositionWidened = 1,
  kSignedIntegerWidened = 2,
  kRawScalarBits = 3
};
struct GltfDeformationAssociationView {
  std::uint64_t mesh, primitive, accessor;
  std::string_view semantic;
  std::string_view declaration_name;
  std::optional<std::uint64_t> target, set;
  GltfDeformationDestination destination;
  std::optional<geometry::index_t> canonical_attribute;
  GltfDeformationTransferEncoding encoding;
};
class GltfDeformationProof {
 public:
  GltfDeformationProof(const GltfDeformationProof&) noexcept;
  GltfDeformationProof& operator=(const GltfDeformationProof&) noexcept;
  ~GltfDeformationProof() noexcept;
  [[nodiscard]] const GltfAsset& Asset() const noexcept;
  [[nodiscard]] const GltfPrimitiveHandle& Primitive() const noexcept;
  [[nodiscard]] GltfAdmissionProfile AdmissionProfile() const noexcept;
  [[nodiscard]] GltfProcessingProfile ProcessingProfile() const noexcept;
  [[nodiscard]] GltfNormativeRevision NormativeRevision() const noexcept;
  [[nodiscard]] GltfDeformationNumericProfile NumericProfile() const noexcept;
  [[nodiscard]] std::string_view QuaternionPolicy() const noexcept;
  [[nodiscard]] GltfTriangleRequest Triangles() const noexcept;
  [[nodiscard]] GltfValueRequest Values() const noexcept;
  [[nodiscard]] const GltfDeformationPolicy& Policy() const noexcept;
  [[nodiscard]] std::span<const GltfDeformationAccessorView> Accessors()
      const noexcept;
  [[nodiscard]] std::span<const GltfDeformationAccessorUseView> AccessorUses()
      const noexcept;
  [[nodiscard]] std::span<const GltfInfluenceSetView> InfluenceSets()
      const noexcept;
  [[nodiscard]] std::span<const GltfSkinView> Skins() const noexcept;
  [[nodiscard]] std::span<const GltfSkinUserView> Users() const noexcept;
  [[nodiscard]] std::span<const GltfMorphPrimitiveView> MorphPrimitives()
      const noexcept;
  [[nodiscard]] std::span<const GltfMorphWeightDefaultView> MorphDefaults()
      const noexcept;
  [[nodiscard]] std::span<const GltfDeformationAnimationView> Animations()
      const noexcept;
  [[nodiscard]] const GltfWeightSumSummary& WeightSums() const noexcept;
  [[nodiscard]] std::span<const GltfDeformationRowLocalDeclaration>
  RowLocalDeclarations() const noexcept;
  [[nodiscard]] const GltfDeformationObservation& Facts() const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;
  [[nodiscard]] bool SharesExecutionBudget(
      const geometry::ExecutionContext&) const noexcept;

 private:
  friend struct gltf_deformation_detail::Access;
  explicit GltfDeformationProof(
      std::shared_ptr<const gltf_deformation_detail::State>) noexcept;
  std::shared_ptr<const gltf_deformation_detail::State> owner_;
};
}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_GLTF_DEFORMATION_H_
