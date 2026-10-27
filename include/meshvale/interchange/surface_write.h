// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SURFACE_WRITE_H_
#define MESHVALE_INTERCHANGE_SURFACE_WRITE_H_

#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/stl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <variant>

namespace meshvale::interchange {
inline constexpr std::string_view kSurfaceBundleSchema =
    "meshvale.surface-bundle/1";
inline constexpr std::size_t kSurfaceDigestBytes = 32;
inline constexpr std::size_t kSurfaceCoreFactorComponents = 6;
// Covers an exact difference of two finite IEEE binary64 values: at most
// 2099 magnitude bits. Not a predicate-limb/allocation ceiling.
inline constexpr std::size_t kSurfaceErrorMagnitudeWords = 34;
using SurfaceDigest = std::array<std::byte, kSurfaceDigestBytes>;

enum class SurfaceEncoding : std::uint32_t {
  kInvalid = 0,
  kGltfJson = 1,
  kGlb = 2,
  kStlAscii = 3,
  kStlBinary = 4
};
enum class SurfaceRepresentation : std::uint32_t {
  kInvalid = 0,
  kStrictBare = 1,
  kAnnotatedBundleV1 = 2
};
enum class SurfaceStatus : std::uint32_t {
  kSuccess = 0,
  kInvalidConfiguration = 1,
  kInvalidStorage = 2,
  kUnsupported = 3,
  kLimitExceeded = 4,
  kBudgetExceeded = 5,
  kCanceled = 6,
  kDelegateFailure = 7,
  kVerificationFailed = 8
};
enum class SurfaceStage : std::uint32_t {
  kConfiguration = 0,
  kSource = 1,
  kCorrespondence = 2,
  kDisposition = 3,
  kCatalog = 4,
  kPlan = 5,
  kNumeric = 6,
  kEncode = 7,
  kCapture = 8,
  kContainerRead = 9,
  kAssetRead = 10,
  kPrimitiveRead = 11,
  kCapsuleRead = 12,
  kCompare = 13,
  kComplete = 14
};
enum class SurfaceCode : std::uint32_t {
  kNone = 0,
  kRequiredFieldZero = 1,
  kConfigurationExtent = 2,
  kUnknownEnum = 3,
  kEncodingLimitsMismatch = 4,
  kInvalidBound = 5,
  kAllowanceTooSmall = 6,
  kForeignLedger = 7,
  kInvalidSourceStorage = 8,
  kSourceCount = 9,
  kTriangleCount = 10,
  kSourceCorrespondence = 11,
  kChannelCoverage = 12,
  kChannelRole = 13,
  kChannelShape = 14,
  kChannelMetadata = 15,
  kBareUnrepresented = 16,
  kLossNotAcknowledged = 17,
  kUvMap = 18,
  kUvReference = 19,
  kNormalAbsent = 20,
  kNormalMissing = 21,
  kNormalNotConstant = 22,
  kNonfinite = 23,
  kMaterialIds = 24,
  kMaterialBinding = 25,
  kCatalogField = 26,
  kCatalogExtension = 27,
  kResourceBinding = 28,
  kResourceMedia = 29,
  kCountLimit = 30,
  kDescriptorLimit = 31,
  kOutputLimit = 32,
  kMapLimit = 33,
  kSizeOverflow = 34,
  kNumericRange = 35,
  kNonzeroUnderflow = 36,
  kExactness = 37,
  kErrorBound = 38,
  kNewCoincidence = 39,
  kCollapsedEdge = 40,
  kCollapsedTriangle = 41,
  kOrientation = 42,
  kRequestBudget = 43,
  kStopRequested = 44,
  kGeometryDelegate = 45,
  kStlDelegate = 46,
  kGlbDelegate = 47,
  kGltfDelegate = 48,
  kMemberInventory = 49,
  kMemberPath = 50,
  kMemberDigest = 51,
  kJsonSyntax = 52,
  kSchema = 53,
  kBinarySlice = 54,
  kCapsuleSource = 55,
  kWireShape = 56,
  kWireValue = 57,
  kWireMap = 58,
  kMaterialReference = 59,
  kResourceBytes = 60,
  kUnits = 61,
  kNumericProfile = 62
};
enum class SurfaceSubject : std::uint32_t {
  kNone = 0,
  kVertex = 1,
  kFace = 2,
  kCorner = 3,
  kAttribute = 4,
  kScalar = 5,
  kMember = 6,
  kMaterial = 7,
  kTexture = 8,
  kImage = 9,
  kSampler = 10,
  kPrimitive = 11,
  kUvSet = 12
};
struct SurfaceDiagnostic {
  SurfaceStage stage = SurfaceStage::kConfiguration;
  SurfaceCode code = SurfaceCode::kNone;
  SurfaceSubject subject = SurfaceSubject::kNone;
  std::optional<std::uint64_t> index;
  std::optional<std::uint64_t> secondary_index;
  std::optional<std::uint32_t> component;
};
enum class SurfaceScalar : std::uint32_t {
  kFloat32 = 0,
  kFloat64 = 1,
  kInt32 = 2,
  kUint8 = 3,
  kUint16 = 4,
  kUint32 = 5,
  kUint64 = 6
};
enum class SurfaceChannelRole : std::uint32_t {
  kInvalid = 0,
  kBundleOnly = 1,
  kUv = 2,
  kStlCornerNormal = 3,
  kMaterialId = 4
};
struct SurfaceChannelRule {
  std::uint64_t source_attribute = 0;
  SurfaceChannelRole role = SurfaceChannelRole::kInvalid;
  bool acknowledge_ordinary_view_loss = false;
};
struct SurfaceUvBinding {
  std::uint64_t source_attribute = 0;
  std::uint32_t source_set = 0;
  std::uint32_t wire_set = 0;
};
struct SurfaceMaterialBinding {
  std::int32_t source_id = 0;
  std::uint64_t catalog_material = 0;
};
struct SurfaceCatalogUvBinding {
  std::uint32_t catalog_set = 0;
  std::uint32_t source_set = 0;
};
enum class SurfaceNarrowing : std::uint32_t {
  kInvalid = 0,
  kExact32 = 1,
  kBoundedRne32 = 2,
  kExact64 = 3
};
struct SurfaceNumericPolicy {
  SurfaceNarrowing positions = SurfaceNarrowing::kInvalid;
  double max_position_linf_error = 0;
  // One explicit bound for every UV binding, in identical order; empty for STL.
  std::span<const double> max_uv_component_errors;
};
enum class SurfaceCoordinates : std::uint32_t {
  kInvalid = 0,
  kDeclaredMetersRightHandedYUp = 1,
  kStlUnitsUnspecified = 2
};
struct GltfSurfaceOptions {
  SurfaceEncoding encoding = SurfaceEncoding::kInvalid;
  SurfaceCoordinates coordinates = SurfaceCoordinates::kInvalid;
  std::span<const SurfaceUvBinding> uv;
  const GltfAsset* catalog = nullptr;
  std::span<const SurfaceMaterialBinding> materials;
  std::span<const SurfaceCatalogUvBinding> catalog_uv;
};
struct StlSurfaceOptions {
  SurfaceEncoding encoding = SurfaceEncoding::kInvalid;
  SurfaceCoordinates coordinates = SurfaceCoordinates::kInvalid;
  std::string_view declared_unit;
  std::optional<std::uint64_t> corner_normal_attribute;
  std::span<const std::byte> binary_header;
  std::string_view ascii_name;
};
struct SurfaceEncodeRequest {
  const geometry::MeshInspection& original;
  const geometry::ChargedTriangulation& triangles;
  SurfaceRepresentation representation = SurfaceRepresentation::kInvalid;
  bool acknowledge_triangle_conversion = false;
  bool acknowledge_corner_expansion = false;
  bool acknowledge_unused_vertices = false;
  std::span<const SurfaceChannelRule> channels;
  SurfaceNumericPolicy numeric;
  std::variant<std::monostate, GltfSurfaceOptions, StlSurfaceOptions>
      destination;
};

// All count/byte/work values below are positive, inclusive and representable
// by uint64_t, size_t, ptrdiff_t and any delegated compiled host extent.
struct SurfaceCountLimits {
  std::uint64_t max_vertices = 0;
  std::uint64_t max_faces = 0;
  std::uint64_t max_corners = 0;
  std::uint64_t max_face_offsets = 0;
  std::uint64_t max_attributes = 0;
  std::uint64_t max_attribute_scalars = 0;
  std::uint64_t max_attribute_bytes = 0;
  std::uint64_t max_attribute_offsets = 0;
  std::uint64_t max_presence_bytes = 0;
  std::uint64_t max_metadata_entries = 0;
  std::uint64_t max_descriptor_bytes = 0;
  std::uint64_t max_wire_primitives = 0;
  std::uint64_t max_wire_vertices = 0;
  std::uint64_t max_wire_corners = 0;
  std::uint64_t max_map_entries = 0;
  std::uint64_t max_members = 0;
  std::uint64_t max_member_bytes = 0;
  std::uint64_t max_total_member_bytes = 0;
  std::uint64_t max_resources = 0;
  std::uint64_t max_resource_bytes = 0;
  std::uint64_t max_json_bytes = 0;
  std::uint64_t max_binary_bytes = 0;
  std::uint64_t max_capsule_json_bytes = 0;
  std::uint64_t max_capsule_binary_bytes = 0;
  std::uint64_t max_material_bindings = 0;
  std::uint64_t max_numeric_token_bytes = 0;
  std::uint64_t max_json_nodes = 0;
  std::uint64_t max_json_depth = 0;
  std::uint64_t max_numeric_integer_bits = 0;
  std::uint64_t max_coincidence_comparisons = 0;
  std::uint64_t max_relationship_steps = 0;
};
struct GltfSurfaceEncodeLimits {
  // Active only for kGlb; JSON requires the canonical all-zero unused object.
  GlbLimits container;
};
struct StlSurfaceEncodeLimits {
  geometry::MeshMaterializationLimits derived_mesh;
  StlWriteLimits codec;
};
struct SurfaceEncodeLimits {
  SurfaceCountLimits counts;
  std::uint64_t max_new_requested_bytes = 0;
  std::uint64_t max_total_requested_allowance = 0;
  std::variant<std::monostate, GltfSurfaceEncodeLimits, StlSurfaceEncodeLimits>
      delegates;
};
struct GltfSurfaceInspectLimits {
  GlbLimits container;
  GltfReadOperationLimits read;
  GltfExtractionLimits extraction;
};
struct StlSurfaceInspectLimits {
  StlReadLimits codec;
};
struct SurfaceInspectLimits {
  SurfaceCountLimits counts;
  std::uint64_t max_new_requested_bytes = 0;
  std::uint64_t max_total_requested_allowance = 0;
  std::variant<std::monostate, GltfSurfaceInspectLimits,
               StlSurfaceInspectLimits>
      delegates;
};
struct SurfaceWorkPolicy {
  geometry::MeshMaterializationWorkPolicy surface;
  GlbWorkPolicy container;
  GltfWorkPolicy gltf;
  StlWorkPolicy stl;
};
struct SurfaceMeshCounts {
  std::uint64_t vertices = 0, faces = 0, corners = 0, face_offsets = 0;
  std::uint64_t attributes = 0, attribute_scalars = 0, attribute_bytes = 0;
  std::uint64_t attribute_offsets = 0, presence_bytes = 0;
  std::uint64_t metadata_entries = 0, descriptor_bytes = 0;
};
struct SurfaceWireCounts {
  std::uint64_t primitives = 0, vertices = 0, triangles = 0, corners = 0;
  std::uint64_t map_entries = 0, materials = 0, textures = 0, images = 0;
  std::uint64_t samplers = 0, members = 0, member_bytes = 0;
};
struct SurfaceWorkCounts {
  std::uint64_t source_scalar_pairs = 0, descriptor_byte_pairs = 0;
  std::uint64_t coincidence_comparisons = 0, relationship_steps = 0;
};
// Nonnegative exact dyadic; zero has used_words=0 and exponent2=0.
// Otherwise highest used word nonzero and low word odd; remaining words zero.
struct SurfaceDyadicError {
  std::array<std::uint64_t, kSurfaceErrorMagnitudeWords> magnitude{};
  std::uint32_t used_words = 0;
  std::int32_t exponent2 = 0;
};
// Latest reached observation per field. All kinds count unique referenced
// source components; DecodedWire retires a group only after every actual copy
// passed. Raw repeated paired checks remain in ExaminedWork().
struct SurfaceNumericSummary {
  enum class Kind : std::uint32_t { kPreparedFields = 0, kDecodedWire = 1 };
  Kind kind = Kind::kPreparedFields;
  bool complete = false;
  std::uint64_t examined_values = 0, changed_values = 0;
  SurfaceDyadicError max_absolute_error;
  std::optional<std::uint64_t> source_attribute;
  std::optional<std::uint64_t> source_row;
  std::optional<std::uint32_t> component;
};
enum class SurfaceMemberKind : std::uint32_t {
  kGltf = 0,
  kGlb = 1,
  kStl = 2,
  kGeometryBuffer = 3,
  kImage = 4,
  kCapsuleJson = 5,
  kCapsuleBinary = 6
};
struct SurfaceMemberView {
  std::string_view path;
  SurfaceMemberKind kind;
  std::span<const std::byte> bytes;
  SurfaceDigest sha256;
};
struct SuppliedSurfaceMember {
  std::string_view path;
  std::span<const std::byte> bytes;
};
struct SurfacePrimitiveView {
  std::uint64_t ordinal, first_wire_vertex, wire_vertices;
  std::uint64_t first_wire_triangle, wire_triangles;
  std::optional<std::uint64_t> material;
};
struct SurfaceUvView {
  std::uint64_t source_attribute;
  std::uint32_t source_set, wire_set;
};
struct SurfaceGeometryFacts {
  geometry::MaterializationStatus status;
  geometry::MaterializationDiagnostic diagnostic;
  geometry::MaterializationExecutionStats execution;
  std::optional<std::uint64_t> requested_bytes_at_return;
};
struct SurfaceGlbFacts {
  GlbStatus status;
  std::uint64_t error_byte_offset;
  std::uint64_t workers_used;
};
struct SurfaceGltfFacts {
  GltfDiagnostic diagnostic;
  std::optional<std::uint64_t> requested_bytes_at_return;
};
struct SurfaceStlFacts {
  StlStatus status;
  StlDiagnostic diagnostic;
  StlExecution execution;
  std::optional<StlRequestedBytes> requested_bytes_at_return;
  std::optional<StlWriteFacts> write_facts;
};
enum class SurfaceDelegateProfileStatus : std::uint32_t {
  kEstablishedBaseline = 0,
  kUnsupportedRounding = 1,
  kUnsupportedFlushState = 2,
  kUnqueryable = 3
};
struct SurfaceDelegateProfileFacts {
  SurfaceDelegateProfileStatus status =
      SurfaceDelegateProfileStatus::kUnqueryable;
  std::optional<bool> nearest_even;
  std::optional<bool> ftz_disabled;
  std::optional<bool> daz_disabled;
};
enum class SurfaceDelegateKind : std::uint32_t {
  kDerivedMaterialization = 0,
  kStlWrite = 1,
  kGlbWrite = 2,
  kGlbRead = 3,
  kGltfRead = 4,
  kGltfExtract = 5,
  kStlRead = 6,
  kSourceMaterialFactors = 7,
  kWireMaterialFactors = 8
};
// call_ordinal is a globally unique zero-based reached logical invocation
// within one Encode/Inspect, shared with DelegateView (never a job/thread ID).
// Nonfactor profiles have factor_component=null; factors use 0..5 in fixed
// RGBA/metallic/roughness order and share their actually entered collector's
// ordinal. Their primitive is null; material identity comes from that
// collector.
struct SurfaceDelegateProfileView {
  SurfaceStage stage;
  SurfaceDelegateKind kind;
  std::uint64_t call_ordinal;
  std::optional<std::uint64_t> primitive;
  std::optional<std::uint32_t> factor_component;
  SurfaceDelegateProfileFacts facts;
};
struct SurfaceCoreFactorFacts {
  std::uint64_t material;
  // baseColor RGBA, metallic, roughness: actual core binary32 words.
  std::array<std::uint32_t, kSurfaceCoreFactorComponents> core_f32;
  std::array<bool, kSurfaceCoreFactorComponents> authored;
  std::array<bool, kSurfaceCoreFactorComponents> observed;
  std::uint64_t lexical_calls;
  bool complete = false;
  std::optional<GltfDiagnostic> failure;
};
struct SurfaceMaterialFactorView {
  std::uint64_t source_material, wire_material;
  std::array<std::uint32_t, kSurfaceCoreFactorComponents> source_core_f32,
      wire_core_f32;
};
// At most one record per operation-local ordinal: an actual native invocation
// or actually entered factor collector, including zero-lexical/default/partial
// collectors. A nonfactor profile-only refusal has no invented DelegateView.
// GltfExtract primitive is its global actual wire primitive ordinal; factors
// never overload primitive with a material index. See docs/surface-output.md
// for joins.
struct SurfaceDelegateView {
  SurfaceStage stage;
  SurfaceDelegateKind kind;
  std::uint64_t call_ordinal;
  std::optional<std::uint64_t> primitive;
  std::variant<SurfaceGeometryFacts, SurfaceGlbFacts, SurfaceGltfFacts,
               SurfaceStlFacts, SurfaceCoreFactorFacts>
      facts;
};
namespace surface_detail {
struct Access;
struct OutputState;
struct InspectionState;
struct FactsState;
}  // namespace surface_detail
class SurfaceFacts {
 public:
  SurfaceFacts(const SurfaceFacts&) noexcept;
  SurfaceFacts& operator=(const SurfaceFacts&) noexcept;
  ~SurfaceFacts() noexcept;
  [[nodiscard]] std::optional<SurfaceMeshCounts> OriginalCounts()
      const noexcept;
  [[nodiscard]] std::optional<SurfaceMeshCounts> TriangleCounts()
      const noexcept;
  [[nodiscard]] std::optional<SurfaceWireCounts> WireCounts() const noexcept;
  [[nodiscard]] std::span<const SurfaceDelegateView> Delegates() const noexcept;
  [[nodiscard]] std::optional<SurfaceNumericSummary> PositionNumeric()
      const noexcept;
  [[nodiscard]] std::span<const SurfaceNumericSummary> UvNumeric()
      const noexcept;
  [[nodiscard]] geometry::MaterializationExecutionStats Execution()
      const noexcept;
  [[nodiscard]] SurfaceWorkCounts ExaminedWork() const noexcept;
  [[nodiscard]] std::span<const SurfaceDelegateProfileView>
  NumericDelegateProfiles() const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct surface_detail::Access;
  explicit SurfaceFacts(
      std::shared_ptr<const surface_detail::FactsState>) noexcept;
  std::shared_ptr<const surface_detail::FactsState> state_;
};
class SurfaceOutput {
 public:
  SurfaceOutput(const SurfaceOutput&) noexcept;
  SurfaceOutput& operator=(const SurfaceOutput&) noexcept;
  ~SurfaceOutput() noexcept;
  [[nodiscard]] SurfaceEncoding Encoding() const noexcept;
  [[nodiscard]] SurfaceRepresentation Representation() const noexcept;
  [[nodiscard]] std::span<const SurfaceMemberView> Members() const noexcept;
  [[nodiscard]] std::span<const SurfacePrimitiveView> Primitives()
      const noexcept;
  [[nodiscard]] std::span<const SurfaceUvView> UvMapping() const noexcept;
  [[nodiscard]] std::span<const SurfaceMaterialFactorView> MaterialFactors()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> WireTriangleFaces()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> WireTriangleSources()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> WireCornerCorners()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> WireCornerSources()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> WireVertexSources()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> SourceVertexOffsets()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> SourceVertexWireMembers()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> SourceFaceOffsets()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> SourceFaceWireTriangles()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> SourceCornerOffsets()
      const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> SourceCornerWireCorners()
      const noexcept;
  [[nodiscard]] const geometry::MeshInspection& Original() const noexcept;
  [[nodiscard]] const geometry::ChargedTriangulation& Triangles()
      const noexcept;
  [[nodiscard]] SurfaceFacts Facts() const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct surface_detail::Access;
  explicit SurfaceOutput(
      std::shared_ptr<const surface_detail::OutputState>) noexcept;
  std::shared_ptr<const surface_detail::OutputState> state_;
};
class SurfaceOutputInspection {
 public:
  SurfaceOutputInspection(const SurfaceOutputInspection&) noexcept;
  SurfaceOutputInspection& operator=(const SurfaceOutputInspection&) noexcept;
  ~SurfaceOutputInspection() noexcept;
  [[nodiscard]] const SurfaceOutput& Expected() const noexcept;
  [[nodiscard]] std::span<const SurfaceMemberView> CapturedMembers()
      const noexcept;
  [[nodiscard]] SurfaceFacts Facts() const noexcept;
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct surface_detail::Access;
  explicit SurfaceOutputInspection(
      std::shared_ptr<const surface_detail::InspectionState>) noexcept;
  std::shared_ptr<const surface_detail::InspectionState> state_;
};
struct SurfaceEncodeResult {
  SurfaceStatus status = SurfaceStatus::kInvalidConfiguration;
  SurfaceDiagnostic diagnostic;
  std::optional<SurfaceOutput> output;
  std::optional<SurfaceFacts> facts;
};
struct SurfaceInspectionResult {
  SurfaceStatus status = SurfaceStatus::kInvalidConfiguration;
  SurfaceDiagnostic diagnostic;
  std::optional<SurfaceOutputInspection> inspection;
  std::optional<SurfaceFacts> facts;
};
// All spans/options and visible input handles remain alive/unassigned through
// return/all joins. Copies of returned handles allocate nothing. Distinct live
// copies permit concurrent reads; assignment/destruction cannot race access to
// the same visible handle or its borrowed views. No public default/move-empty
// owner or constructor from caller-supplied maps/proof flags exists.
[[nodiscard]] SurfaceEncodeResult EncodeSurface(
    const SurfaceEncodeRequest& request, const SurfaceEncodeLimits& limits,
    const SurfaceWorkPolicy& work, std::stop_token stop = {});
[[nodiscard]] SurfaceInspectionResult InspectSurfaceOutput(
    const SurfaceOutput& expected,
    std::span<const SuppliedSurfaceMember> actual_members,
    const SurfaceInspectLimits& limits, const SurfaceWorkPolicy& work,
    std::stop_token stop = {});
enum class SurfaceNumericProfile : std::uint32_t {
  kUnsupportedStorage = 0,
  kIeeeBinary32Binary64 = 1
};
[[nodiscard]] SurfaceNumericProfile SurfaceNumericSupport() noexcept;
}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_SURFACE_WRITE_H_
