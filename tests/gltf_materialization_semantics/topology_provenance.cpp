// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include "fixture_support.h"

namespace gltf_materialization_semantics {
namespace {
constexpr std::uint64_t kTriangleMode = 4;
constexpr std::uint64_t kStripMode = 5;
constexpr std::uint64_t kFanMode = 6;
constexpr geo::index_t kSmallVertices = 4;
constexpr geo::index_t kTriangleWidth = 3;
constexpr geo::index_t kInputFaces = 2;
constexpr geo::index_t kInputCorners = 6;
constexpr std::uint32_t kBeyondBinary32Index = 16777217;
constexpr geo::index_t kLargeVertexCount = 16777218;
constexpr std::array<geo::index_t, 3> kOffsets{0, 3, 6};
constexpr std::array<geo::index_t, 1> kKeptFace{1};
constexpr std::array<geo::index_t, 3> kKeptSourceCorners{3, 4, 5};
constexpr std::array<geo::index_t, 2> kSelectedOffsets{0, 3};

template <class T, std::size_t Count>
void CheckSourceIndices(const mv::GltfPrimitiveExtraction& source,
                        const std::array<T, Count>& expected) {
  const auto indices = source.SourceIndices();
  Require(indices.has_value(), "Original index accessor was lost");
  RequireBits<T>(std::get<std::span<const T>>(indices->raw), expected,
                 "Original index scalar encoding or stream changed");
}

void CheckTopology(TopologyFixture kind, std::uint64_t mode,
                   const std::array<std::uint32_t, 6>& triangles,
                   const std::array<std::uint64_t, 6>& stream_corners) {
  const auto result = Transfer(MakeTopologyFixture(kind));
  const auto& source = result.SourceExtraction();
  const auto view = result.Mesh().View();
  Require(source.SourceMode() == mode, "Source mode was flattened");
  RequireBits<std::uint32_t>(
      source.TriangleIndices(), triangles,
      "Triangle conversion changed winding or degenerates");
  RequireBits<geo::index_t>(view.face_offsets, kOffsets,
                            "Canonical triangle offsets differ");
  Require(view.positions.size() == kSmallVertices * kTriangleWidth &&
              view.corner_vertices.size() == triangles.size(),
          "Unused vertices or triangle corner count changed");
  Require(source.SourceCorners().size() == triangles.size(),
          "Triangle source-corner correspondence is incomplete");
  for (std::size_t i = 0; i < triangles.size(); ++i) {
    Require(view.corner_vertices[i] == triangles[i] &&
                source.SourceCorners()[i].accessor_row == triangles[i] &&
                source.SourceCorners()[i].primitive_stream_corner ==
                    stream_corners[i],
            "Canonical/source triangle correspondence changed");
  }
  Require(result.SourcePrimitive().MeshIndex() == 1 &&
              result.SourcePrimitive().PrimitiveIndex() == 1 &&
              source.MaterialIndex() == 0,
          "Selected primitive or material annotation identity changed");
  if (kind == TopologyFixture::kTriangles) {
    constexpr std::array<std::uint8_t, 6> kIndices{3, 1, 2, 0, 2, 2};
    CheckSourceIndices(source, kIndices);
  } else if (kind == TopologyFixture::kStrip) {
    constexpr std::array<std::uint16_t, 4> kIndices{3, 1, 2, 0};
    CheckSourceIndices(source, kIndices);
  } else {
    constexpr std::array<std::uint32_t, 4> kIndices{3, 1, 2, 0};
    CheckSourceIndices(source, kIndices);
  }

  const auto selected = geo::SelectMeshFaces(
      result.Mesh(), kKeptFace, TransferLimits().materialization, Work(), {});
  Require(
      selected.status == geo::MaterializationStatus::kSuccess &&
          selected.selection,
      "Public Geometry face selection must accept the valid transferred mesh");
  const auto candidate = selected.selection->Candidate().View();
  Require(candidate.positions.data() == view.positions.data() &&
              candidate.positions.size() == view.positions.size(),
          "Selection must share unchanged vertices, including unused rows");
  RequireBits<geo::index_t>(candidate.face_offsets, kSelectedOffsets,
                            "Selected triangle offsets differ");
  RequireBits<geo::index_t>(selected.selection->SourceFaces(), kKeptFace,
                            "Selected face map differs");
  RequireBits<geo::index_t>(selected.selection->SourceCorners(),
                            kKeptSourceCorners,
                            "Selected source corner map differs");
  Require(selected.selection->SourceVertexCount() == kSmallVertices &&
              selected.selection->SourceFaceCount() == kInputFaces &&
              selected.selection->SourceCornerCount() == kInputCorners,
          "Selection source extents were replaced by candidate extents");
  for (std::size_t i = 0; i < candidate.corner_vertices.size(); ++i)
    Require(candidate.corner_vertices[i] == triangles[kTriangleWidth + i],
            "Selected Geometry corners differ from source triangle");
}
}  // namespace

void CheckTopologyAndSelection() {
  constexpr std::array<std::uint32_t, 6> kTriangles{3, 1, 2, 0, 2, 2};
  constexpr std::array<std::uint64_t, 6> kTriangleStream{0, 1, 2, 3, 4, 5};
  constexpr std::array<std::uint32_t, 6> kStripTriangles{3, 1, 2, 2, 1, 0};
  constexpr std::array<std::uint64_t, 6> kStripStream{0, 1, 2, 2, 1, 3};
  constexpr std::array<std::uint32_t, 6> kFanTriangles{3, 1, 2, 3, 2, 0};
  constexpr std::array<std::uint64_t, 6> kFanStream{0, 1, 2, 0, 2, 3};
  CheckTopology(TopologyFixture::kTriangles, kTriangleMode, kTriangles,
                kTriangleStream);
  CheckTopology(TopologyFixture::kStrip, kStripMode, kStripTriangles,
                kStripStream);
  CheckTopology(TopologyFixture::kFan, kFanMode, kFanTriangles, kFanStream);

  const auto result = Transfer(RawChannelsFixture());
  const auto view = result.Mesh().View();
  const auto selected = geo::SelectMeshFaces(
      result.Mesh(), kKeptFace, TransferLimits().materialization, Work(), {});
  Require(selected.status == geo::MaterializationStatus::kSuccess &&
              selected.selection,
          "Multi-channel face selection failed");
  const auto candidate = selected.selection->Candidate().View();
  Require(candidate.attributes.size() == view.attributes.size(),
          "Face selection discarded vertex channels");
  for (std::size_t i = 0; i < view.attributes.size(); ++i) {
    const auto& before = view.attributes[i];
    const auto& after = candidate.attributes[i];
    Require(before.name == after.name && before.semantic == after.semantic &&
                before.set_index == after.set_index &&
                before.components == after.components &&
                before.values.index() == after.values.index(),
            "Selected vertex descriptor changed");
    std::visit(
        [&](auto values) {
          using Span = decltype(values);
          const auto kept = std::get<Span>(after.values);
          Require(values.data() == kept.data() && values.size() == kept.size(),
                  "Unchanged vertex channels must share canonical storage");
        },
        before.values);
  }
  const auto empty = geo::SelectMeshFaces(
      result.Mesh(), {}, TransferLimits().materialization, Work(), {});
  Require(
      empty.status == geo::MaterializationStatus::kSuccess && empty.selection,
      "Empty ordered face selection must be representable");
  const auto empty_view = empty.selection->Candidate().View();
  constexpr std::array<geo::index_t, 1> kEmptyOffsets{0};
  RequireBits<geo::index_t>(empty_view.face_offsets, kEmptyOffsets,
                            "Empty selection must retain its zero offset");
  Require(empty_view.corner_vertices.empty() &&
              empty.selection->SourceFaces().empty() &&
              empty.selection->SourceCorners().empty() &&
              empty_view.positions.data() == view.positions.data(),
          "Empty selection must retain vertices without fabricated maps");
}

void CheckLargeSourceIndex() {
  const auto result =
      Transfer(LargeSourceIndexFixture(), mv::GltfValueRequest::kRaw);
  constexpr std::array<std::uint32_t, 3> kSource{0, kBeyondBinary32Index, 1};
  CheckSourceIndices(result.SourceExtraction(), kSource);
  const auto view = result.Mesh().View();
  Require(view.positions.size() == kLargeVertexCount * kTriangleWidth,
          "Large index fixture must contain the actual physical vertex rows");
  for (std::size_t i = 0; i < kSource.size(); ++i)
    Require(view.corner_vertices[i] == kSource[i] &&
                result.SourceExtraction().SourceCorners()[i].accessor_row ==
                    kSource[i],
            "Source index above binary32 precision was rounded or remapped");
  Require(view.positions[(kLargeVertexCount - 1) * kTriangleWidth] == 0,
          "Last physically materialized implicit-zero vertex is missing");
}
}  // namespace gltf_materialization_semantics
