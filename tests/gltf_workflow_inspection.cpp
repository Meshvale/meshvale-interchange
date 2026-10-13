// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "gltf_topology_bundle_test_internal.h"
#include "gltf_workflow_inspection_support.h"
#include "gltf_workflow_support_test_internal.h"

namespace gltf_workflow_inspection {
namespace {
constexpr std::size_t kTriangleWidth = 3;
constexpr std::uint64_t kTriangleMode = 4;
constexpr std::uint64_t kAppendedIndexAccessor = 7;
constexpr std::uint64_t kAppendedBuffer = 2;
constexpr std::uint64_t kAppendedBufferView = 8;
constexpr std::array<std::byte, 12> kTriangleIndexBytes{
    std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}};
constexpr std::string_view kTriangleDataUri =
    "data:application/octet-stream;base64,AAAAAAIAAAACAAAA";

geo::MeshInspection Select(const mv::GltfMaterializedPrimitive& source,
                           std::span<const geo::index_t> faces) {
  const auto selected = geo::SelectMeshFaces(
      source.Mesh(), faces, TransferLimits().materialization, GeometryWork());
  Require(selected.selection.has_value(),
          "public independently selected candidate");
  return Inspect(selected.selection->Candidate());
}
void CheckLiteral(const authored::Fixture& fixture,
                  const mv::GltfTopologyOutputInspection& inspection,
                  bool identity) {
  const auto expected_faces =
      identity ? std::span<const geo::index_t>(kIdentityFaces)
               : std::span<const geo::index_t>(kSelectedFaces);
  const auto expected_corners =
      identity ? std::span<const geo::index_t>(kIdentityCorners)
               : std::span<const geo::index_t>(kSelectedCorners);
  Require(std::ranges::equal(inspection.SourceFaces(), expected_faces) &&
              std::ranges::equal(inspection.SourceCorners(), expected_corners),
          "owned maps equal independent literal maps");
  Require(inspection.OriginalCorners().size() == expected_corners.size(),
          "whole composed original provenance");
  for (std::size_t i = 0; i < expected_corners.size(); ++i) {
    const auto original = expected_corners[i];
    Require(inspection.OriginalCorners()[i].primitive_stream_corner ==
                    fixture.stream_corners.at(original) &&
                inspection.OriginalCorners()[i].accessor_row ==
                    fixture.source_triangles.at(original),
            "authored literal stream and vertex-row provenance");
  }
  const auto& reloaded = inspection.ReloadedExtraction();
  const auto expected_indices =
      std::span(fixture.source_triangles)
          .subspan(identity ? 0 : kTriangleWidth, expected_corners.size());
  Require(std::ranges::equal(reloaded.TriangleIndices(), expected_indices),
          "actual reload directed topology against literal fixture");
  if (!identity) {
    Require(reloaded.SourceMode() == kTriangleMode &&
                reloaded.SourceIndices() &&
                reloaded.SourceIndices()->component_type ==
                    mv::GltfComponentType::kUint32,
            "changed actual reload uses TRIANGLES and UINT32");
    for (std::size_t i = 0; i < expected_indices.size(); ++i)
      Require(
          reloaded.SourceCorners()[i].primitive_stream_corner == i &&
              reloaded.SourceCorners()[i].accessor_row == expected_indices[i],
          "changed output stream ordinals differ from original stream");
  }
  if (fixture.kind == authored::FixtureKind::kTriangles && !identity) {
    const auto& added = inspection.Output().Resources().back();
    Require(std::ranges::equal(added.bytes, kTriangleIndexBytes) &&
                added.authored_uri == kTriangleDataUri &&
                added.table_index == kAppendedBuffer,
            "literal independent little-endian bytes and canonical base64");
    Require(
        reloaded.SourceIndices()->accessor_index == kAppendedIndexAccessor &&
            reloaded.SourceIndices()->base_buffer_view == kAppendedBufferView,
        "literal append-only table indices");
  }
  if (identity)
    Require(std::ranges::equal(inspection.Output().Json(),
                               std::as_bytes(std::span(fixture.json))),
            "no-op requires all original whitespace, escapes and lexemes");
  if (fixture.kind != authored::FixtureKind::kImplicit) {
    constexpr std::array<std::byte, 8> kPngSignature{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
    const auto resources = inspection.ReloadedAsset().Resources();
    Require(
        resources.size() == (identity ? 4 : 5),
        "independent full two-buffer/two-image inventory plus optional append");
    Require(
        resources[0].kind == mv::GltfResourceKind::kBuffer &&
            resources[0].table_index == 0 &&
            resources[0].logical_bytes == 132 &&
            std::ranges::equal(resources[0].bytes, fixture.external_buffer) &&
            resources[1].kind == mv::GltfResourceKind::kBuffer &&
            resources[1].table_index == 1 &&
            std::ranges::equal(resources[1].bytes, kPngSignature),
        "literal old buffers retain complete logical and trailing bytes");
    const auto first_image = identity ? 2 : 3;
    Require(
        resources[first_image].kind == mv::GltfResourceKind::kImage &&
            resources[first_image].table_index == 0 &&
            resources[first_image + 1].kind == mv::GltfResourceKind::kImage &&
            resources[first_image + 1].table_index == 1 &&
            resources[first_image + 1].buffer_index == 1 &&
            resources[first_image + 1].buffer_view_index == 7 &&
            resources[first_image + 1].bytes.data() ==
                resources[1].bytes.data() &&
            std::ranges::equal(resources[first_image].bytes, kPngSignature) &&
            std::ranges::equal(resources[first_image + 1].bytes, kPngSignature),
        "literal image order and image/buffer alias survive actual reload");
    if (!identity)
      Require(
          resources[2].kind == mv::GltfResourceKind::kBuffer &&
              resources[2].table_index == kAppendedBuffer,
          "new reloaded buffer precedes old images in the reader inventory");
  }
  const auto output_text = std::string_view(
      reinterpret_cast<const char*>(inspection.Output().Json().data()),
      inspection.Output().Json().size());
  for (const auto lexeme : fixture.preserved_lexemes)
    Require(output_text.find(lexeme) != std::string_view::npos,
            "authored opaque graph, numeric and shared-accessor lexemes");
}
authored::Fixture AllEncodingsFixture() {
  authored::Fixture fixture;
  fixture.kind = authored::FixtureKind::kTriangles;
  fixture.primitive_ordinal = 0;
  fixture.source_mode = 4;
  fixture.vertex_count = 4;
  fixture.source_triangles = {0, 1, 2, 0, 2, 3};
  fixture.stream_corners = {0, 1, 2, 3, 4, 5};
  fixture.json =
      R"({"asset":{"version":"2.0"},"buffers":[{"uri":"encodings.bin","byteLength":96}],"bufferViews":[{"buffer":0,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":16,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":32,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":48,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":64,"byteLength":24},{"buffer":0,"byteOffset":88,"byteLength":2},{"buffer":0,"byteOffset":92,"byteLength":4}],"accessors":[{"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[0,0,0]},{"bufferView":0,"componentType":5120,"count":4,"type":"SCALAR","normalized":true},{"bufferView":1,"componentType":5121,"count":4,"type":"SCALAR","normalized":true},{"bufferView":2,"componentType":5122,"count":4,"type":"SCALAR","normalized":true},{"bufferView":3,"componentType":5123,"count":4,"type":"SCALAR","normalized":true},{"componentType":5123,"count":4,"type":"SCALAR","sparse":{"count":2,"indices":{"bufferView":5,"componentType":5121},"values":{"bufferView":6}}},{"bufferView":4,"componentType":5125,"count":6,"type":"SCALAR"}],"meshes":[{"primitives":[{"indices":6,"attributes":{"POSITION":0,"_I8":1,"_U8":2,"_I16":3,"_U16":4,"_SPARSE":5}}]}]})";
  fixture.external_buffer.assign(100, std::byte{0xa5});
  constexpr std::array<std::uint8_t, 4> kSignedBytes{128, 127, 255, 0};
  constexpr std::array<std::uint8_t, 4> kUnsignedBytes{0, 255, 128, 1};
  constexpr std::array<std::uint16_t, 4> kSignedShortBits{32768, 32767, 65535,
                                                          0};
  constexpr std::array<std::uint16_t, 4> kUnsignedShorts{0, 65535, 32768, 1};
  for (std::size_t row = 0; row < 4; ++row) {
    fixture.external_buffer[row * 4] = std::byte{kSignedBytes[row]};
    fixture.external_buffer[16 + row * 4] = std::byte{kUnsignedBytes[row]};
    fixture.external_buffer[32 + row * 4] =
        static_cast<std::byte>(kSignedShortBits[row] & 255);
    fixture.external_buffer[33 + row * 4] =
        static_cast<std::byte>(kSignedShortBits[row] >> 8);
    fixture.external_buffer[48 + row * 4] =
        static_cast<std::byte>(kUnsignedShorts[row] & 255);
    fixture.external_buffer[49 + row * 4] =
        static_cast<std::byte>(kUnsignedShorts[row] >> 8);
  }
  for (std::size_t i = 0; i < fixture.source_triangles.size(); ++i)
    for (unsigned byte = 0; byte < 4; ++byte)
      fixture.external_buffer[64 + i * 4 + byte] = static_cast<std::byte>(
          (fixture.source_triangles[i] >> (byte * 8)) & 255);
  fixture.external_buffer[88] = std::byte{1};
  fixture.external_buffer[89] = std::byte{3};
  fixture.external_buffer[92] = std::byte{255};
  fixture.external_buffer[93] = std::byte{255};
  fixture.external_buffer[94] = std::byte{0};
  fixture.external_buffer[95] = std::byte{128};
  return fixture;
}
void AllEncodingsAndNormalization() {
  const auto fixture = AllEncodingsFixture();
  const auto context = Context();
  const auto source = Source(fixture, context);
  const auto candidate = Select(source, kSelectedFaces);
  const auto output =
      Write(source, candidate, kSelectedFaces, kSelectedCorners);
  const auto reloaded = Reload(output, context);
  const auto extracted = Extract(reloaded, 0, false);
  const auto channels = extracted.Channels();
  const auto channel =
      [&](std::string_view name) -> const mv::GltfChannelView& {
    const auto found = std::ranges::find_if(
        channels, [name](const auto& value) { return value.semantic == name; });
    Require(found != channels.end(), "whole six-channel encoding inventory");
    return *found;
  };
  Require(
      std::ranges::equal(
          std::get<std::span<const std::int8_t>>(channel("_I8").raw),
          std::array<std::int8_t, 4>{-128, 127, -1, 0}) &&
          std::ranges::equal(
              std::get<std::span<const std::uint8_t>>(channel("_U8").raw),
              std::array<std::uint8_t, 4>{0, 255, 128, 1}) &&
          std::ranges::equal(
              std::get<std::span<const std::int16_t>>(channel("_I16").raw),
              std::array<std::int16_t, 4>{-32768, 32767, -1, 0}) &&
          std::ranges::equal(
              std::get<std::span<const std::uint16_t>>(channel("_U16").raw),
              std::array<std::uint16_t, 4>{0, 65535, 32768, 1}) &&
          std::ranges::equal(
              std::get<std::span<const std::uint16_t>>(channel("_SPARSE").raw),
              std::array<std::uint16_t, 4>{0, 65535, 0, 32768}),
      "literal typed original extrema and sparse destinations");
  constexpr std::array<std::uint64_t, 4> kFractionBits{
      0xbf80204081020408ULL, 0x3fe0101010101010ULL, 0xbf00002000400080ULL,
      0x3fe0001000100010ULL};
  std::size_t i = 0;
  for (const auto semantic : {"_I8", "_U8", "_I16", "_U16"})
    Require(std::bit_cast<std::uint64_t>(
                channel(semantic).normalized_values[2]) == kFractionBits[i++],
            "independent IEEE binary64 normalized nontrivial fraction");
  const auto result = mv::InspectGltfTopologyOutput(
      {source, candidate, kSelectedFaces, kSelectedCorners, output, reloaded,
       extracted},
      Limits(), Work());
  CheckComplete(result, false);
  Require(std::ranges::equal(
              result.inspection->OriginalCorners(),
              std::array<mv::GltfSourceCorner, 3>{mv::GltfSourceCorner{3, 0},
                                                  mv::GltfSourceCorner{4, 2},
                                                  mv::GltfSourceCorner{5, 3}},
              [](const auto& a, const auto& b) {
                return a.primitive_stream_corner == b.primitive_stream_corner &&
                       a.accessor_row == b.accessor_row;
              }),
          "literal original UINT32 stream correspondence");
}
void AcceptedSelectionsAndLifetimes() {
  for (const auto kind :
       {authored::FixtureKind::kTriangles, authored::FixtureKind::kStrip,
        authored::FixtureKind::kFan, authored::FixtureKind::kImplicit}) {
    for (const bool identity : {false, true}) {
      auto monitor = Context();
      auto fixture = authored::MakeFixture(kind);
      std::optional<mv::GltfTopologyOutputInspection> retained;
      {
        auto context = monitor;
        const auto source = Source(fixture, context);
        const auto faces = identity
                               ? std::span<const geo::index_t>(kIdentityFaces)
                               : std::span<const geo::index_t>(kSelectedFaces);
        const auto corners =
            identity ? std::span<const geo::index_t>(kIdentityCorners)
                     : std::span<const geo::index_t>(kSelectedCorners);
        const auto candidate = Select(source, faces);
        const auto output = Write(source, candidate, faces, corners);
        const auto reloaded = Reload(output, context);
        const auto extraction =
            Extract(reloaded, fixture.primitive_ordinal,
                    identity && (kind == authored::FixtureKind::kStrip ||
                                 kind == authored::FixtureKind::kFan));
        std::vector<geo::index_t> caller_faces(faces.begin(), faces.end());
        std::vector<geo::index_t> caller_corners(corners.begin(),
                                                 corners.end());
        const mv::GltfTopologyOutputInspectionRequest request{
            source, candidate, caller_faces, caller_corners,
            output, reloaded,  extraction};
        auto serial =
            mv::InspectGltfTopologyOutput(request, Limits(), Work(true));
        if (!serial.inspection) {
          std::cerr << "fixture kind=" << static_cast<int>(kind)
                    << " identity=" << identity << '\n';
          const auto show =
              [](std::string_view name,
                 std::span<const mv::GltfResourceView> resources) {
                for (std::size_t i = 0; i < resources.size(); ++i) {
                  const auto& resource = resources[i];
                  std::cerr
                      << name << '[' << i
                      << "] kind=" << static_cast<int>(resource.kind)
                      << " index=" << resource.table_index
                      << " origin=" << static_cast<int>(resource.origin)
                      << " logical=" << resource.logical_bytes
                      << " bytes=" << resource.bytes.size() << " buffer="
                      << (resource.buffer_index
                              ? std::to_string(*resource.buffer_index)
                              : "absent")
                      << " view="
                      << (resource.buffer_view_index
                              ? std::to_string(*resource.buffer_view_index)
                              : "absent")
                      << '\n';
                }
              };
          show("original", source.SourceAsset().Resources());
          show("bundle", output.Resources());
          show("reload", reloaded.Resources());
        }
        CheckComplete(serial, true);
        CheckLiteral(fixture, *serial.inspection, identity);
        auto parallel =
            mv::InspectGltfTopologyOutput(request, Limits(), Work());
        CheckComplete(parallel, false);
        CheckLiteral(fixture, *parallel.inspection, identity);
        {
          auto occupied = geo::TryReserveWorkers(context, kWorkerBudget);
          Require(occupied.Count() == kWorkerBudget,
                  "occupy real shared worker slots");
          const auto fallback =
              mv::InspectGltfTopologyOutput(request, Limits(), Work());
          CheckComplete(fallback, true);
          CheckLiteral(fixture, *fallback.inspection, identity);
        }
        const auto before_copy = context.ActiveTrackedPayload();
        retained = *parallel.inspection;
        Require(context.ActiveTrackedPayload() == before_copy &&
                    retained->NewRequestedBytes() ==
                        parallel.inspection->NewRequestedBytes(),
                "copy shares one inspection owner and charge");
        caller_faces.assign(caller_faces.size(), 999);
        caller_corners.clear();
        CheckLiteral(fixture, *retained, identity);
      }
      CheckLiteral(fixture, *retained, identity);
      fixture.external_buffer.clear();
      fixture.json.clear();
      Require(
          retained->Source().SourceAsset().NewRequestedBytes().has_value() &&
              retained->ReloadedAsset().NewRequestedBytes().has_value() &&
              retained->Source().SourceExtraction().NewRequestedBytes() > 0 &&
              retained->ReloadedExtraction().NewRequestedBytes() > 0 &&
              retained->NewRequestedBytes() > 0,
          "retained strong stages keep distinct attributable live counters");
      retained.reset();
      Require(
          monitor.ActiveTrackedPayload() == 0 && monitor.ActiveWorkers() == 0,
          "all copied inspection and retained input owners finally release");
    }
  }
}

enum class CandidateChange {
  kPosition,
  kSignedBits,
  kDescriptor,
  kPresence,
  kLoop,
  kInvalidStorage,
  kPositionAndDescriptor,
  kQuad
};
geo::MeshInspection AlterCandidate(const mv::GltfMaterializedPrimitive& source,
                                   const geo::ExecutionContext& context,
                                   CandidateChange change) {
  const auto view = source.Mesh().View();
  std::vector<double> positions(view.positions.begin(), view.positions.end());
  std::array<geo::index_t, 2> offsets{0, 3};
  std::vector<geo::index_t> corners(view.corner_vertices.begin() + 3,
                                    view.corner_vertices.end());
  std::vector<geo::AttributeMaterializationInput> attributes;
  for (const auto& attribute : view.attributes)
    attributes.push_back(
        {attribute.domain, attribute.name, attribute.semantic,
         attribute.set_index, attribute.components,
         std::visit([](auto values) -> geo::ScalarIngress { return values; },
                    attribute.values),
         attribute.offsets, attribute.present, attribute.metadata});
  const auto raw =
      std::get<std::span<const std::int32_t>>(view.attributes[0].values);
  std::vector<std::int32_t> signed_values(raw.begin(), raw.end());
  constexpr std::array<std::uint8_t, 4> kPresence{1, 1, 1, 1};
  switch (change) {
    case CandidateChange::kPosition:
      positions[0] = 0.0;
      break;
    case CandidateChange::kSignedBits:
      ++signed_values[0];
      attributes[0].values = std::span<const std::int32_t>(signed_values);
      break;
    case CandidateChange::kDescriptor:
      attributes[0].set_index = 1;
      break;
    case CandidateChange::kPresence:
      attributes[0].present = kPresence;
      break;
    case CandidateChange::kLoop:
      std::swap(corners[0], corners[1]);
      break;
    case CandidateChange::kInvalidStorage:
      corners[0] = 999;
      break;
    case CandidateChange::kPositionAndDescriptor:
      positions[0] = 0.0;
      attributes[0].set_index = 1;
      break;
    case CandidateChange::kQuad:
      offsets[1] = 4;
      corners = {0, 1, 2, 3};
      break;
  }
  const auto result = geo::MaterializeMesh(
      {std::span<const double>(positions),
       geo::PolygonTopologyInput{offsets, corners}, attributes},
      TransferLimits().materialization, GeometryWork(), context);
  Require(result.mesh.has_value(),
          "independent deliberately altered canonical capture");
  return Inspect(*result.mesh);
}
void LateStopPreservesEarliestFailure() {
  const auto fixture = authored::MakeFixture(authored::FixtureKind::kTriangles);
  const auto context = Context();
  const auto source = Source(fixture, context);
  const auto matching = Select(source, kSelectedFaces);
  const auto output = Write(source, matching, kSelectedFaces, kSelectedCorners);
  const auto reload = Reload(output, context);
  const auto extraction = Extract(reload, fixture.primitive_ordinal, false);
  const auto altered =
      AlterCandidate(source, context, CandidateChange::kPositionAndDescriptor);
  struct StopEntry {
    std::stop_source stop;
    std::atomic<std::size_t> launched{0};
    std::atomic<std::size_t> joined{0};
    std::atomic<std::size_t> entered{0};
    static void Launched(std::size_t, void* state) noexcept {
      ++static_cast<StopEntry*>(state)->launched;
    }
    static void Joined(std::size_t, void* state) noexcept {
      ++static_cast<StopEntry*>(state)->joined;
    }
    static void Entered(std::size_t, void* state) noexcept {
      auto& self = *static_cast<StopEntry*>(state);
      ++self.entered;
      self.stop.request_stop();
    }
  };
  const auto attempt = [&](bool occupy) {
    StopEntry stop;
    const auto baseline = context.ActiveTrackedPayload();
    const mv::gltf_detail::OperationWorkerObserver observer{
        StopEntry::Launched, StopEntry::Joined, StopEntry::Entered, &stop};
    auto lease = occupy ? geo::TryReserveWorkers(context, kWorkerBudget)
                        : geo::WorkerLease();
    auto work = Work();
    // Twelve binary64 position scalars give several independent byte ranges.
    // A single job deliberately stays on the caller in the shared policy.
    work.partition_bytes = 16;
    const auto result =
        mv::gltf_workflow_support_detail::InspectGltfTopologyOutputForTest(
            {source, altered, kSelectedFaces, kSelectedCorners, output, reload,
             extraction},
            Limits(), work, stop.stop.get_token(),
            {mv::gltf_detail::DefaultAllocationBackend(),
             mv::gltf_detail::DefaultNativeWorkerBackend(), &observer});
    Require(!result.inspection && result.status == Status::kOutputMismatch &&
                result.diagnostic.stage == Stage::kSourceCandidate &&
                result.diagnostic.code == Code::kPositionBits &&
                result.diagnostic.element == 0 && stop.entered > 0 &&
                stop.launched == stop.joined &&
                context.ActiveTrackedPayload() == baseline,
            "late stop with known descriptor mismatch retains independently "
            "earlier position failure");
    Require(
        (stop.launched > 0) == !occupy,
        "same first failure with actual workers and saturated caller fallback");
  };
  attempt(false);
  attempt(true);
  Require(context.ActiveWorkers() == 0,
          "late-stop and occupied fallback release all workers");
}
void IndependentInclusiveLimits() {
  const auto fixture = authored::MakeFixture(authored::FixtureKind::kImplicit);
  const auto context = Context();
  const auto source = Source(fixture, context);
  const auto output =
      Write(source, source.Inspection(), kIdentityFaces, kIdentityCorners);
  const auto reload = Reload(output, context);
  const auto extraction = Extract(reload, 0, false);
  const mv::GltfTopologyOutputInspectionRequest request{
      source, source.Inspection(), kIdentityFaces, kIdentityCorners, output,
      reload, extraction};
  // Independent counts of the authored JSON values: root + asset/version,
  // accessor table with its two bound arrays, and mesh/primitive/attributes.
  // Keys are not separate value nodes. Root-to-POSITION has seven value levels.
  constexpr std::uint64_t kLiteralJsonValues = 22;
  constexpr std::uint64_t kLiteralJsonDepth = 7;
  constexpr std::uint64_t kLiteralPositionScalars = 18;
  constexpr std::uint64_t kLiteralBoundScalars = 6;
  constexpr std::uint64_t kLiteralSemanticBytes = 8;
  constexpr std::uint64_t kLiteralSelectedIndexBytes = 24;
  auto limits = Limits();
  limits.max_json_bytes = fixture.json.size();
  limits.max_json_nodes = kLiteralJsonValues;
  limits.max_json_depth = kLiteralJsonDepth;
  limits.max_resources = 1;
  limits.max_resource_bytes = 1;
  limits.max_vertices = 6;
  limits.max_channels = 1;
  // Each exposed source/reload channel also retains the authored min/max:
  // three scalar components each. The canonical mesh has only POSITION.
  limits.max_attribute_scalars = kLiteralPositionScalars + kLiteralBoundScalars;
  limits.max_descriptor_bytes = kLiteralSemanticBytes;
  limits.max_candidate_faces = 2;
  limits.max_candidate_corners = 6;
  limits.max_index_bytes = kLiteralSelectedIndexBytes;
  CheckComplete(mv::InspectGltfTopologyOutput(request, limits, Work()), false);
  for (const auto field :
       {&mv::GltfTopologyOutputInspectionLimits::max_json_bytes,
        &mv::GltfTopologyOutputInspectionLimits::max_json_nodes,
        &mv::GltfTopologyOutputInspectionLimits::max_json_depth,
        &mv::GltfTopologyOutputInspectionLimits::max_vertices,
        &mv::GltfTopologyOutputInspectionLimits::max_attribute_scalars,
        &mv::GltfTopologyOutputInspectionLimits::max_descriptor_bytes,
        &mv::GltfTopologyOutputInspectionLimits::max_candidate_faces,
        &mv::GltfTopologyOutputInspectionLimits::max_candidate_corners,
        &mv::GltfTopologyOutputInspectionLimits::max_index_bytes}) {
    auto smaller = limits;
    --(smaller.*field);
    CheckRejected(request, Status::kLimitExceeded, Code::kCountLimit,
                  Stage::kAdmission, smaller);
  }
}
void RejectionsAndFirstFailure() {
  const auto fixture = authored::MakeFixture(authored::FixtureKind::kStrip);
  const auto context = Context();
  const auto source = Source(fixture, context);
  const auto candidate = Select(source, kSelectedFaces);
  const auto output =
      Write(source, candidate, kSelectedFaces, kSelectedCorners);
  const auto reloaded = Reload(output, context);
  const auto extraction = Extract(reloaded, fixture.primitive_ordinal, false);
  const mv::GltfTopologyOutputInspectionRequest request{
      source, candidate, kSelectedFaces, kSelectedCorners,
      output, reloaded,  extraction};
  const auto zero_fields = std::array{
      &mv::GltfTopologyOutputInspectionLimits::max_json_bytes,
      &mv::GltfTopologyOutputInspectionLimits::max_json_nodes,
      &mv::GltfTopologyOutputInspectionLimits::max_json_depth,
      &mv::GltfTopologyOutputInspectionLimits::max_resources,
      &mv::GltfTopologyOutputInspectionLimits::max_resource_bytes,
      &mv::GltfTopologyOutputInspectionLimits::max_vertices,
      &mv::GltfTopologyOutputInspectionLimits::max_channels,
      &mv::GltfTopologyOutputInspectionLimits::max_attribute_scalars,
      &mv::GltfTopologyOutputInspectionLimits::max_descriptor_bytes,
      &mv::GltfTopologyOutputInspectionLimits::max_candidate_faces,
      &mv::GltfTopologyOutputInspectionLimits::max_candidate_corners,
      &mv::GltfTopologyOutputInspectionLimits::max_index_bytes,
      &mv::GltfTopologyOutputInspectionLimits::max_new_requested_bytes};
  for (const auto field : zero_fields) {
    auto limits = Limits();
    limits.*field = 0;
    CheckRejected(request, Status::kInvalidConfiguration,
                  Code::kRequiredFieldZero, Stage::kAdmission, limits);
  }
  for (const auto field :
       {&mv::GltfTopologyOutputInspectionWorkPolicy::parallel_min_bytes,
        &mv::GltfTopologyOutputInspectionWorkPolicy::partition_bytes,
        &mv::GltfTopologyOutputInspectionWorkPolicy::cancellation_block_bytes,
        &mv::GltfTopologyOutputInspectionWorkPolicy::max_jobs}) {
    auto work = Work();
    work.*field = 0;
    const auto baseline = context.ActiveTrackedPayload();
    const auto rejected =
        mv::InspectGltfTopologyOutput(request, Limits(), work);
    Require(!rejected.inspection &&
                rejected.status == Status::kInvalidConfiguration &&
                rejected.diagnostic.code == Code::kRequiredFieldZero &&
                rejected.diagnostic.stage == Stage::kAdmission &&
                context.ActiveTrackedPayload() == baseline,
            "every explicit work-policy field is required before allocation");
  }
  for (const auto field :
       {&mv::GltfTopologyOutputInspectionLimits::max_json_nodes,
        &mv::GltfTopologyOutputInspectionLimits::max_json_depth,
        &mv::GltfTopologyOutputInspectionLimits::max_resources,
        &mv::GltfTopologyOutputInspectionLimits::max_resource_bytes,
        &mv::GltfTopologyOutputInspectionLimits::max_vertices,
        &mv::GltfTopologyOutputInspectionLimits::max_channels,
        &mv::GltfTopologyOutputInspectionLimits::max_attribute_scalars,
        &mv::GltfTopologyOutputInspectionLimits::max_descriptor_bytes,
        &mv::GltfTopologyOutputInspectionLimits::max_candidate_corners,
        &mv::GltfTopologyOutputInspectionLimits::max_index_bytes}) {
    auto limits = Limits();
    limits.*field = 1;
    CheckRejected(request, Status::kLimitExceeded, Code::kCountLimit,
                  Stage::kAdmission, limits);
  }
  auto too_small = Limits();
  too_small.max_json_bytes = 1;
  CheckRejected(request, Status::kLimitExceeded, Code::kCountLimit,
                Stage::kAdmission, too_small);
  too_small = Limits();
  too_small.max_new_requested_bytes = 1;
  CheckRejected(request, Status::kLimitExceeded, Code::kInspectionRequestLimit,
                Stage::kAdmission, too_small);
  std::stop_source stopped;
  stopped.request_stop();
  CheckRejected(request, Status::kCanceled, Code::kStopRequested,
                Stage::kAdmission, Limits(), stopped.get_token());

  const auto equal_source = Source(fixture, context);
  CheckRejected({equal_source, candidate, kSelectedFaces, kSelectedCorners,
                 output, reloaded, extraction},
                Status::kSourceContractFailure, Code::kSourceIdentity,
                Stage::kAdmission);
  const auto second_reload = Reload(output, context);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners, output,
                 second_reload, extraction},
                Status::kSourceContractFailure, Code::kReloadIdentity,
                Stage::kAdmission);
  const auto wrong_primitive = Extract(reloaded, 0, true);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners, output,
                 reloaded, wrong_primitive},
                Status::kSourceContractFailure, Code::kReloadIdentity,
                Stage::kAdmission);
  const auto foreign_context = Context();
  const auto foreign_reload = Reload(output, foreign_context);
  const auto foreign_extraction =
      Extract(foreign_reload, fixture.primitive_ordinal, false);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners, output,
                 foreign_reload, foreign_extraction},
                Status::kForeignBudget, Code::kForeignBudget,
                Stage::kAdmission);
  const auto raw_only =
      Extract(reloaded, fixture.primitive_ordinal, false, false);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners, output,
                 reloaded, raw_only},
                Status::kOutputMismatch, Code::kChannelBits,
                Stage::kReloadedPrimitive);

  for (const auto change :
       {CandidateChange::kPosition, CandidateChange::kSignedBits,
        CandidateChange::kDescriptor, CandidateChange::kPresence}) {
    const auto altered = AlterCandidate(source, context, change);
    const auto code = change == CandidateChange::kPosition ? Code::kPositionBits
                      : change == CandidateChange::kSignedBits
                          ? Code::kAttributeBits
                          : Code::kAttributeDescriptor;
    CheckRejected({source, altered, {}, {}, output, reloaded, extraction},
                  Status::kOutputMismatch, code, Stage::kSourceCandidate);
  }
  const auto invalid =
      AlterCandidate(source, context, CandidateChange::kInvalidStorage);
  Require(!invalid.IsStorageValid(),
          "real supplied invalid storage inspection");
  const auto invalid_result =
      CheckRejected({source, invalid, {}, {}, output, reloaded, extraction},
                    Status::kInvalidCandidateStorage, Code::kCandidateStorage,
                    Stage::kAdmission);
  Require(invalid_result.diagnostic.storage_issue &&
              !invalid.Issues().empty() &&
              invalid_result.diagnostic.storage_issue->code ==
                  invalid.Issues()[0].code &&
              invalid_result.diagnostic.storage_issue->attribute ==
                  invalid.Issues()[0].attribute &&
              invalid_result.diagnostic.storage_issue->element ==
                  invalid.Issues()[0].element &&
              invalid_result.diagnostic.storage_issue->code ==
                  geo::StorageIssueCode::kCornerVertexRange,
          "inspector retains exact first actual supplied StorageIssue");
  CheckRejected({source, candidate, {}, {}, output, reloaded, extraction},
                Status::kInvalidCorrespondence, Code::kFaceMapShape,
                Stage::kCorrespondence);
  CheckRejected(
      {source, candidate, kSelectedFaces, {}, output, reloaded, extraction},
      Status::kInvalidCorrespondence, Code::kCornerMapShape,
      Stage::kCorrespondence);
  constexpr std::array<geo::index_t, 1> kOutOfRange{2};
  CheckRejected({source, candidate, kOutOfRange, kSelectedCorners, output,
                 reloaded, extraction},
                Status::kInvalidCorrespondence, Code::kFaceMapRange,
                Stage::kCorrespondence);
  constexpr std::array<geo::index_t, 3> kWrongCorners{3, 5, 4};
  CheckRejected({source, candidate, kSelectedFaces, kWrongCorners, output,
                 reloaded, extraction},
                Status::kInvalidCorrespondence, Code::kCornerMapValue,
                Stage::kCorrespondence);
  const auto reversed = AlterCandidate(source, context, CandidateChange::kLoop);
  CheckRejected({source, reversed, kSelectedFaces, kSelectedCorners, output,
                 reloaded, extraction},
                Status::kInvalidCorrespondence, Code::kTriangleLoop,
                Stage::kCorrespondence);
  const auto quad = AlterCandidate(source, context, CandidateChange::kQuad);
  constexpr std::array<geo::index_t, 1> kQuadFace{0};
  constexpr std::array<geo::index_t, 4> kQuadCorners{0, 1, 2, 3};
  CheckRejected(
      {source, quad, kQuadFace, kQuadCorners, output, reloaded, extraction},
      Status::kInvalidCorrespondence, Code::kTriangleShape,
      Stage::kCorrespondence);
  const auto identity = Select(source, kIdentityFaces);
  for (const auto wrong_faces :
       {std::array<geo::index_t, 2>{1, 0}, std::array<geo::index_t, 2>{0, 0}}) {
    constexpr std::array<geo::index_t, 6> kAlsoWrongCorners{3, 4, 5, 0, 1, 2};
    const auto result =
        CheckRejected({source, identity, wrong_faces, kAlsoWrongCorners, output,
                       reloaded, extraction},
                      Status::kInvalidCorrespondence, Code::kFaceMapOrder,
                      Stage::kCorrespondence);
    Require(result.diagnostic.element == 1,
            "face order precedes later corner and actual loop failures at "
            "exact ordinal");
  }
}
void CorruptActualOutputAndReload() {
  namespace fixture_seam = mv::gltf_topology_write_detail;
  const auto fixture = authored::MakeFixture(authored::FixtureKind::kTriangles);
  const auto context = Context();
  const auto source = Source(fixture, context);
  const auto candidate = Select(source, kSelectedFaces);
  const auto output =
      Write(source, candidate, kSelectedFaces, kSelectedCorners);
  const auto original_reload = Reload(output, context);
  const auto original_extraction =
      Extract(original_reload, fixture.primitive_ordinal, false);
  std::string altered_json(reinterpret_cast<const char*>(output.Json().data()),
                           output.Json().size());
  constexpr std::string_view kOldLexeme = "1.00e+01";
  constexpr std::string_view kEqualNumericLexeme = "1.0e+001";
  const auto offset = altered_json.find(kOldLexeme);
  Require(offset != std::string::npos, "authored opaque decimal literal");
  altered_json.replace(offset, kOldLexeme.size(), kEqualNumericLexeme);
  fixture_seam::BundleTestOverrides changed;
  changed.json = std::as_bytes(std::span(altered_json));
  const auto lexical_output = fixture_seam::CloneBundleForTest(output, changed);
  const auto lexical_reload = Reload(lexical_output, context);
  const auto lexical_extraction =
      Extract(lexical_reload, fixture.primitive_ordinal, false);
  const auto lexical_result = CheckRejected(
      {source, candidate, kSelectedFaces, kSelectedCorners, lexical_output,
       lexical_reload, lexical_extraction},
      Status::kOutputMismatch, Code::kLexicalPreservation, Stage::kLexical);
  Require(lexical_result.diagnostic.json_span &&
              lexical_result.diagnostic.json_span->offset == offset + 3 &&
              lexical_result.diagnostic.json_span->length == 1,
          "exact independent first altered opaque lexeme byte");
  // A perfectly equivalent mesh is insufficient when retained actual JSON is
  // not the emitted document. Both independently supplied owners are genuine.
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners, output,
                 lexical_reload, lexical_extraction},
                Status::kOutputMismatch, Code::kOutputJson, Stage::kLexical);

  std::vector<std::byte> tail(output.Resources()[0].bytes.begin(),
                              output.Resources()[0].bytes.end());
  tail.back() ^= std::byte{1};
  std::vector<mv::GltfResourceView> resources(output.Resources().begin(),
                                              output.Resources().end());
  resources[0].bytes = tail;
  changed = {};
  changed.resources = resources;
  const auto tail_output = fixture_seam::CloneBundleForTest(output, changed);
  const auto tail_reload = Reload(tail_output, context);
  const auto tail_extraction =
      Extract(tail_reload, fixture.primitive_ordinal, false);
  Require(
      std::ranges::equal(tail_extraction.TriangleIndices(),
                         original_extraction.TriangleIndices()),
      "unaddressed trailing-byte corruption leaves selected mesh unchanged");
  const auto tail_result = CheckRejected(
      {source, candidate, kSelectedFaces, kSelectedCorners, tail_output,
       tail_reload, tail_extraction},
      Status::kOutputMismatch, Code::kResourceBytes, Stage::kResources);
  Require(tail_result.diagnostic.resource == 0 &&
              tail_result.diagnostic.element == tail.size() - 1,
          "exact first full-resource trailing-byte location");
  // Corrupt only actual reload, preserving the actual emitted bundle.
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners, output,
                 tail_reload, tail_extraction},
                Status::kOutputMismatch, Code::kResourceBytes,
                Stage::kResources);

  resources.assign(output.Resources().begin(), output.Resources().end());
  resources[0].authored_uri = "different.bin";
  changed = {};
  changed.resources = resources;
  const auto uri_output = fixture_seam::CloneBundleForTest(output, changed);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners,
                 uri_output, original_reload, original_extraction},
                Status::kOutputMismatch, Code::kResourceIdentity,
                Stage::kResources);

  changed = {};
  changed.changed_binding = std::optional<mv::GltfTopologyBindingChange>{};
  const auto missing_receipt =
      fixture_seam::CloneBundleForTest(output, changed);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners,
                 missing_receipt, original_reload, original_extraction},
                Status::kOutputMismatch, Code::kAppendedBinding,
                Stage::kProvenance);
  auto binding = *output.ChangedBinding();
  ++binding.output_indices_accessor;
  changed = {};
  changed.changed_binding = binding;
  const auto wrong_receipt = fixture_seam::CloneBundleForTest(output, changed);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners,
                 wrong_receipt, original_reload, original_extraction},
                Status::kOutputMismatch, Code::kAppendedBinding,
                Stage::kProvenance);
  constexpr std::array<geo::index_t, 1> kWrongReceiptFace{0};
  changed = {};
  changed.faces = kWrongReceiptFace;
  const auto wrong_face_receipt =
      fixture_seam::CloneBundleForTest(output, changed);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners,
                 wrong_face_receipt, original_reload, original_extraction},
                Status::kOutputMismatch, Code::kSourceProvenance,
                Stage::kProvenance);
  constexpr std::array<mv::GltfSourceCorner, 3> kWrongOriginalCorners{
      mv::GltfSourceCorner{0, 0}, mv::GltfSourceCorner{1, 2},
      mv::GltfSourceCorner{2, 2}};
  changed = {};
  changed.original_corners = kWrongOriginalCorners;
  const auto wrong_provenance =
      fixture_seam::CloneBundleForTest(output, changed);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners,
                 wrong_provenance, original_reload, original_extraction},
                Status::kOutputMismatch, Code::kSourceProvenance,
                Stage::kProvenance);
  // Earlier lexical failure must win independently of a later bad receipt.
  changed.json = std::as_bytes(std::span(altered_json));
  const auto two_failures = fixture_seam::CloneBundleForTest(output, changed);
  const auto two_reload = Reload(two_failures, context);
  const auto two_extraction =
      Extract(two_reload, fixture.primitive_ordinal, false);
  CheckRejected({source, candidate, kSelectedFaces, kSelectedCorners,
                 two_failures, two_reload, two_extraction},
                Status::kOutputMismatch, Code::kLexicalPreservation,
                Stage::kLexical);
}
void LargeSourceIndex(std::uint64_t budget_mib) {
  constexpr std::uint64_t kMebibyte = 1024 * 1024;
  constexpr std::uint64_t kMinimumBudgetMib = 2048;
  constexpr std::uint64_t kLargeVertexCount = 16777218;
  constexpr std::uint64_t kLargeScalarLimit = 64 * 1024 * 1024;
  constexpr std::array<geo::index_t, 1> kLargeSelectedFace{0};
  constexpr std::array<geo::index_t, 3> kLargeSelectedCorners{0, 1, 2};
  constexpr std::array<std::uint32_t, 3> kLargeSelectedIndices{16777217, 0, 1};
  Require(
      budget_mib >= kMinimumBudgetMib &&
          budget_mib <= std::numeric_limits<std::size_t>::max() / kMebibyte,
      "large inspector fixture requires a host-representable 2048 MiB budget");
  const auto budget = budget_mib * kMebibyte;
  const geo::ExecutionContext context(
      {kWorkerBudget, 1, static_cast<std::size_t>(budget)});
  {
    const auto fixture =
        authored::MakeFixture(authored::FixtureKind::kLargeIndex);
    auto read_limits = ReadLimits();
    read_limits.source.max_accessor_elements = kLargeVertexCount;
    read_limits.max_new_requested_bytes = budget;
    const std::array resources{mv::SuppliedGltfResource{
        mv::GltfResourceKind::kBuffer, 0, fixture.external_buffer}};
    const auto captured = mv::ReadGltfAssetCapped(
        std::as_bytes(std::span(fixture.json)), resources, read_limits,
        {1, kPartitionBytes, 1, 65536}, context);
    Require(captured.asset.has_value(), "actual large source capped read");
    const mv::GltfExtractionLimits extraction_limits{kLargeScalarLimit,
                                                     kCountLimit, budget};
    const auto extracted = mv::ExtractGltfPrimitiveCapped(
        *captured.asset, *captured.asset->Primitive(0),
        {mv::GltfTriangleRequest::kRequireTriangles,
         mv::GltfValueRequest::kRawAndNormalized},
        extraction_limits);
    Require(extracted.extraction.has_value() &&
                extracted.extraction->TriangleIndices()[0] == 16777217,
            "actual source UINT32 index above binary32 exact integer range");
    auto transfer_limits = TransferLimits();
    transfer_limits.materialization.max_vertices = kLargeVertexCount;
    transfer_limits.materialization.max_attribute_scalars = kLargeScalarLimit;
    transfer_limits.materialization.max_new_requested_bytes = budget;
    const auto transfer = mv::MaterializeGltfPrimitive(
        *extracted.extraction, transfer_limits, GeometryWork());
    Require(transfer.primitive.has_value(),
            "actual large numerical source materialization");
    const auto& source = *transfer.primitive;
    const auto selection =
        geo::SelectMeshFaces(source.Mesh(), kLargeSelectedFace,
                             transfer_limits.materialization, GeometryWork());
    Require(selection.selection.has_value(),
            "actual large canonical selection");
    const auto candidate = Inspect(selection.selection->Candidate());
    mv::GltfTopologyWriteLimits writer_limits{
        kByteLimit,
        kCountLimit,
        kDepthLimit,
        kLargeVertexCount,
        kChannelLimit,
        kLargeScalarLimit,
        kByteLimit,
        kCountLimit,
        kCountLimit,
        kByteLimit,
        {kByteLimit, kCountLimit, kByteLimit, kByteLimit, budget}};
    const auto written = mv::WriteGltfTopologySelection(
        {source, candidate, kLargeSelectedFace, kLargeSelectedCorners},
        writer_limits, {1, kPartitionBytes, kCheckpointBytes, kMaxJobs});
    Require(written.bundle.has_value(),
            "actual large candidate topology output");
    const auto& output = *written.bundle;
    const auto reload_resources = std::array{mv::SuppliedGltfResource{
        mv::GltfResourceKind::kBuffer, 0, output.Resources()[0].bytes}};
    const auto reload =
        mv::ReadGltfAssetCapped(output.Json(), reload_resources, read_limits,
                                {1, kPartitionBytes, 1, 65536}, context);
    Require(reload.asset.has_value(),
            "actual large output independently reloaded");
    const auto actual = mv::ExtractGltfPrimitiveCapped(
        *reload.asset, *reload.asset->Primitive(0),
        {mv::GltfTriangleRequest::kRequireTriangles,
         mv::GltfValueRequest::kRawAndNormalized},
        extraction_limits);
    Require(actual.extraction.has_value(),
            "actual complete large reload extraction");
    auto limits = Limits();
    limits.max_vertices = kLargeVertexCount;
    limits.max_attribute_scalars = kLargeScalarLimit;
    limits.max_new_requested_bytes = budget;
    const auto result = mv::InspectGltfTopologyOutput(
        {source, candidate, kLargeSelectedFace, kLargeSelectedCorners, output,
         *reload.asset, *actual.extraction},
        limits, Work());
    CheckComplete(result, false);
    Require(
        std::ranges::equal(
            result.inspection->ReloadedExtraction().TriangleIndices(),
            kLargeSelectedIndices) &&
            result.inspection->OriginalCorners()[0].accessor_row == 16777217 &&
            result.inspection->Candidate().Source().View().positions.size() ==
                kLargeVertexCount * 3,
        "owned complete physical positions and exact original UINT32 "
        "correspondence");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "large inspection and all retained input operations release");
}
}  // namespace
}  // namespace gltf_workflow_inspection

int main(int argc, char** argv) {
  try {
    using gltf_workflow_inspection::Require;
    if (argc != 1) {
      Require(
          argc == 4 && std::string_view(argv[1]) == "--large-source-index" &&
              std::string_view(argv[2]) == "--resource-budget-mib",
          "expected --large-source-index --resource-budget-mib 2048-or-more");
      const std::string_view text(argv[3]);
      std::uint64_t budget_mib = 0;
      const auto parsed =
          std::from_chars(text.data(), text.data() + text.size(), budget_mib);
      Require(
          parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
          "whole unsigned large-case budget");
      gltf_workflow_inspection::LargeSourceIndex(budget_mib);
      std::cout << "large physical UINT32 inspection fixture passed\n";
      return 0;
    }
    gltf_workflow_inspection::AcceptedSelectionsAndLifetimes();
    gltf_workflow_inspection::AllEncodingsAndNormalization();
    gltf_workflow_inspection::RejectionsAndFirstFailure();
    gltf_workflow_inspection::CorruptActualOutputAndReload();
    gltf_workflow_inspection::LateStopPreservesEarliestFailure();
    gltf_workflow_inspection::IndependentInclusiveLimits();
    gltf_workflow_inspection::CheckRequests();
    std::cout << "independent glTF workflow inspection fixtures passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
