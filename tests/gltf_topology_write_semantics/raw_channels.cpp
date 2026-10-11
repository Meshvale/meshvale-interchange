// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

#include "fixture_support.h"

namespace gltf_topology_write_semantics {
namespace {
constexpr std::string_view kPosition = "POSITION";
constexpr std::string_view kSignedChannel = "_SIGNED";
constexpr std::string_view kUvChannel = "TEXCOORD_0";
constexpr std::size_t kRawChannelCount = 3;
constexpr std::uint64_t kPositionStrideBytes = 16;
constexpr std::uint64_t kSparseRowStep = 2;
constexpr std::array<std::uint32_t, 12> kPositionBits{
    0x80000000U, 0, 0, 1, 0, 0, 0x3f800000U, 0, 0, 0, 0x3f800000U, 0};
constexpr std::array<std::int16_t, 8> kSignedValues{-32768, 32767,  -1,    0,
                                                    0,      -32768, 32767, 1};
constexpr std::array<std::uint8_t, 8> kUvValues{0, 0, 255, 0, 0, 0, 1, 254};
constexpr std::array<std::uint64_t, 2> kSparseRows{1, 3};

const mv::GltfChannelView& FindChannel(
    const mv::GltfPrimitiveExtraction& extraction, std::string_view name) {
  const auto channels = extraction.Channels();
  const auto found = std::find_if(
      channels.begin(), channels.end(),
      [name](const auto& channel) { return channel.semantic == name; });
  Require(found != channels.end(), "Expected retained channel is absent");
  return *found;
}
}  // namespace

void CheckRawChannels(const mv::GltfPrimitiveExtraction& extraction) {
  Require(extraction.Channels().size() == kRawChannelCount,
          "Reader reload changed channel declarations");
  const auto& positions = FindChannel(extraction, kPosition);
  Require(positions.accessor_index == 0 &&
              positions.component_type == mv::GltfComponentType::kFloat32 &&
              positions.shape == mv::GltfAccessorShape::kVec3 &&
              positions.element_count == 4 && !positions.normalized &&
              positions.base_buffer_view == 0 &&
              positions.base_byte_offset == 0 &&
              positions.base_byte_stride == kPositionStrideBytes &&
              !positions.sparse_source && positions.exact_min &&
              positions.exact_max,
          "Original interleaved position accessor descriptor changed");
  const auto raw_positions = std::get<std::span<const float>>(positions.raw);
  Require(raw_positions.size() == kPositionBits.size(),
          "Complete positions, including unused rows, must remain");
  for (std::size_t i = 0; i < kPositionBits.size(); ++i)
    Require(std::bit_cast<std::uint32_t>(raw_positions[i]) == kPositionBits[i],
            "Original IEEE position bits changed");
  constexpr std::array<float, 3> kPositionMinimum{0, 0, 0};
  constexpr std::array<float, 3> kPositionMaximum{1, 1, 0};
  RequireBits<float>(std::get<std::span<const float>>(*positions.exact_min),
                     kPositionMinimum, "Original position minimum changed");
  RequireBits<float>(std::get<std::span<const float>>(*positions.exact_max),
                     kPositionMaximum, "Original position maximum changed");

  const auto& signed_channel = FindChannel(extraction, kSignedChannel);
  Require(signed_channel.accessor_index == 1 && signed_channel.normalized &&
              signed_channel.component_type == mv::GltfComponentType::kInt16 &&
              signed_channel.shape == mv::GltfAccessorShape::kVec2 &&
              signed_channel.base_buffer_view == 1 &&
              !signed_channel.sparse_source && signed_channel.exact_min &&
              signed_channel.exact_max,
          "Signed normalized source encoding was narrowed or rewritten");
  RequireBits<std::int16_t>(
      std::get<std::span<const std::int16_t>>(signed_channel.raw),
      kSignedValues,
      "Original signed extrema and raw normalized bytes changed");
  constexpr std::array<std::int16_t, 2> kSignedMinimum{-32768, -32768};
  constexpr std::array<std::int16_t, 2> kSignedMaximum{32767, 32767};
  RequireBits<std::int16_t>(
      std::get<std::span<const std::int16_t>>(*signed_channel.exact_min),
      kSignedMinimum, "Original signed accessor minimum changed");
  RequireBits<std::int16_t>(
      std::get<std::span<const std::int16_t>>(*signed_channel.exact_max),
      kSignedMaximum, "Original signed accessor maximum changed");
  constexpr std::array<double, 8> kSignedNormalized{-1, 1, -1.0 / 32767, 0, 0,
                                                    -1, 1, 1.0 / 32767};
  RequireBits<double>(signed_channel.normalized_values, kSignedNormalized,
                      "Independent signed normalization expectation differs");

  const auto& uv = FindChannel(extraction, kUvChannel);
  Require(uv.accessor_index == 2 && uv.normalized && uv.set_index == 0 &&
              uv.component_type == mv::GltfComponentType::kUint8 &&
              uv.shape == mv::GltfAccessorShape::kVec2 &&
              !uv.base_buffer_view && uv.sparse_source &&
              uv.packed_sparse_step == kSparseRowStep &&
              uv.sparse_source->indices_buffer_view == 2 &&
              uv.sparse_source->values_buffer_view == 3 &&
              uv.sparse_source->indices_byte_offset == 0 &&
              uv.sparse_source->values_byte_offset == 0 &&
              uv.sparse_source->indices_component_type ==
                  mv::GltfComponentType::kUint8,
          "Original sparse selector/value provenance changed");
  RequireBits<std::uint64_t>(uv.sparse_destinations, kSparseRows,
                             "Sparse destination row identities changed");
  RequireBits<std::uint8_t>(std::get<std::span<const std::uint8_t>>(uv.raw),
                            kUvValues,
                            "Sparse reconstructed raw bytes changed");
  constexpr std::array<double, 8> kUvNormalized{0, 0, 1,         0,
                                                0, 0, 1.0 / 255, 254.0 / 255};
  RequireBits<double>(uv.normalized_values, kUvNormalized,
                      "Independent unsigned normalization expectation differs");
}

void CheckSourceExtraction(const Fixture& fixture,
                           const mv::GltfPrimitiveExtraction& extraction) {
  Require(extraction.SourceMode() == fixture.source_mode,
          "Independent input source mode differs");
  RequireBits<std::uint32_t>(extraction.TriangleIndices(),
                             fixture.source_triangles,
                             "Directed triangle expectation differs");
  Require(extraction.SourceCorners().size() == fixture.stream_corners.size(),
          "Source-corner provenance extent changed");
  for (std::size_t i = 0; i < fixture.stream_corners.size(); ++i)
    Require(extraction.SourceCorners()[i].primitive_stream_corner ==
                    fixture.stream_corners[i] &&
                extraction.SourceCorners()[i].accessor_row ==
                    fixture.source_triangles[i],
            "Independent source-stream/accessor-row provenance differs");
  const bool raw_channels = fixture.kind == FixtureKind::kTriangles ||
                            fixture.kind == FixtureKind::kStrip ||
                            fixture.kind == FixtureKind::kFan;
  if (raw_channels) {
    CheckRawChannels(extraction);
    const auto indices = extraction.SourceIndices();
    Require(indices && indices->accessor_index == 3 &&
                indices->component_type == mv::GltfComponentType::kUint16 &&
                extraction.MaterialIndex() == 0,
            "Original shared index encoding or material binding changed");
    constexpr std::array<std::uint16_t, 6> kTriangleStream{3, 1, 2, 0, 2, 2};
    constexpr std::array<std::uint16_t, 4> kConvertedStream{3, 1, 2, 0};
    const auto raw = std::get<std::span<const std::uint16_t>>(indices->raw);
    if (fixture.kind == FixtureKind::kTriangles)
      RequireBits<std::uint16_t>(raw, kTriangleStream,
                                 "Original triangle index stream changed");
    else
      RequireBits<std::uint16_t>(raw, kConvertedStream,
                                 "Original strip/fan index stream changed");
  }
}
}  // namespace gltf_topology_write_semantics
