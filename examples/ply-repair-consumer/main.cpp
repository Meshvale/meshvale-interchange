// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

namespace meshvale::interchange::ply_sample {
// Original example data and independent preservation checks.
inline constexpr std::array kEncodings{PlyEncoding::kAscii,
                                       PlyEncoding::kBinaryLittleEndian,
                                       PlyEncoding::kBinaryBigEndian};
inline constexpr std::array<double, 24> kPositions{-0., 0, 0, 1, 0, 0,  1,   1,
                                                   0,   0, 1, 0, 2, 0,  0,   3,
                                                   0,   0, 2, 1, 0, 99, -0., 5};
inline constexpr std::array<std::uint32_t, 24> kPositionWords{
    0x80000000, 0,          0, 0x3f800000, 0,          0,
    0x3f800000, 0x3f800000, 0, 0,          0x3f800000, 0,
    0x40000000, 0,          0, 0x40400000, 0,          0,
    0x40000000, 0x3f800000, 0, 0x42c60000, 0x80000000, 0x40a00000};
inline constexpr std::array<std::int32_t, 8> kI8{-128, -1, 0,   1,
                                                 2,    3,  126, 127};
inline constexpr std::array<std::uint8_t, 8> kU8{0, 1, 2, 3, 4, 5, 6, 255};
inline constexpr std::array<std::int32_t, 8> kI16{-32768, -1, 0,     1,
                                                  2,      3,  32766, 32767};
inline constexpr std::array<std::uint16_t, 8> kU16{0, 1, 2,     3,
                                                   4, 5, 65534, 65535};
inline constexpr std::array<std::int32_t, 8> kI32{
    -2147483647 - 1, -1, 0, 1, 2, 3, 2147483646, 2147483647};
inline constexpr std::array<std::uint32_t, 8> kU32{
    0, 1, 2, 3, 4, 5, 4294967294, 4294967295};
inline constexpr std::array<std::uint64_t, 8> kDoubleWords{
    0x3fb999999999999a, 0x8000000000000000, 1,
    0x7fefffffffffffff, 0x3ff0000000000000, 0x4000000000000000,
    0x4008000000000000, 0x4010000000000000};
inline constexpr std::array<geometry::index_t, 9> kSampleOffsets{0, 0, 1, 3, 6,
                                                                 6, 7, 9, 10};
inline constexpr std::array<std::int32_t, 10> kSamples{-32768, -1, 0, 1, 2,
                                                       3,      4,  5, 6, 32767};
inline constexpr std::array<geometry::index_t, 4> kOffsets{0, 4, 8, 11};
inline constexpr std::array<geometry::index_t, 11> kCorners{0, 1, 2, 3, 2, 3,
                                                            0, 1, 4, 5, 6};
inline constexpr std::array<geometry::index_t, 3> kChangedOffsets{0, 4, 7};
inline constexpr std::array<geometry::index_t, 7> kChangedCorners{0, 1, 2, 3,
                                                                  4, 5, 6};
inline constexpr std::array<geometry::index_t, 3> kFaceMap{0, 0, 1};
inline constexpr std::array<geometry::index_t, 11> kCornerMap{0, 1, 2, 3, 2, 3,
                                                              0, 1, 4, 5, 6};
inline constexpr std::array<geometry::index_t, 2> kSourceFaces{0, 2};
inline constexpr std::array<geometry::index_t, 7> kSourceCorners{0, 1, 2, 3,
                                                                 8, 9, 10};
inline constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
inline constexpr std::uint64_t kRequestLimit = 8 * kPhysicalLimit;
inline constexpr std::uint64_t kCountLimit = 65536;
inline constexpr std::uint64_t kDescriptorLimit = 8192;
inline constexpr std::uint64_t kPropertyCount = 14;
inline constexpr std::uint64_t kNameBytes = 112;
inline constexpr std::uint64_t kListRows = 14;
inline constexpr std::uint64_t kPropertyValues = 73;
struct Shape {
  bool face_first = false;
  bool changed = false;
  bool empty = false;
  bool omit_face = false;
  bool different_face = false;
};
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
inline std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
inline std::string Replace(std::string source, std::string_view from,
                           std::string_view to) {
  const auto at = source.find(from);
  Require(at != std::string::npos, "literal mutation marker exists");
  source.replace(at, from.size(), to);
  return source;
}
inline std::string_view EncodingWord(PlyEncoding encoding) {
  switch (encoding) {
    case PlyEncoding::kAscii:
      return "ascii";
    case PlyEncoding::kBinaryLittleEndian:
      return "binary_little_endian";
    case PlyEncoding::kBinaryBigEndian:
      return "binary_big_endian";
    default:
      throw std::runtime_error("fixture encoding must be explicit");
  }
}
inline std::string Header(PlyEncoding encoding, Shape shape = {},
                          bool canonical = false) {
  std::string text = canonical ? "ply\n" : "ply\r\n";
  text += "format ";
  text += EncodingWord(encoding);
  text += " 1.0\n";
  if (!canonical) text += "comment original typed polygons\n";
  std::string vertex =
      "element vertex " + std::to_string(shape.empty ? 0 : 8) +
      "\n"
      "property char sample_i8\nproperty float y\nproperty uchar sample_u8\n"
      "property short sample_i16\nproperty float z\nproperty ushort "
      "sample_u16\n"
      "property int sample_i32\nproperty float x\nproperty uint sample_u32\n"
      "property double sample_f64\nproperty list uchar short samples\n";
  std::string face = "element face " +
                     std::to_string(shape.empty ? 0 : (shape.changed ? 2 : 3)) +
                     "\n"
                     "property float weight\nproperty list ushort uint "
                     "bins\nproperty list uchar int vertex_indices\n";
  if (shape.face_first && !shape.omit_face) text += face;
  text += vertex;
  if (!shape.face_first && !shape.omit_face) text += face;
  return text + "end_header\n";
}
inline void Word(std::vector<std::byte>& out, std::uint64_t value,
                 std::size_t width, PlyEncoding encoding) {
  constexpr unsigned kBitsPerByte = 8;
  constexpr std::uint64_t kByteMask = 0xff;
  for (std::size_t i = 0; i < width; ++i) {
    const auto byte =
        encoding == PlyEncoding::kBinaryLittleEndian ? i : width - 1 - i;
    out.push_back(std::byte((value >> (byte * kBitsPerByte)) & kByteMask));
  }
}
inline std::vector<std::byte> Wire(PlyEncoding encoding, Shape shape = {},
                                   bool canonical = false) {
  const auto header = Header(encoding, shape, canonical);
  std::vector<std::byte> out(Bytes(header).begin(), Bytes(header).end());
  if (shape.empty) return out;
  const auto append = [&](std::string_view text) {
    out.insert(out.end(), Bytes(text).begin(), Bytes(text).end());
  };
  const auto vertices = [&] {
    if (encoding == PlyEncoding::kAscii) {
      constexpr std::array<std::string_view, 8> kRows{
          "-128 0 0 -32768 0 0 -2147483648 -0 0 .1 0\n",
          "-1 0 1 -1 0 1 -1 1 1 -0 1 -32768\n",
          "0 1 2 0 0 2 0 1 2 4.9406564584124654e-324 2 -1 0\n",
          "1 1 3 1 0 3 1 0 3 1.7976931348623157e308 3 1 2 3\n",
          "2 0 4 2 0 4 2 2 4 1 0\n",
          "3 0 5 3 0 5 3 3 5 2 1 4\n",
          "126 1 6 32766 0 65534 2147483646 2 4294967294 3 2 5 6\n",
          "127 -0 255 32767 5 65535 2147483647 99 4294967295 4 1 32767\n"};
      for (const auto row : kRows) append(row);
      return;
    }
    constexpr std::array<std::uint8_t, 8> kSignedBytes{0x80, 0xff, 0,    1,
                                                       2,    3,    0x7e, 0x7f};
    constexpr std::array<std::uint16_t, 8> kShortWords{
        0x8000, 0xffff, 0, 1, 2, 3, 0x7ffe, 0x7fff};
    constexpr std::array<std::uint32_t, 8> kIntWords{
        0x80000000, 0xffffffff, 0, 1, 2, 3, 0x7ffffffe, 0x7fffffff};
    constexpr std::array<std::uint16_t, 10> kListWords{
        0x8000, 0xffff, 0, 1, 2, 3, 4, 5, 6, 0x7fff};
    for (std::size_t i = 0; i < 8; ++i) {
      Word(out, kSignedBytes[i], 1, encoding);
      Word(out, kPositionWords[3 * i + 1], 4, encoding);
      Word(out, kU8[i], 1, encoding);
      Word(out, kShortWords[i], 2, encoding);
      Word(out, kPositionWords[3 * i + 2], 4, encoding);
      Word(out, kU16[i], 2, encoding);
      Word(out, kIntWords[i], 4, encoding);
      Word(out, kPositionWords[3 * i], 4, encoding);
      Word(out, kU32[i], 4, encoding);
      Word(out, kDoubleWords[i], 8, encoding);
      Word(out, kSampleOffsets[i + 1] - kSampleOffsets[i], 1, encoding);
      for (auto j = kSampleOffsets[i]; j < kSampleOffsets[i + 1]; ++j)
        Word(out, kListWords[j], 2, encoding);
    }
  };
  const auto faces = [&] {
    if (shape.omit_face) return;
    for (std::size_t face = 0; face < 3; ++face) {
      if (shape.changed && face == 1) continue;
      if (encoding == PlyEncoding::kAscii) {
        if (face == 0)
          append(".5 2 7 4294967295 4 0 1 2 3\n");
        else if (face == 1)
          append(shape.different_face ? ".5 2 7 4294967294 4 2 3 0 1\n"
                                      : ".5 2 7 4294967295 4 2 3 0 1\n");
        else
          append("-0 0 3 4 5 6\n");
      } else {
        Word(out, face == 2 ? 0x80000000 : 0x3f000000, 4, encoding);
        Word(out, face == 2 ? 0 : 2, 2, encoding);
        if (face != 2) {
          Word(out, 7, 4, encoding);
          Word(out, shape.different_face && face == 1 ? 0xfffffffe : 0xffffffff,
               4, encoding);
        }
        Word(out, kOffsets[face + 1] - kOffsets[face], 1, encoding);
        for (auto j = kOffsets[face]; j < kOffsets[face + 1]; ++j)
          Word(out, kCorners[j], 4, encoding);
      }
    }
  };
  if (shape.face_first) faces();
  vertices();
  if (!shape.face_first) faces();
  return out;
}
inline geometry::MeshMaterializationLimits MeshLimits() {
  return {kCountLimit, kCountLimit,      kCountLimit,  kCountLimit,
          kCountLimit, kCountLimit,      kCountLimit,  kCountLimit,
          kCountLimit, kDescriptorLimit, kRequestLimit};
}
inline PlyReadLimits ReadLimits() {
  return {kPhysicalLimit,
          kPhysicalLimit,
          kCountLimit,
          4096,
          128,
          256,
          128,
          kDescriptorLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kRequestLimit,
          3 * kRequestLimit,
          MeshLimits(),
          {kCountLimit, kRequestLimit}};
}
inline PlyWriteLimits WriteLimits() {
  return {kPhysicalLimit,
          kCountLimit,
          4096,
          128,
          128,
          kDescriptorLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kRequestLimit,
          2 * kRequestLimit,
          kPhysicalLimit,
          {kCountLimit, kRequestLimit}};
}
inline PlyWorkPolicy Work() { return {16, 4, 1, {1, 64, 8, 64}}; }
inline PlyWriteOptions Options(PlyEncoding encoding) {
  return {encoding, PlyRewritePolicy::kCanonical};
}
template <class T, std::size_t N>
inline void Values(const geometry::AttributeReadView& attribute,
                   const std::array<T, N>& expected, bool empty) {
  const auto values = std::get<std::span<const T>>(attribute.values);
  Require(values.size() == (empty ? 0 : N), "typed property literal extent");
  for (std::size_t i = 0; i < values.size(); ++i) {
    if constexpr (std::is_floating_point_v<T>)
      Require(std::bit_cast<std::array<std::byte, sizeof(T)>>(values[i]) ==
                  std::bit_cast<std::array<std::byte, sizeof(T)>>(expected[i]),
              "typed property exact floating bits");
    else
      Require(values[i] == expected[i], "typed property exact integer value");
  }
}
inline const geometry::AttributeReadView& Attribute(
    const geometry::MeshReadView& view, geometry::AttributeDomain domain,
    std::string_view name) {
  for (const auto& attribute : view.attributes)
    if (attribute.domain == domain && attribute.name == name) return attribute;
  throw std::runtime_error("missing independently named raw property");
}
inline void CheckMesh(const geometry::MaterializedMesh& mesh,
                      Shape shape = {}) {
  const auto view = mesh.View();
  Require(view.positions.size() == (shape.empty ? 0 : kPositions.size()),
          "literal vertex count including unused row");
  for (std::size_t i = 0; i < view.positions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) ==
                std::bit_cast<std::uint64_t>(kPositions[i]),
            "literal positions keep all bits/order");
  constexpr std::array<geometry::index_t, 1> kEmptyOffsets{0};
  const auto offsets =
      shape.empty || shape.omit_face
          ? std::span<const geometry::index_t>(kEmptyOffsets)
          : (shape.changed ? std::span<const geometry::index_t>(kChangedOffsets)
                           : std::span<const geometry::index_t>(kOffsets));
  const auto corners =
      shape.empty || shape.omit_face
          ? std::span<const geometry::index_t>{}
          : (shape.changed ? std::span<const geometry::index_t>(kChangedCorners)
                           : std::span<const geometry::index_t>(kCorners));
  Require(std::ranges::equal(view.face_offsets, offsets) &&
              std::ranges::equal(view.corner_vertices, corners),
          "literal polygon loops/start/winding/shared IDs");
  Require(view.attributes.size() == (shape.omit_face ? 8 : 10),
          "all custom properties represented exactly once");
  for (const auto& attribute : view.attributes)
    Require(attribute.components == 1 && attribute.semantic.empty() &&
                !attribute.set_index && !attribute.present &&
                attribute.metadata.empty(),
            "raw property has no fabricated semantics/missingness/metadata");
  const auto vertex =
      [&](std::string_view name) -> const geometry::AttributeReadView& {
    return Attribute(view, geometry::AttributeDomain::vertex, name);
  };
  Values(vertex("sample_i8"), kI8, shape.empty);
  Values(vertex("sample_u8"), kU8, shape.empty);
  Values(vertex("sample_i16"), kI16, shape.empty);
  Values(vertex("sample_u16"), kU16, shape.empty);
  Values(vertex("sample_i32"), kI32, shape.empty);
  Values(vertex("sample_u32"), kU32, shape.empty);
  const auto doubles =
      std::get<std::span<const double>>(vertex("sample_f64").values);
  Require(doubles.size() == (shape.empty ? 0 : 8),
          "double custom property extent");
  for (std::size_t i = 0; i < doubles.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(doubles[i]) == kDoubleWords[i],
            "independent custom double bitwords");
  Values(vertex("samples"), kSamples, shape.empty);
  const auto sample_offsets =
      shape.empty ? std::span<const geometry::index_t>(kEmptyOffsets)
                  : std::span<const geometry::index_t>(kSampleOffsets);
  Require(vertex("samples").offsets &&
              std::ranges::equal(*vertex("samples").offsets, sample_offsets),
          "authored empty/nonempty list offsets retained");
  for (const auto& attribute : view.attributes)
    if (attribute.name != "samples" && attribute.name != "bins")
      Require(!attribute.offsets, "scalar property never becomes list");
  if (shape.omit_face) return;
  const auto& weight =
      Attribute(view, geometry::AttributeDomain::face, "weight");
  constexpr std::array<float, 3> kWeights{.5f, .5f, -0.f};
  constexpr std::array<float, 2> kSelectedWeights{.5f, -0.f};
  if (shape.changed)
    Values(weight, kSelectedWeights, shape.empty);
  else
    Values(weight, kWeights, shape.empty);
  const auto& bins = Attribute(view, geometry::AttributeDomain::face, "bins");
  const std::array<std::uint32_t, 4> source_bins{
      7, 4294967295, 7, shape.different_face ? 4294967294u : 4294967295u};
  constexpr std::array<std::uint32_t, 2> kSelectedBins{7, 4294967295};
  if (shape.changed)
    Values(bins, kSelectedBins, shape.empty);
  else
    Values(bins, source_bins, shape.empty);
  constexpr std::array<geometry::index_t, 4> kBinOffsets{0, 2, 4, 4};
  constexpr std::array<geometry::index_t, 3> kSelectedBinOffsets{0, 2, 2};
  const auto expected_offsets =
      shape.empty
          ? std::span<const geometry::index_t>(kEmptyOffsets)
          : (shape.changed
                 ? std::span<const geometry::index_t>(kSelectedBinOffsets)
                 : std::span<const geometry::index_t>(kBinOffsets));
  Require(bins.offsets && std::ranges::equal(*bins.offsets, expected_offsets),
          "selected face ragged rows keep exact values/lengths");
}
inline void CheckSchema(const PlyDocument& document, Shape shape = {}) {
  const auto schema = document.Schema();
  Require(schema.size() == (shape.omit_face ? 1 : 2),
          "literal element inventory");
  std::size_t next_attribute = 0;
  for (std::size_t element = 0; element < schema.size(); ++element) {
    const bool face =
        !shape.omit_face && (shape.face_first ? element == 0 : element == 1);
    const auto& actual = schema[element];
    Require(actual.kind ==
                    (face ? PlyElementKind::kFace : PlyElementKind::kVertex) &&
                actual.original_count ==
                    (shape.empty ? 0 : (face ? (shape.changed ? 2 : 3) : 8)),
            "exact element order/count provenance");
    struct Expected {
      std::string_view name;
      PlyScalarType type;
      PlyBinding binding;
      PlyScalarType count = PlyScalarType::kInvalid;
    };
    const std::array<Expected, 11> vertex{
        {{"sample_i8", PlyScalarType::kChar, PlyBinding::kAttribute},
         {"y", PlyScalarType::kFloat, PlyBinding::kY},
         {"sample_u8", PlyScalarType::kUchar, PlyBinding::kAttribute},
         {"sample_i16", PlyScalarType::kShort, PlyBinding::kAttribute},
         {"z", PlyScalarType::kFloat, PlyBinding::kZ},
         {"sample_u16", PlyScalarType::kUshort, PlyBinding::kAttribute},
         {"sample_i32", PlyScalarType::kInt, PlyBinding::kAttribute},
         {"x", PlyScalarType::kFloat, PlyBinding::kX},
         {"sample_u32", PlyScalarType::kUint, PlyBinding::kAttribute},
         {"sample_f64", PlyScalarType::kDouble, PlyBinding::kAttribute},
         {"samples", PlyScalarType::kShort, PlyBinding::kAttribute,
          PlyScalarType::kUchar}}};
    const std::array<Expected, 3> faces{
        {{"weight", PlyScalarType::kFloat, PlyBinding::kAttribute},
         {"bins", PlyScalarType::kUint, PlyBinding::kAttribute,
          PlyScalarType::kUshort},
         {"vertex_indices", PlyScalarType::kInt, PlyBinding::kVertexIndices,
          PlyScalarType::kUchar}}};
    const auto expected = face ? std::span<const Expected>(faces)
                               : std::span<const Expected>(vertex);
    Require(actual.properties.size() == expected.size(),
            "every original property descriptor retained");
    for (std::size_t i = 0; i < expected.size(); ++i) {
      const auto& want = expected[i];
      const auto& got = actual.properties[i];
      Require(got.name == want.name && got.value_type == want.type &&
                  got.binding == want.binding &&
                  got.kind == (want.count == PlyScalarType::kInvalid
                                   ? PlyPropertyKind::kScalar
                                   : PlyPropertyKind::kList) &&
                  got.count_type == (want.count == PlyScalarType::kInvalid
                                         ? std::optional<PlyScalarType>{}
                                         : std::optional(want.count)),
              "literal ordered schema/name/narrow/count/item type binding");
      if (want.binding == PlyBinding::kAttribute)
        Require(got.attribute && *got.attribute == next_attribute++,
                "schema attribute ordinal matches canonical property order");
      else
        Require(!got.attribute, "core property is not fabricated attribute");
    }
  }
}
inline void Check(const PlyDocument& document, Shape shape = {}) {
  Require(document.Inspection().IsStorageValid(),
          "complete actual storage inspection");
  CheckMesh(document.Mesh(), shape);
  CheckSchema(document, shape);
}
inline void CheckOutput(const PlyOutput& output, PlyEncoding source,
                        PlyEncoding destination, Shape original = {},
                        Shape supplied = {}) {
  const auto& facts = output.Facts();
  Require(facts.source_encoding == source &&
              facts.source_vertex_count == (original.empty ? 0 : 8) &&
              facts.source_face_count == (original.empty || original.omit_face
                                              ? 0
                                              : (original.changed ? 2 : 3)) &&
              facts.encoding == destination &&
              facts.rewrite_policy == PlyRewritePolicy::kCanonical &&
              facts.vertex_count == (supplied.empty ? 0 : 8) &&
              facts.face_count == (supplied.empty || supplied.omit_face
                                       ? 0
                                       : (supplied.changed ? 2 : 3)) &&
              facts.corner_count == (supplied.empty || supplied.omit_face
                                         ? 0
                                         : (supplied.changed ? 7 : 11)) &&
              facts.property_count == (supplied.omit_face ? 11 : 14) &&
              facts.correspondence == PlyCorrespondence::kIdentity,
          "actual source-layout provenance and supplied-mesh identity facts");
  if (destination != PlyEncoding::kAscii)
    Require(
        std::ranges::equal(output.Bytes(), Wire(destination, supplied, true)),
        "binary output equals independently packed entire wire");
  Require(Text(output.Bytes()).starts_with("ply\nformat "),
          "canonical header begins with exact LF framing");
}
inline std::vector<std::byte> PointWire(PlyEncoding encoding, bool wide,
                                        std::span<const std::uint64_t> words,
                                        std::string_view ascii) {
  Require(words.size() % 3 == 0, "literal point coordinates form XYZ rows");
  std::string header = "ply\nformat ";
  header += EncodingWord(encoding);
  header += " 1.0\nelement vertex " + std::to_string(words.size() / 3) + "\n";
  for (const auto name : {"x", "y", "z"})
    header +=
        std::string("property ") + (wide ? "double " : "float ") + name + "\n";
  header += "end_header\n";
  std::vector<std::byte> out(Bytes(header).begin(), Bytes(header).end());
  if (encoding == PlyEncoding::kAscii)
    out.insert(out.end(), Bytes(ascii).begin(), Bytes(ascii).end());
  else
    for (auto word : words) Word(out, word, wide ? 8 : 4, encoding);
  return out;
}
}  // namespace meshvale::interchange::ply_sample

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::ply_sample;
namespace fs = std::filesystem;
namespace repair = meshvale::repair;
using f::Require;
constexpr std::uint64_t kContextBytes = 64 * f::kPhysicalLimit;
repair::MaterializedDuplicateLimits RepairLimits();
void WriteFile(const fs::path& path, std::span<const std::byte> bytes) {
  Require(!fs::exists(path), "Sample output path must be new");
  std::ofstream stream(path, std::ios::binary);
  Require(bool(stream), "Cannot create sample file");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.flush();
  Require(bool(stream), "Cannot write complete sample file");
}
std::vector<std::byte> ReadFile(const fs::path& path) {
  const auto size = fs::file_size(path);
  Require(size <= f::kPhysicalLimit, "Sample file exceeds configured bound");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream stream(path, std::ios::binary);
  Require(bool(stream), "Cannot open sample file");
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  Require(stream.gcount() == static_cast<std::streamsize>(size) &&
              stream.peek() == std::char_traits<char>::eof(),
          "Physical file incomplete or changed");
  return bytes;
}
class Directory {
 public:
  explicit Directory(const std::optional<fs::path>& requested)
      : path_(requested ? *requested
                        : fs::temp_directory_path() /
                              ("meshvale-ply-" +
                               std::to_string(std::chrono::steady_clock::now()
                                                  .time_since_epoch()
                                                  .count()))),
        temporary_(!requested) {
    Require(fs::create_directory(path_), "Output directory must be new");
  }
  Directory(const Directory&) = delete;
  Directory& operator=(const Directory&) = delete;
  ~Directory() {
    if (temporary_) {
      std::error_code ignored;
      fs::remove_all(path_, ignored);
    }
  }
  const fs::path& Path() const { return path_; }

 private:
  fs::path path_;
  bool temporary_;
};
void Empty(const geo::ExecutionContext& context) {
  for (const auto encoding : f::kEncodings) {
    f::Shape shape;
    shape.empty = true;
    const auto read = ic::ReadPly(f::Wire(encoding, shape), f::ReadLimits(),
                                  f::Work(), context);
    Require(read.document.has_value(),
            "Common empty numerical profile must work");
    f::Check(*read.document, shape);
    const auto operation = repair::RemoveMaterializedDuplicateFaces(
        read.document->Mesh(), {}, {}, RepairLimits(), f::Work().geometry);
    Require(operation.status == repair::MaterializedDuplicateStatus::kSuccess &&
                operation.repair &&
                operation.repair->Outcome() ==
                    repair::CandidateOutcome::unchanged &&
                operation.repair->FaceMap().empty() &&
                operation.repair->CornerMap().empty() &&
                operation.repair->SourceFaces().empty() &&
                operation.repair->SourceCorners().empty(),
            "Actual empty-target Repair is unchanged with all four complete "
            "empty correspondence maps");
    f::CheckMesh(operation.repair->Candidate(), shape);
    for (const auto destination : f::kEncodings) {
      const auto output =
          ic::WritePly(operation.repair->Candidate(), *read.document,
                       f::Options(destination), f::WriteLimits(), f::Work());
      Require(output.output.has_value(), "Actual empty export");
      const auto reload = ic::ReadPly(output.output->Bytes(), f::ReadLimits(),
                                      f::Work(), context);
      Require(reload.document.has_value(), "Actual empty reload");
      f::Check(*reload.document, shape);
    }
  }
}
bool Unsupported(std::span<const fs::path> args,
                 const geo::ExecutionContext& context) {
  if (args.size() != 1 || args[0] != fs::path("--unsupported-profile"))
    return false;
  for (const auto encoding : f::kEncodings) {
    const auto result =
        ic::ReadPly(f::Wire(encoding), f::ReadLimits(), f::Work(), context);
    Require(result.status == ic::PlyStatus::kGeometryFailure &&
                !result.document &&
                result.diagnostic.geometry_status ==
                    geo::MaterializationStatus::kUnsupportedAllocationProfile,
            "Actual nonempty unsupported numerical-profile refusal");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "Unsupported requests release every owner/worker");
  std::cout << "Actual three-encoding unsupported profile and common empty "
               "roundtrips verified\n";
  return true;
}
std::optional<fs::path> Output(std::span<const fs::path> args) {
  if (args.empty()) return {};
  Require(args.size() == 2 && args[0] == fs::path("--output"),
          "Use --output NEW_DIRECTORY or --unsupported-profile");
  return args[1];
}

constexpr std::array<repair::DuplicateTarget, 1> kTarget{{{0, 1}}};
// These are authored measurements, not references to face/corner identities.
// The codec itself makes no such declaration for arbitrary imported data.
constexpr std::array<repair::RowLocalAttributeDeclaration, 10> kMeasurements{
    {{geo::AttributeDomain::vertex, "sample_i8"},
     {geo::AttributeDomain::vertex, "sample_u8"},
     {geo::AttributeDomain::vertex, "sample_i16"},
     {geo::AttributeDomain::vertex, "sample_u16"},
     {geo::AttributeDomain::vertex, "sample_i32"},
     {geo::AttributeDomain::vertex, "sample_u32"},
     {geo::AttributeDomain::vertex, "sample_f64"},
     {geo::AttributeDomain::vertex, "samples"},
     {geo::AttributeDomain::face, "weight"},
     {geo::AttributeDomain::face, "bins"}}};
repair::MaterializedDuplicateLimits RepairLimits() {
  return {f::kCountLimit, f::kCountLimit,   f::kDescriptorLimit,
          4096,           f::kRequestLimit, {f::kCountLimit, f::kRequestLimit},
          f::MeshLimits()};
}
template <std::size_t N>
void Equal(std::span<const geo::index_t> actual,
           const std::array<geo::index_t, N>& expected) {
  Require(std::ranges::equal(actual, expected),
          "Complete actual Repair map equals independent literal");
}
void Maps(const repair::MaterializedDuplicateRepair& result, bool changed) {
  if (changed) {
    Equal(result.FaceMap(), f::kFaceMap);
    Equal(result.CornerMap(), f::kCornerMap);
    Equal(result.SourceFaces(), f::kSourceFaces);
    Equal(result.SourceCorners(), f::kSourceCorners);
  } else {
    constexpr std::array<geo::index_t, 3> kFaces{0, 1, 2};
    constexpr std::array<geo::index_t, 11> kCorners{0, 1, 2, 3, 4, 5,
                                                    6, 7, 8, 9, 10};
    Equal(result.FaceMap(), kFaces);
    Equal(result.SourceFaces(), kFaces);
    Equal(result.CornerMap(), kCorners);
    Equal(result.SourceCorners(), kCorners);
  }
}
void Rejected(std::span<const std::byte> wire,
              const geo::ExecutionContext& context,
              repair::MaterializedDuplicateOptions options,
              repair::MaterializedDuplicateDiagnosticCode code) {
  const auto read = ic::ReadPly(wire, f::ReadLimits(), f::Work(), context);
  Require(read.document.has_value(), "Negative control must genuinely import");
  const auto result = repair::RemoveMaterializedDuplicateFaces(
      read.document->Mesh(), kTarget, options, RepairLimits(),
      f::Work().geometry);
  Require(result.status == repair::MaterializedDuplicateStatus::kRejected &&
              !result.repair && result.diagnostic.code == code,
          "Real operation rejects selected unsafe/non-equivalent source");
}
void Negatives(const geo::ExecutionContext& context) {
  Rejected(f::Wire(ic::PlyEncoding::kAscii), context, {},
           repair::MaterializedDuplicateDiagnosticCode::kUnsupportedAttribute);
  f::Shape different;
  different.different_face = true;
  Rejected(f::Wire(ic::PlyEncoding::kAscii, different), context,
           {kMeasurements},
           repair::MaterializedDuplicateDiagnosticCode::kNotEquivalent);
  constexpr std::string_view kReversed =
      "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty "
      "float y\nproperty float z\nelement face 2\nproperty list uchar int "
      "vertex_indices\nend_header\n0 0 0\n1 0 0\n1 1 0\n0 1 0\n4 0 1 2 3\n4 0 "
      "3 2 1\n";
  constexpr std::string_view kDifferentIds =
      "ply\nformat ascii 1.0\nelement vertex 8\nproperty float x\nproperty "
      "float y\nproperty float z\nelement face 2\nproperty list uchar int "
      "vertex_indices\nend_header\n0 0 0\n1 0 0\n1 1 0\n0 1 0\n0 0 0\n1 0 0\n1 "
      "1 0\n0 1 0\n4 0 1 2 3\n4 4 5 6 7\n";
  Rejected(f::Bytes(kReversed), context, {},
           repair::MaterializedDuplicateDiagnosticCode::kNotEquivalent);
  Rejected(f::Bytes(kDifferentIds), context, {},
           repair::MaterializedDuplicateDiagnosticCode::kNotEquivalent);
}
int Run(std::span<const fs::path> args) {
  geo::ExecutionContext context({4, 1, kContextBytes});
  Empty(context);
  if (Unsupported(args, context)) return 0;
  Directory directory(Output(args));
  for (std::size_t source = 0; source < f::kEncodings.size(); ++source) {
    const auto original = f::Wire(f::kEncodings[source]);
    const auto source_path =
        directory.Path() / ("source-" + std::to_string(source) + ".ply");
    WriteFile(source_path, original);
    {
      const auto bytes = ReadFile(source_path);
      const auto read = ic::ReadPly(bytes, f::ReadLimits(), f::Work(), context);
      Require(read.document.has_value(),
              "Actual source import for charged Repair");
      f::Check(*read.document);
      for (const bool changed : {false, true}) {
        const auto targets =
            changed ? std::span<const repair::DuplicateTarget>(kTarget)
                    : std::span<const repair::DuplicateTarget>{};
        const auto operation = repair::RemoveMaterializedDuplicateFaces(
            read.document->Mesh(), targets, {kMeasurements}, RepairLimits(),
            f::Work().geometry);
        Require(
            operation.status == repair::MaterializedDuplicateStatus::kSuccess &&
                operation.repair &&
                operation.repair->Outcome() ==
                    (changed ? repair::CandidateOutcome::accepted
                             : repair::CandidateOutcome::unchanged),
            "Actual selected charged Repair outcome");
        Maps(*operation.repair, changed);
        f::Shape candidate;
        candidate.changed = changed;
        f::CheckMesh(operation.repair->Candidate(), candidate);
        for (std::size_t destination = 0; destination < f::kEncodings.size();
             ++destination) {
          const auto written =
              ic::WritePly(operation.repair->Candidate(), *read.document,
                           f::Options(f::kEncodings[destination]),
                           f::WriteLimits(), f::Work());
          Require(written.output.has_value(),
                  "Actual changed strong mesh binds to original narrow schema");
          f::CheckOutput(*written.output, f::kEncodings[source],
                         f::kEncodings[destination], {}, candidate);
          const auto path =
              directory.Path() / ("output-" + std::to_string(source) + "-" +
                                  (changed ? "changed-" : "unchanged-") +
                                  std::to_string(destination) + ".ply");
          WriteFile(path, written.output->Bytes());
          const auto physical = ReadFile(path);
          Require(std::ranges::equal(physical, written.output->Bytes()),
                  "Physical output equals real serialization");
          const auto reload =
              ic::ReadPly(physical, f::ReadLimits(), f::Work(), context);
          Require(reload.document.has_value(),
                  "Actual edited physical output reload");
          f::Check(*reload.document, candidate);
        }
      }
    }
    Require(std::ranges::equal(ReadFile(source_path), original),
            "Original source file remains unchanged");
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "All actual chain owners/workers release");
  }
  Negatives(context);
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "Negative controls rollback all owners/workers");
  std::cout
      << "PLY + Repair: eighteen physical chains, all four complete maps, "
         "typed properties and real rejection boundaries verified\n";
  return 0;
}

template <class Character>
int Main(int argc, Character** argv) {
  try {
    Require(argc >= 1 && argc <= 3, "Too many sample arguments");
    std::array<fs::path, 2> args;
    for (int i = 1; i < argc; ++i)
      args[static_cast<std::size_t>(i - 1)] = fs::path(argv[i]);
    return Run(std::span(args).first(static_cast<std::size_t>(argc - 1)));
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
}  // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return Main(argc, argv); }
#else
int main(int argc, char** argv) { return Main(argc, argv); }
#endif