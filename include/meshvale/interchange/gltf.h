// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_GLTF_H_
#define MESHVALE_INTERCHANGE_GLTF_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <variant>

#include "meshvale/geometry/editable_mesh.h"
#include "meshvale/geometry/execution.h"

namespace meshvale::interchange {
namespace gltf_detail {
struct AssetState;
struct ExtractionState;
struct Access;
}  // namespace gltf_detail

// Required bounds initialize to zero, an invalid caller configuration.
struct GltfReadLimits {
  std::uint64_t max_json_bytes = 0;
  std::uint64_t max_resource_bytes = 0;
  std::uint64_t max_owned_source_resource_bytes = 0;
  std::uint64_t max_document_nodes = 0;
  std::uint64_t max_document_depth = 0;
  std::uint64_t max_decoded_string_bytes = 0;
  std::uint64_t max_numeric_token_bytes = 0;
  std::uint64_t max_decimal_coefficient_digits = 0;
  std::uint64_t max_decimal_exponent_digits = 0;
  std::uint64_t max_table_rows = 0;
  std::uint64_t max_graph_edges = 0;
  std::uint64_t max_instances = 0;
  std::uint64_t max_accessor_elements = 0;
  std::uint64_t max_projection_bytes = 0;
};
struct GltfDirectTranslationLimits {
  std::uint64_t max_node_rows = 0;
  std::uint64_t max_mesh_users = 0;
  std::uint64_t max_channel_references = 0;
  std::uint64_t max_distinct_samplers = 0;
  std::uint64_t max_keys_per_sampler = 0;
  std::uint64_t max_total_keys = 0;
  std::uint64_t max_relationship_steps = 0;
  std::uint64_t max_metadata_bytes = 0;
  std::uint64_t max_decoded_bytes = 0;
  std::uint64_t max_new_requested_bytes = 0;
};
struct GltfDirectTranslationWorkPolicy {
  std::uint64_t parallel_min_keys = 0;
  std::uint64_t partition_keys = 0;
  std::uint64_t cancellation_block_keys = 0;
  std::uint64_t max_jobs = 0;
};
struct GltfExtractionLimits {
  std::uint64_t max_components = 0;
  std::uint64_t max_triangle_corners = 0;
  std::uint64_t max_output_requested_bytes = 0;
  GltfDirectTranslationLimits direct_translation = {};
};
struct GltfWorkPolicy {
  std::uint64_t parallel_min_resource_bytes = 0;
  std::uint64_t resource_partition_bytes = 0;
  std::uint64_t parallel_min_channel_components = 0;
  std::uint64_t channel_partition_elements = 0;
  GltfDirectTranslationWorkPolicy direct_translation = {};
};

enum class GltfResourceKind { kInvalid, kBuffer, kImage };
struct SuppliedGltfResource {
  GltfResourceKind kind = GltfResourceKind::kInvalid;
  std::uint64_t table_index = 0;
  std::span<const std::byte> bytes = {};
};
enum class GltfResourceOrigin { kCallerBinding, kDecodedDataUri, kBufferView };
struct GltfResourceView {
  GltfResourceKind kind;
  std::uint64_t table_index;
  GltfResourceOrigin origin;
  std::string_view authored_uri;
  std::span<const std::byte> bytes;
  std::uint64_t logical_bytes;
  std::optional<std::uint64_t> buffer_index;
  std::optional<std::uint64_t> buffer_view_index;
};

// Fixed inline first diagnostic. No allocating strings or emergency detail
// path.
enum class GltfStatus {
  kAccepted,
  kInvalidLimits,
  kInvalidRequest,
  kMalformed,
  kUnsupportedProfile,
  kResourceBindingMismatch,
  kCanceled,
  kBudgetExceeded,
  kInvalidSelection,
  kForeignOwner
};
enum class GltfDiagnosticCode {
  kNone,
  kZeroLimit,
  kLimitNotRepresentable,
  kInvalidRequest,
  kInvalidBindingKind,
  kUnsupportedResourceRepresentation,
  kEmbeddedUriSyntax,
  kEmbeddedMediaMismatch,
  kLimitExceeded,
  kSizeOverflow,
  kJsonSyntax,
  kUnicodeScalar,
  kDecodedDuplicateKey,
  kCoreFieldType,
  kCoreFieldRequired,
  kExactInteger,
  kCoreRange,
  kCoreArrayLength,
  kCoreReference,
  kGraphRelationship,
  kBinaryLayout,
  kBinaryValue,
  kIndexRole,
  kUnsupportedEnum,
  kUnsupportedNumberRepresentation,
  kUnsupportedCoreString,
  kRequiredExtension,
  kMissingResource,
  kDuplicateResource,
  kExtraResource,
  kResourceExtent,
  kCanceled,
  kPayloadBudget,
  kInvalidSelection,
  kForeignOwner
};
struct GltfSourceSpan {
  std::uint64_t offset;
  std::uint64_t length;
};
struct GltfDiagnostic {
  GltfStatus status;
  GltfDiagnosticCode code;
  std::optional<GltfSourceSpan> json_span;
  std::optional<GltfResourceKind> resource_kind;
  std::optional<std::uint64_t> resource_index;
  std::optional<GltfSourceSpan> resource_span;
  std::uint32_t rule_id;
};
struct GltfDirectTranslationExecutionObservation {
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t caller_jobs = 0;
};
struct GltfDirectTranslationObservation {
  GltfStatus status = GltfStatus::kAccepted;
  GltfDiagnostic diagnostic = {
      GltfStatus::kAccepted, GltfDiagnosticCode::kNone, {}, {}, {}, {}, 0};
  bool complete = false;
  std::uint64_t node_rows = 0;
  std::uint64_t mesh_users = 0;
  std::uint64_t scanned_channels = 0;
  std::uint64_t affected_channels = 0;
  std::uint64_t distinct_samplers = 0;
  std::uint64_t keys_checked = 0;
  std::uint64_t decoded_bytes_checked = 0;
  std::uint64_t relationship_steps = 0;
  std::uint64_t new_requested_bytes = 0;
  std::uint64_t effective_request_cap = 0;
  GltfDirectTranslationWorkPolicy work;
  GltfDirectTranslationExecutionObservation execution;
};
enum class GltfAdmissionProfile { kRetainedCoreMetadataV1 };
enum class GltfJsonNumberKind { kIntegerSpelling, kDecimalSpelling };
struct GltfRawNumberView {
  GltfSourceSpan source;
  GltfJsonNumberKind kind;
  std::string_view lexeme;
};
enum class GltfTransformKind { kDefaultTrs, kTrs, kMatrix };
// Slot and authored-mask owners for GltfNodeView::local_transform_bits.
struct GltfLocalTransformLayout {
  static constexpr std::size_t kSlotCount = 16;
  static constexpr std::size_t kMatrixDimension = 4;
  static constexpr std::size_t kTranslationOffset = 0;
  static constexpr std::size_t kRotationOffset = 3;
  static constexpr std::size_t kRotationWSlot = 6;
  static constexpr std::size_t kScaleOffset = 7;
  static constexpr std::size_t kScaleComponents = 3;
  static constexpr std::uint8_t kAuthoredTranslation = 1;
  static constexpr std::uint8_t kAuthoredRotation = 2;
  static constexpr std::uint8_t kAuthoredScale = 4;
  static constexpr std::uint8_t kAuthoredMatrix = 8;
};
struct GltfNodeView {
  std::uint64_t table_index;
  std::string_view name;
  std::optional<std::uint64_t> parent;
  std::span<const std::uint64_t> children;
  std::optional<std::uint64_t> mesh;
  std::optional<std::uint64_t> skin;
  GltfTransformKind transform_kind;
  // IEEE binary32 bit patterns. Matrix uses column-major slot column*4+row.
  // TRS uses translation xyz[0..2], rotation xyzw[3..6], scale xyz[7..9];
  // unused slots[10..15] are zero. Omitted TRS defaults are zero translation,
  // identity quaternion and unit scale, including kDefaultTrs nodes.
  std::array<std::uint32_t, GltfLocalTransformLayout::kSlotCount>
      local_transform_bits;
  // OR of the authored layout flags, independent of the populated defaults.
  // Matrix has only kAuthoredMatrix; an entirely omitted TRS has mask zero.
  std::uint8_t authored_transform_mask;
};
struct GltfSceneView {
  std::uint64_t table_index;
  std::string_view name;
  std::span<const std::uint64_t> roots;
};

class GltfPrimitiveHandle {
 public:
  GltfPrimitiveHandle(const GltfPrimitiveHandle&);
  GltfPrimitiveHandle& operator=(const GltfPrimitiveHandle&);
  ~GltfPrimitiveHandle();
  [[nodiscard]] std::uint64_t MeshIndex() const noexcept;
  [[nodiscard]] std::uint64_t PrimitiveIndex() const noexcept;

 private:
  friend struct gltf_detail::Access;
  GltfPrimitiveHandle(std::shared_ptr<const gltf_detail::AssetState>,
                      std::uint64_t mesh, std::uint64_t primitive);
  std::shared_ptr<const gltf_detail::AssetState> owner_;
  std::uint64_t mesh_;
  std::uint64_t primitive_;
};
class GltfInstanceHandle {
 public:
  GltfInstanceHandle(const GltfInstanceHandle&);
  GltfInstanceHandle& operator=(const GltfInstanceHandle&);
  ~GltfInstanceHandle();
  [[nodiscard]] std::uint64_t SceneIndex() const noexcept;
  [[nodiscard]] std::uint64_t NodeIndex() const noexcept;
  [[nodiscard]] GltfPrimitiveHandle Primitive() const;

 private:
  friend struct gltf_detail::Access;
  GltfInstanceHandle(std::shared_ptr<const gltf_detail::AssetState>,
                     std::uint64_t ordinal);
  std::shared_ptr<const gltf_detail::AssetState> owner_;
  std::uint64_t ordinal_;
};
enum class GltfProcessingProfile {
  kStaticLocalTrianglesV1 = 0,
  kDirectTranslationTrianglesV1 = 1
};
enum class GltfDirectTranslationInterpolation { kLinear = 0, kStep = 1 };
struct GltfDirectTranslationTrackView {
  std::uint64_t animation_index;
  std::uint64_t channel_index;
  std::uint64_t sampler_index;
  std::uint64_t node_index;
  std::uint64_t input_accessor;
  std::uint64_t output_accessor;
  std::uint64_t key_count;
  GltfDirectTranslationInterpolation interpolation;
};
struct GltfDirectTranslationProofView {
  static constexpr std::uint8_t kMeshUser = 1;
  static constexpr std::uint8_t kStrictAncestor = 2;
  std::span<const std::uint64_t> mesh_users;
  std::span<const std::uint8_t> node_membership;
  std::span<const GltfDirectTranslationTrackView> tracks;
};
enum class GltfCapabilityReason {
  kReady,
  kBinaryAdmissionPending,
  kPrimitiveMode,
  kSkinBinding,
  kJointWeightChannels,
  kMorphBinding,
  kAnimationBinding,
  kOptionalExtensionEffect,
  kUnknownCoreRole,
  kUnsupportedEnum,
  kInvalidSelection,
  kForeignOwner
};
struct GltfPrimitiveCapability {
  GltfProcessingProfile profile;
  GltfCapabilityReason reason;
};
class GltfAsset {
 public:
  GltfAsset(const GltfAsset&);
  GltfAsset& operator=(const GltfAsset&);
  ~GltfAsset();
  // True only when this asset's retained execution context and context share
  // one worker/payload ledger. Moved-from contexts yield false. No allocation,
  // lock, operation-counter equality or raw ledger identity is exposed.
  // Concurrent copies/queries are allowed; mutation of either object requires
  // caller synchronization.
  [[nodiscard]] bool SharesExecutionBudget(
      const geometry::ExecutionContext& context) const noexcept;
  [[nodiscard]] GltfAdmissionProfile AdmissionProfile() const noexcept;
  [[nodiscard]] const GltfDirectTranslationWorkPolicy& DirectTranslationWork()
      const noexcept;
  [[nodiscard]] std::span<const std::byte> OriginalJson() const noexcept;
  [[nodiscard]] std::span<const GltfRawNumberView> RawNumbers() const noexcept;
  [[nodiscard]] std::span<const GltfResourceView> Resources() const noexcept;
  [[nodiscard]] std::span<const GltfNodeView> Nodes() const noexcept;
  [[nodiscard]] std::span<const GltfSceneView> Scenes() const noexcept;
  [[nodiscard]] std::uint64_t PrimitiveCount() const noexcept;
  [[nodiscard]] std::uint64_t InstanceCount() const noexcept;
  // Draft workflow-support accessor under qualification. Current live fresh
  // capped-read requests; legacy ReadGltfAsset owners report nullopt, not zero.
  [[nodiscard]] std::optional<std::uint64_t> NewRequestedBytes() const noexcept;
  [[nodiscard]] std::optional<GltfPrimitiveHandle> Primitive(
      std::uint64_t ordinal) const;
  [[nodiscard]] std::optional<GltfInstanceHandle> Instance(
      std::uint64_t ordinal) const;
  [[nodiscard]] GltfPrimitiveCapability Capability(
      const GltfPrimitiveHandle& primitive) const;

 private:
  friend struct gltf_detail::Access;
  explicit GltfAsset(std::shared_ptr<const gltf_detail::AssetState>);
  std::shared_ptr<const gltf_detail::AssetState> owner_;
};
struct GltfReadResult {
  std::optional<GltfAsset> asset;
  GltfDiagnostic diagnostic;
};
// Caller spans remain alive and unmodified until this synchronous call returns.
// All worker threads join before return; the result owns captured bytes.
[[nodiscard]] GltfReadResult ReadGltfAsset(
    std::span<const std::byte> json,
    std::span<const SuppliedGltfResource> supplied_resources,
    const GltfReadLimits& limits, const GltfWorkPolicy& work_policy,
    const geometry::ExecutionContext& context, std::stop_token stop);

// Draft capped-reader implementation under qualification.
// Existing source-admission limits remain unchanged. The additional positive
// cap covers every fresh read request, including retained and temporary blocks.
struct GltfReadOperationLimits {
  GltfReadLimits source;
  std::uint64_t max_new_requested_bytes = 0;
};
// Same source admission/result semantics as ReadGltfAsset; unlike that legacy
// entrypoint, this operation has a complete fresh-request cap and live counter.
[[nodiscard]] GltfReadResult ReadGltfAssetCapped(
    std::span<const std::byte> json,
    std::span<const SuppliedGltfResource> supplied_resources,
    const GltfReadOperationLimits& limits, const GltfWorkPolicy& work_policy,
    const geometry::ExecutionContext& context, std::stop_token stop = {});

enum class GltfComponentType {
  kInt8,
  kUint8,
  kInt16,
  kUint16,
  kUint32,
  kFloat32
};
enum class GltfAccessorShape {
  kScalar,
  kVec2,
  kVec3,
  kVec4,
  kMat2,
  kMat3,
  kMat4
};
using GltfRawValues =
    std::variant<std::span<const std::int8_t>, std::span<const std::uint8_t>,
                 std::span<const std::int16_t>, std::span<const std::uint16_t>,
                 std::span<const std::uint32_t>, std::span<const float>>;
struct GltfSparseProvenance {
  std::uint64_t indices_buffer_view;
  std::uint64_t indices_byte_offset;
  GltfComponentType indices_component_type;
  std::uint64_t values_buffer_view;
  std::uint64_t values_byte_offset;
};
struct GltfChannelView {
  std::string_view semantic;
  std::optional<std::uint64_t> set_index;
  std::uint64_t accessor_index;
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
  std::optional<GltfRawValues> exact_min;
  std::optional<GltfRawValues> exact_max;
  GltfRawValues raw;
  std::span<const double> normalized_values;
};
struct GltfSourceCorner {
  std::uint64_t primitive_stream_corner;
  std::uint64_t accessor_row;
};
enum class GltfTriangleRequest {
  kInvalid,
  kRequireTriangles,
  kConvertStripOrFan
};
enum class GltfValueRequest { kInvalid, kRaw, kRawAndNormalized };
struct GltfExtractionRequest {
  GltfTriangleRequest triangles = GltfTriangleRequest::kInvalid;
  GltfValueRequest values = GltfValueRequest::kInvalid;
  GltfProcessingProfile processing_profile =
      GltfProcessingProfile::kStaticLocalTrianglesV1;
};
class GltfPrimitiveExtraction {
 public:
  GltfPrimitiveExtraction(const GltfPrimitiveExtraction&);
  GltfPrimitiveExtraction& operator=(const GltfPrimitiveExtraction&);
  ~GltfPrimitiveExtraction();
  [[nodiscard]] std::uint64_t SourceMode() const noexcept;
  [[nodiscard]] GltfProcessingProfile ProcessingProfile() const noexcept;
  [[nodiscard]] std::optional<GltfDirectTranslationProofView>
  DirectTranslation() const noexcept;
  [[nodiscard]] const GltfDirectTranslationObservation* DirectTranslationFacts()
      const noexcept;
  // Draft workflow-support accessor under qualification. Current live fresh
  // extraction requests under the existing mandatory extraction cap.
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;
  [[nodiscard]] std::span<const std::uint32_t> TriangleIndices() const noexcept;
  [[nodiscard]] std::span<const GltfSourceCorner> SourceCorners()
      const noexcept;
  [[nodiscard]] std::span<const GltfChannelView> Channels() const noexcept;
  [[nodiscard]] std::optional<GltfChannelView> SourceIndices() const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> MaterialIndex() const noexcept;

 private:
  friend struct gltf_detail::Access;
  explicit GltfPrimitiveExtraction(
      std::shared_ptr<const gltf_detail::ExtractionState>);
  std::shared_ptr<const gltf_detail::ExtractionState> owner_;
};
struct GltfExtractionResult {
  std::optional<GltfPrimitiveExtraction> extraction;
  GltfDiagnostic diagnostic;
  std::optional<GltfDirectTranslationObservation> direct_translation = {};
};
[[nodiscard]] GltfExtractionResult ExtractGltfPrimitive(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
    const GltfExtractionRequest& request, const GltfExtractionLimits& limits,
    std::stop_token stop);

// Draft fully accounted extraction implementation under qualification.
// Same binary admission/decoder/result as ExtractGltfPrimitive. The existing
// max_output_requested_bytes also covers every explicit native callable,
// handle and exception record; legacy stdthread runtime exclusions stay legacy.
// Uses the work policy and execution ledger retained by the source asset.
[[nodiscard]] GltfExtractionResult ExtractGltfPrimitiveCapped(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
    const GltfExtractionRequest& request, const GltfExtractionLimits& limits,
    std::stop_token stop = {});

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_GLTF_H_
