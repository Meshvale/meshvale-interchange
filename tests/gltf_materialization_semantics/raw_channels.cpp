// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

#include "fixture_support.h"

namespace gltf_materialization_semantics {
namespace {
constexpr std::string_view kSignedByte = "_SIGNED_BYTE";
constexpr std::string_view kSignedShort = "_SIGNED_SHORT";
constexpr std::string_view kFirstUv = "TEXCOORD_0";
constexpr std::string_view kSecondUv = "TEXCOORD_1";
constexpr std::string_view kByteColor = "COLOR_0";
constexpr std::string_view kFloatColor = "COLOR_1";
constexpr std::string_view kNormal = "NORMAL";
constexpr std::string_view kTangent = "TANGENT";
constexpr std::string_view kFloatCustom = "_FLOAT4";
constexpr std::string_view kImplicitCustom = "_IMPLICIT";
constexpr std::size_t kExpectedChannels = 11;
constexpr std::size_t kPositionWidth = 3;
constexpr std::size_t kInterleavedByteStride = 4;
constexpr std::size_t kSparseRowStep = 2;
constexpr std::array<std::uint32_t, kExpectedChannels> kChannelWidths{
    3, 1, 2, 2, 2, 4, 3, 3, 4, 4, 3};

template <class T, std::size_t Count>
void CheckAttribute(const geo::MeshReadView& mesh, std::string_view name,
                    const std::array<T, Count>& expected) {
  const auto& attribute = Attribute(mesh, name);
  RequireBits<T>(std::get<std::span<const T>>(attribute.values), expected,
                 "Canonical scalar encoding or bytes changed");
}
template <class T, class Bits, std::size_t Count>
void CheckFloatingBits(std::span<const T> actual,
                       const std::array<Bits, Count>& expected) {
  Require(actual.size() == expected.size(), "Floating extent changed");
  for (std::size_t i = 0; i < expected.size(); ++i)
    Require(std::bit_cast<Bits>(actual[i]) == expected[i],
            "Independent IEEE bit expectation differs");
}
}  // namespace

void CheckRawChannels() {
  const auto fixture = RawChannelsFixture();
  const auto result = Transfer(fixture);
  const auto view = result.Mesh().View();
  const auto source = result.SourceExtraction().Channels();
  Require(source.size() == kExpectedChannels &&
              view.attributes.size() + 1 == kExpectedChannels,
          "Every admitted source channel must have one destination");
  const auto mappings = result.ChannelMappings();
  Require(mappings.size() == source.size(), "Channel correspondence missing");
  Require(mappings[0].destination ==
                  mv::GltfMaterializationDestination::kPositions &&
              !mappings[0].attribute &&
              mappings[0].encoding ==
                  mv::GltfMaterializationEncoding::kFloatPositionWidened,
          "POSITION correspondence must identify widening without attribute");
  for (std::size_t i = 0; i < source.size(); ++i) {
    Require(mappings[i].source_channel == i, "Source order was reordered");
    if (i == 0) continue;
    const auto& attribute = view.attributes[i - 1];
    Require(mappings[i].destination ==
                    mv::GltfMaterializationDestination::kVertexAttribute &&
                mappings[i].attribute == i - 1,
            "Destination attribute ordinal differs from authored order");
    Require(attribute.domain == geo::AttributeDomain::vertex &&
                attribute.name == source[i].semantic &&
                attribute.semantic == source[i].semantic &&
                attribute.set_index == source[i].set_index &&
                attribute.components == kChannelWidths[i] &&
                !attribute.offsets && !attribute.present &&
                attribute.metadata.empty(),
            "Dense descriptor or opaque metadata policy changed");
    const bool signed_ingress =
        source[i].semantic == kSignedByte || source[i].semantic == kSignedShort;
    Require(mappings[i].encoding ==
                (signed_ingress
                     ? mv::GltfMaterializationEncoding::kSignedIntegerWidened
                     : mv::GltfMaterializationEncoding::kExact),
            "Raw source encoding correspondence changed");
  }
  // Independent binary64 image of raw binary32 zero, minimum subnormal and one.
  constexpr std::array<std::uint64_t, 12> kPositionBits{
      UINT64_C(0x8000000000000000),
      0,
      0,
      UINT64_C(0x36a0000000000000),
      0,
      0,
      UINT64_C(0x3ff0000000000000),
      0,
      0,
      0,
      UINT64_C(0x3ff0000000000000),
      0};
  CheckFloatingBits(view.positions, kPositionBits);
  constexpr std::array<std::uint32_t, 12> kRawPositionBits{
      0x80000000U, 0, 0, 1, 0, 0, 0x3f800000U, 0, 0, 0, 0x3f800000U, 0};
  CheckFloatingBits(std::get<std::span<const float>>(source[0].raw),
                    kRawPositionBits);
  constexpr std::array<float, 3> kPositionMinimum{0, 0, 0};
  constexpr std::array<float, 3> kPositionMaximum{1, 1, 0};
  Require(source[0].exact_min && source[0].exact_max,
          "Original POSITION bounds lost");
  RequireBits<float>(std::get<std::span<const float>>(*source[0].exact_min),
                     kPositionMinimum, "POSITION minimum changed");
  RequireBits<float>(std::get<std::span<const float>>(*source[0].exact_max),
                     kPositionMaximum, "POSITION maximum changed");
  Require(view.positions.size() == source[0].element_count * kPositionWidth,
          "Canonical XYZ row count differs from source");
  constexpr std::array<std::int32_t, 4> kSignedByteValues{-128, -1, 0, 127};
  constexpr std::array<std::int32_t, 8> kSignedShortValues{
      -32768, 32767, -1, 0, 0, -32768, 32767, 1};
  constexpr std::array<std::uint8_t, 8> kSparseUvValues{0, 0, 255, 0,
                                                        0, 0, 1,   254};
  constexpr std::array<std::uint16_t, 8> kSecondUvValues{
      0, 65535, 32768, 1, 65535, 0, 1, 32768};
  constexpr std::array<std::uint8_t, 16> kByteColorValues{
      0, 255, 1, 254, 255, 0, 128, 127, 1, 2, 3, 4, 255, 255, 255, 255};
  constexpr std::array<float, 12> kFloatColorValues{
      0, 1, 0.5F, 1, 0, 0.25F, 0.5F, 0.25F, 1, 0, 0, 0};
  constexpr std::array<float, 12> kNormalValues{0, 0, 1, 0, 0, 1,
                                                0, 0, 1, 0, 0, 1};
  constexpr std::array<float, 16> kTangentValues{1, 0, 0, 1, 1, 0, 0, -1,
                                                 1, 0, 0, 1, 1, 0, 0, -1};
  constexpr std::array<std::uint16_t, 12> kImplicitValues{};
  CheckAttribute(view, kSignedByte, kSignedByteValues);
  CheckAttribute(view, kSignedShort, kSignedShortValues);
  CheckAttribute(view, kFirstUv, kSparseUvValues);
  CheckAttribute(view, kSecondUv, kSecondUvValues);
  CheckAttribute(view, kByteColor, kByteColorValues);
  CheckAttribute(view, kFloatColor, kFloatColorValues);
  CheckAttribute(view, kNormal, kNormalValues);
  CheckAttribute(view, kTangent, kTangentValues);
  CheckAttribute(view, kImplicitCustom, kImplicitValues);
  constexpr std::array<std::uint32_t, 16> kCustomFloatBits{
      0x80000000U, 1,           0xbf800000U, 0x3f800000U,
      0,           0,           0x40000000U, 0xc0000000U,
      0x3f800000U, 0x40000000U, 0x40400000U, 0x40800000U,
      0,           0,           0,           0};
  CheckFloatingBits(
      std::get<std::span<const float>>(Attribute(view, kFloatCustom).values),
      kCustomFloatBits);
  // Uint32 is an admitted index encoding, not a custom attribute encoding.
  constexpr std::array<std::uint32_t, 6> kOriginalIndices{3, 1, 2, 0, 2, 2};
  const auto original_indices = result.SourceExtraction().SourceIndices();
  Require(original_indices.has_value(), "Original unsigned index channel lost");
  RequireBits<std::uint32_t>(
      std::get<std::span<const std::uint32_t>>(original_indices->raw),
      kOriginalIndices, "Original uint32 index encoding was changed");
  const auto& signed_source = Channel(result.SourceExtraction(), kSignedByte);
  constexpr std::array<std::int8_t, 4> kRawSigned{-128, -1, 0, 127};
  RequireBits<std::int8_t>(
      std::get<std::span<const std::int8_t>>(signed_source.raw), kRawSigned,
      "Original signed encoding lost");
  Require(
      signed_source.normalized &&
          signed_source.base_byte_stride == kInterleavedByteStride &&
          std::get<std::span<const std::int8_t>>(*signed_source.exact_min)[0] ==
              -128 &&
          std::get<std::span<const std::int8_t>>(*signed_source.exact_max)[0] ==
              127,
      "Interleaved layout, normalized declaration or exact bound lost");
  // Rational-to-binary64 expectations are checked independently in Python.
  constexpr std::array<std::uint64_t, 4> kNormalizedSignedBits{
      UINT64_C(0xbff0000000000000), UINT64_C(0xbf80204081020408), 0,
      UINT64_C(0x3ff0000000000000)};
  CheckFloatingBits(signed_source.normalized_values, kNormalizedSignedBits);
  const auto& short_source = Channel(result.SourceExtraction(), kSignedShort);
  constexpr std::array<std::int16_t, 8> kRawShort{-32768, 32767,  -1,    0,
                                                  0,      -32768, 32767, 1};
  constexpr std::array<std::int16_t, 2> kShortMinimum{-32768, -32768};
  constexpr std::array<std::int16_t, 2> kShortMaximum{32767, 32767};
  Require(short_source.exact_min && short_source.exact_max &&
              short_source.normalized_values.size() == kRawShort.size(),
          "Original signed-short bounds or available normalized view lost");
  RequireBits<std::int16_t>(
      std::get<std::span<const std::int16_t>>(short_source.raw), kRawShort,
      "Original int16 source bytes changed");
  RequireBits<std::int16_t>(
      std::get<std::span<const std::int16_t>>(*short_source.exact_min),
      kShortMinimum, "Signed-short minimum changed");
  RequireBits<std::int16_t>(
      std::get<std::span<const std::int16_t>>(*short_source.exact_max),
      kShortMaximum, "Signed-short maximum changed");
  constexpr std::array<std::uint64_t, 8> kNormalizedShortBits{
      UINT64_C(0xbff0000000000000),
      UINT64_C(0x3ff0000000000000),
      UINT64_C(0xbf00002000400080),
      0,
      0,
      UINT64_C(0xbff0000000000000),
      UINT64_C(0x3ff0000000000000),
      UINT64_C(0x3f00002000400080)};
  CheckFloatingBits(short_source.normalized_values, kNormalizedShortBits);
  const auto& uv_source = Channel(result.SourceExtraction(), kFirstUv);
  constexpr std::array<std::uint64_t, 2> kSparseDestinations{1, 3};
  RequireBits<std::uint64_t>(uv_source.sparse_destinations, kSparseDestinations,
                             "Sparse selectors lost");
  Require(uv_source.sparse_source && !uv_source.base_buffer_view &&
              uv_source.packed_sparse_step == kSparseRowStep &&
              uv_source.set_index == 0 &&
              Channel(result.SourceExtraction(), kSecondUv).set_index == 1,
          "Multi-UV sparse/base/set provenance lost");
  constexpr std::array<std::uint64_t, 8> kNormalizedUvBits{
      0, 0, UINT64_C(0x3ff0000000000000), 0,
      0, 0, UINT64_C(0x3f70101010101010), UINT64_C(0x3fefdfdfdfdfdfe0)};
  CheckFloatingBits(uv_source.normalized_values, kNormalizedUvBits);
  constexpr std::array<std::uint8_t, 2> kUvMinimum{0, 0};
  constexpr std::array<std::uint8_t, 2> kUvMaximum{255, 254};
  Require(uv_source.exact_min && uv_source.exact_max,
          "Original sparse UV bounds lost");
  RequireBits<std::uint8_t>(
      std::get<std::span<const std::uint8_t>>(*uv_source.exact_min), kUvMinimum,
      "Sparse UV minimum changed");
  RequireBits<std::uint8_t>(
      std::get<std::span<const std::uint8_t>>(*uv_source.exact_max), kUvMaximum,
      "Sparse UV maximum changed");
  constexpr std::array<std::uint64_t, 8> kNormalizedSecondUvBits{
      0,
      UINT64_C(0x3ff0000000000000),
      UINT64_C(0x3fe0001000100010),
      UINT64_C(0x3ef0001000100010),
      UINT64_C(0x3ff0000000000000),
      0,
      UINT64_C(0x3ef0001000100010),
      UINT64_C(0x3fe0001000100010)};
  CheckFloatingBits(
      Channel(result.SourceExtraction(), kSecondUv).normalized_values,
      kNormalizedSecondUvBits);
  constexpr std::array<std::uint64_t, 16> kNormalizedByteColorBits{
      0,
      UINT64_C(0x3ff0000000000000),
      UINT64_C(0x3f70101010101010),
      UINT64_C(0x3fefdfdfdfdfdfe0),
      UINT64_C(0x3ff0000000000000),
      0,
      UINT64_C(0x3fe0101010101010),
      UINT64_C(0x3fdfdfdfdfdfdfe0),
      UINT64_C(0x3f70101010101010),
      UINT64_C(0x3f80101010101010),
      UINT64_C(0x3f88181818181818),
      UINT64_C(0x3f90101010101010),
      UINT64_C(0x3ff0000000000000),
      UINT64_C(0x3ff0000000000000),
      UINT64_C(0x3ff0000000000000),
      UINT64_C(0x3ff0000000000000)};
  CheckFloatingBits(
      Channel(result.SourceExtraction(), kByteColor).normalized_values,
      kNormalizedByteColorBits);
  const auto raw_only = Transfer(fixture, mv::GltfValueRequest::kRaw);
  for (const auto& channel : raw_only.SourceExtraction().Channels())
    Require(
        channel.normalized_values.empty(),
        "Transfer must not invent normalized views for raw-only extraction");
  CheckAttribute(raw_only.Mesh().View(), kSignedByte, kSignedByteValues);
  CheckAttribute(raw_only.Mesh().View(), kFirstUv, kSparseUvValues);
}
}  // namespace gltf_materialization_semantics
