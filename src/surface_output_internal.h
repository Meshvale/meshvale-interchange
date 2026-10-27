// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_SURFACE_OUTPUT_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_SURFACE_OUTPUT_INTERNAL_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/surface_write.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_json_snapshot.h"
#include "gltf_operation_workers.h"
#include "surface_numeric.h"

namespace meshvale::interchange::surface_detail {
inline constexpr std::size_t kNoOrdinal = static_cast<std::size_t>(-1);
inline constexpr std::size_t kMinimumParallelWorkers = 2;
inline constexpr std::uint64_t kUnsigned32WireMaximum = UINT64_C(0xfffffffe);
inline constexpr std::string_view kMainGltfPath = "surface.gltf";
inline constexpr std::string_view kMainGlbPath = "surface.glb";
inline constexpr std::string_view kMainStlPath = "surface.stl";
inline constexpr std::string_view kGeometryBufferPath = "geometry.bin";
inline constexpr std::string_view kCapsuleJsonPath = "surface.json";
inline constexpr std::string_view kCapsuleBinaryPath = "surface.bin";

class Failure final : public std::exception {
 public:
  Failure(SurfaceStatus status, SurfaceDiagnostic diagnostic) noexcept;
  [[nodiscard]] SurfaceStatus Status() const noexcept;
  [[nodiscard]] const SurfaceDiagnostic& Diagnostic() const noexcept;
  [[nodiscard]] const char* what() const noexcept override;

 private:
  SurfaceStatus status_;
  SurfaceDiagnostic diagnostic_;
};
[[noreturn]] void Fail(SurfaceStatus status, SurfaceStage stage,
                       SurfaceCode code,
                       SurfaceSubject subject = SurfaceSubject::kNone,
                       std::optional<std::uint64_t> index = {},
                       std::optional<std::uint64_t> secondary = {},
                       std::optional<std::uint32_t> component = {});
[[nodiscard]] std::size_t Size(std::uint64_t value);
[[nodiscard]] std::uint64_t AddCount(std::uint64_t a, std::uint64_t b);
[[nodiscard]] std::uint64_t Product(std::uint64_t a, std::uint64_t b);

// Surface reports actual root or operation-cap refusal as RequestBudget,
// without attributing competing root consumers. Untagged size arithmetic stays
// overflow.
[[nodiscard]] inline gltf_detail::AdmissionCode SurfaceAdmissionCode(
    const gltf_detail::AdmissionFailure& failure) noexcept {
  const auto code = failure.Code();
  return code == gltf_detail::AdmissionCode::kSizeLimit &&
                 failure.Provenance() ==
                     gltf_detail::AdmissionProvenance::kOperationRequestLimit
             ? gltf_detail::AdmissionCode::kBudgetExceeded
             : code;
}

struct FactsState {
  explicit FactsState(const gltf_detail::AllocationContext& allocation);
  gltf_detail::AllocationContext allocation;
  std::optional<SurfaceMeshCounts> original;
  std::optional<SurfaceMeshCounts> triangles;
  std::optional<SurfaceWireCounts> wire;
  Vector<SurfaceDelegateView> delegates;
  Vector<SurfaceDelegateProfileView> profiles;
  std::optional<SurfaceNumericSummary> positions;
  Vector<SurfaceNumericSummary> uv;
  geometry::MaterializationExecutionStats execution;
  SurfaceWorkCounts work;
};
struct Member {
  explicit Member(const gltf_detail::AllocationContext& allocation);
  String path;
  SurfaceMemberKind kind = SurfaceMemberKind::kGltf;
  Vector<std::byte> bytes;
  SurfaceDigest digest{};
};
struct UvPlan {
  UvPlan(const gltf_detail::AllocationContext& allocation,
         SurfaceUvBinding binding);
  SurfaceUvBinding binding;
  Vector<std::uint32_t> prepared;
  Vector<std::uint64_t> source_rows;
};
struct Primitive {
  explicit Primitive(const gltf_detail::AllocationContext& allocation);
  SurfacePrimitiveView view{};
  Vector<std::uint64_t> triangles;
  std::optional<std::uint64_t> position_accessor;
  std::optional<std::uint64_t> index_accessor;
  Vector<std::uint64_t> uv_accessors;
};
struct CatalogDefinition {
  explicit CatalogDefinition(const gltf_detail::AllocationContext& allocation);
  String kind;
  std::uint64_t source = 0;
  std::optional<std::uint64_t> wire;
  String raw_json;
  std::optional<std::array<std::uint32_t, kSurfaceCoreFactorComponents>>
      source_factors;
  std::optional<std::array<std::uint32_t, kSurfaceCoreFactorComponents>>
      wire_factors;
};
struct CatalogImage {
  explicit CatalogImage(const gltf_detail::AllocationContext& allocation);
  std::uint64_t source = 0, resource = 0, table = 0, wire = 0;
  GltfResourceKind kind = GltfResourceKind::kImage;
  GltfResourceOrigin origin = GltfResourceOrigin::kCallerBinding;
  std::optional<std::uint64_t> buffer, buffer_view;
  String mime;
  Vector<std::byte> bytes;
  String ordinary_path;
  std::optional<std::uint64_t> bin_offset;
};
struct OutputPayload {
  OutputPayload(const gltf_detail::AllocationContext& allocation,
                const SurfaceEncodeRequest& request);
  geometry::MeshInspection original;
  geometry::ChargedTriangulation triangles;
  std::optional<GltfAsset> catalog;
  SurfaceEncoding encoding;
  SurfaceRepresentation representation;
  SurfaceCoordinates coordinates;
  SurfaceNarrowing narrowing;
  std::uint64_t position_bound_bits = 0;
  Vector<std::uint64_t> uv_bound_bits;
  String unit;
  String ascii_name;
  Vector<std::byte> binary_header;
  bool unused_acknowledged = false;
  Vector<SurfaceChannelRule> channels;
  Vector<SurfaceUvBinding> uv_bindings;
  Vector<SurfaceUvView> uv_views;
  Vector<UvPlan> uv;
  Vector<std::uint64_t> position_words;
  Vector<std::uint64_t> used_vertices;
  Vector<Primitive> primitives;
  Vector<SurfacePrimitiveView> primitive_views;
  Vector<std::uint64_t> triangle_sources, triangle_faces;
  Vector<std::uint64_t> corner_sources, corner_corners, vertex_sources;
  Vector<std::uint64_t> vertex_offsets, vertex_members;
  Vector<std::uint64_t> face_offsets, face_members;
  Vector<std::uint64_t> corner_offsets, corner_members;
  Vector<SurfaceMaterialBinding> material_bindings;
  Vector<SurfaceCatalogUvBinding> catalog_uv;
  Vector<SurfaceMaterialFactorView> material_factors;
  Vector<CatalogDefinition> definitions;
  Vector<CatalogImage> images;
  std::optional<SurfaceDigest> catalog_digest;
  std::optional<std::uint64_t> material_attribute;
  Vector<Member> storage;
  Vector<SurfaceMemberView> members;
};
struct OutputState {
  explicit OutputState(const gltf_detail::AllocationContext& allocation);
  gltf_detail::AllocationContext allocation;
  FactsState facts;
  std::optional<OutputPayload> payload;
};
struct InspectionPayload {
  InspectionPayload(const gltf_detail::AllocationContext& allocation,
                    const SurfaceOutput& expected);
  SurfaceOutput expected;
  Vector<Member> storage;
  Vector<SurfaceMemberView> members;
};
struct InspectionState {
  explicit InspectionState(const gltf_detail::AllocationContext& allocation);
  gltf_detail::AllocationContext allocation;
  FactsState facts;
  std::optional<InspectionPayload> payload;
};
struct Access {
  [[nodiscard]] static SurfaceOutput Output(
      std::shared_ptr<const OutputState> state);
  [[nodiscard]] static SurfaceOutputInspection Inspection(
      std::shared_ptr<const InspectionState> state);
  [[nodiscard]] static SurfaceFacts Facts(
      const std::shared_ptr<const OutputState>& state);
  [[nodiscard]] static SurfaceFacts Facts(
      const std::shared_ptr<const InspectionState>& state);
  [[nodiscard]] static const OutputState& State(
      const SurfaceOutput& output) noexcept;
};

enum class CatalogCopyKind { kDefinition, kImage };
// Per-call private tests only. Allocation-backend state outlives every retained
// free; native-worker state remains stable through all synchronous joins.
struct TestOptions {
  gltf_detail::AllocationBackend allocation =
      gltf_detail::DefaultAllocationBackend();
  gltf_detail::NativeExecutionOptions execution;
  void (*checkpoint)(SurfaceStage, std::uint64_t, void*) = nullptr;
  void* checkpoint_state = nullptr;
  // Observe only after an actual catalog byte write. Progress is the number of
  // bytes written by that disjoint copy range, starting at one. The callback
  // can run concurrently, must not throw or allocate, and its state outlives
  // joins. A null callback is a no-op; allocation/free never invokes this
  // observation.
  void (*catalog_copy_progress)(CatalogCopyKind, std::uint64_t,
                                void*) noexcept = nullptr;
  void* catalog_copy_state = nullptr;
};
struct Run {
  Run(const geometry::ExecutionContext& execution,
      const gltf_detail::AllocationContext& allocation,
      const SurfaceCountLimits& limits, const SurfaceWorkPolicy& work,
      FactsState& facts, std::stop_token stop, const TestOptions& test);
  const geometry::ExecutionContext& execution;
  const gltf_detail::AllocationContext& allocation;
  const SurfaceCountLimits& limits;
  const SurfaceWorkPolicy& policy;
  FactsState& facts;
  std::stop_token stop;
  const TestOptions& test;
  SurfaceStage stage = SurfaceStage::kSource;
  std::uint64_t next_ordinal = 0;
  void Check(std::uint64_t completed = 0) const;
  void Bound(std::uint64_t actual, std::uint64_t limit,
             SurfaceCode code = SurfaceCode::kCountLimit) const;
  void Pair(std::uint64_t count = 1);
  [[nodiscard]] std::uint64_t Enter(SurfaceDelegateKind kind,
                                    std::optional<std::uint64_t> primitive,
                                    bool factor_collector);
  [[nodiscard]] SurfaceDelegateProfileFacts Profile(
      SurfaceDelegateKind kind, std::uint64_t ordinal,
      std::optional<std::uint64_t> primitive,
      std::optional<std::uint32_t> factor_component);
};
// Jobs are actual disjoint ranges. Each owns its counters and exception slot;
// the caller reduces them only after all actual joins, including launch
// failure.
struct JobObservation {
  SurfaceWorkCounts work;
  std::exception_ptr failure;
  bool entered = false;
};
using RangeOperation = void (*)(void*, std::uint64_t, std::uint64_t,
                                JobObservation&);
void Bulk(Run& run, std::uint64_t items, std::uint64_t bytes_per_item,
          RangeOperation operation, void* state);
[[nodiscard]] SurfaceEncodeResult EncodeForTest(
    const SurfaceEncodeRequest& request, const SurfaceEncodeLimits& limits,
    const SurfaceWorkPolicy& work, std::stop_token stop,
    const TestOptions& test);
[[nodiscard]] SurfaceInspectionResult InspectForTest(
    const SurfaceOutput& expected,
    std::span<const SuppliedSurfaceMember> members,
    const SurfaceInspectLimits& limits, const SurfaceWorkPolicy& work,
    std::stop_token stop, const TestOptions& test);

[[nodiscard]] SurfaceDigest Digest(std::span<const std::byte> bytes, Run& run);
void RefreshViews(OutputPayload& output, Run& run);
void RefreshViews(InspectionPayload& output, Run& run);
enum class MeshRole { kOriginal, kTriangulated };
[[nodiscard]] SurfaceMeshCounts MeshCounts(geometry::MeshReadView mesh,
                                           Run& run,
                                           MeshRole role = MeshRole::kOriginal);
void ValidateEncodeConfiguration(const SurfaceEncodeRequest& request,
                                 const SurfaceEncodeLimits& limits,
                                 const SurfaceWorkPolicy& work);
void ValidateInspectConfiguration(SurfaceEncoding encoding,
                                  const SurfaceInspectLimits& limits,
                                  const SurfaceWorkPolicy& work);
void ValidateCorrespondence(OutputPayload& output, Run& run);
void ValidateDisposition(OutputPayload& output,
                         const SurfaceEncodeRequest& request, Run& run);
void BuildPlan(OutputPayload& output, const SurfaceEncodeRequest& request,
               Run& run);
void PrepareNumeric(OutputPayload& output, const SurfaceEncodeRequest& request,
                    Run& run);
void CheckSurfaceCoincidence(const geometry::MeshInspection& original,
                             std::span<const std::uint64_t> used_vertices,
                             std::span<const std::uint64_t> selected_words,
                             SurfaceEncoding encoding, Run& run);
// Private production seams also used by focused controls. Snapshot and byte
// views borrow their real owners through the synchronous call/actual joins;
// all scratch storage is charged to Run and no borrowed state escapes.
[[nodiscard]] SurfaceCoreFactorFacts CollectMaterialFactors(
    const gltf_detail::JsonSnapshot& actual, std::size_t material,
    std::uint64_t ordinal, SurfaceDelegateKind kind, Run& run);
void CompareNativeBytes(std::span<const std::byte> native,
                        std::span<const std::byte> captured, Run& run);
void CompareNativeIdentity(std::span<const std::uint32_t> indices, Run& run);
void BuildCatalog(OutputPayload& output, const SurfaceEncodeRequest& request,
                  Run& run);
void EncodeGltf(OutputPayload& output, const SurfaceEncodeRequest& request,
                const SurfaceEncodeLimits& limits, Run& run);
void EncodeStl(OutputPayload& output, const SurfaceEncodeRequest& request,
               const SurfaceEncodeLimits& limits, Run& run);
void VerifyEncodedNumeric(OutputPayload& output, Run& run);
[[nodiscard]] Vector<SurfaceMaterialFactorView> ObserveWireCatalog(
    const OutputPayload& expected, const gltf_detail::JsonSnapshot& actual,
    Run& run);
void BuildCapsule(OutputPayload& output, Run& run);
// Parsed claims stay private and are not promoted to actual V numeric facts.
// The core compares them to fresh actual decoded observations at Compare.
struct CapsuleNumericClaims {
  explicit CapsuleNumericClaims(
      const gltf_detail::AllocationContext& allocation)
      : uv(gltf_detail::Allocator<SurfaceNumericSummary>(allocation)) {}
  std::optional<SurfaceNumericSummary> positions;
  Vector<SurfaceNumericSummary> uv;
};
[[nodiscard]] CapsuleNumericClaims InspectCapsule(
    const OutputPayload& expected, std::span<const SurfaceMemberView> captured,
    Run& run);
[[nodiscard]] const SurfaceMemberView& MemberByKind(
    std::span<const SurfaceMemberView> members, SurfaceMemberKind kind);
}  // namespace meshvale::interchange::surface_detail
#endif  // MESHVALE_INTERCHANGE_SRC_SURFACE_OUTPUT_INTERNAL_H_
