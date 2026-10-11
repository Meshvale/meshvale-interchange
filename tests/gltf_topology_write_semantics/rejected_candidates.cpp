// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string_view>
#include <variant>
#include <vector>

#include "fixture_support.h"

namespace gltf_topology_write_semantics {
namespace {
constexpr std::array<geo::index_t, 1> kSelectedFace{1};
constexpr std::array<geo::index_t, 3> kSelectedCorners{3, 4, 5};
constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
constexpr std::string_view kRenamedChannel = "_RENAMED";
constexpr std::string_view kExtraChannel = "_EXTRA";
constexpr std::array<geo::MetadataEntryView, 1> kExtraMetadata{
    geo::MetadataEntryView{"authored", "altered"}};
constexpr std::array<geo::index_t, 5> kDenseRaggedOffsets{0, 2, 4, 6, 8};
constexpr std::array<std::uint8_t, 4> kAuthoredMask{1, 1, 1, 1};

enum class Change {
  kNone,
  kSignedZero,
  kUnusedPosition,
  kSignedValue,
  kName,
  kSetIndex,
  kDomain,
  kWidth,
  kMetadata,
  kRagged,
  kPresence,
  kExtraChannel,
  kMissingChannel,
  kReversedLoop,
  kReorderedFaces,
  kRepeatedFaces,
  kInvalidCorner,
  kQuad,
  kEmpty
};

geo::MaterializedMesh MakeCandidate(const mv::GltfMaterializedPrimitive& source,
                                    const geo::ExecutionContext& context,
                                    Change change) {
  const auto view = source.Mesh().View();
  std::vector<double> positions(view.positions.begin(), view.positions.end());
  std::vector<geo::index_t> offsets{0, 3};
  std::vector<geo::index_t> corners(view.corner_vertices.begin() + 3,
                                    view.corner_vertices.end());
  std::vector<geo::AttributeMaterializationInput> attributes;
  for (const auto& attribute : view.attributes) {
    attributes.push_back(
        {attribute.domain, attribute.name, attribute.semantic,
         attribute.set_index, attribute.components,
         std::visit([](auto values) -> geo::ScalarIngress { return values; },
                    attribute.values),
         attribute.offsets, attribute.present, attribute.metadata});
  }
  const auto original_signed =
      std::get<std::span<const std::int32_t>>(view.attributes[0].values);
  std::vector<std::int32_t> signed_values(original_signed.begin(),
                                          original_signed.end());
  switch (change) {
    case Change::kNone:
      break;
    case Change::kSignedZero:
      positions[0] = 0.0;
      break;
    case Change::kUnusedPosition:
      positions[9] = 2;
      break;
    case Change::kSignedValue:
      ++signed_values[0];
      attributes[0].values = std::span<const std::int32_t>(signed_values);
      break;
    case Change::kName:
      attributes[0].name = kRenamedChannel;
      break;
    case Change::kSetIndex:
      attributes[0].set_index = 1;
      break;
    case Change::kDomain:
      attributes[0].domain = geo::AttributeDomain::face;
      signed_values.resize(2);
      attributes[0].values = std::span<const std::int32_t>(signed_values);
      break;
    case Change::kWidth:
      attributes[0].components = 1;
      signed_values.resize(4);
      attributes[0].values = std::span<const std::int32_t>(signed_values);
      break;
    case Change::kMetadata:
      attributes[0].metadata = kExtraMetadata;
      break;
    case Change::kRagged:
      attributes[0].offsets = kDenseRaggedOffsets;
      break;
    case Change::kPresence:
      attributes[0].present = kAuthoredMask;
      break;
    case Change::kExtraChannel:
      attributes.push_back(attributes[1]);
      attributes.back().name = kExtraChannel;
      break;
    case Change::kMissingChannel:
      attributes.pop_back();
      break;
    case Change::kReversedLoop:
      corners = {0, 1, 2};
      break;
    case Change::kReorderedFaces:
      offsets = {0, 3, 6};
      corners = {2, 1, 0, 3, 1, 2};
      break;
    case Change::kRepeatedFaces:
      offsets = {0, 3, 6};
      corners = {3, 1, 2, 3, 1, 2};
      break;
    case Change::kInvalidCorner:
      corners[0] = 999;
      break;
    case Change::kQuad:
      offsets = {0, 4};
      corners = {2, 1, 0, 2};
      break;
    case Change::kEmpty:
      offsets = {0};
      corners.clear();
      break;
  }
  const auto result = geo::MaterializeMesh(
      {std::span<const double>(positions),
       geo::PolygonTopologyInput{offsets, corners}, attributes},
      TransferLimits().materialization, GeometryWork(), context, {});
  Require(result.mesh && result.status == geo::MaterializationStatus::kSuccess,
          "Independent altered candidate capture must succeed");
  return *result.mesh;
}
geo::MeshInspection InspectRaw(const geo::MaterializedMesh& mesh) {
  const auto result = geo::InspectMaterializedMesh(
      mesh, TransferLimits().inspection, GeometryWork(), {});
  Require(result.inspection &&
              result.status == geo::MaterializationStatus::kSuccess,
          "Independent altered candidate inspection must succeed");
  return *result.inspection;
}
void Reject(const mv::GltfMaterializedPrimitive& source,
            const geo::MeshInspection& inspection,
            std::span<const geo::index_t> faces,
            std::span<const geo::index_t> corners,
            mv::GltfTopologyWriteStatus status,
            mv::GltfTopologyWriteDiagnosticCode code,
            std::stop_token stop = {}) {
  const auto ledger = source.Mesh().ShareExecutionContext();
  const auto baseline = ledger.ActiveTrackedPayload();
  const auto result = mv::WriteGltfTopologySelection(
      {source, inspection, faces, corners}, WriteLimits(), WriteWork(), stop);
  Require(!result.bundle && result.status == status &&
              result.diagnostic.code == code,
          "Rejected candidate/map must expose its exact typed outcome and no "
          "bundle");
  Require(
      ledger.ActiveTrackedPayload() == baseline && ledger.ActiveWorkers() == 0,
      "Rejected output leaked writer charges or workers");
  const std::array phases{
      result.execution.verification, result.execution.index_encoding,
      result.execution.base64_encoding, result.execution.output_copy};
  for (const auto& phase : phases)
    Require(phase.launched_workers == phase.joined_workers,
            "Rejected output returned before real worker joins");
}
}  // namespace

void CheckRejectedCandidates() {
  const auto fixture = MakeFixture(FixtureKind::kStrip);
  const auto context = Context();
  const auto source = Transfer(fixture, context);
  const auto matching = Inspect(MakeCandidate(source, context, Change::kNone));
  using Status = mv::GltfTopologyWriteStatus;
  using Code = mv::GltfTopologyWriteDiagnosticCode;
  Reject(source, matching, {}, kSelectedCorners, Status::kInvalidCorrespondence,
         Code::kFaceMapShape);
  constexpr std::array<geo::index_t, 1> kOutOfRangeFace{2};
  Reject(source, matching, kOutOfRangeFace, kSelectedCorners,
         Status::kInvalidCorrespondence, Code::kFaceMapRange);
  Reject(source, matching, kSelectedFace, {}, Status::kInvalidCorrespondence,
         Code::kCornerMapShape);
  constexpr std::array<geo::index_t, 3> kSwappedCorners{3, 5, 4};
  Reject(source, matching, kSelectedFace, kSwappedCorners,
         Status::kInvalidCorrespondence, Code::kCornerMapValue);
  const auto quad = Inspect(MakeCandidate(source, context, Change::kQuad));
  Reject(source, quad, kSelectedFace, kSelectedCorners,
         Status::kInvalidCorrespondence, Code::kCornerMapShape);
  constexpr std::array<geo::index_t, 4> kQuadCorners{3, 4, 5, 3};
  Reject(source, quad, kSelectedFace, kQuadCorners,
         Status::kPreservationMismatch, Code::kTriangleShape);
  const auto reordered =
      Inspect(MakeCandidate(source, context, Change::kReorderedFaces));
  constexpr std::array<geo::index_t, 2> kReversedFaces{1, 0};
  constexpr std::array<geo::index_t, 6> kReversedCorners{3, 4, 5, 0, 1, 2};
  Reject(source, reordered, kReversedFaces, kReversedCorners,
         Status::kInvalidCorrespondence, Code::kFaceMapOrder);
  const auto repeated =
      Inspect(MakeCandidate(source, context, Change::kRepeatedFaces));
  constexpr std::array<geo::index_t, 2> kRepeatedFaces{0, 0};
  constexpr std::array<geo::index_t, 6> kRepeatedCorners{0, 1, 2, 0, 1, 2};
  Reject(source, repeated, kRepeatedFaces, kRepeatedCorners,
         Status::kInvalidCorrespondence, Code::kFaceMapOrder);

  struct Alteration {
    Change change;
    Code code;
  };
  constexpr std::array alterations{
      Alteration{Change::kSignedZero, Code::kPositionBits},
      Alteration{Change::kUnusedPosition, Code::kPositionBits},
      Alteration{Change::kSignedValue, Code::kAttributeBits},
      Alteration{Change::kName, Code::kAttributeDescriptor},
      Alteration{Change::kSetIndex, Code::kAttributeDescriptor},
      Alteration{Change::kDomain, Code::kAttributeDescriptor},
      Alteration{Change::kWidth, Code::kAttributeDescriptor},
      Alteration{Change::kMetadata, Code::kAttributeDescriptor},
      Alteration{Change::kRagged, Code::kAttributeDescriptor},
      Alteration{Change::kPresence, Code::kAttributeDescriptor},
      Alteration{Change::kExtraChannel, Code::kChannelCount},
      Alteration{Change::kMissingChannel, Code::kChannelCount},
      Alteration{Change::kReversedLoop, Code::kTriangleLoop}};
  for (const auto& alteration : alterations) {
    const auto changed =
        Inspect(MakeCandidate(source, context, alteration.change));
    Reject(source, changed, kSelectedFace, kSelectedCorners,
           Status::kPreservationMismatch, alteration.code);
  }
  const auto foreign_context = Context();
  const auto foreign =
      Inspect(MakeCandidate(source, foreign_context, Change::kNone));
  Reject(source, foreign, kSelectedFace, kSelectedCorners,
         Status::kForeignBudget, Code::kForeignBudget);
  const auto invalid =
      InspectRaw(MakeCandidate(source, context, Change::kInvalidCorner));
  Require(!invalid.IsStorageValid() && !invalid.Issues().empty(),
          "Raw invalid corner must create an actual typed inspection issue");
  const auto rejected = mv::WriteGltfTopologySelection(
      {source, invalid, kSelectedFace, kSelectedCorners}, WriteLimits(),
      WriteWork(), {});
  Require(
      !rejected.bundle && rejected.status == Status::kInvalidCandidateStorage &&
          rejected.diagnostic.code == Code::kCandidateStorage &&
          rejected.diagnostic.storage_issue &&
          rejected.diagnostic.storage_issue->code == invalid.Issues()[0].code &&
          rejected.diagnostic.storage_issue->attribute ==
              invalid.Issues()[0].attribute &&
          rejected.diagnostic.storage_issue->element ==
              invalid.Issues()[0].element,
      "Writer must retain the supplied inspection's actual first issue");
  const auto empty = Inspect(MakeCandidate(source, context, Change::kEmpty));
  Reject(source, empty, {}, {}, Status::kUnsupportedSelection,
         Code::kEmptyChangedSelection);
  std::stop_source stop;
  stop.request_stop();
  Reject(source, matching, kSelectedFace, kSelectedCorners, Status::kCanceled,
         Code::kStopRequested, stop.get_token());
  // Identity is not permission to bypass correspondence verification.
  Reject(source, source.Inspection(), {}, kIdentityCorners,
         Status::kInvalidCorrespondence, Code::kFaceMapShape);
  constexpr std::array<geo::index_t, 6> kBadIdentityCorners{0, 1, 2, 3, 5, 4};
  Reject(source, source.Inspection(), kIdentityFaces, kBadIdentityCorners,
         Status::kInvalidCorrespondence, Code::kCornerMapValue);
}
}  // namespace gltf_topology_write_semantics
