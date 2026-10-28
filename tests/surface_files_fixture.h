// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_SURFACE_FILES_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_SURFACE_FILES_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/surface_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "surface_fixture.h"

namespace meshvale::interchange::surface_files_fixture {
namespace geom = meshvale::geometry;
namespace mv = meshvale::interchange;
namespace fixture = mv::surface_fixture;
namespace fs = std::filesystem;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kScopeBytes = 64 * kMiB;
constexpr std::uint64_t kAllowanceBytes = 512 * kMiB;
constexpr geom::MeshMaterializationWorkPolicy kWork{4096, 8192, 64, 65536};
constexpr geom::MeshInspectionLimits kInspect{1024, kScopeBytes};
constexpr std::array<geom::index_t, 1> kEmptyOffsets{0};
constexpr std::array<std::byte, 80> kHeader{};
constexpr std::array<mv::SurfaceUvBinding, 3> kUv{
    {{15, 0, 0}, {16, 2, 1}, {17, 5, 2}}};
constexpr std::array<mv::SurfaceMaterialBinding, 3> kMaterials{
    {{17, 0}, {-4, 1}, {1000003, 2}}};
constexpr std::array<mv::SurfaceCatalogUvBinding, 3> kCatalogUv{
    {{0, 0}, {1, 2}, {2, 5}}};
constexpr std::array<double, 3> kUvBounds{};
constexpr std::array<mv::SurfaceEncoding, 4> kEncodings{
    mv::SurfaceEncoding::kGltfJson, mv::SurfaceEncoding::kGlb,
    mv::SurfaceEncoding::kStlAscii, mv::SurfaceEncoding::kStlBinary};
inline void Require(bool good, std::string_view message) {
  if (!good) throw std::runtime_error(std::string(message));
}
inline geom::MeshMaterializationLimits MeshLimits() {
  return {1024,  1024,  8192, 1025, 64,         65536,
          65536, 65536, 4096, kMiB, kScopeBytes};
}
inline mv::SurfaceCountLimits Counts() {
  mv::SurfaceCountLimits l;
  l.max_vertices = 1024;
  l.max_faces = 1024;
  l.max_corners = 8192;
  l.max_face_offsets = 1025;
  l.max_attributes = 64;
  l.max_attribute_scalars = 65536;
  l.max_attribute_bytes = kMiB;
  l.max_attribute_offsets = 65536;
  l.max_presence_bytes = 65536;
  l.max_metadata_entries = 4096;
  l.max_descriptor_bytes = kMiB;
  l.max_wire_primitives = 1024;
  l.max_wire_vertices = 8192;
  l.max_wire_corners = 8192;
  l.max_map_entries = 65536;
  l.max_members = 1024;
  l.max_member_bytes = 4 * kMiB;
  l.max_total_member_bytes = 16 * kMiB;
  l.max_resources = 1024;
  l.max_resource_bytes = 4 * kMiB;
  l.max_json_bytes = 4 * kMiB;
  l.max_binary_bytes = 4 * kMiB;
  l.max_capsule_json_bytes = 4 * kMiB;
  l.max_capsule_binary_bytes = 4 * kMiB;
  l.max_material_bindings = 1024;
  l.max_numeric_token_bytes = 4096;
  l.max_json_nodes = 262144;
  l.max_json_depth = 128;
  l.max_numeric_integer_bits = 65536;
  l.max_coincidence_comparisons = 1048576;
  l.max_relationship_steps = 4194304;
  return l;
}
inline bool Stl(mv::SurfaceEncoding encoding) {
  return encoding == mv::SurfaceEncoding::kStlAscii ||
         encoding == mv::SurfaceEncoding::kStlBinary;
}
inline mv::SurfaceWorkPolicy Work(mv::SurfaceEncoding encoding,
                                  bool read = false) {
  mv::SurfaceWorkPolicy p;
  p.surface = kWork;
  if (encoding == mv::SurfaceEncoding::kGlb) p.container = {4096, 8192};
  if (Stl(encoding))
    p.stl = {4096, 64, 16, kWork};
  else if (read)
    p.gltf = {4096, 8192, 4096, 8192, {}};
  return p;
}
inline mv::GltfReadOperationLimits ReadLimits() {
  return {{4 * kMiB, 4 * kMiB, 4 * kMiB, 262144, 128, 4 * kMiB, 4096, 4096, 16,
           65536, 65536, 65536, 65536, 4 * kMiB},
          kScopeBytes};
}
inline mv::SurfaceEncodeLimits EncodeLimits(mv::SurfaceEncoding encoding) {
  mv::SurfaceEncodeLimits l;
  l.counts = Counts();
  l.max_new_requested_bytes = kScopeBytes;
  l.max_total_requested_allowance = kAllowanceBytes;
  if (Stl(encoding))
    l.delegates =
        mv::StlSurfaceEncodeLimits{MeshLimits(),
                                   {4 * kMiB, 4096, 1024, 65536, 4096,
                                    kScopeBytes, 2 * kScopeBytes, kInspect}};
  else {
    mv::GltfSurfaceEncodeLimits d;
    if (encoding == mv::SurfaceEncoding::kGlb)
      d.container = {4 * kMiB, 4, kScopeBytes};
    l.delegates = d;
  }
  return l;
}
inline mv::SurfaceInspectLimits InspectLimits(mv::SurfaceEncoding encoding) {
  mv::SurfaceInspectLimits l;
  l.counts = Counts();
  l.max_new_requested_bytes = kScopeBytes;
  l.max_total_requested_allowance = kAllowanceBytes;
  if (Stl(encoding))
    l.delegates = mv::StlSurfaceInspectLimits{
        {4 * kMiB, 4096, 1024, 65536, 4096, kScopeBytes, 3 * kScopeBytes,
         MeshLimits(), kInspect}};
  else {
    mv::GltfSurfaceInspectLimits d;
    if (encoding == mv::SurfaceEncoding::kGlb)
      d.container = {4 * kMiB, 4, kScopeBytes};
    d.read = ReadLimits();
    d.extraction = {65536, 8192, kScopeBytes, {}};
    l.delegates = d;
  }
  return l;
}

inline SurfaceFilePublicationLimits FileLimits(SurfaceEncoding encoding) {
  return {{1024, 4 * kMiB, 16 * kMiB, 4 * kMiB, kMiB, 1024, kMiB, 32768, 2048,
           16, kScopeBytes},
          InspectLimits(encoding),
          kScopeBytes + kAllowanceBytes};
}
inline SurfaceFilePublicationWorkPolicy FileWork(SurfaceEncoding encoding,
                                                 std::uint64_t jobs = 4) {
  return {{64, 1, 256, 32, jobs}, Work(encoding, true)};
}

class TempTree {
 public:
  TempTree() {
    static std::atomic<std::uint64_t> serial{0};
    const auto temporary_parent = fs::temp_directory_path();
    const auto canonical_parent = fs::canonical(temporary_parent);
    if (serial.load(std::memory_order_relaxed) == 0)
      std::cerr << "surface files temporary parent: supplied="
                << temporary_parent << " canonical=" << canonical_parent
                << '\n';
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      const auto candidate =
          canonical_parent /
          ("meshvale-surface-files-" + std::to_string(stamp) + "-" +
           std::to_string(serial.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(candidate, error)) {
        root = candidate;
        return;
      }
      Require(!error || error == std::errc::file_exists,
              "create exclusive test parent");
    }
    throw std::runtime_error("exclusive test parent attempts exhausted");
  }
  TempTree(const TempTree&) = delete;
  TempTree& operator=(const TempTree&) = delete;
  ~TempTree() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  fs::path root;
};

inline std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  Require(stream.is_open(), "open actual physical member");
  const std::vector<char> bytes{std::istreambuf_iterator<char>{stream}, {}};
  Require(!stream.bad(), "read complete physical member");
  std::vector<std::byte> result(bytes.size());
  std::transform(bytes.begin(), bytes.end(), result.begin(),
                 [](char c) { return static_cast<std::byte>(c); });
  stream.close();
  Require(!stream.fail(), "close actual physical member");
  return result;
}
inline void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  Require(stream.is_open(), "open test mutation");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(!stream.fail(), "write and close actual test bytes");
}
inline std::map<std::string, std::vector<std::byte>> Inventory(
    const fs::path& root) {
  std::map<std::string, std::vector<std::byte>> result;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    const auto status = entry.symlink_status();
    Require(!fs::is_symlink(status),
            "no symbolic links in published inventory");
    if (fs::is_directory(status)) continue;
    Require(fs::is_regular_file(status), "only physical regular members");
    result.emplace(entry.path().lexically_relative(root).generic_string(),
                   Read(entry.path()));
  }
  return result;
}

struct Captured {
  explicit Captured(const SurfaceOutput& output) {
    for (const auto& member : output.Members()) {
      paths.emplace_back(member.path);
      bytes.emplace_back(member.bytes.begin(), member.bytes.end());
    }
    for (std::size_t i = 0; i < paths.size(); ++i)
      views.push_back({paths[i], bytes[i]});
  }
  std::vector<std::string> paths;
  std::vector<std::vector<std::byte>> bytes;
  std::vector<SuppliedSurfaceMember> views;
};
struct Proof {
  std::optional<SurfaceOutputInspection> inspection;
  bool unsupported_source = false;
  bool empty = false;
};
enum class Shape { kTriangle, kRich, kEmpty };

// Every returned proof comes from a real Materialize -> E -> independent V.
// The rich DATA and literal numeric expectations remain in surface_fixture.h.
inline Proof MakeProof(
    SurfaceEncoding encoding, SurfaceRepresentation representation, Shape shape,
    const geom::ExecutionContext& context,
    fixture::Variant variant = fixture::Variant::kOriginal,
    fixture::MetadataRoute route = fixture::MetadataRoute::kRetained) {
  constexpr std::array<double, 9> kTrianglePositions{0, 0, 0, 1, 0, 0, 0, 1, 0};
  constexpr std::array<geom::index_t, 2> kTriangleFaceOffsets{0, 3};
  constexpr std::array<geom::index_t, 3> kTriangleCorners{0, 1, 2};
  constexpr std::array<float, 9> kTriangleNormals{0, 0, 1, 0, 0, 1, 0, 0, 1};
  const geom::AttributeMaterializationInput normal{
      geom::AttributeDomain::corner,
      "normal",
      "NORMAL",
      std::nullopt,
      3,
      std::span<const float>{kTriangleNormals},
      std::nullopt,
      std::nullopt,
      {}};
  auto empty_normal = normal;
  empty_normal.values = std::span<const float>{};
  geom::MeshMaterializationInput input{
      std::span<const double>{kTrianglePositions},
      geom::PolygonTopologyInput{kTriangleFaceOffsets, kTriangleCorners},
      Stl(encoding)
          ? std::span<const geom::AttributeMaterializationInput>{&normal, 1}
          : std::span<const geom::AttributeMaterializationInput>{}};
  if (shape == Shape::kRich) input = fixture::RichInput(variant, route);
  if (shape == Shape::kEmpty)
    input = {
        std::span<const double>{},
        geom::PolygonTopologyInput{kEmptyOffsets, {}},
        Stl(encoding)
            ? std::span<
                  const geom::AttributeMaterializationInput>{&empty_normal, 1}
            : std::span<const geom::AttributeMaterializationInput>{}};
  auto source = geom::MaterializeMesh(input, MeshLimits(), kWork, context);
  Proof proof;
  proof.unsupported_source =
      source.status ==
      geom::MaterializationStatus::kUnsupportedAllocationProfile;
  proof.empty = shape == Shape::kEmpty || proof.unsupported_source;
  if (proof.unsupported_source) {
    Require(!source.mesh,
            "actual unsupported requested Geometry storage produces no owner");
    source =
        geom::MaterializeMesh({std::span<const double>{},
                               geom::PolygonTopologyInput{kEmptyOffsets, {}},
                               {}},
                              MeshLimits(), kWork, context);
  }
  Require(source.status == geom::MaterializationStatus::kSuccess && source.mesh,
          "genuine materialized input");
  const bool has_normal =
      Stl(encoding) && !source.mesh->View().attributes.empty();
  const auto original =
      geom::InspectMaterializedMesh(*source.mesh, kInspect, kWork);
  Require(original.inspection && original.inspection->IsStorageValid(),
          "genuine original inspection");
  const auto triangles =
      geom::TriangulateCharged(*source.mesh, {64, 1},
                               {MeshLimits(), kInspect, MeshLimits(), kInspect,
                                65536, kScopeBytes, kScopeBytes},
                               kWork);
  Require(triangles.status == geom::ChargedTriangulationStatus::kAccepted &&
              triangles.triangulation,
          "genuine charged triangulation");
  std::optional<GltfAsset> catalog;
  if (shape == Shape::kRich && !proof.empty && !Stl(encoding)) {
    const auto text = fixture::CatalogJson();
    const auto read =
        ReadGltfAssetCapped(std::as_bytes(std::span{text.data(), text.size()}),
                            fixture::CatalogResources(), ReadLimits(),
                            {4096, 8192, 4096, 8192, {}}, context);
    Require(read.asset.has_value(), "genuine material and UV catalog");
    catalog = read.asset;
  }
  std::array<SurfaceChannelRule, 19> channels{};
  std::size_t channel_count = 0;
  if (!proof.empty && shape == Shape::kRich) {
    channel_count = channels.size();
    for (std::size_t i = 0; i < channels.size(); ++i)
      channels[i] = {i, SurfaceChannelRole::kBundleOnly, true};
    if (Stl(encoding))
      channels[14].role = SurfaceChannelRole::kStlCornerNormal;
    else {
      for (std::size_t i = 15; i < 18; ++i)
        channels[i].role = SurfaceChannelRole::kUv;
      channels[18].role = SurfaceChannelRole::kMaterialId;
    }
  } else if (has_normal) {
    channels[0] = {0, SurfaceChannelRole::kStlCornerNormal, false};
    channel_count = 1;
  }
  SurfaceEncodeRequest request{
      .original = *original.inspection,
      .triangles = *triangles.triangulation,
      .representation = representation,
      .acknowledge_triangle_conversion = true,
      .acknowledge_corner_expansion = true,
      .acknowledge_unused_vertices = true,
      .channels = std::span{channels}.first(channel_count),
      .numeric = {},
      .destination = {}};
  request.numeric.positions = encoding == SurfaceEncoding::kStlAscii
                                  ? SurfaceNarrowing::kExact64
                              : variant == fixture::Variant::kTranslated
                                  ? SurfaceNarrowing::kBoundedRne32
                                  : SurfaceNarrowing::kExact32;
  request.numeric.max_position_linf_error =
      variant == fixture::Variant::kTranslated ? 0x1p-29 : 0;
  if (Stl(encoding)) {
    StlSurfaceOptions options;
    options.encoding = encoding;
    options.coordinates = SurfaceCoordinates::kStlUnitsUnspecified;
    if (has_normal)
      options.corner_normal_attribute =
          shape == Shape::kRich && !proof.empty ? 14 : 0;
    if (encoding == SurfaceEncoding::kStlAscii)
      options.ascii_name = "physical";
    else
      options.binary_header = kHeader;
    request.destination = options;
  } else {
    GltfSurfaceOptions options;
    options.encoding = encoding;
    options.coordinates = SurfaceCoordinates::kDeclaredMetersRightHandedYUp;
    if (catalog) {
      options.uv = kUv;
      options.catalog = &*catalog;
      options.materials = kMaterials;
      options.catalog_uv = kCatalogUv;
      request.numeric.max_uv_component_errors = kUvBounds;
    }
    request.destination = options;
  }
  const auto encoded =
      EncodeSurface(request, EncodeLimits(encoding), Work(encoding));
  if (!proof.empty && shape == Shape::kRich &&
      representation == SurfaceRepresentation::kStrictBare) {
    Require(encoded.status != SurfaceStatus::kSuccess && !encoded.output,
            "rich bundle-only attributes cannot manufacture a strict-bare "
            "publication input");
    return proof;
  }
  if (proof.empty && Stl(encoding) && !has_normal) {
    Require(encoded.status == SurfaceStatus::kUnsupported && !encoded.output &&
                encoded.diagnostic.code == SurfaceCode::kNormalAbsent,
            "actual empty normal-absent STL refusal, no manufactured "
            "publication input");
    return proof;
  }
  Require(encoded.status == SurfaceStatus::kSuccess && encoded.output,
          "real encoding before file-publication test");
  Captured captured(*encoded.output);
  auto verified =
      InspectSurfaceOutput(*encoded.output, captured.views,
                           InspectLimits(encoding), Work(encoding, true));
  if (proof.empty && verified.status == SurfaceStatus::kUnsupported &&
      verified.diagnostic.code == SurfaceCode::kNumericProfile) {
    Require(!verified.inspection && verified.facts,
            "actual unsupported reader profile");
    for (const auto& call : verified.facts->Delegates())
      Require(call.kind != SurfaceDelegateKind::kGltfRead &&
                  call.kind != SurfaceDelegateKind::kGltfExtract,
              "no invented reader/extractor after profile refusal");
    return proof;
  }
  Require(verified.status == SurfaceStatus::kSuccess && verified.inspection,
          "real independent input inspection");
  proof.inspection = verified.inspection;
  return proof;
}

inline void PublicationDiagnostic(const SurfaceFilePublicationResult& result,
                                  std::string_view context) {
  const auto& diagnostic = result.diagnostic;
  std::cerr << context << ": outcome=" << static_cast<int>(result.outcome)
            << " owner=" << result.bundle.has_value()
            << " status=" << static_cast<int>(diagnostic.status)
            << " stage=" << static_cast<int>(diagnostic.stage)
            << " code=" << static_cast<int>(diagnostic.code);
  const auto print_optional = [](std::string_view name, const auto& value) {
    std::cerr << ' ' << name << '=';
    if (value)
      std::cerr << *value;
    else
      std::cerr << "null";
  };
  const auto print_error = [](std::string_view name, const auto& error) {
    std::cerr << ' ' << name << '=';
    if (error)
      std::cerr << error->category().name() << ':' << error->value() << ':'
                << error->message();
    else
      std::cerr << "null";
  };
  print_optional("member", diagnostic.member_ordinal);
  print_optional("offset", diagnostic.byte_offset);
  print_error("system_error", diagnostic.system_error);
  std::cerr << " cleanup=" << static_cast<int>(result.cleanup.outcome);
  print_error("cleanup_error", result.cleanup.error);
  std::cerr << '\n';
  const auto print_io = [](std::string_view name, const auto& phase) {
    std::cerr << name << ": reached=" << phase.has_value();
    if (phase)
      std::cerr << " outcome=" << static_cast<int>(phase->outcome)
                << " read=" << phase->bytes_read
                << " written=" << phase->bytes_written
                << " copied=" << phase->bytes_copied
                << " compared=" << phase->bytes_compared
                << " hashed=" << phase->bytes_hashed
                << " completed_files=" << phase->completed_files
                << " closed_files=" << phase->closed_files
                << " planned_jobs=" << phase->execution.planned_jobs
                << " launched=" << phase->execution.launched_workers
                << " joined=" << phase->execution.joined_workers;
    std::cerr << '\n';
  };
  print_io("payload_write", result.facts.payload_write);
  print_io("payload_reread", result.facts.payload_reread);
  print_io("receipt_write", result.facts.receipt_write);
  print_io("receipt_reread", result.facts.receipt_reread);
  print_io("final_payload", result.facts.final_payload_verification);
  print_io("final_receipt", result.facts.final_receipt_verification);
  const auto print_inventory = [](std::string_view name, const auto& phase) {
    std::cerr << name << ": reached=" << phase.has_value();
    if (phase)
      std::cerr << " outcome=" << static_cast<int>(phase->outcome)
                << " files=" << phase->observed_files
                << " directories=" << phase->observed_directories;
    std::cerr << '\n';
  };
  print_inventory("payload_inventory", result.facts.payload_inventory);
  print_inventory("final_inventory", result.facts.final_inventory);
  if (result.facts.inspection)
    std::cerr << "inspection: status="
              << static_cast<int>(result.facts.inspection->status) << " stage="
              << static_cast<int>(result.facts.inspection->diagnostic.stage)
              << " code="
              << static_cast<int>(result.facts.inspection->diagnostic.code)
              << '\n';
  if (result.facts.commit)
    std::cerr << "commit: outcome="
              << static_cast<int>(result.facts.commit->outcome) << '\n';
  std::cerr << "reached: payload_inventory="
            << result.facts.payload_inventory.has_value()
            << " inspection=" << result.facts.inspection.has_value()
            << " provider=" << result.facts.provider.has_value()
            << " report_import=" << result.facts.report_import.has_value()
            << " final_inventory=" << result.facts.final_inventory.has_value()
            << " commit=" << result.facts.commit.has_value() << '\n';
}

inline void Published(const SurfaceFilePublicationResult& result) {
  if (!(result.outcome == SurfaceFilePublicationOutcome::kPublished &&
        result.diagnostic.status == SurfaceFileStatus::kSuccess &&
        result.bundle &&
        result.diagnostic.stage == SurfaceFileStage::kComplete &&
        result.diagnostic.code == SurfaceFileCode::kNone &&
        result.cleanup.outcome == SurfaceFileCleanupOutcome::kNotRequired))
    PublicationDiagnostic(result, "expected successful publication");
  Require(result.outcome == SurfaceFilePublicationOutcome::kPublished &&
              result.diagnostic.status == SurfaceFileStatus::kSuccess &&
              result.bundle &&
              result.diagnostic.stage == SurfaceFileStage::kComplete &&
              result.diagnostic.code == SurfaceFileCode::kNone &&
              result.cleanup.outcome == SurfaceFileCleanupOutcome::kNotRequired,
          "publication owner only on actual successful commit");
  Require(result.facts.commit && result.facts.commit->outcome ==
                                     SurfaceFileCommitOutcome::kCommitted,
          "actual committed fact");
}
inline void NoOwner(const SurfaceFilePublicationResult& result) {
  Require(!result.bundle &&
              result.outcome != SurfaceFilePublicationOutcome::kPublished &&
              result.diagnostic.status != SurfaceFileStatus::kSuccess,
          "failure/cancellation cannot manufacture a published owner");
}
}  // namespace meshvale::interchange::surface_files_fixture
#endif  // MESHVALE_INTERCHANGE_TESTS_SURFACE_FILES_FIXTURE_H_
