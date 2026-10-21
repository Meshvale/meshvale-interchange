// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_STL_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_STL_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace meshvale::interchange::stl_fixture {

// Independent wire values, deliberately not imported from the codec.
inline constexpr std::size_t kHeaderBytes = 80;
inline constexpr std::size_t kCountOffset = 80;
inline constexpr std::size_t kRecordOffset = 84;
inline constexpr std::size_t kRecordBytes = 50;
inline constexpr std::size_t kNormalComponents = 3;
inline constexpr std::size_t kFacetComponents = 12;
inline constexpr std::size_t kAttributeOffset = 48;
inline constexpr std::size_t kComponentsPerPosition = 3;
inline constexpr std::uint32_t kNegativeZeroBits = 0x80000000;
inline constexpr std::uint32_t kOneBits = 0x3f800000;
inline constexpr std::uint32_t kNegativeTwoBits = 0xc0000000;
inline constexpr std::string_view kName = "original part";
inline constexpr std::string_view kAscii =
    "solid original part\n"
    "facet normal +0 0 -2e0\n"
    " outer loop\n"
    "  vertex -0 0 0\n"
    "  vertex 1 0 0\n"
    "  vertex 0 1 0\n"
    " endloop\n"
    "endfacet\n"
    "facet normal 0 0 0\n"
    " outer loop\n"
    "  vertex -0 0 0\n"
    "  vertex 1 0 0\n"
    "  vertex 0 1 0\n"
    " endloop\n"
    "endfacet\n"
    "endsolid original part\n";
inline constexpr std::string_view kEmptyAscii = "solid \nendsolid \n";
inline constexpr std::array<std::uint32_t, kFacetComponents> kFirstFacet{
    0,        0, kNegativeTwoBits, kNegativeZeroBits, 0, 0, kOneBits, 0, 0, 0,
    kOneBits, 0};
inline constexpr std::array<std::uint32_t, kFacetComponents> kSecondFacet{
    0, 0, 0, kNegativeZeroBits, 0, 0, kOneBits, 0, 0, 0, kOneBits, 0};

inline void Require(
    bool condition, std::string_view message,
    std::source_location where = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}

inline std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}

inline std::array<std::byte, kHeaderBytes> Header() {
  std::array<std::byte, kHeaderBytes> header{};
  constexpr std::string_view kPrefix = "solid binary is not ASCII";
  for (std::size_t i = 0; i < kPrefix.size(); ++i)
    header[i] = std::byte(static_cast<unsigned char>(kPrefix[i]));
  header.back() = std::byte{0xff};
  return header;
}

inline void PutWord(std::span<std::byte> bytes, std::size_t offset,
                    std::uint32_t value) {
  constexpr unsigned kBitsPerByte = 8;
  constexpr std::uint32_t kByteMask = 0xff;
  for (std::size_t i = 0; i < sizeof(value); ++i)
    bytes[offset + i] = std::byte((value >> (kBitsPerByte * i)) & kByteMask);
}

inline std::uint32_t Word(std::span<const std::byte> bytes,
                          std::size_t offset) {
  constexpr unsigned kBitsPerByte = 8;
  std::uint32_t value = 0;
  Require(offset <= bytes.size() && sizeof(value) <= bytes.size() - offset,
          "literal binary word is in range");
  for (std::size_t i = 0; i < sizeof(value); ++i)
    value |= std::to_integer<std::uint32_t>(bytes[offset + i])
             << (kBitsPerByte * i);
  return value;
}

inline std::vector<std::byte> Binary(bool empty = false) {
  std::vector<std::byte> bytes(kRecordOffset + (empty ? 0 : 2 * kRecordBytes));
  const auto header = Header();
  for (std::size_t i = 0; i < header.size(); ++i) bytes[i] = header[i];
  PutWord(bytes, kCountOffset, empty ? 0 : 2);
  if (!empty) {
    for (std::size_t i = 0; i < kFacetComponents; ++i) {
      PutWord(bytes, kRecordOffset + i * sizeof(std::uint32_t), kFirstFacet[i]);
      PutWord(bytes, kRecordOffset + kRecordBytes + i * sizeof(std::uint32_t),
              kSecondFacet[i]);
    }
  }
  return bytes;
}

inline double Widen(std::uint32_t bits) {
  return static_cast<double>(std::bit_cast<float>(bits));
}

inline std::string Replace(std::string source, std::string_view from,
                           std::string_view to) {
  const auto offset = source.find(from);
  Require(offset != std::string::npos, "owned literal mutation has its marker");
  source.replace(offset, from.size(), to);
  return source;
}

inline constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
inline constexpr std::uint64_t kRequestLimit = 8 * kPhysicalLimit;
inline constexpr std::uint64_t kCountLimit = 65536;
inline constexpr std::uint64_t kDescriptorLimit = 4096;
inline constexpr std::uint64_t kAttributeLimit = 8;
inline constexpr std::uint64_t kNameLimit = 256;
inline constexpr std::uint64_t kLineLimit = 512;
inline constexpr std::uint64_t kTokenLimit = 128;

inline geometry::MeshMaterializationLimits MeshLimits() {
  return {kCountLimit,      kCountLimit,      kCountLimit,  kCountLimit,
          kAttributeLimit,  kCountLimit,      kCountLimit,  kCountLimit,
          kDescriptorLimit, kDescriptorLimit, kRequestLimit};
}
inline StlReadLimits ReadLimits() {
  return {kPhysicalLimit,    kCountLimit,  kNameLimit,
          kLineLimit,        kTokenLimit,  kRequestLimit,
          3 * kRequestLimit, MeshLimits(), {kCountLimit, kRequestLimit}};
}
inline StlWriteLimits WriteLimits() {
  return {kPhysicalLimit,    kCountLimit,
          kNameLimit,        kLineLimit,
          kTokenLimit,       kRequestLimit,
          2 * kRequestLimit, {kCountLimit, kRequestLimit}};
}
inline StlWorkPolicy Work() { return {16, 4, 1, {1, 64, 8, 64}}; }

inline StlWriteOptions Options(const StlDocument& document,
                               StlEncoding encoding) {
  static const auto kDestinationHeader = Header();
  constexpr std::string_view kConvertedName = "converted STL";
  StlWriteOptions options;
  options.encoding = encoding;
  if (encoding == StlEncoding::kBinary)
    options.binary_header =
        document.Encoding() == encoding
            ? document.BinaryHeader()
            : std::span<const std::byte>(kDestinationHeader);
  else
    options.ascii_name =
        document.Encoding() == encoding ? document.AsciiName() : kConvertedName;
  options.face_normal_attribute = 0;
  options.acknowledge_corner_expansion = true;
  options.source_document = &document;
  return options;
}

inline void SameBits(double actual, double expected) {
  Require(std::bit_cast<std::uint64_t>(actual) ==
              std::bit_cast<std::uint64_t>(expected),
          "canonical binary64 component preserves exact value and zero sign");
}

inline void CheckDocument(const StlDocument& document, bool empty = false) {
  const auto mesh = document.Mesh().View();
  Require(document.Inspection().IsStorageValid(), "complete valid inspection");
  Require(mesh.positions.size() == (empty ? 0 : 18),
          "three distinct rows/facet");
  const std::array<geometry::index_t, 3> kFaceOffsets{0, 3, 6};
  const std::array<geometry::index_t, 6> kCornerVertices{0, 1, 2, 3, 4, 5};
  Require(mesh.face_offsets.size() == (empty ? 1 : kFaceOffsets.size()),
          "literal facet offset shape");
  Require(mesh.corner_vertices.size() == (empty ? 0 : kCornerVertices.size()),
          "literal triangle soup corner shape");
  Require(mesh.face_offsets.front() == 0,
          "empty/nonempty offsets begin at zero");
  if (!empty) {
    Require(std::ranges::equal(mesh.face_offsets, kFaceOffsets),
            "literal ordered two-facet offsets");
    Require(std::ranges::equal(mesh.corner_vertices, kCornerVertices),
            "coordinate-equal facets do not acquire shared indices");
    for (std::size_t facet = 0; facet < 2; ++facet) {
      const auto& bits = facet == 0 ? kFirstFacet : kSecondFacet;
      for (std::size_t component = 0; component < 9; ++component)
        SameBits(mesh.positions[facet * 9 + component],
                 Widen(bits[kNormalComponents + component]));
    }
  }
  Require(mesh.attributes.size() == 1, "exactly one facet-normal channel");
  const auto& normal = mesh.attributes.front();
  Require(normal.domain == geometry::AttributeDomain::face &&
              normal.name == "facet_normal" && normal.semantic == "normal" &&
              !normal.set_index && normal.components == kNormalComponents &&
              !normal.offsets && !normal.present && normal.metadata.empty(),
          "literal dense face normal descriptor and no manufactured metadata");
  const auto values = std::get<std::span<const double>>(normal.values);
  Require(values.size() == (empty ? 0 : 6),
          "one unnormalized normal per facet");
  if (!empty) {
    for (std::size_t component = 0; component < kNormalComponents;
         ++component) {
      SameBits(values[component], Widen(kFirstFacet[component]));
      SameBits(values[kNormalComponents + component],
               Widen(kSecondFacet[component]));
    }
  }
}

inline void CheckBinary(std::span<const std::byte> bytes,
                        std::span<const std::byte> header) {
  Require(bytes.size() == kRecordOffset + 2 * kRecordBytes,
          "independent binary extent");
  Require(std::ranges::equal(bytes.first(kHeaderBytes), header),
          "all opaque header bytes retained");
  Require(Word(bytes, kCountOffset) == 2, "little-endian facet count");
  for (std::size_t component = 0; component < kFacetComponents; ++component) {
    Require(Word(bytes, kRecordOffset + component * sizeof(std::uint32_t)) ==
                    kFirstFacet[component] &&
                Word(bytes, kRecordOffset + kRecordBytes +
                                component * sizeof(std::uint32_t)) ==
                    kSecondFacet[component],
            "independent ordered facet components, no normal regeneration");
  }
  for (std::size_t facet = 0; facet < 2; ++facet) {
    const auto offset = kRecordOffset + facet * kRecordBytes + kAttributeOffset;
    Require(bytes[offset] == std::byte{0} && bytes[offset + 1] == std::byte{0},
            "attribute words explicitly zero");
  }
}

}  // namespace meshvale::interchange::stl_fixture

#endif  // MESHVALE_INTERCHANGE_TESTS_STL_FIXTURE_H_
