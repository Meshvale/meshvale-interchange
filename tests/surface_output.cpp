// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/surface_write.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_operation_workers.h"
#include "surface_fixture.h"
#include "surface_numeric.h"
#include "surface_output_internal.h"

namespace {
namespace geom = meshvale::geometry;
namespace mv = meshvale::interchange;
namespace detail = mv::surface_detail;
namespace allocation = mv::gltf_detail;
namespace fixture = mv::surface_fixture;
namespace fs = std::filesystem;
constexpr std::uint64_t kMiB = 1024 * 1024, kScopeBytes = 16 * kMiB,
                        kOwnBytes = 64 * kMiB, kAllowance = 256 * kMiB;
constexpr std::uint64_t kSourceRows = 128, kSourceCorners = 512,
                        kAttributes = 32, kScalars = 65536, kMetadata = 1024;
constexpr std::uint64_t kMapEntries = 65536, kMembers = 32, kJsonNodes = 65536,
                        kDepth = 128, kIntegerBits = 8192;
constexpr std::uint64_t kComparisons = 1048576, kRelationships = 16777216,
                        kTokenBytes = 4096;
constexpr std::uint64_t kPositionErrorWord = UINT64_C(0x3e20000000000000);
constexpr geom::MeshMaterializationWorkPolicy kWork{1, 256, 32, 65536};
constexpr geom::MeshInspectionLimits kInspection{kMetadata, kScopeBytes};
constexpr std::array<std::byte, 80> kHeader{};
constexpr std::array<geom::index_t, 1> kEmptyOffsets{0};
constexpr std::array<std::int32_t, 3> kRepeatedMaterialIds{17, -4, 17};
constexpr std::size_t kNormalAttribute = 14, kFirstUvAttribute = 15,
                      kMaterialAttribute = 18, kUvFields = 3;
constexpr std::string_view kObservationSchema =
    "meshvale.surface-tests.observation/1";
constexpr std::size_t kBootstrapCapBytes = 1, kAllocationProbeBytes = 1,
                      kAllocationProbeAlignment = alignof(std::max_align_t),
                      kOverflowMultiplier = 2;
constexpr std::string_view kAdmissionFailureText =
    "glTF allocation admission failed";

void Require(bool value, std::string_view why) {
  if (!value) throw std::runtime_error(std::string(why));
}
void AdmissionFields(const allocation::AdmissionFailure& failure,
                     allocation::AdmissionCode code,
                     allocation::AdmissionProvenance provenance) {
  Require(failure.Code() == code && failure.Provenance() == provenance &&
              std::string_view(failure.what()) == kAdmissionFailureText,
          "legacy admission code/text and actual private provenance");
}
void AdmissionCopies(const allocation::AdmissionFailure& failure,
                     allocation::AdmissionCode code,
                     allocation::AdmissionProvenance provenance) {
  AdmissionFields(failure, code, provenance);
  const allocation::AdmissionFailure copied(failure);
  try {
    throw copied;
  } catch (const allocation::AdmissionFailure& actual) {
    AdmissionFields(actual, code, provenance);
    try {
      throw;
    } catch (const allocation::AdmissionFailure& rethrown) {
      AdmissionFields(rethrown, code, provenance);
    }
  }
}
std::size_t OperationSeedBytes(const geom::ExecutionContext& context) {
  allocation::AllocationContext root(context, {});
  const auto admitted = root.WithRequestLimit(kOwnBytes);
  const auto seed = admitted.OperationActiveBytes();
  Require(seed > kBootstrapCapBytes && seed <= kOwnBytes,
          "measure real admitted operation seed without private-size guess");
  return seed;
}
void AllocationAdmissionProvenance() {
  for (const auto code : {allocation::AdmissionCode::kSizeLimit,
                          allocation::AdmissionCode::kBudgetExceeded,
                          allocation::AdmissionCode::kCanceled,
                          allocation::AdmissionCode::kInvalidObject}) {
    const allocation::AdmissionFailure legacy(code);
    AdmissionCopies(legacy, code,
                    allocation::AdmissionProvenance::kUnspecified);
    Require(detail::SurfaceAdmissionCode(legacy) == code,
            "legacy one-argument exception remains untagged and unchanged");
  }
  geom::ExecutionContext context({4, 1, kOwnBytes});
  const auto baseline = context.ActiveTrackedPayload();
  allocation::AllocationContext root(context, {});
  try {
    const auto unexpected = root.WithRequestLimit(kBootstrapCapBytes);
    (void)unexpected;
    Require(false, "actual seed admission must exceed one byte");
  } catch (const allocation::AdmissionFailure& actual) {
    AdmissionCopies(actual, allocation::AdmissionCode::kSizeLimit,
                    allocation::AdmissionProvenance::kOperationRequestLimit);
    Require(detail::SurfaceAdmissionCode(actual) ==
                allocation::AdmissionCode::kBudgetExceeded,
            "actual bootstrap cap is Surface request budget");
  }
  Require(context.ActiveTrackedPayload() == baseline,
          "bootstrap refusal allocates no seed");
  const auto seed = OperationSeedBytes(context);
  Require(context.ActiveTrackedPayload() == baseline,
          "measured operation seed fully released");
  {
    const auto capped = root.WithRequestLimit(seed);
    Require(capped.OperationActiveBytes() == seed,
            "exact measured cap admits real operation seed");
    try {
      auto* unexpected =
          capped.Allocate(kAllocationProbeBytes, kAllocationProbeAlignment);
      allocation::AllocationContext::Deallocate(unexpected);
      Require(false, "actual postseed request must exceed exact seed cap");
    } catch (const allocation::AdmissionFailure& actual) {
      AdmissionCopies(actual, allocation::AdmissionCode::kSizeLimit,
                      allocation::AdmissionProvenance::kOperationRequestLimit);
      Require(detail::SurfaceAdmissionCode(actual) ==
                      allocation::AdmissionCode::kBudgetExceeded &&
                  capped.OperationActiveBytes() == seed,
              "actual postseed cap preserves measured reservation");
    }
  }
  Require(context.ActiveTrackedPayload() == baseline,
          "postseed refusal and seed release restore actual root occupancy");
  try {
    (void)allocation::CheckedProduct(std::numeric_limits<std::size_t>::max(),
                                     kOverflowMultiplier);
    Require(false, "actual checked product overflow must throw");
  } catch (const allocation::AdmissionFailure& actual) {
    AdmissionCopies(actual, allocation::AdmissionCode::kSizeLimit,
                    allocation::AdmissionProvenance::kUnspecified);
    Require(detail::SurfaceAdmissionCode(actual) ==
                allocation::AdmissionCode::kSizeLimit,
            "checked product overflow is not an operation-cap refusal");
  }
  try {
    (void)allocation::AllocationRequestBytes(
        std::numeric_limits<std::size_t>::max(), kAllocationProbeAlignment);
    Require(false, "actual full-request arithmetic overflow must throw");
  } catch (const allocation::AdmissionFailure& actual) {
    AdmissionCopies(actual, allocation::AdmissionCode::kSizeLimit,
                    allocation::AdmissionProvenance::kUnspecified);
    Require(detail::SurfaceAdmissionCode(actual) ==
                allocation::AdmissionCode::kSizeLimit,
            "full-request overflow is not an operation-cap refusal");
  }
  Require(context.ActiveTrackedPayload() == baseline,
          "arithmetic controls allocate no payload");
}
geom::MeshMaterializationLimits MeshLimits() {
  return {kSourceRows, kSourceRows, kSourceCorners, kSourceRows + 1,
          kAttributes, kScalars,    kScalars,       kScalars,
          kMetadata,   kMiB,        kScopeBytes};
}
mv::SurfaceCountLimits Counts() {
  mv::SurfaceCountLimits c;
  c.max_vertices = kSourceRows;
  c.max_faces = kSourceRows;
  c.max_corners = kSourceCorners;
  c.max_face_offsets = kSourceRows + 1;
  c.max_attributes = kAttributes;
  c.max_attribute_scalars = kScalars;
  c.max_attribute_bytes = 8 * kScalars;
  c.max_attribute_offsets = kScalars;
  c.max_presence_bytes = kScalars;
  c.max_metadata_entries = kMetadata;
  c.max_descriptor_bytes = kMiB;
  c.max_wire_primitives = kSourceRows;
  c.max_wire_vertices = kSourceCorners;
  c.max_wire_corners = kSourceCorners;
  c.max_map_entries = kMapEntries;
  c.max_members = kMembers;
  c.max_member_bytes = 4 * kMiB;
  c.max_total_member_bytes = 16 * kMiB;
  c.max_resources = kMembers;
  c.max_resource_bytes = 4 * kMiB;
  c.max_json_bytes = 4 * kMiB;
  c.max_binary_bytes = 4 * kMiB;
  c.max_capsule_json_bytes = 4 * kMiB;
  c.max_capsule_binary_bytes = 4 * kMiB;
  c.max_material_bindings = kSourceRows;
  c.max_numeric_token_bytes = kTokenBytes;
  c.max_json_nodes = kJsonNodes;
  c.max_json_depth = kDepth;
  c.max_numeric_integer_bits = kIntegerBits;
  c.max_coincidence_comparisons = kComparisons;
  c.max_relationship_steps = kRelationships;
  return c;
}
bool Stl(mv::SurfaceEncoding encoding) {
  return encoding == mv::SurfaceEncoding::kStlAscii ||
         encoding == mv::SurfaceEncoding::kStlBinary;
}
mv::SurfaceWorkPolicy Work(mv::SurfaceEncoding encoding, bool inspect) {
  mv::SurfaceWorkPolicy work;
  work.surface = kWork;
  if (encoding == mv::SurfaceEncoding::kGlb) work.container = {1, 256};
  if (Stl(encoding))
    work.stl = {256, 32, 16, kWork};
  else if (inspect)
    work.gltf = {1, 256, 1, 256, {}};
  return work;
}
mv::GltfReadOperationLimits ReadLimits() {
  return {
      {4 * kMiB, 4 * kMiB, 4 * kMiB, kJsonNodes, kDepth, 4 * kMiB, kTokenBytes,
       kTokenBytes, 32, kScalars, kRelationships, kScalars, kScalars, 4 * kMiB},
      kScopeBytes};
}
mv::SurfaceEncodeLimits EncodeLimits(mv::SurfaceEncoding encoding) {
  mv::SurfaceEncodeLimits limits;
  limits.counts = Counts();
  limits.max_new_requested_bytes = kOwnBytes;
  limits.max_total_requested_allowance = kAllowance;
  if (Stl(encoding))
    limits.delegates = mv::StlSurfaceEncodeLimits{
        MeshLimits(),
        {4 * kMiB, kSourceRows, 1024, kTokenBytes, kTokenBytes, kScopeBytes,
         2 * kScopeBytes, kInspection}};
  else {
    mv::GltfSurfaceEncodeLimits gltf;
    if (encoding == mv::SurfaceEncoding::kGlb)
      gltf.container = {4 * kMiB, 4, kScopeBytes};
    limits.delegates = gltf;
  }
  return limits;
}
mv::SurfaceInspectLimits InspectLimits(mv::SurfaceEncoding encoding) {
  mv::SurfaceInspectLimits limits;
  limits.counts = Counts();
  limits.max_new_requested_bytes = kOwnBytes;
  limits.max_total_requested_allowance = kAllowance;
  if (Stl(encoding))
    limits.delegates = mv::StlSurfaceInspectLimits{
        {4 * kMiB, kSourceRows, 1024, kTokenBytes, kTokenBytes, kScopeBytes,
         3 * kScopeBytes, MeshLimits(), kInspection}};
  else {
    mv::GltfSurfaceInspectLimits gltf;
    if (encoding == mv::SurfaceEncoding::kGlb)
      gltf.container = {4 * kMiB, 4, kScopeBytes};
    gltf.read = ReadLimits();
    gltf.extraction = {kScalars, kSourceCorners, kScopeBytes, {}};
    limits.delegates = gltf;
  }
  return limits;
}
struct Prepared {
  geom::MeshInspection original;
  geom::ChargedTriangulation triangles;
  std::optional<mv::GltfAsset> catalog;
};
struct ConstructionWitness {
  bool observed = false;
  geom::MaterializationStatus status =
      geom::MaterializationStatus::kInvalidConfiguration;
  geom::MaterializationDiagnostic diagnostic;
  geom::MaterializationExecutionStats execution;
  bool owner_returned = false;
};
std::optional<Prepared> Prepare(geom::MeshMaterializationInput input,
                                const geom::ExecutionContext& context,
                                bool catalog,
                                ConstructionWitness* witness = nullptr) {
  const auto materialized =
      geom::MaterializeMesh(input, MeshLimits(), kWork, context);
  if (witness)
    *witness = {true, materialized.status, materialized.diagnostic,
                materialized.execution, materialized.mesh.has_value()};
  if (materialized.status ==
      geom::MaterializationStatus::kUnsupportedAllocationProfile) {
    Require(!materialized.mesh,
            "genuine unsupported materialization has no owner");
    return {};
  }
  Require(materialized.status == geom::MaterializationStatus::kSuccess &&
              materialized.mesh,
          "genuine original capture");
  const auto inspection =
      geom::InspectMaterializedMesh(*materialized.mesh, kInspection, kWork);
  Require(inspection.status == geom::MaterializationStatus::kSuccess &&
              inspection.inspection && inspection.inspection->IsStorageValid(),
          "genuine original storage proof");
  const geom::ChargedTriangulationLimits limits{
      MeshLimits(), kInspection, MeshLimits(), kInspection,
      kMapEntries,  kScopeBytes, kScopeBytes};
  const auto triangles =
      geom::TriangulateCharged(*materialized.mesh, {64, 1}, limits, kWork);
  Require(triangles.status == geom::ChargedTriangulationStatus::kAccepted &&
              triangles.triangulation,
          "genuine charged result");
  std::optional<mv::GltfAsset> asset;
  if (catalog) {
    const auto text = fixture::CatalogJson();
    const auto read = mv::ReadGltfAssetCapped(
        std::as_bytes(std::span{text.data(), text.size()}),
        fixture::CatalogResources(), ReadLimits(), {1, 256, 1, 256, {}},
        context);
    Require(read.diagnostic.status == mv::GltfStatus::kAccepted && read.asset,
            "genuine catalog capture");
    asset = read.asset;
  }
  return Prepared{*inspection.inspection, *triangles.triangulation, asset};
}
struct Plan {
  Plan(const Prepared& source, mv::SurfaceEncoding encoding,
       fixture::Variant variant, bool repeated = false)
      : request{.original = source.original,
                .triangles = source.triangles,
                .representation = mv::SurfaceRepresentation::kInvalid,
                .acknowledge_triangle_conversion = false,
                .acknowledge_corner_expansion = false,
                .acknowledge_unused_vertices = false,
                .channels = {},
                .numeric = {},
                .destination = {}} {
    request.representation = mv::SurfaceRepresentation::kAnnotatedBundleV1;
    request.acknowledge_triangle_conversion = true;
    request.acknowledge_corner_expansion = true;
    request.acknowledge_unused_vertices = true;
    request.numeric.positions = variant == fixture::Variant::kTranslated
                                    ? mv::SurfaceNarrowing::kBoundedRne32
                                    : mv::SurfaceNarrowing::kExact32;
    request.numeric.max_position_linf_error =
        std::bit_cast<double>(kPositionErrorWord);
    const auto attributes = source.original.Source().View().attributes;
    for (std::uint64_t i = 0; i < attributes.size(); ++i) {
      auto role = mv::SurfaceChannelRole::kBundleOnly;
      if (Stl(encoding) && i == kNormalAttribute)
        role = mv::SurfaceChannelRole::kStlCornerNormal;
      if (!Stl(encoding) && i >= kFirstUvAttribute && i < kMaterialAttribute)
        role = mv::SurfaceChannelRole::kUv;
      if (!Stl(encoding) && i == kMaterialAttribute)
        role = mv::SurfaceChannelRole::kMaterialId;
      channels.push_back(
          {i, role, role == mv::SurfaceChannelRole::kBundleOnly});
    }
    request.channels = channels;
    if (Stl(encoding)) {
      mv::StlSurfaceOptions options;
      options.encoding = encoding;
      options.coordinates = mv::SurfaceCoordinates::kStlUnitsUnspecified;
      options.declared_unit = "m";
      options.corner_normal_attribute = kNormalAttribute;
      if (encoding == mv::SurfaceEncoding::kStlBinary)
        options.binary_header = kHeader;
      else
        options.ascii_name = "rich-surface";
      request.destination = options;
    } else {
      for (const auto& binding : fixture::MaterialBindings()) {
        uv.push_back(
            {binding.source_attribute, binding.source_set, binding.wire_set});
        if (!repeated || binding.source_id != 1000003) {
          materials.push_back({binding.source_id, binding.catalog_material});
          aliases.push_back({binding.catalog_uv, binding.source_set});
        }
      }
      request.numeric.max_uv_component_errors = uv_bounds;
      mv::GltfSurfaceOptions options;
      options.encoding = encoding;
      options.coordinates =
          mv::SurfaceCoordinates::kDeclaredMetersRightHandedYUp;
      options.uv = uv;
      options.catalog = &*source.catalog;
      options.materials = materials;
      options.catalog_uv = aliases;
      request.destination = options;
    }
  }
  Plan(const Plan&) = delete;
  Plan& operator=(const Plan&) = delete;
  std::vector<mv::SurfaceChannelRule> channels;
  std::vector<mv::SurfaceUvBinding> uv;
  std::vector<mv::SurfaceMaterialBinding> materials;
  std::vector<mv::SurfaceCatalogUvBinding> aliases;
  std::array<double, kUvFields> uv_bounds{};
  mv::SurfaceEncodeRequest request;
};
struct Captured {
  explicit Captured(const mv::SurfaceOutput& output) {
    paths.reserve(output.Members().size());
    bytes.reserve(output.Members().size());
    for (const auto& member : output.Members()) {
      paths.emplace_back(member.path);
      bytes.emplace_back(member.bytes.begin(), member.bytes.end());
    }
    for (std::size_t i = 0; i < paths.size(); ++i)
      views.push_back({paths[i], bytes[i]});
  }
  Captured(const Captured&) = delete;
  Captured& operator=(const Captured&) = delete;
  std::vector<std::string> paths;
  std::vector<std::vector<std::byte>> bytes;
  std::vector<mv::SuppliedSurfaceMember> views;
};
void Maps(const mv::SurfaceOutput& output) {
  const auto& tri = output.Triangles();
  const auto mesh = tri.Mesh().View();
  for (std::uint64_t wire = 0; wire < output.WireTriangleSources().size();
       ++wire) {
    const auto triangle = output.WireTriangleSources()[wire];
    Require(output.WireTriangleFaces()[wire] == tri.FaceSources()[triangle],
            "real composed face map");
    for (std::uint64_t c = 0; c < detail::kTriangleCorners; ++c) {
      const auto i = wire * detail::kTriangleCorners + c,
                 corner = triangle * detail::kTriangleCorners + c;
      Require(
          output.WireCornerSources()[i] == corner &&
              output.WireCornerCorners()[i] == tri.CornerSources()[corner] &&
              output.WireVertexSources()[i] == mesh.corner_vertices[corner],
          "real composed corner/vertex maps");
    }
  }
  auto inverse = [&](std::span<const std::uint64_t> forward,
                     std::span<const std::uint64_t> offsets,
                     std::span<const std::uint64_t> members) {
    Require(!offsets.empty() && offsets.front() == 0 &&
                offsets.back() == members.size(),
            "CSR sentinels");
    for (std::uint64_t row = 0; row + 1 < offsets.size(); ++row) {
      std::vector<std::uint64_t> expected;
      for (std::uint64_t i = 0; i < forward.size(); ++i)
        if (forward[i] == row) expected.push_back(i);
      Require(std::ranges::equal(
                  expected, members.subspan(offsets[row],
                                            offsets[row + 1] - offsets[row])),
              "complete ascending inverse row");
    }
  };
  inverse(output.WireVertexSources(), output.SourceVertexOffsets(),
          output.SourceVertexWireMembers());
  inverse(output.WireTriangleFaces(), output.SourceFaceOffsets(),
          output.SourceFaceWireTriangles());
  inverse(output.WireCornerCorners(), output.SourceCornerOffsets(),
          output.SourceCornerWireCorners());
}
void Numeric(const mv::SurfaceFacts& facts, bool translated,
             bool ascii64 = false) {
  const auto position = facts.PositionNumeric();
  Require(position &&
              position->kind == mv::SurfaceNumericSummary::Kind::kDecodedWire &&
              position->complete && position->examined_values == 33 &&
              position->changed_values == (translated && !ascii64 ? 11 : 0),
          "literal unique used POSITION summary");
  if (translated && !ascii64) {
    Require(position->max_absolute_error.used_words == 1 &&
                position->max_absolute_error.magnitude[0] == 1 &&
                position->max_absolute_error.exponent2 == -30,
            "literal bounded displacement");
  }
  Require(
      facts.Execution().launched_workers == facts.Execution().joined_workers,
      "real joined workers");
  for (const auto& field : facts.UvNumeric())
    Require(field.complete &&
                field.kind == mv::SurfaceNumericSummary::Kind::kDecodedWire &&
                field.examined_values == 24 && field.changed_values == 0,
            "literal unique corner UV summary");
}
void DumpCase(const fs::path& directory, std::string_view name,
              const mv::SurfaceEncodeResult& encoded,
              const mv::SurfaceInspectionResult* inspected);
void DumpConstruction(const fs::path& directory, std::string_view name,
                      const ConstructionWitness& witness);
void Rich(const geom::ExecutionContext& context, fixture::Variant variant,
          fixture::MetadataRoute route, mv::SurfaceEncoding encoding,
          const std::optional<fs::path>& dump, bool repeated = false,
          bool ascii64 = false) {
  std::string name =
      variant == fixture::Variant::kOriginal ? "original" : "translated";
  name +=
      route == fixture::MetadataRoute::kRetained ? "-retained-" : "-legacy-";
  name += std::to_string(static_cast<std::uint32_t>(encoding));
  if (repeated) name += "-repeated-material";
  if (ascii64) name += "-exact64";
  auto input = fixture::RichInput(variant, route);
  std::vector<geom::AttributeMaterializationInput> attributes;
  if (repeated) {
    attributes.assign(input.attributes.begin(), input.attributes.end());
    attributes[kMaterialAttribute].values =
        std::span<const std::int32_t>{kRepeatedMaterialIds};
    input.attributes = attributes;
  }
  ConstructionWitness witness;
  auto prepared = Prepare(input, context, !Stl(encoding), &witness);
  if (!prepared) {
    if (dump) DumpConstruction(*dump, name, witness);
    return;  // The real unsupported construction is asserted above.
  }
  Plan plan(*prepared, encoding, variant, repeated);
  if (ascii64) plan.request.numeric.positions = mv::SurfaceNarrowing::kExact64;
  const auto encoded = mv::EncodeSurface(plan.request, EncodeLimits(encoding),
                                         Work(encoding, false));
  if (dump && encoded.status != mv::SurfaceStatus::kSuccess)
    DumpCase(*dump, name, encoded, nullptr);
  Require(encoded.status == mv::SurfaceStatus::kSuccess && encoded.output,
          "real rich surface encoding");
  Require(encoded.output->Original().Source().View().attributes.size() == 19 &&
              encoded.output->Triangles().Mesh().View().attributes.size() == 19,
          "all nineteen genuine channels retained");
  Maps(*encoded.output);
  Numeric(encoded.output->Facts(), variant == fixture::Variant::kTranslated,
          ascii64);
  Captured captured(*encoded.output);
  const auto inspected =
      mv::InspectSurfaceOutput(*encoded.output, captured.views,
                               InspectLimits(encoding), Work(encoding, true));
  if (dump && inspected.status != mv::SurfaceStatus::kSuccess)
    DumpCase(*dump, name, encoded, &inspected);
  Require(
      inspected.status == mv::SurfaceStatus::kSuccess && inspected.inspection,
      "real actual-byte native inspection");
  Numeric(inspected.inspection->Facts(),
          variant == fixture::Variant::kTranslated, ascii64);
  if (repeated && !Stl(encoding)) {
    Require(encoded.output->Primitives().size() == 2,
            "two actual first-use material groups");
    std::vector<std::uint64_t> expected;
    const auto ranges = prepared->triangles.FaceOutputOffsets();
    for (auto face : {0U, 2U, 1U})
      for (auto triangle = ranges[face]; triangle < ranges[face + 1];
           ++triangle)
        expected.push_back(triangle);
    Require(std::ranges::equal(expected, encoded.output->WireTriangleSources()),
            "noncontiguous repeated material stable group order");
    Maps(inspected.inspection->Expected());
  }
  if (!Stl(encoding)) {
    std::uint64_t largest_native = 0;
    bool collector = false;
    for (const auto& delegate : inspected.facts->Delegates()) {
      if (delegate.kind == mv::SurfaceDelegateKind::kWireMaterialFactors) {
        Require(delegate.stage == mv::SurfaceStage::kCompare &&
                    delegate.call_ordinal > largest_native,
                "actual wire factor collector follows native calls at Compare");
        collector = true;
      } else {
        Require(!collector, "no native delegate after wire Compare collector");
        largest_native = std::max(largest_native, delegate.call_ordinal);
      }
    }
    Require(collector,
            "rich selected materials have genuine Compare collectors");
    if (variant == fixture::Variant::kOriginal &&
        route == fixture::MetadataRoute::kRetained && !repeated) {
      Captured broken(*encoded.output);
      bool corrupted = false;
      for (std::size_t i = 0; i < broken.paths.size(); ++i)
        if (broken.paths[i] == "surface.json") {
          Require(!broken.bytes[i].empty(), "actual capsule bytes exist");
          broken.bytes[i][0] = std::byte{'!'};
          corrupted = true;
        }
      Require(corrupted, "genuine annotated member corrupted");
      const auto refused = mv::InspectSurfaceOutput(
          *encoded.output, broken.views, InspectLimits(encoding),
          Work(encoding, true));
      Require(refused.status == mv::SurfaceStatus::kVerificationFailed &&
                  refused.diagnostic.stage == mv::SurfaceStage::kCapsuleRead &&
                  !refused.inspection && refused.facts,
              "actual capsule failure precedes Compare");
      for (const auto& delegate : refused.facts->Delegates())
        Require(delegate.kind != mv::SurfaceDelegateKind::kWireMaterialFactors,
                "no future wire collector on failed capsule");
    }
  }
  if (dump) {
    DumpCase(*dump, name, encoded, &inspected);
  }
  auto bare = plan.request;
  bare.representation = mv::SurfaceRepresentation::kStrictBare;
  const auto refused =
      mv::EncodeSurface(bare, EncodeLimits(encoding), Work(encoding, false));
  Require(refused.status == mv::SurfaceStatus::kUnsupported &&
              refused.diagnostic.code == mv::SurfaceCode::kBareUnrepresented &&
              !refused.output,
          "rich ordinary view cannot silently drop channels");
  auto tiny = EncodeLimits(encoding);
  tiny.max_new_requested_bytes = 1;
  const auto limited =
      mv::EncodeSurface(plan.request, tiny, Work(encoding, false));
  Require(limited.status == mv::SurfaceStatus::kBudgetExceeded &&
              limited.diagnostic.code == mv::SurfaceCode::kRequestBudget &&
              !limited.output,
          "actual operation-cap admission");
  auto tiny_inspection = InspectLimits(encoding);
  tiny_inspection.max_new_requested_bytes = kBootstrapCapBytes;
  const auto inspect_bootstrap = mv::InspectSurfaceOutput(
      *encoded.output, captured.views, tiny_inspection, Work(encoding, true));
  Require(inspect_bootstrap.status == mv::SurfaceStatus::kBudgetExceeded &&
              inspect_bootstrap.diagnostic.code ==
                  mv::SurfaceCode::kRequestBudget &&
              !inspect_bootstrap.inspection,
          "actual inspector bootstrap cap is request budget");
  const auto seed = OperationSeedBytes(context);
  auto seeded_encode = EncodeLimits(encoding);
  seeded_encode.max_new_requested_bytes = seed;
  const auto encode_postseed =
      mv::EncodeSurface(plan.request, seeded_encode, Work(encoding, false));
  Require(
      encode_postseed.status == mv::SurfaceStatus::kBudgetExceeded &&
          encode_postseed.diagnostic.code == mv::SurfaceCode::kRequestBudget &&
          !encode_postseed.output,
      "actual encoder postseed cap is request budget");
  auto seeded_inspection = InspectLimits(encoding);
  seeded_inspection.max_new_requested_bytes = seed;
  const auto inspect_postseed = mv::InspectSurfaceOutput(
      *encoded.output, captured.views, seeded_inspection, Work(encoding, true));
  Require(
      inspect_postseed.status == mv::SurfaceStatus::kBudgetExceeded &&
          inspect_postseed.diagnostic.code == mv::SurfaceCode::kRequestBudget &&
          !inspect_postseed.inspection,
      "actual inspector postseed cap is request budget");
  std::stop_source stop;
  stop.request_stop();
  auto invalid = EncodeLimits(encoding);
  invalid.counts.max_members = 0;
  const auto first = mv::EncodeSurface(plan.request, invalid,
                                       Work(encoding, false), stop.get_token());
  Require(first.status == mv::SurfaceStatus::kInvalidConfiguration &&
              !first.facts && !first.output,
          "configuration precedes stop and allocation");
  if (encoding != mv::SurfaceEncoding::kStlAscii) {
    auto mismatch = plan.request;
    mismatch.numeric.positions = mv::SurfaceNarrowing::kExact64;
    const auto wrong =
        mv::EncodeSurface(mismatch, EncodeLimits(encoding),
                          Work(encoding, false), stop.get_token());
    Require(
        wrong.status == mv::SurfaceStatus::kInvalidConfiguration &&
            wrong.diagnostic.code == mv::SurfaceCode::kEncodingLimitsMismatch &&
            !wrong.output && !wrong.facts,
        "known incompatible mode precedes requested stop");
  } else if (variant == fixture::Variant::kTranslated && !ascii64) {
    auto exact = plan.request;
    exact.numeric.positions = mv::SurfaceNarrowing::kExact32;
    const auto wrong =
        mv::EncodeSurface(exact, EncodeLimits(encoding), Work(encoding, false));
    Require(wrong.status == mv::SurfaceStatus::kUnsupported &&
                wrong.diagnostic.code == mv::SurfaceCode::kExactness &&
                !wrong.output,
            "ASCII Exact32 refuses the non-F32 variant without rounding");
  }
  if (encoding == mv::SurfaceEncoding::kGltfJson &&
      variant == fixture::Variant::kOriginal &&
      route == fixture::MetadataRoute::kRetained && !repeated) {
    auto bounded = EncodeLimits(encoding);
    bounded.counts.max_coincidence_comparisons = 1;
    const auto denied =
        mv::EncodeSurface(plan.request, bounded, Work(encoding, false));
    Require(denied.status == mv::SurfaceStatus::kLimitExceeded &&
                denied.diagnostic.code == mv::SurfaceCode::kCountLimit &&
                !denied.output && denied.facts &&
                denied.facts->ExaminedWork().coincidence_comparisons <= 1,
            "actual comparison permits cannot overshoot the inclusive cap");
  }
}
void Empty(const geom::ExecutionContext& context,
           mv::SurfaceEncoding encoding) {
  const auto prepared = Prepare({std::span<const double>{},
                                 geom::PolygonTopologyInput{kEmptyOffsets, {}},
                                 {}},
                                context, false);
  Require(prepared.has_value(), "genuine empty source and charged result");
  mv::SurfaceEncodeRequest request{
      .original = prepared->original,
      .triangles = prepared->triangles,
      .representation = mv::SurfaceRepresentation::kInvalid,
      .acknowledge_triangle_conversion = false,
      .acknowledge_corner_expansion = false,
      .acknowledge_unused_vertices = false,
      .channels = {},
      .numeric = {},
      .destination = {}};
  request.representation = mv::SurfaceRepresentation::kAnnotatedBundleV1;
  request.acknowledge_triangle_conversion = true;
  request.acknowledge_corner_expansion = true;
  request.numeric.positions = mv::SurfaceNarrowing::kExact32;
  if (Stl(encoding)) {
    mv::StlSurfaceOptions options;
    options.encoding = encoding;
    options.coordinates = mv::SurfaceCoordinates::kStlUnitsUnspecified;
    if (encoding == mv::SurfaceEncoding::kStlBinary)
      options.binary_header = kHeader;
    else
      options.ascii_name = "empty";
    request.destination = options;
  } else {
    mv::GltfSurfaceOptions options;
    options.encoding = encoding;
    options.coordinates = mv::SurfaceCoordinates::kDeclaredMetersRightHandedYUp;
    request.destination = options;
  }
  const auto encoded =
      mv::EncodeSurface(request, EncodeLimits(encoding), Work(encoding, false));
  if (Stl(encoding)) {
    Require(encoded.status == mv::SurfaceStatus::kUnsupported &&
                encoded.diagnostic.code == mv::SurfaceCode::kNormalAbsent &&
                !encoded.output && encoded.facts &&
                encoded.facts->Delegates().empty() &&
                !encoded.facts->PositionNumeric(),
            "actual absent-normal empty refusal before codec/numeric");
    return;
  }
  Require(encoded.status == mv::SurfaceStatus::kSuccess && encoded.output,
          "real empty glTF encoding");
  Captured captured(*encoded.output);
  const auto inspected =
      mv::InspectSurfaceOutput(*encoded.output, captured.views,
                               InspectLimits(encoding), Work(encoding, true));
  if (inspected.status == mv::SurfaceStatus::kUnsupported &&
      inspected.diagnostic.code == mv::SurfaceCode::kNumericProfile) {
    Require(!inspected.inspection && inspected.facts,
            "actual unsupported parser profile");
    for (const auto& delegate : inspected.facts->Delegates())
      Require(delegate.kind != mv::SurfaceDelegateKind::kGltfRead &&
                  delegate.kind != mv::SurfaceDelegateKind::kGltfExtract,
              "profile refusal has no invented native read");
    return;
  }
  Require(
      inspected.status == mv::SurfaceStatus::kSuccess && inspected.inspection,
      "actual empty native read");
  const auto numeric = inspected.facts->PositionNumeric();
  Require(numeric && numeric->complete && numeric->examined_values == 0 &&
              numeric->changed_values == 0,
          "complete zero logical field");
  for (const auto& delegate : inspected.facts->Delegates())
    Require(delegate.kind != mv::SurfaceDelegateKind::kGltfExtract,
            "no zero primitive extraction invented");
}
void PointOnly(const geom::ExecutionContext& context,
               mv::SurfaceEncoding encoding) {
  auto input = fixture::RichInput(fixture::Variant::kOriginal,
                                  fixture::MetadataRoute::kRetained);
  input.topology = geom::PolygonTopologyInput{kEmptyOffsets, {}};
  std::vector<geom::AttributeMaterializationInput> attributes(
      input.attributes.begin(), input.attributes.end());
  for (auto& a : attributes)
    if (a.domain != geom::AttributeDomain::vertex) {
      a.values = std::visit(
          [](auto values) -> geom::ScalarIngress {
            using T = std::remove_cvref_t<decltype(values)>;
            if constexpr (std::is_same_v<T, std::monostate>)
              return std::monostate{};
            else
              return values.first(0);
          },
          a.values);
      if (a.offsets) a.offsets = std::span<const geom::index_t>{kEmptyOffsets};
      if (a.present) a.present = std::span<const std::uint8_t>{};
    }
  input.attributes = attributes;
  const auto prepared = Prepare(input, context, !Stl(encoding));
  if (!prepared) return;
  Plan plan(*prepared, encoding, fixture::Variant::kOriginal);
  if (!Stl(encoding)) {
    plan.materials.clear();
    plan.aliases.clear();
    auto options = std::get<mv::GltfSurfaceOptions>(plan.request.destination);
    options.materials = {};
    options.catalog_uv = {};
    plan.request.destination = options;
  }
  const auto encoded = mv::EncodeSurface(plan.request, EncodeLimits(encoding),
                                         Work(encoding, false));
  Require(encoded.status == mv::SurfaceStatus::kSuccess && encoded.output,
          "genuine rich point-only encoding including explicit empty normal");
  Require(encoded.output->Original().Source().View().attributes.size() == 19 &&
              encoded.output->WireVertexSources().empty(),
          "all rich point-only backing retained without invented wire rows");
  Captured captured(*encoded.output);
  const auto inspected =
      mv::InspectSurfaceOutput(*encoded.output, captured.views,
                               InspectLimits(encoding), Work(encoding, true));
  Require(
      inspected.status == mv::SurfaceStatus::kSuccess && inspected.inspection,
      "actual point-only reload");
  const auto p = inspected.facts->PositionNumeric();
  Require(p && p->complete && !p->examined_values,
          "point-only complete zero POSITION");
  for (const auto& uv : inspected.facts->UvNumeric())
    Require(uv.complete && !uv.examined_values,
            "point-only zero referenced UV domain");
  Maps(*encoded.output);
}
void Boundaries(const geom::ExecutionContext& context) {
  constexpr std::array<mv::SurfaceCode, 5> expected{
      mv::SurfaceCode::kNewCoincidence, mv::SurfaceCode::kCollapsedTriangle,
      mv::SurfaceCode::kOrientation, mv::SurfaceCode::kNone,
      mv::SurfaceCode::kNone};
  std::size_t index = 0;
  for (const auto& test : fixture::TriangleCases()) {
    const auto prepared = Prepare(test.input, context, false);
    if (!prepared) return;
    mv::SurfaceEncodeRequest request{
        .original = prepared->original,
        .triangles = prepared->triangles,
        .representation = mv::SurfaceRepresentation::kInvalid,
        .acknowledge_triangle_conversion = false,
        .acknowledge_corner_expansion = false,
        .acknowledge_unused_vertices = false,
        .channels = {},
        .numeric = {},
        .destination = {}};
    request.representation = mv::SurfaceRepresentation::kAnnotatedBundleV1;
    request.acknowledge_triangle_conversion = true;
    request.acknowledge_corner_expansion = true;
    request.acknowledge_unused_vertices = true;
    request.numeric.positions = mv::SurfaceNarrowing::kBoundedRne32;
    request.numeric.max_position_linf_error =
        std::bit_cast<double>(test.bound_f64);
    request.destination = mv::GltfSurfaceOptions{
        .encoding = mv::SurfaceEncoding::kGltfJson,
        .coordinates = mv::SurfaceCoordinates::kDeclaredMetersRightHandedYUp,
        .uv = {},
        .catalog = nullptr,
        .materials = {},
        .catalog_uv = {}};
    const auto encoded =
        mv::EncodeSurface(request, EncodeLimits(mv::SurfaceEncoding::kGltfJson),
                          Work(mv::SurfaceEncoding::kGltfJson, false));
    if (expected[index] == mv::SurfaceCode::kNone) {
      Require(encoded.status == mv::SurfaceStatus::kSuccess && encoded.output,
              "literal positive geometry including distinct coincident IDs");
      Captured captured(*encoded.output);
      const auto inspected = mv::InspectSurfaceOutput(
          *encoded.output, captured.views,
          InspectLimits(mv::SurfaceEncoding::kGltfJson),
          Work(mv::SurfaceEncoding::kGltfJson, true));
      Require(inspected.status == mv::SurfaceStatus::kSuccess &&
                  inspected.inspection,
              "literal positive geometry actual readback");
    } else
      Require(encoded.status == mv::SurfaceStatus::kUnsupported &&
                  encoded.diagnostic.code == expected[index] && !encoded.output,
              "literal geometry refusal without fabricated output");
    ++index;
  }
}
struct StopState {
  std::stop_source* stop;
  std::atomic<bool> observed = false;
};
void StopInterior(mv::SurfaceStage stage, std::uint64_t completed,
                  void* opaque) {
  auto& state = *static_cast<StopState*>(opaque);
  if (stage == mv::SurfaceStage::kEncode && completed >= 4) {
    state.observed.store(true, std::memory_order_relaxed);
    state.stop->request_stop();
  }
}
struct LaunchState {
  std::array<std::optional<std::thread>, 8> threads;
  std::size_t launched = 0, joined = 0;
};
std::uintptr_t Launch(mv::gltf_detail::NativeWorkerRecord* record,
                      void* opaque) {
  auto& state = *static_cast<LaunchState*>(opaque);
  if (state.launched == 1)
    throw std::runtime_error("actual selected partial launch failure");
  const auto index = state.launched;
  state.threads[index].emplace([record] { record->entry(*record); });
  ++state.launched;
  return index + 1;
}
void Join(std::uintptr_t handle, void* opaque) noexcept {
  auto& state = *static_cast<LaunchState*>(opaque);
  auto& thread = state.threads[handle - 1];
  thread->join();
  thread.reset();
  ++state.joined;
}
struct ThrowAllocation {
  mv::gltf_detail::AllocationBackend actual =
      mv::gltf_detail::DefaultAllocationBackend();
  std::atomic<std::size_t> calls = 0;
};
void* Allocate(std::size_t bytes, std::size_t alignment, void* opaque) {
  auto& state = *static_cast<ThrowAllocation*>(opaque);
  if (++state.calls == 5) throw std::bad_alloc();
  return state.actual.allocate(bytes, alignment, state.actual.state);
}
void Free(void* block, std::size_t bytes, std::size_t alignment,
          void* opaque) noexcept {
  auto& state = *static_cast<ThrowAllocation*>(opaque);
  state.actual.deallocate(block, bytes, alignment, state.actual.state);
}
void PressureAndFailures() {
  geom::ExecutionContext context({4, 1, 64 * kMiB});
  const auto prepared =
      Prepare(fixture::RichInput(fixture::Variant::kOriginal,
                                 fixture::MetadataRoute::kRetained),
              context, true);
  if (!prepared) return;
  Plan plan(*prepared, mv::SurfaceEncoding::kGltfJson,
            fixture::Variant::kOriginal);
  const auto baseline = context.ActiveTrackedPayload();
  {
    const auto available =
        context.TrackedPayloadBudget() - context.ActiveTrackedPayload();
    mv::gltf_detail::AllocationContext backing(context, {});
    detail::Vector<std::byte> pressure{
        mv::gltf_detail::Allocator<std::byte>(backing)};
    const auto overhead =
        mv::gltf_detail::AllocationRequestBytes(0, alignof(std::byte));
    Require(available > overhead + 1, "actual pressure headroom");
    pressure.resize(available - overhead - 1);
    Require(
        context.ActiveTrackedPayload() == context.TrackedPayloadBudget() - 1,
        "real owned backing leaves one ledger byte");
    const auto refused = mv::EncodeSurface(
        plan.request, EncodeLimits(mv::SurfaceEncoding::kGltfJson),
        Work(mv::SurfaceEncoding::kGltfJson, false));
    Require(
        refused.status == mv::SurfaceStatus::kBudgetExceeded && !refused.output,
        "actual shared pressure admission refusal");
  }
  Require(context.ActiveTrackedPayload() == baseline,
          "pressure release restores actual occupancy");
  {
    geom::ExecutionContext foreign({4, 1, 64 * kMiB});
    mv::gltf_detail::AllocationContext backing(foreign, {});
    detail::Vector<std::byte> occupied{
        mv::gltf_detail::Allocator<std::byte>(backing)};
    const auto overhead =
        mv::gltf_detail::AllocationRequestBytes(0, alignof(std::byte));
    occupied.resize(foreign.TrackedPayloadBudget() - overhead - 1);
    Require(
        foreign.ActiveTrackedPayload() == foreign.TrackedPayloadBudget() - 1,
        "actual unrelated owned backing");
    const auto success = mv::EncodeSurface(
        plan.request, EncodeLimits(mv::SurfaceEncoding::kGltfJson),
        Work(mv::SurfaceEncoding::kGltfJson, false));
    Require(success.status == mv::SurfaceStatus::kSuccess && success.output,
            "foreign occupancy cannot consume this ledger");
  }
  auto work = Work(mv::SurfaceEncoding::kGltfJson, false);
  work.surface.partition_bytes = 16;
  {
    std::stop_source stop;
    StopState state{&stop};
    detail::TestOptions options;
    options.checkpoint = StopInterior;
    options.checkpoint_state = &state;
    const auto stopped = detail::EncodeForTest(
        plan.request, EncodeLimits(mv::SurfaceEncoding::kGltfJson), work,
        stop.get_token(), options);
    Require(state.observed && stopped.status == mv::SurfaceStatus::kCanceled &&
                !stopped.output && stopped.facts,
            "real begun Encode-interior stop");
    const auto execution = stopped.facts->Execution();
    Require(execution.launched_workers > 0 &&
                execution.launched_workers == execution.joined_workers,
            "all real workers joined after interior stop");
  }
  Require(context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "stopped owners and slots released");
  {
    LaunchState state;
    detail::TestOptions options;
    options.execution.backend = {Launch, Join, &state};
    bool caught = false;
    try {
      (void)detail::EncodeForTest(plan.request,
                                  EncodeLimits(mv::SurfaceEncoding::kGltfJson),
                                  work, {}, options);
    } catch (const std::runtime_error&) {
      caught = true;
    }
    Require(caught && state.launched == 1 && state.joined == 1,
            "actual partial-launch exception after real join");
  }
  Require(context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "launch exception cleanup");
  {
    ThrowAllocation state;
    detail::TestOptions options;
    options.allocation = {Allocate, Free, &state};
    bool caught = false;
    try {
      (void)detail::EncodeForTest(plan.request,
                                  EncodeLimits(mv::SurfaceEncoding::kGltfJson),
                                  work, {}, options);
    } catch (const std::bad_alloc&) {
      caught = true;
    }
    Require(caught && state.calls == 5,
            "actual backend exception, no invented normal result");
  }
  Require(context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "allocator exception cleanup");
}
struct CatalogCopyStopState {
  std::stop_source* stop;
  detail::CatalogCopyKind wanted;
  std::thread::id caller;
  std::atomic<bool> observed = false;
  std::atomic<bool> caller_observed = false;
  std::atomic<detail::CatalogCopyKind> observed_kind =
      detail::CatalogCopyKind::kDefinition;
  std::atomic<std::uint64_t> first_completed = 0;
};
void StopCatalogCopy(detail::CatalogCopyKind kind, std::uint64_t completed,
                     void* opaque) noexcept {
  auto& state = *static_cast<CatalogCopyStopState*>(opaque);
  if (kind != state.wanted || completed == 0) return;
  if (std::this_thread::get_id() == state.caller) {
    state.caller_observed.store(true, std::memory_order_relaxed);
    return;
  }
  if (!state.observed.exchange(true, std::memory_order_relaxed)) {
    state.observed_kind.store(kind, std::memory_order_relaxed);
    state.first_completed.store(completed, std::memory_order_relaxed);
    state.stop->request_stop();
  }
}
bool CatalogCopyAndConfigurationRegressions(
    const geom::ExecutionContext& context) {
  constexpr std::uint64_t kCatalogCopyPartitionBytes = 32;
  constexpr auto kEncoding = mv::SurfaceEncoding::kGltfJson;
  const auto prepared =
      Prepare(fixture::RichInput(fixture::Variant::kOriginal,
                                 fixture::MetadataRoute::kRetained),
              context, true);
  if (!prepared) {
    std::cerr << "catalog/configuration controls unreached after genuine "
                 "nonempty constructor refusal\n";
    return true;
  }
  Plan plan(*prepared, kEncoding, fixture::Variant::kOriginal);
  auto copy_work = Work(kEncoding, false);
  copy_work.surface.partition_bytes = kCatalogCopyPartitionBytes;
  const auto positive =
      mv::EncodeSurface(plan.request, EncodeLimits(kEncoding), copy_work);
  Require(positive.status == mv::SurfaceStatus::kSuccess && positive.output &&
              positive.facts,
          "genuine catalog positive with only partition32 scheduling change");
  Maps(*positive.output);
  Numeric(*positive.facts, false);
  Captured captured(*positive.output);
  const auto baseline = context.ActiveTrackedPayload();
  bool passed = true;
  std::stop_source already_stopped;
  already_stopped.request_stop();
  {
    auto limits = EncodeLimits(kEncoding);
    limits.max_total_requested_allowance = limits.max_new_requested_bytes - 1;
    auto work = Work(kEncoding, false);
    work.stl.cancellation_block_bytes = 1;
    const auto refused = mv::EncodeSurface(plan.request, limits, work,
                                           already_stopped.get_token());
    const bool correct =
        refused.status == mv::SurfaceStatus::kInvalidConfiguration &&
        refused.diagnostic.stage == mv::SurfaceStage::kConfiguration &&
        refused.diagnostic.code == mv::SurfaceCode::kAllowanceTooSmall &&
        !refused.output && !refused.facts &&
        context.ActiveTrackedPayload() == baseline &&
        context.ActiveWorkers() == 0;
    if (!correct)
      std::cerr << "paired E allowance/inactive-tail status="
                << static_cast<std::uint32_t>(refused.status) << " code="
                << static_cast<std::uint32_t>(refused.diagnostic.code) << '\n';
    passed = correct && passed;
  }
  {
    auto request = plan.request;
    request.numeric.max_position_linf_error = -1;
    auto work = Work(kEncoding, false);
    work.stl.cancellation_block_bytes = 1;
    const auto refused = mv::EncodeSurface(request, EncodeLimits(kEncoding),
                                           work, already_stopped.get_token());
    const bool correct =
        refused.status == mv::SurfaceStatus::kInvalidConfiguration &&
        refused.diagnostic.stage == mv::SurfaceStage::kConfiguration &&
        refused.diagnostic.code == mv::SurfaceCode::kInvalidBound &&
        !refused.output && !refused.facts &&
        context.ActiveTrackedPayload() == baseline &&
        context.ActiveWorkers() == 0;
    if (!correct)
      std::cerr << "paired E bound/inactive-tail status="
                << static_cast<std::uint32_t>(refused.status) << " code="
                << static_cast<std::uint32_t>(refused.diagnostic.code) << '\n';
    passed = correct && passed;
  }
  {
    auto limits = InspectLimits(kEncoding);
    limits.max_total_requested_allowance = limits.max_new_requested_bytes - 1;
    auto work = Work(kEncoding, true);
    work.stl.cancellation_block_bytes = 1;
    const auto refused =
        mv::InspectSurfaceOutput(*positive.output, captured.views, limits, work,
                                 already_stopped.get_token());
    const bool correct =
        refused.status == mv::SurfaceStatus::kInvalidConfiguration &&
        refused.diagnostic.stage == mv::SurfaceStage::kConfiguration &&
        refused.diagnostic.code == mv::SurfaceCode::kAllowanceTooSmall &&
        !refused.inspection && !refused.facts &&
        context.ActiveTrackedPayload() == baseline &&
        context.ActiveWorkers() == 0;
    if (!correct)
      std::cerr << "paired V allowance/inactive-tail status="
                << static_cast<std::uint32_t>(refused.status) << " code="
                << static_cast<std::uint32_t>(refused.diagnostic.code) << '\n';
    passed = correct && passed;
  }
  for (const auto kind : {detail::CatalogCopyKind::kDefinition,
                          detail::CatalogCopyKind::kImage}) {
    {
      std::stop_source stop;
      CatalogCopyStopState observed{&stop, kind, std::this_thread::get_id()};
      detail::TestOptions options;
      options.catalog_copy_progress = StopCatalogCopy;
      options.catalog_copy_state = &observed;
      const auto stopped =
          detail::EncodeForTest(plan.request, EncodeLimits(kEncoding),
                                copy_work, stop.get_token(), options);
      const bool joined = stopped.facts &&
                          stopped.facts->Execution().launched_workers > 0 &&
                          stopped.facts->Execution().launched_workers ==
                              stopped.facts->Execution().joined_workers;
      const bool correct =
          observed.observed.load(std::memory_order_relaxed) &&
          observed.observed_kind.load(std::memory_order_relaxed) == kind &&
          observed.first_completed.load(std::memory_order_relaxed) == 1 &&
          stopped.status == mv::SurfaceStatus::kCanceled &&
          stopped.diagnostic.stage == mv::SurfaceStage::kCatalog &&
          stopped.diagnostic.code == mv::SurfaceCode::kStopRequested &&
          !stopped.output && joined && context.ActiveWorkers() == 0;
      if (!correct) {
        std::cerr << "actual catalog "
                  << (kind == detail::CatalogCopyKind::kDefinition
                          ? "definition"
                          : "image")
                  << " write observed="
                  << observed.observed.load(std::memory_order_relaxed)
                  << " caller="
                  << observed.caller_observed.load(std::memory_order_relaxed)
                  << " status=" << static_cast<std::uint32_t>(stopped.status)
                  << " stage="
                  << static_cast<std::uint32_t>(stopped.diagnostic.stage);
        if (stopped.facts) {
          const auto execution = stopped.facts->Execution();
          std::cerr << " launched=" << execution.launched_workers
                    << " joined=" << execution.joined_workers;
        } else {
          std::cerr << " execution=absent";
        }
        std::cerr << '\n';
      }
      passed = correct && passed;
    }
    Require(context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "actual catalog stop facts and temporary owners released");
  }
  return passed;
}
void Quote(std::ostream& stream, std::string_view text) {
  stream << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\')
      stream << '\\' << static_cast<char>(c);
    else if (c < 0x20)
      stream << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
             << static_cast<unsigned>(c) << std::dec;
    else
      stream << static_cast<char>(c);
  }
  stream << '"';
}
void U64(std::ostream& stream, std::uint64_t value) {
  stream << '"' << value << '"';
}
void HexWord(std::ostream& stream, std::uint64_t value, std::size_t bytes = 8) {
  stream << '"' << std::hex << std::setw(static_cast<int>(bytes * 2))
         << std::setfill('0') << value << std::dec << '"';
}
void HexBytes(std::ostream& stream, std::span<const std::byte> bytes) {
  stream << '"' << std::hex << std::setfill('0');
  for (auto byte : bytes)
    stream << std::setw(2) << std::to_integer<unsigned>(byte);
  stream << std::dec << '"';
}
void HexString(std::ostream& stream, std::string_view text) {
  HexBytes(stream, std::as_bytes(std::span{text.data(), text.size()}));
}
template <class T>
void OptionalU64(std::ostream& stream, const std::optional<T>& value) {
  if (value)
    U64(stream, static_cast<std::uint64_t>(*value));
  else
    stream << "null";
}
void OptionalBool(std::ostream& stream, std::optional<bool> value) {
  if (value)
    stream << (*value ? "true" : "false");
  else
    stream << "null";
}
template <class T>
void Indices(std::ostream& stream, std::span<const T> values) {
  stream << '[';
  bool first = true;
  for (auto value : values) {
    if (!first) stream << ',';
    first = false;
    U64(stream, value);
  }
  stream << ']';
}
namespace observer_wire {
constexpr std::string_view kPlannedJobs = "{\"planned_jobs\":";
constexpr std::string_view kLaunchedWorkers = ",\"launched_workers\":";
constexpr std::string_view kJoinedWorkers = ",\"joined_workers\":";
constexpr std::string_view kCallerJobs = ",\"caller_jobs\":";
constexpr std::string_view kStage = "{\"stage\":";
constexpr std::string_view kCode = ",\"code\":";
constexpr std::string_view kSubject = ",\"subject\":";
constexpr std::string_view kIndex = ",\"index\":";
constexpr std::string_view kSecondaryIndex = ",\"secondary_index\":";
constexpr std::string_view kComponent = ",\"component\":";
constexpr std::string_view kCode2 = "{\"code\":";
constexpr std::string_view kAttribute = ",\"attribute\":";
constexpr std::string_view kElement = ",\"element\":";
constexpr std::string_view kStorageIssue = ",\"storage_issue\":";
constexpr std::string_view kOffset = "{\"offset\":";
constexpr std::string_view kLength = ",\"length\":";
constexpr std::string_view kStatus = "{\"status\":";
constexpr std::string_view kJsonSpan = ",\"json_span\":";
constexpr std::string_view kResourceKind = ",\"resource_kind\":";
constexpr std::string_view kResourceIndex = ",\"resource_index\":";
constexpr std::string_view kResourceSpan = ",\"resource_span\":";
constexpr std::string_view kRuleId = ",\"rule_id\":";
constexpr std::string_view kByteOffset = ",\"byte_offset\":";
constexpr std::string_view kFacet = ",\"facet\":";
constexpr std::string_view kGeometryStatus = ",\"geometry_status\":";
constexpr std::string_view kGeometryDiagnostic = ",\"geometry_diagnostic\":";
constexpr std::string_view kKind = "{\"kind\":";
constexpr std::string_view kComplete = ",\"complete\":";
constexpr std::string_view kExaminedValues = ",\"examined_values\":";
constexpr std::string_view kChangedValues = ",\"changed_values\":";
constexpr std::string_view kMaxAbsoluteErrorUsedWords =
    ",\"max_absolute_error\":{\"used_words\":";
constexpr std::string_view kExponent2 = ",\"exponent2\":";
constexpr std::string_view kMagnitudeHexLeWords =
    ",\"magnitude_hex_le_words\":[";
constexpr std::string_view kSourceAttribute = "]},\"source_attribute\":";
constexpr std::string_view kSourceRow = ",\"source_row\":";
constexpr std::string_view kOriginalCounts = "{\"original_counts\":";
constexpr std::string_view kTriangleCounts = ",\"triangle_counts\":";
constexpr std::string_view kWireCounts = ",\"wire_counts\":";
constexpr std::string_view kPositions = ",\"positions\":";
constexpr std::string_view kUv = ",\"uv\":[";
constexpr std::string_view kExecution = ",\"execution\":";
constexpr std::string_view kWorkSourceScalarPairs =
    ",\"work\":{\"source_scalar_pairs\":";
constexpr std::string_view kDescriptorBytePairs = ",\"descriptor_byte_pairs\":";
constexpr std::string_view kCoincidenceComparisons =
    ",\"coincidence_comparisons\":";
constexpr std::string_view kRelationshipSteps = ",\"relationship_steps\":";
constexpr std::string_view kNewRequestedBytes = ",\"new_requested_bytes\":";
constexpr std::string_view kProfiles = ",\"profiles\":[";
constexpr std::string_view kKind2 = ",\"kind\":";
constexpr std::string_view kCallOrdinal = ",\"call_ordinal\":";
constexpr std::string_view kPrimitive = ",\"primitive\":";
constexpr std::string_view kFactorComponent = ",\"factor_component\":";
constexpr std::string_view kStatus2 = ",\"status\":";
constexpr std::string_view kNearestEven = ",\"nearest_even\":";
constexpr std::string_view kFtzDisabled = ",\"ftz_disabled\":";
constexpr std::string_view kDazDisabled = ",\"daz_disabled\":";
constexpr std::string_view kDelegates = "],\"delegates\":[";
constexpr std::string_view kFacts = ",\"facts\":";
constexpr std::string_view kDiagnostic = ",\"diagnostic\":";
constexpr std::string_view kRequestedBytesAtReturn =
    ",\"requested_bytes_at_return\":";
constexpr std::string_view kErrorByteOffset = ",\"error_byte_offset\":";
constexpr std::string_view kWorkersUsed = ",\"workers_used\":";
constexpr std::string_view kDiagnostic2 = "{\"diagnostic\":";
constexpr std::string_view kExecutionCodec = ",\"execution\":{\"codec\":";
constexpr std::string_view kMaterialization = ",\"materialization\":";
constexpr std::string_view kInspection = ",\"inspection\":";
constexpr std::string_view kRequestedBytesAtReturn2 =
    "},\"requested_bytes_at_return\":";
constexpr std::string_view kCodec = "{\"codec\":";
constexpr std::string_view kTotal = ",\"total\":";
constexpr std::string_view kWriteFacts = ",\"write_facts\":";
constexpr std::string_view kEncoding = "{\"encoding\":";
constexpr std::string_view kFaceNormalAttribute = ",\"face_normal_attribute\":";
constexpr std::string_view kSourceVertexCount = ",\"source_vertex_count\":";
constexpr std::string_view kSourceFaceCount = ",\"source_face_count\":";
constexpr std::string_view kCornerExpansionAcknowledged =
    ",\"corner_expansion_acknowledged\":";
constexpr std::string_view kSourceEncoding = ",\"source_encoding\":";
constexpr std::string_view kMetadataPolicy = ",\"metadata_policy\":";
constexpr std::string_view kMaterial = "{\"material\":";
constexpr std::string_view kCoreF32 = ",\"core_f32\":[";
constexpr std::string_view kAuthored = "],\"authored\":[";
constexpr std::string_view kObserved = "],\"observed\":[";
constexpr std::string_view kLexicalCalls = "],\"lexical_calls\":";
constexpr std::string_view kFailure = ",\"failure\":";
constexpr std::string_view kPositionsF64 = "{\"positions_f64\":[";
constexpr std::string_view kFaceOffsets = "],\"face_offsets\":";
constexpr std::string_view kCornerVertices = ",\"corner_vertices\":";
constexpr std::string_view kAttributes = ",\"attributes\":[";
constexpr std::string_view kDomain = "{\"domain\":";
constexpr std::string_view kNameHex = ",\"name_hex\":";
constexpr std::string_view kSemanticHex = ",\"semantic_hex\":";
constexpr std::string_view kSetIndex = ",\"set_index\":";
constexpr std::string_view kComponents = ",\"components\":";
constexpr std::string_view kScalar = ",\"scalar\":";
constexpr std::string_view kWords = ",\"words\":[";
constexpr std::string_view kOffsets = "],\"offsets\":";
constexpr std::string_view kPresenceHex = ",\"presence_hex\":";
constexpr std::string_view kMetadata = ",\"metadata\":[";
constexpr std::string_view kKeyHex = "{\"key_hex\":";
constexpr std::string_view kValueHex = ",\"value_hex\":";
constexpr std::string_view kRepresentation = ",\"representation\":";
constexpr std::string_view kOriginal = ",\"original\":";
constexpr std::string_view kTriangles = ",\"triangles\":";
constexpr std::string_view kGeometryMapsFaceSources =
    ",\"geometry_maps\":{\"face_sources\":";
constexpr std::string_view kCornerSources = ",\"corner_sources\":";
constexpr std::string_view kFaceOutputOffsets = ",\"face_output_offsets\":";
constexpr std::string_view kWireMaps = ",\"wire_maps\":{";
constexpr std::string_view kPrimitives = ",\"primitives\":[";
constexpr std::string_view kOrdinal = "{\"ordinal\":";
constexpr std::string_view kFirstWireVertex = ",\"first_wire_vertex\":";
constexpr std::string_view kWireVertices = ",\"wire_vertices\":";
constexpr std::string_view kFirstWireTriangle = ",\"first_wire_triangle\":";
constexpr std::string_view kWireTriangles = ",\"wire_triangles\":";
constexpr std::string_view kMaterial2 = ",\"material\":";
constexpr std::string_view kUvMap = "],\"uv_map\":[";
constexpr std::string_view kSourceAttribute2 = "{\"source_attribute\":";
constexpr std::string_view kSourceSet = ",\"source_set\":";
constexpr std::string_view kWireSet = ",\"wire_set\":";
constexpr std::string_view kMaterialFactors = "],\"material_factors\":[";
constexpr std::string_view kSourceMaterial = "{\"source_material\":";
constexpr std::string_view kWireMaterial = ",\"wire_material\":";
constexpr std::string_view kSourceCoreF32 = ",\"source_core_f32\":[";
constexpr std::string_view kWireCoreF32 = "],\"wire_core_f32\":[";
constexpr std::string_view kMembers = "],\"members\":[";
constexpr std::string_view kPath = "{\"path\":";
constexpr std::string_view kBytes = ",\"bytes\":";
constexpr std::string_view kSha256 = ",\"sha256\":";
constexpr std::string_view kNewRequestedBytes2 = "],\"new_requested_bytes\":";
constexpr std::string_view kSchema = "{\"schema\":";
constexpr std::string_view kCase = ",\"case\":";
constexpr std::string_view kEncodeStatus = ",\"encode\":{\"status\":";
constexpr std::string_view kOutput = ",\"output\":";
constexpr std::string_view kInspect = "},\"inspect\":";
constexpr std::string_view kProofReturned = ",\"proof_returned\":";
constexpr std::string_view kCapturedMembers = ",\"captured_members\":[";
constexpr std::string_view kProofNewRequestedBytes =
    "],\"proof_new_requested_bytes\":";
constexpr std::string_view kOwnerReturned = ",\"owner_returned\":";
constexpr std::string_view kSurfaceCalled = ",\"surface_called\":false}\n";
}  // namespace observer_wire
void Execution(std::ostream& stream,
               const geom::MaterializationExecutionStats& execution) {
  stream << observer_wire::kPlannedJobs;
  U64(stream, execution.planned_jobs);
  stream << observer_wire::kLaunchedWorkers;
  U64(stream, execution.launched_workers);
  stream << observer_wire::kJoinedWorkers;
  U64(stream, execution.joined_workers);
  stream << observer_wire::kCallerJobs;
  U64(stream, execution.caller_jobs);
  stream << '}';
}
void SurfaceDiagnostic(std::ostream& stream,
                       const mv::SurfaceDiagnostic& diagnostic) {
  stream << observer_wire::kStage
         << static_cast<std::uint32_t>(diagnostic.stage) << observer_wire::kCode
         << static_cast<std::uint32_t>(diagnostic.code)
         << observer_wire::kSubject
         << static_cast<std::uint32_t>(diagnostic.subject)
         << observer_wire::kIndex;
  OptionalU64(stream, diagnostic.index);
  stream << observer_wire::kSecondaryIndex;
  OptionalU64(stream, diagnostic.secondary_index);
  stream << observer_wire::kComponent;
  if (diagnostic.component)
    stream << *diagnostic.component;
  else
    stream << "null";
  stream << '}';
}
void StorageIssue(std::ostream& stream,
                  const std::optional<geom::StorageIssue>& issue) {
  if (!issue) {
    stream << "null";
    return;
  }
  stream << observer_wire::kCode2 << static_cast<unsigned>(issue->code)
         << observer_wire::kAttribute;
  OptionalU64(stream, issue->attribute);
  stream << observer_wire::kElement;
  OptionalU64(stream, issue->element);
  stream << '}';
}
void GeometryDiagnostic(std::ostream& stream,
                        const geom::MaterializationDiagnostic& diagnostic) {
  stream << observer_wire::kCode2 << static_cast<unsigned>(diagnostic.code)
         << observer_wire::kAttribute;
  OptionalU64(stream, diagnostic.attribute);
  stream << observer_wire::kElement;
  OptionalU64(stream, diagnostic.element);
  stream << observer_wire::kStorageIssue;
  StorageIssue(stream, diagnostic.storage_issue);
  stream << '}';
}
void SourceSpan(std::ostream& stream,
                const std::optional<mv::GltfSourceSpan>& span) {
  if (!span) {
    stream << "null";
    return;
  }
  stream << observer_wire::kOffset;
  U64(stream, span->offset);
  stream << observer_wire::kLength;
  U64(stream, span->length);
  stream << '}';
}
void GltfDiagnostic(std::ostream& stream,
                    const mv::GltfDiagnostic& diagnostic) {
  stream << observer_wire::kStatus << static_cast<unsigned>(diagnostic.status)
         << observer_wire::kCode << static_cast<unsigned>(diagnostic.code)
         << observer_wire::kJsonSpan;
  SourceSpan(stream, diagnostic.json_span);
  stream << observer_wire::kResourceKind;
  if (diagnostic.resource_kind)
    stream << static_cast<unsigned>(*diagnostic.resource_kind);
  else
    stream << "null";
  stream << observer_wire::kResourceIndex;
  OptionalU64(stream, diagnostic.resource_index);
  stream << observer_wire::kResourceSpan;
  SourceSpan(stream, diagnostic.resource_span);
  stream << observer_wire::kRuleId << diagnostic.rule_id << '}';
}
void StlDiagnostic(std::ostream& stream, const mv::StlDiagnostic& diagnostic) {
  stream << observer_wire::kStage << static_cast<unsigned>(diagnostic.stage)
         << observer_wire::kCode << static_cast<unsigned>(diagnostic.code)
         << observer_wire::kByteOffset;
  OptionalU64(stream, diagnostic.byte_offset);
  stream << observer_wire::kFacet;
  OptionalU64(stream, diagnostic.facet);
  stream << observer_wire::kAttribute;
  OptionalU64(stream, diagnostic.attribute);
  stream << observer_wire::kGeometryStatus;
  if (diagnostic.geometry_status)
    stream << static_cast<unsigned>(*diagnostic.geometry_status);
  else
    stream << "null";
  stream << observer_wire::kGeometryDiagnostic;
  if (diagnostic.geometry_diagnostic)
    GeometryDiagnostic(stream, *diagnostic.geometry_diagnostic);
  else
    stream << "null";
  stream << observer_wire::kStorageIssue;
  StorageIssue(stream, diagnostic.storage_issue);
  stream << '}';
}
void Summary(std::ostream& stream, const mv::SurfaceNumericSummary& summary) {
  stream << observer_wire::kKind << static_cast<unsigned>(summary.kind)
         << observer_wire::kComplete << (summary.complete ? "true" : "false")
         << observer_wire::kExaminedValues;
  U64(stream, summary.examined_values);
  stream << observer_wire::kChangedValues;
  U64(stream, summary.changed_values);
  stream << observer_wire::kMaxAbsoluteErrorUsedWords
         << summary.max_absolute_error.used_words << observer_wire::kExponent2
         << summary.max_absolute_error.exponent2
         << observer_wire::kMagnitudeHexLeWords;
  for (std::size_t i = 0; i < summary.max_absolute_error.magnitude.size();
       ++i) {
    if (i) stream << ',';
    HexWord(stream, summary.max_absolute_error.magnitude[i]);
  }
  stream << observer_wire::kSourceAttribute;
  OptionalU64(stream, summary.source_attribute);
  stream << observer_wire::kSourceRow;
  OptionalU64(stream, summary.source_row);
  stream << observer_wire::kComponent;
  if (summary.component)
    stream << *summary.component;
  else
    stream << "null";
  stream << '}';
}
void MeshCounts(std::ostream& stream,
                const std::optional<mv::SurfaceMeshCounts>& counts) {
  if (!counts) {
    stream << "null";
    return;
  }
  const auto& c = *counts;
  constexpr std::array<std::string_view, 11> names{
      "vertices",        "faces",
      "corners",         "face_offsets",
      "attributes",      "attribute_scalars",
      "attribute_bytes", "attribute_offsets",
      "presence_bytes",  "metadata_entries",
      "descriptor_bytes"};
  const std::array<std::uint64_t, 11> values{
      c.vertices,        c.faces,
      c.corners,         c.face_offsets,
      c.attributes,      c.attribute_scalars,
      c.attribute_bytes, c.attribute_offsets,
      c.presence_bytes,  c.metadata_entries,
      c.descriptor_bytes};
  stream << '{';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) stream << ',';
    Quote(stream, names[i]);
    stream << ':';
    U64(stream, values[i]);
  }
  stream << '}';
}
void WireCounts(std::ostream& stream,
                const std::optional<mv::SurfaceWireCounts>& counts) {
  if (!counts) {
    stream << "null";
    return;
  }
  const auto& c = *counts;
  constexpr std::array<std::string_view, 11> names{
      "primitives",  "vertices",  "triangles",   "corners",
      "map_entries", "materials", "textures",    "images",
      "samplers",    "members",   "member_bytes"};
  const std::array<std::uint64_t, 11> values{
      c.primitives,  c.vertices,  c.triangles,   c.corners,
      c.map_entries, c.materials, c.textures,    c.images,
      c.samplers,    c.members,   c.member_bytes};
  stream << '{';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) stream << ',';
    Quote(stream, names[i]);
    stream << ':';
    U64(stream, values[i]);
  }
  stream << '}';
}
void Facts(std::ostream& stream, const mv::SurfaceFacts& facts) {
  stream << observer_wire::kOriginalCounts;
  MeshCounts(stream, facts.OriginalCounts());
  stream << observer_wire::kTriangleCounts;
  MeshCounts(stream, facts.TriangleCounts());
  stream << observer_wire::kWireCounts;
  WireCounts(stream, facts.WireCounts());
  stream << observer_wire::kPositions;
  if (facts.PositionNumeric())
    Summary(stream, *facts.PositionNumeric());
  else
    stream << "null";
  stream << observer_wire::kUv;
  bool first = true;
  for (const auto& field : facts.UvNumeric()) {
    if (!first) stream << ',';
    first = false;
    Summary(stream, field);
  }
  stream << ']';
  stream << observer_wire::kExecution;
  Execution(stream, facts.Execution());
  const auto work = facts.ExaminedWork();
  stream << observer_wire::kWorkSourceScalarPairs;
  U64(stream, work.source_scalar_pairs);
  stream << observer_wire::kDescriptorBytePairs;
  U64(stream, work.descriptor_byte_pairs);
  stream << observer_wire::kCoincidenceComparisons;
  U64(stream, work.coincidence_comparisons);
  stream << observer_wire::kRelationshipSteps;
  U64(stream, work.relationship_steps);
  stream << '}';
  stream << observer_wire::kNewRequestedBytes;
  U64(stream, facts.NewRequestedBytes());
  stream << observer_wire::kProfiles;
  first = true;
  for (const auto& p : facts.NumericDelegateProfiles()) {
    if (!first) stream << ',';
    first = false;
    stream << observer_wire::kStage << static_cast<unsigned>(p.stage)
           << observer_wire::kKind2 << static_cast<unsigned>(p.kind)
           << observer_wire::kCallOrdinal;
    U64(stream, p.call_ordinal);
    stream << observer_wire::kPrimitive;
    OptionalU64(stream, p.primitive);
    stream << observer_wire::kFactorComponent;
    if (p.factor_component)
      stream << *p.factor_component;
    else
      stream << "null";
    stream << observer_wire::kStatus2 << static_cast<unsigned>(p.facts.status)
           << observer_wire::kNearestEven;
    OptionalBool(stream, p.facts.nearest_even);
    stream << observer_wire::kFtzDisabled;
    OptionalBool(stream, p.facts.ftz_disabled);
    stream << observer_wire::kDazDisabled;
    OptionalBool(stream, p.facts.daz_disabled);
    stream << '}';
  }
  stream << observer_wire::kDelegates;
  first = true;
  for (const auto& delegate : facts.Delegates()) {
    if (!first) stream << ',';
    first = false;
    stream << observer_wire::kStage << static_cast<unsigned>(delegate.stage)
           << observer_wire::kKind2 << static_cast<unsigned>(delegate.kind)
           << observer_wire::kCallOrdinal;
    U64(stream, delegate.call_ordinal);
    stream << observer_wire::kPrimitive;
    OptionalU64(stream, delegate.primitive);
    stream << observer_wire::kFacts;
    std::visit(
        [&](const auto& value) {
          using T = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::is_same_v<T, mv::SurfaceGeometryFacts>) {
            stream << observer_wire::kStatus
                   << static_cast<unsigned>(value.status)
                   << observer_wire::kDiagnostic;
            GeometryDiagnostic(stream, value.diagnostic);
            stream << observer_wire::kExecution;
            Execution(stream, value.execution);
            stream << observer_wire::kRequestedBytesAtReturn;
            OptionalU64(stream, value.requested_bytes_at_return);
            stream << '}';
          } else if constexpr (std::is_same_v<T, mv::SurfaceGlbFacts>) {
            stream << observer_wire::kStatus
                   << static_cast<unsigned>(value.status)
                   << observer_wire::kErrorByteOffset;
            U64(stream, value.error_byte_offset);
            stream << observer_wire::kWorkersUsed;
            U64(stream, value.workers_used);
            stream << '}';
          } else if constexpr (std::is_same_v<T, mv::SurfaceGltfFacts>) {
            stream << observer_wire::kDiagnostic2;
            GltfDiagnostic(stream, value.diagnostic);
            stream << observer_wire::kRequestedBytesAtReturn;
            OptionalU64(stream, value.requested_bytes_at_return);
            stream << '}';
          } else if constexpr (std::is_same_v<T, mv::SurfaceStlFacts>) {
            stream << observer_wire::kStatus
                   << static_cast<unsigned>(value.status)
                   << observer_wire::kDiagnostic;
            StlDiagnostic(stream, value.diagnostic);
            stream << observer_wire::kExecutionCodec;
            Execution(stream, value.execution.codec);
            stream << observer_wire::kMaterialization;
            Execution(stream, value.execution.materialization);
            stream << observer_wire::kInspection;
            Execution(stream, value.execution.inspection);
            stream << observer_wire::kRequestedBytesAtReturn2;
            if (value.requested_bytes_at_return) {
              const auto& bytes = *value.requested_bytes_at_return;
              stream << observer_wire::kCodec;
              U64(stream, bytes.codec);
              stream << observer_wire::kMaterialization;
              U64(stream, bytes.materialization);
              stream << observer_wire::kInspection;
              U64(stream, bytes.inspection);
              stream << observer_wire::kTotal;
              U64(stream, bytes.total);
              stream << '}';
            } else
              stream << "null";
            stream << observer_wire::kWriteFacts;
            if (value.write_facts) {
              const auto& written = *value.write_facts;
              stream << observer_wire::kEncoding
                     << static_cast<unsigned>(written.encoding)
                     << observer_wire::kFaceNormalAttribute;
              U64(stream, written.face_normal_attribute);
              stream << observer_wire::kSourceVertexCount;
              U64(stream, written.source_vertex_count);
              stream << observer_wire::kSourceFaceCount;
              U64(stream, written.source_face_count);
              stream << observer_wire::kCornerExpansionAcknowledged
                     << (written.corner_expansion_acknowledged ? "true"
                                                               : "false")
                     << observer_wire::kSourceEncoding;
              if (written.source_encoding)
                stream << static_cast<unsigned>(*written.source_encoding);
              else
                stream << "null";
              stream << observer_wire::kMetadataPolicy
                     << static_cast<unsigned>(written.metadata_policy) << '}';
            } else
              stream << "null";
            stream << '}';
          } else {
            stream << observer_wire::kMaterial;
            U64(stream, value.material);
            stream << observer_wire::kCoreF32;
            for (std::size_t i = 0; i < value.core_f32.size(); ++i) {
              if (i) stream << ',';
              HexWord(stream, value.core_f32[i], 4);
            }
            stream << observer_wire::kAuthored;
            for (std::size_t i = 0; i < value.authored.size(); ++i) {
              if (i) stream << ',';
              stream << (value.authored[i] ? "true" : "false");
            }
            stream << observer_wire::kObserved;
            for (std::size_t i = 0; i < value.observed.size(); ++i) {
              if (i) stream << ',';
              stream << (value.observed[i] ? "true" : "false");
            }
            stream << observer_wire::kLexicalCalls;
            U64(stream, value.lexical_calls);
            stream << observer_wire::kComplete
                   << (value.complete ? "true" : "false")
                   << observer_wire::kFailure;
            if (value.failure)
              GltfDiagnostic(stream, *value.failure);
            else
              stream << "null";
            stream << '}';
          }
        },
        delegate.facts);
    stream << '}';
  }
  stream << "]}";
}
void Mesh(std::ostream& stream, geom::MeshReadView mesh) {
  stream << observer_wire::kPositionsF64;
  for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
    if (i) stream << ',';
    HexWord(stream, std::bit_cast<std::uint64_t>(mesh.positions[i]));
  }
  stream << observer_wire::kFaceOffsets;
  Indices(stream, mesh.face_offsets);
  stream << observer_wire::kCornerVertices;
  Indices(stream, mesh.corner_vertices);
  stream << observer_wire::kAttributes;
  for (std::size_t ordinal = 0; ordinal < mesh.attributes.size(); ++ordinal) {
    const auto& a = mesh.attributes[ordinal];
    if (ordinal) stream << ',';
    stream << observer_wire::kDomain << static_cast<unsigned>(a.domain)
           << observer_wire::kNameHex;
    HexString(stream, a.name);
    stream << observer_wire::kSemanticHex;
    HexString(stream, a.semantic);
    stream << observer_wire::kSetIndex;
    if (a.set_index)
      stream << *a.set_index;
    else
      stream << "null";
    stream << observer_wire::kComponents << a.components
           << observer_wire::kScalar << a.values.index()
           << observer_wire::kWords;
    std::visit(
        [&](auto values) {
          using T = typename decltype(values)::value_type;
          for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) stream << ',';
            if constexpr (std::is_same_v<T, float>)
              HexWord(stream, std::bit_cast<std::uint32_t>(values[i]),
                      sizeof(T));
            else if constexpr (std::is_same_v<T, double>)
              HexWord(stream, std::bit_cast<std::uint64_t>(values[i]),
                      sizeof(T));
            else if constexpr (std::is_same_v<T, std::int32_t>)
              HexWord(stream, std::bit_cast<std::uint32_t>(values[i]),
                      sizeof(T));
            else
              HexWord(stream, values[i], sizeof(T));
          }
        },
        a.values);
    stream << observer_wire::kOffsets;
    if (a.offsets)
      Indices(stream, *a.offsets);
    else
      stream << "null";
    stream << observer_wire::kPresenceHex;
    if (a.present)
      HexBytes(stream, std::as_bytes(*a.present));
    else
      stream << "null";
    stream << observer_wire::kMetadata;
    for (std::size_t i = 0; i < a.metadata.size(); ++i) {
      if (i) stream << ',';
      stream << observer_wire::kKeyHex;
      HexString(stream, a.metadata[i].key);
      stream << observer_wire::kValueHex;
      HexString(stream, a.metadata[i].value);
      stream << '}';
    }
    stream << "]}";
  }
  stream << "]}";
}
void Output(std::ostream& stream, const mv::SurfaceOutput& output) {
  stream << observer_wire::kEncoding << static_cast<unsigned>(output.Encoding())
         << observer_wire::kRepresentation
         << static_cast<unsigned>(output.Representation())
         << observer_wire::kOriginal;
  Mesh(stream, output.Original().Source().View());
  stream << observer_wire::kTriangles;
  Mesh(stream, output.Triangles().Mesh().View());
  stream << observer_wire::kGeometryMapsFaceSources;
  Indices(stream, output.Triangles().FaceSources());
  stream << observer_wire::kCornerSources;
  Indices(stream, output.Triangles().CornerSources());
  stream << observer_wire::kFaceOutputOffsets;
  Indices(stream, output.Triangles().FaceOutputOffsets());
  stream << '}';
  constexpr std::array<std::string_view, 11> names{
      "triangle_sources",      "triangle_faces",       "corner_sources",
      "corner_corners",        "vertex_sources",       "source_vertex_offsets",
      "source_vertex_members", "source_face_offsets",  "source_face_members",
      "source_corner_offsets", "source_corner_members"};
  const std::array<std::span<const std::uint64_t>, 11> maps{
      output.WireTriangleSources(),     output.WireTriangleFaces(),
      output.WireCornerSources(),       output.WireCornerCorners(),
      output.WireVertexSources(),       output.SourceVertexOffsets(),
      output.SourceVertexWireMembers(), output.SourceFaceOffsets(),
      output.SourceFaceWireTriangles(), output.SourceCornerOffsets(),
      output.SourceCornerWireCorners()};
  stream << observer_wire::kWireMaps;
  for (std::size_t i = 0; i < maps.size(); ++i) {
    if (i) stream << ',';
    Quote(stream, names[i]);
    stream << ':';
    Indices(stream, maps[i]);
  }
  stream << '}';
  stream << observer_wire::kPrimitives;
  bool first = true;
  for (const auto& p : output.Primitives()) {
    if (!first) stream << ',';
    first = false;
    stream << observer_wire::kOrdinal;
    U64(stream, p.ordinal);
    stream << observer_wire::kFirstWireVertex;
    U64(stream, p.first_wire_vertex);
    stream << observer_wire::kWireVertices;
    U64(stream, p.wire_vertices);
    stream << observer_wire::kFirstWireTriangle;
    U64(stream, p.first_wire_triangle);
    stream << observer_wire::kWireTriangles;
    U64(stream, p.wire_triangles);
    stream << observer_wire::kMaterial2;
    OptionalU64(stream, p.material);
    stream << '}';
  }
  stream << observer_wire::kUvMap;
  first = true;
  for (const auto& uv : output.UvMapping()) {
    if (!first) stream << ',';
    first = false;
    stream << observer_wire::kSourceAttribute2;
    U64(stream, uv.source_attribute);
    stream << observer_wire::kSourceSet << uv.source_set
           << observer_wire::kWireSet << uv.wire_set << '}';
  }
  stream << observer_wire::kMaterialFactors;
  first = true;
  for (const auto& material : output.MaterialFactors()) {
    if (!first) stream << ',';
    first = false;
    stream << observer_wire::kSourceMaterial;
    U64(stream, material.source_material);
    stream << observer_wire::kWireMaterial;
    U64(stream, material.wire_material);
    stream << observer_wire::kSourceCoreF32;
    for (std::size_t i = 0; i < material.source_core_f32.size(); ++i) {
      if (i) stream << ',';
      HexWord(stream, material.source_core_f32[i], 4);
    }
    stream << observer_wire::kWireCoreF32;
    for (std::size_t i = 0; i < material.wire_core_f32.size(); ++i) {
      if (i) stream << ',';
      HexWord(stream, material.wire_core_f32[i], 4);
    }
    stream << "]}";
  }
  stream << observer_wire::kMembers;
  first = true;
  for (const auto& member : output.Members()) {
    if (!first) stream << ',';
    first = false;
    stream << observer_wire::kPath;
    Quote(stream, member.path);
    stream << observer_wire::kKind2 << static_cast<unsigned>(member.kind)
           << observer_wire::kBytes;
    U64(stream, member.bytes.size());
    stream << observer_wire::kSha256;
    HexBytes(stream, member.sha256);
    stream << '}';
  }
  stream << observer_wire::kNewRequestedBytes2;
  U64(stream, output.NewRequestedBytes());
  stream << '}';
}
void DumpCase(const fs::path& directory, std::string_view name,
              const mv::SurfaceEncodeResult& encoded,
              const mv::SurfaceInspectionResult* inspected) {
  const auto root = directory / fs::path(name);
  Require(!fs::exists(root), "observation case path must be fresh");
  Require(fs::create_directory(root), "create owned observation case");
  if (encoded.output)
    for (const auto& member : encoded.output->Members()) {
      const auto path = root / "bundle" / fs::path(member.path);
      fs::create_directories(path.parent_path());
      Require(!fs::exists(path), "member artifact must be new");
      std::ofstream file(path, std::ios::binary | std::ios::out);
      Require(file.good(), "open actual returned member artifact");
      file.write(reinterpret_cast<const char*>(member.bytes.data()),
                 static_cast<std::streamsize>(member.bytes.size()));
      file.close();
      Require(!file.fail(), "close actual returned member artifact");
    }
  std::ofstream stream(root / "observation.json",
                       std::ios::binary | std::ios::out);
  stream.imbue(std::locale::classic());
  Require(stream.good(), "open observation JSON");
  stream << observer_wire::kSchema;
  Quote(stream, kObservationSchema);
  stream << observer_wire::kCase;
  Quote(stream, name);
  stream << observer_wire::kEncodeStatus
         << static_cast<unsigned>(encoded.status) << observer_wire::kDiagnostic;
  SurfaceDiagnostic(stream, encoded.diagnostic);
  stream << observer_wire::kFacts;
  if (encoded.facts)
    Facts(stream, *encoded.facts);
  else
    stream << "null";
  stream << observer_wire::kOutput;
  if (encoded.output)
    Output(stream, *encoded.output);
  else
    stream << "null";
  stream << observer_wire::kInspect;
  if (inspected) {
    stream << observer_wire::kStatus << static_cast<unsigned>(inspected->status)
           << observer_wire::kDiagnostic;
    SurfaceDiagnostic(stream, inspected->diagnostic);
    stream << observer_wire::kFacts;
    if (inspected->facts)
      Facts(stream, *inspected->facts);
    else
      stream << "null";
    stream << observer_wire::kProofReturned
           << (inspected->inspection ? "true" : "false")
           << observer_wire::kCapturedMembers;
    if (inspected->inspection) {
      bool first = true;
      for (const auto& member : inspected->inspection->CapturedMembers()) {
        const auto path = root / "captured" / fs::path(member.path);
        fs::create_directories(path.parent_path());
        Require(!fs::exists(path), "captured artifact must be new");
        std::ofstream file(path, std::ios::binary | std::ios::out);
        Require(file.good(), "open actual captured member artifact");
        file.write(reinterpret_cast<const char*>(member.bytes.data()),
                   static_cast<std::streamsize>(member.bytes.size()));
        file.close();
        Require(!file.fail(), "close actual captured member artifact");
        if (!first) stream << ',';
        first = false;
        stream << observer_wire::kPath;
        Quote(stream, member.path);
        stream << observer_wire::kKind2 << static_cast<unsigned>(member.kind)
               << observer_wire::kBytes;
        U64(stream, member.bytes.size());
        stream << observer_wire::kSha256;
        HexBytes(stream, member.sha256);
        stream << '}';
      }
    }
    stream << observer_wire::kProofNewRequestedBytes;
    if (inspected->inspection)
      U64(stream, inspected->inspection->NewRequestedBytes());
    else
      stream << "null";
    stream << '}';
  } else
    stream << "null";
  stream << "}\n";
  stream.close();
  Require(!stream.fail(), "close observation JSON");
}
void DumpConstruction(const fs::path& directory, std::string_view name,
                      const ConstructionWitness& witness) {
  constexpr std::string_view kConstructionSchema =
      "meshvale.surface-tests.construction/1";
  Require(witness.observed, "only an actual returned construction is recorded");
  const auto root = directory / fs::path(name);
  Require(!fs::exists(root), "construction witness path must be fresh");
  Require(fs::create_directory(root), "create owned construction witness");
  std::ofstream stream(root / "construction.json",
                       std::ios::binary | std::ios::out);
  stream.imbue(std::locale::classic());
  Require(stream.good(), "open actual construction witness");
  stream << observer_wire::kSchema;
  Quote(stream, kConstructionSchema);
  stream << observer_wire::kCase;
  Quote(stream, name);
  stream << observer_wire::kStatus2 << static_cast<unsigned>(witness.status)
         << observer_wire::kDiagnostic;
  GeometryDiagnostic(stream, witness.diagnostic);
  stream << observer_wire::kExecution;
  Execution(stream, witness.execution);
  stream << observer_wire::kOwnerReturned
         << (witness.owner_returned ? "true" : "false")
         << observer_wire::kSurfaceCalled;
  stream.close();
  Require(!stream.fail(), "close actual construction witness");
}
mv::SurfaceEncodeRequest SimpleRequest(const Prepared& source) {
  mv::SurfaceEncodeRequest request{
      .original = source.original,
      .triangles = source.triangles,
      .representation = mv::SurfaceRepresentation::kInvalid,
      .acknowledge_triangle_conversion = false,
      .acknowledge_corner_expansion = false,
      .acknowledge_unused_vertices = false,
      .channels = {},
      .numeric = {},
      .destination = {}};
  request.representation = mv::SurfaceRepresentation::kAnnotatedBundleV1;
  request.acknowledge_triangle_conversion = true;
  request.acknowledge_corner_expansion = true;
  request.acknowledge_unused_vertices = true;
  request.numeric.positions = mv::SurfaceNarrowing::kExact32;
  request.destination = mv::GltfSurfaceOptions{
      .encoding = mv::SurfaceEncoding::kGltfJson,
      .coordinates = mv::SurfaceCoordinates::kDeclaredMetersRightHandedYUp,
      .uv = {},
      .catalog = nullptr,
      .materials = {},
      .catalog_uv = {}};
  return request;
}
void CompleteCorrespondence(const geom::ExecutionContext& context) {
  constexpr std::array<double, 15> positions{0, 0, 0, 2, 0,  0,  2, 2,
                                             0, 0, 2, 0, 77, 88, 99};
  constexpr std::array<geom::index_t, 2> quad_offsets{0, 4},
      triangle_offsets{0, 3};
  constexpr std::array<geom::index_t, 4> quad{0, 1, 2, 3};
  constexpr std::array<geom::index_t, 3> triangle{0, 1, 2};
  const auto original =
      Prepare({positions, geom::PolygonTopologyInput{quad_offsets, quad}, {}},
              context, false);
  if (!original) return;
  const auto unrelated = Prepare(
      {positions, geom::PolygonTopologyInput{triangle_offsets, triangle}, {}},
      context, false);
  Require(unrelated.has_value(), "genuine unrelated same-ledger triangle");
  const Prepared mixed{original->original, unrelated->triangles, {}};
  auto request = SimpleRequest(mixed);
  const auto refused =
      mv::EncodeSurface(request, EncodeLimits(mv::SurfaceEncoding::kGltfJson),
                        Work(mv::SurfaceEncoding::kGltfJson, false));
  Require(
      refused.status == mv::SurfaceStatus::kInvalidStorage &&
          refused.diagnostic.stage == mv::SurfaceStage::kCorrespondence &&
          refused.diagnostic.code == mv::SurfaceCode::kSourceCorrespondence &&
          refused.diagnostic.subject == mv::SurfaceSubject::kCorner &&
          refused.diagnostic.index == 3 && !refused.output,
      "unrelated genuine triangulation cannot omit original corner three");
  const auto valid = mv::EncodeSurface(
      SimpleRequest(*original), EncodeLimits(mv::SurfaceEncoding::kGltfJson),
      Work(mv::SurfaceEncoding::kGltfJson, false));
  Require(
      valid.status == mv::SurfaceStatus::kSuccess && valid.output &&
          valid.output->Original().Source().View().positions.size() ==
              positions.size() &&
          valid.output->SourceVertexOffsets()[4] ==
              valid.output->SourceVertexOffsets()[5],
      "unused original position remains legal with complete polygon coverage");
  const auto empty_range =
      Prepare({positions, geom::PolygonTopologyInput{kEmptyOffsets, {}}, {}},
              context, false);
  Require(empty_range.has_value(), "genuine zero-face source");
  const auto empty_valid = mv::EncodeSurface(
      SimpleRequest(*empty_range), EncodeLimits(mv::SurfaceEncoding::kGltfJson),
      Work(mv::SurfaceEncoding::kGltfJson, false));
  Require(empty_valid.status == mv::SurfaceStatus::kSuccess &&
              empty_valid.output &&
              empty_valid.output->Triangles().FaceOutputOffsets().size() == 1 &&
              empty_valid.output->Triangles().FaceOutputOffsets()[0] == 0,
          "genuine empty face-output range remains legal");
}
struct FocusRun {
  FocusRun(const geom::ExecutionContext& context, std::stop_token stop = {},
           const detail::TestOptions& options = {})
      : allocation(context, stop),
        facts(allocation),
        counts(Counts()),
        work(Work(mv::SurfaceEncoding::kGltfJson, false)),
        test(options),
        run(context, allocation, counts, work, facts, stop, test) {
    run.stage = mv::SurfaceStage::kNumeric;
  }
  mv::gltf_detail::AllocationContext allocation;
  detail::FactsState facts;
  mv::SurfaceCountLimits counts;
  mv::SurfaceWorkPolicy work;
  detail::TestOptions test;
  detail::Run run;
};
void SourceOrderedCoincidence(const geom::ExecutionContext& context) {
  constexpr std::array<std::uint64_t, 12> source_words{
      0x4000000000000000, 0, 0, 0x4000000000200000, 0, 0,
      0x3ff0000000000000, 0, 0, 0x3ff0000000400000, 0, 0};
  std::array<double, source_words.size()> source{};
  for (std::size_t i = 0; i < source.size(); ++i)
    source[i] = std::bit_cast<double>(source_words[i]);
  const auto prepared =
      Prepare({source, geom::PolygonTopologyInput{kEmptyOffsets, {}}, {}},
              context, false);
  if (!prepared) return;
  constexpr std::array<std::uint64_t, 4> ids{0, 1, 2, 3};
  constexpr std::array<std::uint64_t, 12> target{
      0x40000000, 0, 0, 0x40000000, 0, 0, 0x3f800000, 0, 0, 0x3f800000, 0, 0};
  for (bool both : {false, true}) {
    FocusRun scope(context);
    try {
      detail::CheckSurfaceCoincidence(
          prepared->original,
          std::span<const std::uint64_t>{ids}.first(both ? 4 : 2), target,
          mv::SurfaceEncoding::kGltfJson, scope.run);
      Require(false, "actual distinct source anchor must be refused");
    } catch (const detail::Failure& failure) {
      Require(failure.Diagnostic().code == mv::SurfaceCode::kNewCoincidence &&
                  failure.Diagnostic().index == 0 &&
                  failure.Diagnostic().secondary_index == 1,
              "earliest source pair wins even with reverse decoded-key groups");
    }
  }
  constexpr std::size_t kGroupVertices = 64;
  constexpr std::uint64_t kCompletedKeyAndGroupComparisons = 255;
  std::array<double, kGroupVertices * detail::kPositionComponents> same{};
  const auto coincident =
      Prepare({same, geom::PolygonTopologyInput{kEmptyOffsets, {}}, {}},
              context, false);
  Require(coincident.has_value(), "genuine large coincident source backing");
  std::array<std::uint64_t, kGroupVertices> used{};
  std::array<std::uint64_t, same.size()> words{};
  for (std::size_t i = 0; i < used.size(); ++i) used[i] = i;
  {
    FocusRun scope(context);
    detail::CheckSurfaceCoincidence(coincident->original, used, words,
                                    mv::SurfaceEncoding::kGltfJson, scope.run);
    Require(scope.facts.execution.launched_workers > 0 &&
                scope.facts.execution.launched_workers ==
                    scope.facts.execution.joined_workers &&
                scope.facts.work.source_scalar_pairs ==
                    (kGroupVertices - 1) * detail::kPositionComponents,
            "actual planned large anchor work joined and counted");
  }
  // For this deterministic all-equal 64-row sort: six half-run merge levels
  // complete 192 key comparisons, then 63 group comparisons. This threshold
  // selects only the subsequent real anchor callback, not sort/group polling.
  {
    FocusRun limited(context);
    limited.counts.max_coincidence_comparisons =
        kCompletedKeyAndGroupComparisons + 1;
    bool refused = false;
    try {
      detail::CheckSurfaceCoincidence(coincident->original, used, words,
                                      mv::SurfaceEncoding::kGltfJson,
                                      limited.run);
    } catch (const detail::Failure& failure) {
      refused = failure.Status() == mv::SurfaceStatus::kLimitExceeded;
    }
    Require(refused && limited.facts.work.source_scalar_pairs == 3 &&
                limited.facts.work.coincidence_comparisons ==
                    kCompletedKeyAndGroupComparisons + 1 &&
                limited.facts.execution.launched_workers ==
                    limited.facts.execution.joined_workers,
            "actual anchor permit cap includes one completed XYZ comparison");
  }
  struct AnchorStop {
    std::stop_source* stop;
    detail::FactsState* facts;
    std::atomic<bool> reached{false};
  };
  std::stop_source stop;
  FocusRun scope(context, stop.get_token());
  AnchorStop state{&stop, &scope.facts};
  scope.test.checkpoint_state = &state;
  scope.test.checkpoint = [](mv::SurfaceStage, std::uint64_t completed,
                             void* opaque) {
    auto& state = *static_cast<AnchorStop*>(opaque);
    if (state.facts->work.coincidence_comparisons ==
            kCompletedKeyAndGroupComparisons &&
        completed == 4) {
      state.reached.store(true, std::memory_order_relaxed);
      state.stop->request_stop();
    }
  };
  bool canceled = false;
  try {
    detail::CheckSurfaceCoincidence(coincident->original, used, words,
                                    mv::SurfaceEncoding::kGltfJson, scope.run);
  } catch (const detail::Failure& failure) {
    canceled = failure.Status() == mv::SurfaceStatus::kCanceled;
  }
  Require(canceled && state.reached.load(std::memory_order_relaxed) &&
              scope.facts.work.source_scalar_pairs > 0 &&
              scope.facts.execution.launched_workers ==
                  scope.facts.execution.joined_workers,
          "actual begun anchor comparison stop retains partial work and joins");
}
void EstablishedCoincidence(const geom::ExecutionContext& context) {
  // Four real source rows and two decoded-key groups in reverse source order.
  constexpr std::array<std::uint64_t, 12> original_words{
      0x4000000000000000, 0, 0, 0x4000000000200000, 0, 0,
      0x3ff0000000000000, 0, 0, 0x3ff0000000400000, 0, 0};
  std::array<double, original_words.size()> positions{};
  for (std::size_t i = 0; i < positions.size(); ++i)
    positions[i] = std::bit_cast<double>(original_words[i]);
  const auto prepared =
      Prepare({positions, geom::PolygonTopologyInput{kEmptyOffsets, {}}, {}},
              context, false);
  if (!prepared) return;
  constexpr std::array<std::uint64_t, 4> used{0, 1, 2, 3};
  constexpr std::array<std::uint64_t, 12> decoded{
      0x40000000, 0, 0, 0x40000000, 0, 0, 0x3f800000, 0, 0, 0x3f800000, 0, 0};
  constexpr std::uint64_t kKeyAndGroupComparisons = 7;
  constexpr std::uint64_t kAllComparisons = kKeyAndGroupComparisons + 2;
  enum class Interruption { kLaterStop, kLaterCap, kReducerStop, kUnexpected };
  for (const auto mode :
       {Interruption::kLaterStop, Interruption::kLaterCap,
        Interruption::kReducerStop, Interruption::kUnexpected}) {
    std::stop_source stop;
    FocusRun scope(context, stop.get_token());
    // Real selected serial policy, not an invented worker result: each Bulk
    // has one job and therefore uses the existing caller fallback.
    scope.work.surface.parallel_min_bytes = kMiB;
    struct State {
      std::stop_source* stop;
      detail::FactsState* facts;
      Interruption mode;
      bool reached = false;
    } state{&stop, &scope.facts, mode};
    scope.test.checkpoint_state = &state;
    scope.test.checkpoint = [](mv::SurfaceStage, std::uint64_t completed,
                               void* opaque) {
      auto& state = *static_cast<State*>(opaque);
      const bool later_anchor = state.facts->work.coincidence_comparisons ==
                                    kKeyAndGroupComparisons &&
                                completed == 1;
      const bool late_reducer =
          state.facts->work.coincidence_comparisons == kAllComparisons &&
          completed == 0;
      if ((state.mode == Interruption::kLaterStop && later_anchor) ||
          (state.mode == Interruption::kReducerStop && late_reducer)) {
        state.reached = true;
        state.stop->request_stop();
      } else if (state.mode == Interruption::kUnexpected && later_anchor) {
        state.reached = true;
        throw std::runtime_error(
            "unexpected anchor callback after actual bad pair");
      }
    };
    if (mode == Interruption::kLaterCap)
      scope.counts.max_coincidence_comparisons = kKeyAndGroupComparisons + 1;
    bool observed = false;
    try {
      detail::CheckSurfaceCoincidence(prepared->original, used, decoded,
                                      mv::SurfaceEncoding::kGltfJson,
                                      scope.run);
    } catch (const detail::Failure& failure) {
      const bool complete = mode == Interruption::kReducerStop;
      observed =
          mode != Interruption::kUnexpected &&
          failure.Status() == mv::SurfaceStatus::kUnsupported &&
          failure.Diagnostic().code == mv::SurfaceCode::kNewCoincidence &&
          failure.Diagnostic().index == (complete ? 0 : 2) &&
          failure.Diagnostic().secondary_index == (complete ? 1 : 3);
    } catch (const std::runtime_error& error) {
      observed = mode == Interruption::kUnexpected &&
                 std::string_view(error.what()) ==
                     "unexpected anchor callback after actual bad pair";
    }
    const bool complete = mode == Interruption::kReducerStop;
    Require(
        observed && (mode == Interruption::kLaterCap || state.reached) &&
            scope.facts.work.source_scalar_pairs == (complete ? 6 : 3) &&
            scope.facts.work.coincidence_comparisons == (complete ? 9 : 8) &&
            scope.facts.execution.caller_jobs > 0 &&
            scope.facts.execution.launched_workers == 0 &&
            scope.facts.execution.joined_workers == 0,
        "established source pair survives later expected interruption, not "
        "unexpected exception");
  }
  constexpr std::size_t kParallelVertices = 64;
  constexpr std::uint64_t kParallelKeyAndGroupComparisons = 255;
  std::array<double, kParallelVertices * detail::kPositionComponents>
      distinct{};
  std::array<std::uint64_t, kParallelVertices> ids{};
  std::array<std::uint64_t, distinct.size()> same_wire{};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    ids[i] = i;
    distinct[i * detail::kPositionComponents] = static_cast<double>(i);
  }
  const auto parallel =
      Prepare({distinct, geom::PolygonTopologyInput{kEmptyOffsets, {}}, {}},
              context, false);
  Require(parallel.has_value(), "genuine parallel source backing");
  for (bool unexpected : {false, true}) {
    std::stop_source stop;
    FocusRun scope(context, stop.get_token());
    struct State {
      std::stop_source* stop;
      detail::FactsState* facts;
      bool unexpected;
      std::atomic<bool> reached{false};
    } state{&stop, &scope.facts, unexpected};
    scope.test.checkpoint_state = &state;
    scope.test.checkpoint = [](mv::SurfaceStage, std::uint64_t completed,
                               void* opaque) {
      auto& state = *static_cast<State*>(opaque);
      if (state.facts->work.coincidence_comparisons ==
              kParallelKeyAndGroupComparisons &&
          completed == 1) {
        state.reached.store(true, std::memory_order_relaxed);
        if (state.unexpected)
          throw std::runtime_error("unexpected parallel anchor callback");
        state.stop->request_stop();
      }
    };
    bool observed = false;
    try {
      detail::CheckSurfaceCoincidence(parallel->original, ids, same_wire,
                                      mv::SurfaceEncoding::kGltfJson,
                                      scope.run);
    } catch (const detail::Failure& failure) {
      observed =
          !unexpected &&
          failure.Diagnostic().code == mv::SurfaceCode::kNewCoincidence &&
          failure.Diagnostic().index == 0 &&
          failure.Diagnostic().secondary_index == 1;
    } catch (const std::runtime_error& error) {
      observed = unexpected && std::string_view(error.what()) ==
                                   "unexpected parallel anchor callback";
    }
    Require(observed && state.reached.load(std::memory_order_relaxed) &&
                scope.facts.work.source_scalar_pairs >= 3 &&
                scope.facts.work.source_scalar_pairs <
                    (kParallelVertices - 1) * detail::kPositionComponents &&
                scope.facts.execution.launched_workers > 0 &&
                scope.facts.execution.launched_workers ==
                    scope.facts.execution.joined_workers,
            "actual partial parallel established observations/joins and "
            "unexpected precedence");
  }
}
void PartialFactorCollector(const geom::ExecutionContext& context) {
  constexpr std::string_view json =
      R"({"asset":{"version":"2.0"},"materials":[{"pbrMetallicRoughness":{"metallicFactor":0.5}}]})";
  const auto read = mv::ReadGltfAssetCapped(
      std::as_bytes(std::span{json.data(), json.size()}), {}, ReadLimits(),
      {1, 256, 1, 256, {}}, context);
  Require(read.asset.has_value(), "actual factor document reader");
  const auto& doc = mv::gltf_detail::Access::State(*read.asset).document;
  std::optional<std::size_t> material;
  for (auto child = doc.FirstChild(0); child; child = doc.NextSibling(*child))
    if (doc.Key(*child) == "materials") material = doc.FirstChild(*child);
  Require(material.has_value(), "actual selected material node");
  const auto check_partial = [](const detail::FactsState& facts) {
    Require(facts.delegates.size() == 1, "one actually entered collector");
    const auto& actual =
        std::get<mv::SurfaceCoreFactorFacts>(facts.delegates[0].facts);
    for (std::size_t i = 0; i < 4; ++i)
      Require(!actual.authored[i] && actual.observed[i] &&
                  actual.core_f32[i] == 0x3f800000,
              "earlier default fields actually visited");
    Require(
        !actual.observed[4] && !actual.authored[5] && !actual.observed[5] &&
            !actual.complete && actual.lexical_calls == 0,
        "current and future fields unobserved with no invented lexical calls");
  };
  {
    std::stop_source stop;
    detail::TestOptions test;
    test.checkpoint_state = &stop;
    test.checkpoint = [](mv::SurfaceStage stage, std::uint64_t completed,
                         void* opaque) {
      if (stage == mv::SurfaceStage::kCatalog && completed == 4)
        static_cast<std::stop_source*>(opaque)->request_stop();
    };
    FocusRun scope(context, stop.get_token(), test);
    scope.run.stage = mv::SurfaceStage::kCatalog;
    bool canceled = false;
    try {
      (void)detail::CollectMaterialFactors(
          doc, *material, 0, mv::SurfaceDelegateKind::kSourceMaterialFactors,
          scope.run);
    } catch (const detail::Failure& failure) {
      canceled = failure.Status() == mv::SurfaceStatus::kCanceled;
    }
    Require(canceled, "real entered collector interior stop");
    check_partial(scope.facts);
  }
  struct RestoreRound {
    int mode = std::fegetround();
    ~RestoreRound() { (void)std::fesetround(mode); }
  } restore;
  if (std::fesetround(FE_UPWARD) == 0) {
    FocusRun scope(context);
    scope.run.stage = mv::SurfaceStage::kCatalog;
    bool refused = false;
    try {
      (void)detail::CollectMaterialFactors(
          doc, *material, 0, mv::SurfaceDelegateKind::kSourceMaterialFactors,
          scope.run);
    } catch (const detail::Failure& failure) {
      refused = failure.Diagnostic().code == mv::SurfaceCode::kNumericProfile;
    }
    Require(refused && scope.facts.profiles.size() == 1,
            "actual nonbaseline collector refusal");
    check_partial(scope.facts);
    Require(std::get<mv::SurfaceCoreFactorFacts>(scope.facts.delegates[0].facts)
                .authored[4],
            "authored metallic field actually reached before profile query");
  }
}
void NativeComparisonControls(const geom::ExecutionContext& context) {
  constexpr std::size_t kItems = 4096;
  std::array<std::byte, kItems> bytes{};
  std::array<std::uint32_t, kItems> indices{};
  for (std::size_t i = 0; i < indices.size(); ++i)
    indices[i] = static_cast<std::uint32_t>(i);
  for (bool identity : {false, true}) {
    FocusRun positive(context);
    positive.run.stage = mv::SurfaceStage::kAssetRead;
    const auto descriptor_before = positive.facts.work.descriptor_byte_pairs;
    if (identity)
      detail::CompareNativeIdentity(indices, positive.run);
    else
      detail::CompareNativeBytes(bytes, bytes, positive.run);
    Require(positive.facts.work.descriptor_byte_pairs == descriptor_before &&
                positive.facts.execution.planned_jobs > 1 &&
                positive.facts.execution.launched_workers > 0 &&
                positive.facts.execution.launched_workers ==
                    positive.facts.execution.joined_workers,
            "actual large native comparison work joins");
    FocusRun capped(context);
    capped.work.surface.max_jobs = 1;
    bool limited = false;
    try {
      if (identity)
        detail::CompareNativeIdentity(indices, capped.run);
      else
        detail::CompareNativeBytes(bytes, bytes, capped.run);
    } catch (const detail::Failure& failure) {
      limited = failure.Status() == mv::SurfaceStatus::kLimitExceeded;
    }
    Require(limited && capped.facts.execution.planned_jobs == 0,
            "positive job cap refuses before native comparison launches");
    std::stop_source stop;
    struct Progress {
      std::stop_source* stop;
      std::atomic<bool> after_four{false};
    } progress{&stop};
    detail::TestOptions test;
    test.checkpoint_state = &progress;
    test.checkpoint = [](mv::SurfaceStage, std::uint64_t completed,
                         void* opaque) {
      if (completed == 4) {
        auto& progress = *static_cast<Progress*>(opaque);
        progress.after_four.store(true, std::memory_order_relaxed);
        progress.stop->request_stop();
      }
    };
    FocusRun stopped(context, stop.get_token(), test);
    stopped.run.stage = mv::SurfaceStage::kAssetRead;
    bool canceled = false;
    try {
      if (identity)
        detail::CompareNativeIdentity(indices, stopped.run);
      else
        detail::CompareNativeBytes(bytes, bytes, stopped.run);
    } catch (const detail::Failure& failure) {
      canceled = failure.Status() == mv::SurfaceStatus::kCanceled;
    }
    Require(canceled && progress.after_four.load(std::memory_order_relaxed) &&
                stopped.facts.work.descriptor_byte_pairs == descriptor_before &&
                stopped.facts.execution.planned_jobs > 1 &&
                stopped.facts.execution.launched_workers > 0 &&
                stopped.facts.execution.launched_workers ==
                    stopped.facts.execution.joined_workers &&
                (!identity || stopped.facts.work.source_scalar_pairs > 0),
            "actual native comparison progress/joins without descriptor "
            "attribution");
  }
}
void SignedZeroBounds(const geom::ExecutionContext& context) {
  constexpr std::array<geom::index_t, 2> offsets{0, 3};
  constexpr std::array<geom::index_t, 3> corners{0, 1, 2};
  for (bool negative_first : {false, true}) {
    std::array<double, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    positions[2] = std::bit_cast<double>(
        negative_first ? UINT64_C(0x8000000000000000) : UINT64_C(0));
    positions[5] = std::bit_cast<double>(
        negative_first ? UINT64_C(0) : UINT64_C(0x8000000000000000));
    const auto source =
        Prepare({positions, geom::PolygonTopologyInput{offsets, corners}, {}},
                context, false);
    if (!source) return;
    const auto encoded = mv::EncodeSurface(
        SimpleRequest(*source), EncodeLimits(mv::SurfaceEncoding::kGltfJson),
        Work(mv::SurfaceEncoding::kGltfJson, false));
    Require(encoded.output.has_value(), "actual mixed signed-zero primitive");
    std::string_view json;
    for (const auto& member : encoded.output->Members())
      if (member.path == "surface.gltf")
        json = {reinterpret_cast<const char*>(member.bytes.data()),
                member.bytes.size()};
    Require(json.find("\"min\":[0,0,-0]") != std::string_view::npos &&
                json.find("\"max\":[1,1,0]") != std::string_view::npos,
            "independent literal emitted min negative-zero/max positive-zero "
            "tokens");
    // Independently call the real public reader/extractor and inspect its raw
    // bound words, rather than accepting agreement of the two extrema loops.
    std::span<const std::byte> binary;
    for (const auto& member : encoded.output->Members())
      if (member.path == "geometry.bin") binary = member.bytes;
    const std::array<mv::SuppliedGltfResource, 1> resources{
        {{mv::GltfResourceKind::kBuffer, 0, binary}}};
    const auto read = mv::ReadGltfAssetCapped(
        std::as_bytes(std::span{json.data(), json.size()}), resources,
        ReadLimits(), {1, 256, 1, 256, {}}, context);
    Require(read.asset && read.asset->PrimitiveCount() == 1,
            "actual signed-zero native asset");
    const auto extraction = mv::ExtractGltfPrimitiveCapped(
        *read.asset, *read.asset->Primitive(0),
        {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw,
         mv::GltfProcessingProfile::kStaticLocalTrianglesV1},
        {kScalars, kSourceCorners, kScopeBytes, {}}, {});
    Require(extraction.extraction.has_value(),
            "actual native raw bound extraction");
    bool checked = false;
    for (const auto& channel : extraction.extraction->Channels())
      if (channel.semantic == "POSITION") {
        Require(channel.exact_min && channel.exact_max,
                "actual native bounds present");
        const auto* low =
            std::get_if<std::span<const float>>(&*channel.exact_min);
        const auto* high =
            std::get_if<std::span<const float>>(&*channel.exact_max);
        Require(low && high && low->size() == 3 && high->size() == 3 &&
                    std::bit_cast<std::uint32_t>((*low)[2]) ==
                        UINT32_C(0x80000000) &&
                    std::bit_cast<std::uint32_t>((*high)[2]) == 0,
                "independent literal native -0 minimum/+0 maximum bits");
        checked = true;
      }
    Require(checked, "actual POSITION native bounds checked");
    Captured copied(*encoded.output);
    const auto inspected =
        mv::InspectSurfaceOutput(*encoded.output, copied.views,
                                 InspectLimits(mv::SurfaceEncoding::kGltfJson),
                                 Work(mv::SurfaceEncoding::kGltfJson, true));
    Require(
        inspected.inspection.has_value(),
        "actual native signed-zero bounds compared to exact emitted tokens");
  }
}
void Lifetime() {
  std::optional<mv::SurfaceOutput> retained;
  std::optional<geom::ExecutionContext> ledger;
  {
    geom::ExecutionContext original({4, 1, 512 * kMiB});
    const auto source =
        Prepare(fixture::RichInput(fixture::Variant::kOriginal,
                                   fixture::MetadataRoute::kRetained),
                original, true);
    if (!source) return;
    Plan plan(*source, mv::SurfaceEncoding::kGlb, fixture::Variant::kOriginal);
    const auto encoded =
        mv::EncodeSurface(plan.request, EncodeLimits(mv::SurfaceEncoding::kGlb),
                          Work(mv::SurfaceEncoding::kGlb, false));
    Require(encoded.status == mv::SurfaceStatus::kSuccess && encoded.output,
            "actual retained output lifetime setup");
    retained = encoded.output;
    ledger.emplace(retained->Triangles().ShareExecutionContext());
  }
  Require(ledger->ActiveTrackedPayload() > 0 && ledger->ActiveWorkers() == 0,
          "real output survives original handles/context");
  {
    Captured copied(*retained);
    const auto inspected = mv::InspectSurfaceOutput(
        *retained, copied.views, InspectLimits(mv::SurfaceEncoding::kGlb),
        Work(mv::SurfaceEncoding::kGlb, true));
    Require(
        inspected.status == mv::SurfaceStatus::kSuccess && inspected.inspection,
        "actual retained copied-byte proof");
    auto copy = *inspected.inspection;
    retained.reset();
    Maps(copy.Expected());
    Require(copy.Expected().Members().size() > 0,
            "actual proof retains its expected owner");
  }
  Require(ledger->ActiveTrackedPayload() == 0 && ledger->ActiveWorkers() == 0,
          "final real owner release");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    std::optional<fs::path> dump;
    if (argc != 1) {
      Require(argc == 3 && std::string_view(argv[1]) == "--write-observations",
              "closed observation invocation");
      dump = fs::path(argv[2]);
      Require(!fs::exists(*dump), "observation output root must be fresh");
      Require(fs::create_directories(*dump),
              "create owned observation output root");
    }
    AllocationAdmissionProvenance();
    geom::ExecutionContext context({4, 1, 512 * kMiB});
    const bool catalog_and_configuration =
        CatalogCopyAndConfigurationRegressions(context);
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "catalog/configuration regression owners released");
    Require(catalog_and_configuration,
            "catalog writes and first-invalid configuration precedence");
    for (const auto variant :
         {fixture::Variant::kOriginal, fixture::Variant::kTranslated})
      for (const auto route :
           {fixture::MetadataRoute::kRetained, fixture::MetadataRoute::kLegacy})
        for (const auto encoding :
             {mv::SurfaceEncoding::kGltfJson, mv::SurfaceEncoding::kGlb,
              mv::SurfaceEncoding::kStlAscii,
              mv::SurfaceEncoding::kStlBinary}) {
          Rich(context, variant, route, encoding, dump);
          Require(context.ActiveTrackedPayload() == 0 &&
                      context.ActiveWorkers() == 0,
                  "complete rich operation cleanup");
        }
    for (const auto encoding :
         {mv::SurfaceEncoding::kGltfJson, mv::SurfaceEncoding::kGlb})
      Rich(context, fixture::Variant::kOriginal,
           fixture::MetadataRoute::kRetained, encoding, dump, true);
    Rich(context, fixture::Variant::kTranslated,
         fixture::MetadataRoute::kRetained, mv::SurfaceEncoding::kStlAscii,
         dump, false, true);
    for (const auto encoding :
         {mv::SurfaceEncoding::kGltfJson, mv::SurfaceEncoding::kGlb,
          mv::SurfaceEncoding::kStlAscii, mv::SurfaceEncoding::kStlBinary}) {
      Empty(context, encoding);
      PointOnly(context, encoding);
      Require(
          context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "actual empty/point-only cleanup");
    }
    CompleteCorrespondence(context);
    SourceOrderedCoincidence(context);
    EstablishedCoincidence(context);
    PartialFactorCollector(context);
    NativeComparisonControls(context);
    SignedZeroBounds(context);
    Boundaries(context);
    PressureAndFailures();
    Lifetime();
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "all real core operations released");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
