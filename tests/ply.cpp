// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#if defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#endif

#include "ply_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::ply_fixture;
using f::Require;
constexpr std::uint64_t kContextBytes = 64 * f::kPhysicalLimit;

void RoundTrips(bool unsupported) {
  geo::ExecutionContext context({4, 1, kContextBytes});
  for (const auto source : f::kEncodings) {
    for (const bool first : {false, true}) {
      for (const auto variant : {0, 1, 2}) {
        f::Shape shape;
        shape.face_first = first;
        shape.empty = variant == 1;
        shape.omit_face = variant == 2;
        const auto bytes = f::Wire(source, shape);
        {
          auto read = ic::ReadPly(bytes, f::ReadLimits(), f::Work(), context);
          Require(read.execution.codec.launched_workers == 0 &&
                      read.execution.codec.joined_workers == 0 &&
                      read.execution.materialization.launched_workers ==
                          read.execution.materialization.joined_workers,
                  "serial codec and actual joined delegated workers");
          if (unsupported && !shape.empty) {
            Require(read.status == ic::PlyStatus::kGeometryFailure &&
                        !read.document &&
                        read.diagnostic.geometry_status ==
                            geo::MaterializationStatus::
                                kUnsupportedAllocationProfile,
                    "nonempty actual unsupported profile is not skipped");
            continue;
          }
          Require(read.status == ic::PlyStatus::kSuccess && read.document,
                  "literal schema and all-width property read");
          f::Check(*read.document, shape);
          for (const auto destination : f::kEncodings) {
            auto write = ic::WritePly(read.document->Mesh(), *read.document,
                                      f::Options(destination), f::WriteLimits(),
                                      f::Work());
            Require(write.status == ic::PlyStatus::kSuccess && write.output,
                    "actual cross-encoding writer");
            f::CheckOutput(*write.output, source, destination, shape, shape);
            auto reload = ic::ReadPly(write.output->Bytes(), f::ReadLimits(),
                                      f::Work(), context);
            Require(reload.status == ic::PlyStatus::kSuccess && reload.document,
                    "actual serialized bytes independently reload");
            f::Check(*reload.document, shape);
          }
          Require(std::ranges::equal(read.document->OriginalBytes(), bytes),
                  "source bytes retained unmodified across every destination");
        }
        Require(
            context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "complete roundtrip releases all owners and workers");
      }
    }
  }
}

void WireRefusals() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  const auto source = std::string(f::Text(f::Wire(ic::PlyEncoding::kAscii)));
  std::size_t ordinal = 0;
  const auto refuse = [&](std::string_view text, ic::PlyStatus status) {
    const auto result =
        ic::ReadPly(f::Bytes(text), f::ReadLimits(), f::Work(), context);
    Require(result.status == status && !result.document,
            "literal wire refusal " + std::to_string(ordinal) + " expected " +
                std::to_string(static_cast<int>(status)) + " actual " +
                std::to_string(static_cast<int>(result.status)) + " code " +
                std::to_string(static_cast<int>(result.diagnostic.code)));
    ++ordinal;
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "refused source rolls back all requests");
  };
  for (const auto& bytes :
       {f::Replace(source, "ply\r\n", " ply\n"),
        f::Replace(source, "ply\r\n", "ply\r"),
        f::Replace(source, "format ascii", "\nformat ascii"),
        f::Replace(source, "sample_u8", "sample_i8"),
        f::Replace(source, "sample_i8", "not-valid"),
        f::Replace(source, "element face 3", "element vertex 3"),
        f::Replace(source, "-128 0", "-129 0"),
        f::Replace(source, "-0 0 3 4 5 6", "-0 0 3 4 5 8"),
        f::Replace(source, ".5 2 7 4294967295 4", ".5 2 7 4294967295 +4"),
        f::Replace(source, ".1 0\n", "nan 0\n")}) {
    refuse(bytes, ic::PlyStatus::kMalformed);
  }
  refuse(source + "0", ic::PlyStatus::kUnsupportedVariant);
  for (const auto& bytes :
       {f::Replace(source, "format ascii 1.0", "format ascii 2.0"),
        f::Replace(source, "comment original typed polygons",
                   "obj_info origin"),
        f::Replace(source, "property char sample_i8",
                   "property int8 sample_i8"),
        f::Replace(source, "element face 3", "element edge 3"),
        f::Replace(source, "property float y", "property double y"),
        f::Replace(source, "vertex_indices", "vertex_index"),
        f::Replace(source, "property list uchar int vertex_indices",
                   "property list float int vertex_indices")}) {
    refuse(bytes, ic::PlyStatus::kUnsupportedVariant);
  }
  std::string bom("\xef\xbb\xbf");
  refuse(bom + source, ic::PlyStatus::kMalformed);
  auto nul = source;
  nul[nul.size() - 2] = '\0';
  refuse(nul, ic::PlyStatus::kMalformed);
  for (const auto encoding : {ic::PlyEncoding::kBinaryLittleEndian,
                              ic::PlyEncoding::kBinaryBigEndian}) {
    auto bytes = f::Wire(encoding);
    bytes.pop_back();
    refuse(f::Text(bytes), ic::PlyStatus::kMalformed);
    bytes = f::Wire(encoding);
    bytes.push_back(std::byte{0});
    refuse(f::Text(bytes), ic::PlyStatus::kUnsupportedVariant);
  }
  refuse(f::Replace(source, "property float x\n", ""),
         ic::PlyStatus::kUnsupportedVariant);
  refuse(f::Replace(source, "element face 3", "element face 0"),
         ic::PlyStatus::kUnsupportedVariant);
  refuse(f::Replace(source, "samples\n", "samples\nproperty float extra\n"),
         ic::PlyStatus::kMalformed);
  // A raw binary NaN is refused without normalizing its representation.
  for (const auto encoding : {ic::PlyEncoding::kBinaryLittleEndian,
                              ic::PlyEncoding::kBinaryBigEndian}) {
    constexpr std::array<std::uint64_t, 3> kNanWords{0x7f800001, 0, 0};
    refuse(f::Text(f::PointWire(encoding, false, kNanWords, {})),
           ic::PlyStatus::kMalformed);
  }
}

void NumericCases(bool wide, std::span<const std::uint64_t> source_words,
                  std::span<const std::uint64_t> expected,
                  std::string_view ascii, unsigned mode_mask = 0,
                  unsigned selected = 0) {
  const auto check_mode = [&] {
#if defined(_M_X64) || defined(__SSE2__)
    Require(
        (_mm_getcsr() & mode_mask) == selected,
        "actual codec preserves caller MXCSR controls before fixture cleanup");
#else
    (void)mode_mask;
    (void)selected;
#endif
  };
  geo::ExecutionContext context({4, 1, kContextBytes});
  const auto check = [&](const ic::PlyDocument& document,
                         std::string_view phase, ic::PlyEncoding source,
                         ic::PlyEncoding destination) {
    const auto view = document.Mesh().View();
    Require(view.positions.size() == expected.size() &&
                view.face_offsets.size() == 1 && view.attributes.empty(),
            "numeric point-only schema exact shape");
    for (std::size_t i = 0; i < expected.size(); ++i) {
      const auto actual = std::bit_cast<std::uint64_t>(view.positions[i]);
      if (actual != expected[i]) {
        std::ostringstream message;
        message << "independent IEEE literal exact coordinate bits: phase="
                << phase << " source=" << f::EncodingWord(source)
                << " destination=" << f::EncodingWord(destination)
                << " width=" << (wide ? 64 : 32) << " index=" << i
                << " expected=0x" << std::hex << std::setfill('0')
                << std::setw(16) << expected[i] << " actual=0x" << std::setw(16)
                << actual << " mode_mask=0x" << mode_mask
                << " selected_controls=0x" << selected;
#if defined(_M_X64) || defined(__SSE2__)
        message << " mxcsr=0x" << _mm_getcsr();
#else
        message << " mxcsr=unavailable";
#endif
        Require(actual == expected[i], message.str());
      }
    }
  };
  for (const auto source : f::kEncodings) {
    const auto bytes = f::PointWire(source, wide, source_words, ascii);
    auto read = ic::ReadPly(bytes, f::ReadLimits(), f::Work(), context);
    check_mode();
    Require(read.status == ic::PlyStatus::kSuccess && read.document,
            "all finite declared-width extrema/subnormals admit");
    check(*read.document, "read", source, source);
    for (const auto destination : f::kEncodings) {
      auto write =
          ic::WritePly(read.document->Mesh(), *read.document,
                       f::Options(destination), f::WriteLimits(), f::Work());
      check_mode();
      Require(write.status == ic::PlyStatus::kSuccess && write.output,
              "exact numeric export in caller mode");
      if (destination != ic::PlyEncoding::kAscii)
        Require(std::ranges::equal(
                    write.output->Bytes(),
                    f::PointWire(destination, wide, source_words, {})),
                "independent exact binary numeric wire");
      auto reload = ic::ReadPly(write.output->Bytes(), f::ReadLimits(),
                                f::Work(), context);
      check_mode();
      Require(reload.status == ic::PlyStatus::kSuccess && reload.document,
              "numeric output actually reloads");
      check(*reload.document, "reload", source, destination);
    }
  }
}

void NumericFidelity() {
  constexpr std::array<std::uint64_t, 6> kFloatWords{
      1, 0x80000001, 0x80000000, 0x7f7fffff, 0xff7fffff, 0x3dcccccd};
  constexpr std::array<std::uint64_t, 6> kFloatDoubles{
      0x36a0000000000000, 0xb6a0000000000000, 0x8000000000000000,
      0x47efffffe0000000, 0xc7efffffe0000000, 0x3fb99999a0000000};
  constexpr std::array<std::uint64_t, 6> kDoubles{1,
                                                  0x8000000000000001,
                                                  0x8000000000000000,
                                                  0x7fefffffffffffff,
                                                  0xffefffffffffffff,
                                                  0x3fb999999999999a};
  constexpr std::string_view kFloatText =
      "1.40129846e-45 -1.40129846e-45 -0\n3.40282347e38 -3.40282347e38 .1\n";
  constexpr std::string_view kDoubleText =
      "4.9406564584124654e-324 -4.9406564584124654e-324 "
      "-0\n1.7976931348623157e308 -1.7976931348623157e308 .1\n";
  // Exact dyadic halfway decimals: the retained even endpoint is alternately
  // below and above the source, so truncation cannot satisfy both oracles.
  constexpr std::array<std::uint64_t, 3> kFloatTies{0x3f800000, 0x3f800002,
                                                    0xbf800000};
  constexpr std::array<std::uint64_t, 3> kFloatTieDoubles{
      0x3ff0000000000000, 0x3ff0000040000000, 0xbff0000000000000};
  constexpr std::array<std::uint64_t, 3> kDoubleTies{
      0x3ff0000000000000, 0x3ff0000000000002, 0xbff0000000000000};
  constexpr std::string_view kFloatTieText =
      "1.000000059604644775390625 1.000000178813934326171875 "
      "-1.000000059604644775390625\n";
  constexpr std::string_view kDoubleTieText =
      "1.00000000000000011102230246251565404236316680908203125 "
      "1.00000000000000033306690738754696212708950042724609375 "
      "-1.00000000000000011102230246251565404236316680908203125\n";
  NumericCases(false, kFloatWords, kFloatDoubles, kFloatText);
  NumericCases(true, kDoubles, kDoubles, kDoubleText);
  NumericCases(false, kFloatTies, kFloatTieDoubles, kFloatTieText);
  NumericCases(true, kDoubleTies, kDoubleTies, kDoubleTieText);
#if defined(_M_X64) || defined(__SSE2__)
  constexpr unsigned kModeMask = 0xffc0;
  constexpr unsigned kRoundingMask = 0x6000;
  constexpr unsigned kFlushMask = 0x8040;
  struct Restore {
    unsigned original;
    ~Restore() { _mm_setcsr(original); }
  } restore{_mm_getcsr()};
  for (const unsigned rounding : {0u, 0x2000u, 0x4000u, 0x6000u}) {
    for (const unsigned flush : {0u, kFlushMask}) {
      const auto selected =
          (restore.original & ~(kRoundingMask | kFlushMask)) | rounding | flush;
      _mm_setcsr(selected);
      NumericCases(false, kFloatWords, kFloatDoubles, kFloatText, kModeMask,
                   selected & kModeMask);
      NumericCases(true, kDoubles, kDoubles, kDoubleText, kModeMask,
                   selected & kModeMask);
      NumericCases(false, kFloatTies, kFloatTieDoubles, kFloatTieText,
                   kModeMask, selected & kModeMask);
      NumericCases(true, kDoubleTies, kDoubleTies, kDoubleTieText, kModeMask,
                   selected & kModeMask);
    }
  }
#endif
}

void LiteralTopologyAndGrammar() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  constexpr std::string_view kSource =
      "ply\nformat ascii 1.0\ncomment\n\tcomment  exact space\n"
      "element vertex 4\nproperty double x\nproperty double y\nproperty double "
      "z\n"
      "element face 3\nproperty list short uint vertex_indices\nend_header\n"
      "0 0 0\n1 0 0\n1 1 0\n0 1 0\n"
      "4 0 3 2 1\n\n3\t0 0\n1\n5 0 1 2 3 0";
  constexpr std::array<geo::index_t, 4> kOffsets{0, 4, 7, 12};
  constexpr std::array<geo::index_t, 12> kCorners{0, 3, 2, 1, 0, 0,
                                                  1, 0, 1, 2, 3, 0};
  const auto check = [&](const ic::PlyDocument& document) {
    const auto view = document.Mesh().View();
    Require(view.positions.size() == 12 && view.attributes.empty() &&
                std::ranges::equal(view.face_offsets, kOffsets) &&
                std::ranges::equal(view.corner_vertices, kCorners),
            "reversed winding, finite repeated IDs and polygon closure "
            "preserved without triangulation");
  };
  const auto read =
      ic::ReadPly(f::Bytes(kSource), f::ReadLimits(), f::Work(), context);
  Require(read.document.has_value(),
          "wrapped/blank ASCII records, keyword-only/indented comments and "
          "final EOF token admit");
  check(*read.document);
  for (const auto destination : f::kEncodings) {
    const auto output =
        ic::WritePly(read.document->Mesh(), *read.document,
                     f::Options(destination), f::WriteLimits(), f::Work());
    Require(output.output.has_value(),
            "literal repeated polygon topology serializes");
    const auto reload = ic::ReadPly(output.output->Bytes(), f::ReadLimits(),
                                    f::Work(), context);
    Require(reload.document.has_value(),
            "repeated polygon actual output reload");
    check(*reload.document);
  }
  constexpr std::array<std::string_view, 6> kNames{"char",   "uchar", "short",
                                                   "ushort", "int",   "uint"};
  constexpr std::array<ic::PlyScalarType, 6> kTypes{
      ic::PlyScalarType::kChar,  ic::PlyScalarType::kUchar,
      ic::PlyScalarType::kShort, ic::PlyScalarType::kUshort,
      ic::PlyScalarType::kInt,   ic::PlyScalarType::kUint};
  const auto source = std::string(f::Text(f::Wire(ic::PlyEncoding::kAscii)));
  for (std::size_t i = 0; i < kNames.size(); ++i) {
    const auto wire =
        f::Replace(source, "list uchar short samples",
                   "list " + std::string(kNames[i]) + " short samples");
    const auto admitted =
        ic::ReadPly(f::Bytes(wire), f::ReadLimits(), f::Work(), context);
    Require(admitted.document.has_value(),
            "every original integer list count kind admits");
    f::CheckMesh(admitted.document->Mesh());
    Require(
        admitted.document->Schema()[0].properties[10].count_type == kTypes[i],
        "declared count kind retained independently");
    for (const auto destination : f::kEncodings) {
      const auto output =
          ic::WritePly(admitted.document->Mesh(), *admitted.document,
                       f::Options(destination), f::WriteLimits(), f::Work());
      Require(output.output.has_value(),
              "all count widths serialize without type promotion");
      const auto reload = ic::ReadPly(output.output->Bytes(), f::ReadLimits(),
                                      f::Work(), context);
      Require(reload.document &&
                  reload.document->Schema()[0].properties[10].count_type ==
                      kTypes[i],
              "all count widths independently reload");
      f::CheckMesh(reload.document->Mesh());
    }
  }
}

void BinaryBoundary() {
  struct Case {
    ic::PlyEncoding encoding;
    std::uint64_t word;
    std::uint64_t expected;
  };
  constexpr std::array<Case, 4> kCases{
      {{ic::PlyEncoding::kBinaryLittleEndian, 0x3f800020, 0x3ff0000400000000},
       {ic::PlyEncoding::kBinaryBigEndian, 0x20000000, 0x3c00000000000000},
       {ic::PlyEncoding::kBinaryLittleEndian, 0x3f80000a, 0x3ff0000140000000},
       {ic::PlyEncoding::kBinaryBigEndian, 0x0a000000, 0x3940000000000000}}};
  for (const auto& item : kCases) {
    const std::array<std::uint64_t, 3> words{item.word, 0, 0};
    const std::array<std::uint64_t, 3> expected{item.expected, 0, 0};
    geo::ExecutionContext context({4, 1, kContextBytes});
    const auto wire = f::PointWire(item.encoding, false, words, {});
    const auto read = ic::ReadPly(wire, f::ReadLimits(), f::Work(), context);
    Require(read.document.has_value(),
            "binary first payload whitespace byte is not consumed as header");
    const auto view = read.document->Mesh().View();
    for (std::size_t i = 0; i < 3; ++i)
      Require(std::bit_cast<std::uint64_t>(view.positions[i]) == expected[i],
              "literal payload boundary bits");
  }
}

geo::MaterializedMesh Modified(const ic::PlyDocument& source,
                               const geo::ExecutionContext& context,
                               int change) {
  const auto view = source.Mesh().View();
  std::vector<double> positions(view.positions.begin(), view.positions.end());
  std::vector<geo::AttributeMaterializationInput> attributes;
  for (const auto& attribute : view.attributes) {
    geo::AttributeMaterializationInput input;
    input.domain = attribute.domain;
    input.name = attribute.name;
    input.components = attribute.components;
    input.offsets = attribute.offsets;
    std::visit([&](auto values) { input.values = values; }, attribute.values);
    attributes.push_back(input);
  }
  std::array<std::int32_t, 8> narrow = f::kI8;
  if (change == 0) attributes.erase(attributes.begin());
  if (change == 1) std::swap(attributes[0], attributes[1]);
  if (change == 2) attributes[0].name = "renamed";
  if (change == 3) attributes[0].semantic = "measurement";
  if (change == 4) {
    narrow[0] = -129;
    attributes[0].values = std::span<const std::int32_t>(narrow);
  }
  if (change == 5) positions[0] = .1;
  if (change == 6) {
    attributes.push_back(attributes.front());
    attributes.back().name = "extra";
  }
  if (change == 7) attributes[0].set_index = 1;
  constexpr std::array<std::uint8_t, 8> kPresent{1, 1, 1, 1, 1, 1, 1, 1};
  if (change == 8)
    attributes[0].present = std::span<const std::uint8_t>(kPresent);
  constexpr std::array<geo::index_t, 9> kScalarListOffsets{0, 1, 2, 3, 4,
                                                           5, 6, 7, 8};
  if (change == 9)
    attributes[0].offsets = std::span<const geo::index_t>(kScalarListOffsets);
  auto result = geo::MaterializeMesh(
      {std::span<const double>(positions),
       geo::PolygonTopologyInput{view.face_offsets, view.corner_vertices},
       attributes},
      f::MeshLimits(), f::Work().geometry, context);
  Require(result.mesh.has_value(), "real alternate strong mesh construction");
  return *result.mesh;
}

void WriterRefusals() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  const auto read = ic::ReadPly(f::Wire(ic::PlyEncoding::kAscii),
                                f::ReadLimits(), f::Work(), context);
  Require(read.document.has_value(), "writer refusal genuine schema source");
  for (const auto encoding :
       {ic::PlyEncoding::kInvalid, static_cast<ic::PlyEncoding>(999)}) {
    const auto result =
        ic::WritePly(read.document->Mesh(), *read.document,
                     f::Options(encoding), f::WriteLimits(), f::Work());
    Require(
        result.status == ic::PlyStatus::kInvalidConfiguration && !result.output,
        "unknown encoding before writer work");
  }
  auto options = f::Options(ic::PlyEncoding::kAscii);
  options.rewrite_policy = ic::PlyRewritePolicy::kInvalid;
  Require(ic::WritePly(read.document->Mesh(), *read.document, options,
                       f::WriteLimits(), f::Work())
                  .status == ic::PlyStatus::kInvalidConfiguration,
          "explicit lexical rewrite required");
  for (int change = 0; change < 10; ++change) {
    const auto mesh = Modified(*read.document, context, change);
    const auto result =
        ic::WritePly(mesh, *read.document, f::Options(ic::PlyEncoding::kAscii),
                     f::WriteLimits(), f::Work());
    const auto expected = (change == 4 || change == 5)
                              ? ic::PlyStatus::kUnsupportedNumericConversion
                              : ic::PlyStatus::kUnsupportedBinding;
    Require(result.status == expected && !result.output,
            "exact complete schema binding and narrowing refusal");
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported,
            "known source-selected profile argument only");
    RoundTrips(unsupported);
    WireRefusals();
    if (!unsupported) {
      LiteralTopologyAndGrammar();
      BinaryBoundary();
      NumericFidelity();
      WriterRefusals();
    }
    std::cout << "PLY literal schema, wire and numeric checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
