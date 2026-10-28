// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/surface_files.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace geom = meshvale::geometry;
namespace mv = meshvale::interchange;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kScopeBytes = 16 * kMiB;
constexpr std::uint64_t kAllowanceBytes = 128 * kMiB;
constexpr geom::MeshMaterializationWorkPolicy kMeshWork{1, 256, 64, 2048};
constexpr geom::MeshInspectionLimits kInspectionLimits{64, kScopeBytes};
constexpr std::array<double, 9> kPositions{0, 0, 0, 1, 0, 0, 0, 1, 0};
constexpr std::array<geom::index_t, 2> kFaceOffsets{0, 3};
constexpr std::array<geom::index_t, 3> kCorners{0, 1, 2};
constexpr std::array<geom::index_t, 1> kEmptyOffsets{0};
constexpr std::array<float, 9> kNormals{0, 0, 1, 0, 0, 1, 0, 0, 1};
constexpr std::array<std::byte, 80> kBinaryHeader{};
constexpr std::array<geom::AttributeMaterializationInput, 1> kAttributes{{
    {geom::AttributeDomain::corner,
     "normal",
     "NORMAL",
     std::nullopt,
     3,
     std::span<const float>{kNormals},
     std::nullopt,
     std::nullopt,
     {}},
}};
constexpr std::array<mv::SurfaceChannelRule, 1> kNormalRule{{
    {0, mv::SurfaceChannelRole::kStlCornerNormal, false},
}};

void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

geom::MeshMaterializationLimits MeshLimits() {
  geom::MeshMaterializationLimits limits;
  limits.max_vertices = 64;
  limits.max_faces = 64;
  limits.max_corners = 192;
  limits.max_face_offsets = 65;
  limits.max_attributes = 8;
  limits.max_attribute_scalars = 2048;
  limits.max_attribute_offsets = 256;
  limits.max_presence_bytes = 256;
  limits.max_metadata_entries = 64;
  limits.max_descriptor_bytes = 4096;
  limits.max_new_requested_bytes = kScopeBytes;
  return limits;
}

mv::SurfaceCountLimits CountLimits() {
  mv::SurfaceCountLimits limits;
  limits.max_vertices = 64;
  limits.max_faces = 64;
  limits.max_corners = 192;
  limits.max_face_offsets = 65;
  limits.max_attributes = 8;
  limits.max_attribute_scalars = 2048;
  limits.max_attribute_bytes = 16384;
  limits.max_attribute_offsets = 256;
  limits.max_presence_bytes = 256;
  limits.max_metadata_entries = 64;
  limits.max_descriptor_bytes = 4096;
  limits.max_wire_primitives = 64;
  limits.max_wire_vertices = 192;
  limits.max_wire_corners = 192;
  limits.max_map_entries = 4096;
  limits.max_members = 16;
  limits.max_member_bytes = kMiB;
  limits.max_total_member_bytes = 4 * kMiB;
  limits.max_resources = 16;
  limits.max_resource_bytes = kMiB;
  limits.max_json_bytes = kMiB;
  limits.max_binary_bytes = kMiB;
  limits.max_capsule_json_bytes = kMiB;
  limits.max_capsule_binary_bytes = kMiB;
  limits.max_material_bindings = 64;
  limits.max_numeric_token_bytes = 1024;
  limits.max_json_nodes = 4096;
  limits.max_json_depth = 64;
  limits.max_numeric_integer_bits = 8192;
  limits.max_coincidence_comparisons = 65536;
  limits.max_relationship_steps = 1048576;
  return limits;
}

bool IsStl(mv::SurfaceEncoding encoding) {
  return encoding == mv::SurfaceEncoding::kStlAscii ||
         encoding == mv::SurfaceEncoding::kStlBinary;
}

mv::SurfaceWorkPolicy Work(mv::SurfaceEncoding encoding, bool inspect) {
  mv::SurfaceWorkPolicy work;
  work.surface = kMeshWork;
  if (encoding == mv::SurfaceEncoding::kGlb) work.container = {1, 256};
  if (IsStl(encoding)) {
    work.stl = {256, 64, 16, kMeshWork};
  } else if (inspect) {
    work.gltf = {1, 256, 1, 256, {}};
  }
  return work;
}

mv::SurfaceEncodeLimits EncodeLimits(mv::SurfaceEncoding encoding) {
  mv::SurfaceEncodeLimits limits;
  limits.counts = CountLimits();
  limits.max_new_requested_bytes = kScopeBytes;
  limits.max_total_requested_allowance = kAllowanceBytes;
  if (IsStl(encoding)) {
    limits.delegates =
        mv::StlSurfaceEncodeLimits{MeshLimits(),
                                   {kMiB, 64, 64, 4096, 1024, kScopeBytes,
                                    2 * kScopeBytes, kInspectionLimits}};
  } else {
    mv::GltfSurfaceEncodeLimits delegates;
    if (encoding == mv::SurfaceEncoding::kGlb)
      delegates.container = {kMiB, 4, kScopeBytes};
    limits.delegates = delegates;
  }
  return limits;
}

mv::SurfaceInspectLimits InspectLimits(mv::SurfaceEncoding encoding) {
  mv::SurfaceInspectLimits limits;
  limits.counts = CountLimits();
  limits.max_new_requested_bytes = kScopeBytes;
  limits.max_total_requested_allowance = kAllowanceBytes;
  if (IsStl(encoding)) {
    limits.delegates = mv::StlSurfaceInspectLimits{
        {kMiB, 64, 64, 4096, 1024, kScopeBytes, 3 * kScopeBytes, MeshLimits(),
         kInspectionLimits}};
  } else {
    mv::GltfSurfaceInspectLimits delegates;
    if (encoding == mv::SurfaceEncoding::kGlb)
      delegates.container = {kMiB, 4, kScopeBytes};
    delegates.read = {{kMiB, kMiB, kMiB, 4096, 64, 65536, 1024, 1024, 16, 4096,
                       4096, 4096, 4096, kMiB},
                      kScopeBytes};
    delegates.extraction = {4096, 192, kScopeBytes, {}};
    limits.delegates = delegates;
  }
  return limits;
}

std::optional<mv::SurfaceOutputInspection> Prepare(
    mv::SurfaceEncoding encoding, const geom::ExecutionContext& context) {
  const auto attributes =
      IsStl(encoding)
          ? std::span<const geom::AttributeMaterializationInput>{kAttributes}
          : std::span<const geom::AttributeMaterializationInput>{};
  auto materialized = geom::MaterializeMesh(
      {std::span<const double>{kPositions},
       geom::PolygonTopologyInput{kFaceOffsets, kCorners}, attributes},
      MeshLimits(), kMeshWork, context);
  const bool empty = materialized.status ==
                     geom::MaterializationStatus::kUnsupportedAllocationProfile;
  if (empty) {
    Require(!materialized.mesh, "actual nonempty construction refused");
    materialized =
        geom::MaterializeMesh({std::span<const double>{},
                               geom::PolygonTopologyInput{kEmptyOffsets, {}},
                               {}},
                              MeshLimits(), kMeshWork, context);
  }
  Require(materialized.status == geom::MaterializationStatus::kSuccess &&
              materialized.mesh.has_value(),
          "actual canonical source");
  const auto inspected = geom::InspectMaterializedMesh(
      *materialized.mesh, kInspectionLimits, kMeshWork);
  Require(inspected.inspection && inspected.inspection->IsStorageValid(),
          "actual original inspection");
  const geom::ChargedTriangulationLimits triangle_limits{
      MeshLimits(), kInspectionLimits, MeshLimits(), kInspectionLimits,
      4096,         kScopeBytes,       kScopeBytes};
  const auto triangulated = geom::TriangulateCharged(
      *materialized.mesh, {64, 1}, triangle_limits, kMeshWork);
  Require(triangulated.status == geom::ChargedTriangulationStatus::kAccepted &&
              triangulated.triangulation.has_value(),
          "actual charged owner");

  mv::SurfaceEncodeRequest request{
      .original = *inspected.inspection,
      .triangles = *triangulated.triangulation,
      .representation = mv::SurfaceRepresentation::kInvalid,
      .acknowledge_triangle_conversion = false,
      .acknowledge_corner_expansion = false,
      .acknowledge_unused_vertices = false,
      .channels = {},
      .numeric = {},
      .destination = {}};
  request.representation = mv::SurfaceRepresentation::kStrictBare;
  request.acknowledge_triangle_conversion = true;
  request.acknowledge_corner_expansion = true;
  request.numeric.positions = encoding == mv::SurfaceEncoding::kStlAscii
                                  ? mv::SurfaceNarrowing::kExact64
                                  : mv::SurfaceNarrowing::kExact32;
  if (IsStl(encoding)) {
    if (!empty) request.channels = kNormalRule;
    mv::StlSurfaceOptions destination;
    destination.encoding = encoding;
    destination.coordinates = mv::SurfaceCoordinates::kStlUnitsUnspecified;
    if (!empty) destination.corner_normal_attribute = 0;
    if (encoding == mv::SurfaceEncoding::kStlAscii)
      destination.ascii_name = "surface";
    else
      destination.binary_header = kBinaryHeader;
    request.destination = destination;
  } else {
    mv::GltfSurfaceOptions destination;
    destination.encoding = encoding;
    destination.coordinates =
        mv::SurfaceCoordinates::kDeclaredMetersRightHandedYUp;
    request.destination = destination;
  }
  const auto encoded =
      mv::EncodeSurface(request, EncodeLimits(encoding), Work(encoding, false));
  if (empty && IsStl(encoding)) {
    Require(encoded.status == mv::SurfaceStatus::kUnsupported &&
                encoded.diagnostic.code == mv::SurfaceCode::kNormalAbsent &&
                !encoded.output,
            "actual empty absent-normal refusal");
    return std::nullopt;
  }
  Require(encoded.status == mv::SurfaceStatus::kSuccess && encoded.output,
          "actual surface output");
  Require(encoded.output->WireTriangleSources().size() == (empty ? 0 : 1),
          "actual triangle map extent");

  // Capture fresh caller-owned bytes. No expected digest or proof flag is
  // supplied to the independent inspector.
  std::vector<std::string> paths;
  std::vector<std::vector<std::byte>> bytes;
  for (const auto& member : encoded.output->Members()) {
    paths.emplace_back(member.path);
    bytes.emplace_back(member.bytes.begin(), member.bytes.end());
  }
  std::vector<mv::SuppliedSurfaceMember> captured;
  for (std::size_t index = 0; index < paths.size(); ++index)
    captured.push_back({paths[index], bytes[index]});
  const auto verified = mv::InspectSurfaceOutput(
      *encoded.output, captured, InspectLimits(encoding), Work(encoding, true));
  if (verified.status == mv::SurfaceStatus::kUnsupported &&
      verified.diagnostic.code == mv::SurfaceCode::kNumericProfile) {
    Require(empty && !verified.inspection,
            "actual unsupported parser profile, no decode proof");
    Require(verified.facts.has_value(), "actual reached profile facts");
    bool refused_read_profile = false;
    for (const auto& profile : verified.facts->NumericDelegateProfiles())
      refused_read_profile |=
          profile.kind == mv::SurfaceDelegateKind::kGltfRead &&
          profile.facts.status !=
              mv::SurfaceDelegateProfileStatus::kEstablishedBaseline;
    Require(refused_read_profile, "actual parser profile refusal observation");
    for (const auto& delegate : verified.facts->Delegates())
      Require(delegate.kind != mv::SurfaceDelegateKind::kGltfRead &&
                  delegate.kind != mv::SurfaceDelegateKind::kGltfExtract,
              "no native read or extraction invented after profile refusal");
    return std::nullopt;
  }
  Require(verified.status == mv::SurfaceStatus::kSuccess && verified.inspection,
          "actual independent bytes inspection");

  return verified.inspection;
}
namespace fs = std::filesystem;
class OutputDirectory {
 public:
  OutputDirectory() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto canonical_parent = fs::canonical(fs::temp_directory_path());
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      auto candidate =
          canonical_parent /
          ("meshvale-surface-files-consumer-" + std::to_string(stamp) + "-" +
           std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(candidate, error)) {
        parent = candidate;
        return;
      }
      Require(!error || error == std::errc::file_exists,
              "exclusive application output parent");
    }
    throw std::runtime_error("application output parent attempts exhausted");
  }
  OutputDirectory(const OutputDirectory&) = delete;
  OutputDirectory& operator=(const OutputDirectory&) = delete;
  ~OutputDirectory() {
    std::error_code ignored;
    fs::remove_all(parent, ignored);
  }
  fs::path parent;
};
std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  Require(input.is_open(), "open published physical file");
  const std::vector<char> raw{std::istreambuf_iterator<char>{input}, {}};
  Require(!input.bad(), "read complete published physical file");
  input.close();
  Require(!input.fail(), "close physical file");
  std::vector<std::byte> bytes(raw.size());
  std::transform(raw.begin(), raw.end(), bytes.begin(),
                 [](char c) { return static_cast<std::byte>(c); });
  return bytes;
}
mv::SurfaceFilePublicationLimits PublicationLimits(
    mv::SurfaceEncoding encoding) {
  return {
      {16, kMiB, 4 * kMiB, kMiB, 4096, 1024, 16384, 32768, 64, 16, kScopeBytes},
      InspectLimits(encoding),
      kScopeBytes + kAllowanceBytes};
}
mv::SurfaceFilePublicationWorkPolicy PublicationWork(
    mv::SurfaceEncoding encoding) {
  return {{64, 1, 256, 32, 4}, Work(encoding, true)};
}
void Run(mv::SurfaceEncoding encoding, const geom::ExecutionContext& context) {
  OutputDirectory directory;
  const auto destination = directory.parent / "published";
  std::optional<mv::SurfacePublishedBundle> owner;
  {
    const auto inspected = Prepare(encoding, context);
    if (!inspected)
      return;  // Actual unsupported construction/reader path asserted above.
    mv::SurfaceFilePublicationOptions options;
    options.receipt = {
        [](const mv::SurfaceFileVerifiedContent& content, void*) {
          Require(!content.entry_path.empty() &&
                      !content.inspection.CapturedMembers().empty(),
                  "application provider receives genuine freshly read-back "
                  "content");
          static constexpr std::string_view kApplicationReport =
              "Example application report, not a Geometry certification.";
          return std::as_bytes(
              std::span{kApplicationReport.data(), kApplicationReport.size()});
        },
        nullptr};
    const auto result = mv::PublishSurfaceBundle(
        *inspected, destination, options, PublicationLimits(encoding),
        PublicationWork(encoding));
    Require(result.outcome == mv::SurfaceFilePublicationOutcome::kPublished &&
                result.bundle &&
                result.diagnostic.status == mv::SurfaceFileStatus::kSuccess &&
                result.facts.commit &&
                result.facts.commit->outcome ==
                    mv::SurfaceFileCommitOutcome::kCommitted,
            "actual verified no-replace directory publication");
    owner = result.bundle;
  }
  // The result and all original E/V wrappers have expired. The retained owner
  // still owns its independently captured physical bytes and receipt.
  const auto& retained = owner->Inspection();
  std::vector<std::string> paths;
  std::vector<std::vector<std::byte>> bytes;
  std::vector<mv::SuppliedSurfaceMember> actual;
  for (const auto& member : retained.CapturedMembers()) {
    paths.emplace_back(member.path);
    bytes.push_back(Read(destination / member.path));
    Require(std::ranges::equal(bytes.back(), member.bytes),
            "reopened payload equals retained physical capture");
  }
  for (std::size_t i = 0; i < paths.size(); ++i)
    actual.push_back({paths[i], bytes[i]});
  const auto reread =
      mv::InspectSurfaceOutput(retained.Expected(), actual,
                               InspectLimits(encoding), Work(encoding, true));
  Require(reread.status == mv::SurfaceStatus::kSuccess && reread.inspection,
          "public application independently reinspects actual published files");
  const auto receipt = owner->Receipt();
  Require(
      receipt.path == "publication.json" &&
          std::ranges::equal(Read(destination / receipt.path), receipt.bytes),
      "physically reopened module-generated neutral receipt");
  std::size_t files = 0;
  for (const auto& entry : fs::recursive_directory_iterator(destination)) {
    Require(!entry.is_symlink(), "closed output contains no symbolic links");
    if (entry.is_regular_file()) ++files;
  }
  Require(files == actual.size() + 1,
          "closed payload inventory plus one receipt");
  auto alias = *owner;
  owner.reset();
  Require(
      alias.NewRequestedBytes() != 0 &&
          alias.Inspection().NewRequestedBytes() != 0,
      "copy-only published owner retains separate P/fresh-V charged storage");
  const auto repeat = mv::PublishSurfaceBundle(alias.Inspection(), destination,
                                               {}, PublicationLimits(encoding),
                                               PublicationWork(encoding));
  Require(!repeat.bundle && repeat.diagnostic.status ==
                                mv::SurfaceFileStatus::kDestinationExists,
          "application destination is never replaced");
}
}  // namespace
int main() {
  try {
    geom::ExecutionContext context({4, 1, 512 * kMiB});
    for (auto encoding :
         {mv::SurfaceEncoding::kGltfJson, mv::SurfaceEncoding::kGlb,
          mv::SurfaceEncoding::kStlAscii, mv::SurfaceEncoding::kStlBinary})
      Run(encoding, context);
    Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
            "all application owners and actual worker leases released");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
