// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#if defined(_MSC_VER) && defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "stl_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::stl_fixture;
using f::Require;
constexpr std::size_t kWorkers = 4;
constexpr std::size_t kContextBytes = 64 * f::kPhysicalLimit;
constexpr std::string_view kUnsupportedOption = "--unsupported-profile";

void CheckDelegation(const ic::StlReadResult& result) {
  Require(result.status == ic::StlStatus::kGeometryFailure && !result.document,
          "valid source produces a genuine delegated profile refusal");
  Require(result.diagnostic.stage == ic::StlStage::kMaterialization &&
              result.diagnostic.geometry_status ==
                  geo::MaterializationStatus::kUnsupportedAllocationProfile,
          "actual Geometry allocation-profile diagnostic retained");
}

void RoundTrips(bool unsupported) {
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  for (const auto encoding :
       {ic::StlEncoding::kAscii, ic::StlEncoding::kBinary}) {
    for (bool empty : {false, true}) {
      auto binary = f::Binary(empty);
      const auto original = encoding == ic::StlEncoding::kAscii
                                ? f::Bytes(empty ? f::kEmptyAscii : f::kAscii)
                                : std::span<const std::byte>(binary);
      auto result =
          ic::ReadStl(original, encoding, f::ReadLimits(), f::Work(), context);
      Require(result.execution.codec.launched_workers == 0 &&
                  result.execution.codec.joined_workers == 0,
              "serial codec counters are honest");
      Require(result.execution.materialization.launched_workers ==
                  result.execution.materialization.joined_workers,
              "all actual materialization workers joined");
      if (unsupported && !empty) {
        CheckDelegation(result);
        Require(context.ActiveTrackedPayload() == 0,
                "unsupported read leaves no payload");
        continue;
      }
      Require(result.status == ic::StlStatus::kSuccess && result.document,
              "literal source read succeeds");
      const auto& document = *result.document;
      f::CheckDocument(document, empty);
      Require(document.Encoding() == encoding &&
                  std::ranges::equal(document.OriginalBytes(), original),
              "physical source and explicit encoding retained");
      Require(
          document.AsciiName() ==
              (encoding == ic::StlEncoding::kAscii && !empty ? f::kName : ""),
          "ASCII source name retained only in its encoding");
      const auto header = f::Header();
      Require(encoding == ic::StlEncoding::kBinary
                  ? std::ranges::equal(document.BinaryHeader(), header)
                  : document.BinaryHeader().empty(),
              "binary header is opaque and has no ASCII interpretation");
      for (const auto destination :
           {ic::StlEncoding::kAscii, ic::StlEncoding::kBinary}) {
        const auto options = f::Options(document, destination);
        auto written =
            ic::WriteStl(document.Mesh(), options, f::WriteLimits(), f::Work());
        Require(written.status == ic::StlStatus::kSuccess && written.output,
                "explicit same/cross-encoding write succeeds");
        const auto& output = *written.output;
        const auto& facts = output.Facts();
        Require(facts.encoding == destination &&
                    facts.face_normal_attribute == 0 &&
                    facts.source_vertex_count == (empty ? 0 : 6) &&
                    facts.source_face_count == (empty ? 0 : 2) &&
                    facts.corner_expansion_acknowledged &&
                    facts.source_encoding == encoding,
                "actual serialization conversion facts");
        Require(facts.metadata_policy ==
                    (destination == encoding
                         ? ic::StlMetadataPolicy::kRetainedSourceMetadata
                         : ic::StlMetadataPolicy::kReplacedSourceMetadata),
                "same/cross encoding metadata policy is explicit");
        for (std::size_t i = 0; i < output.CornerSourceVertices().size(); ++i)
          Require(output.CornerSourceVertices()[i] == i,
                  "triangle-soup source vertex correspondence is literal");
        Require(output.CornerSourceVertices().size() == (empty ? 0 : 6),
                "complete emitted corner map");
        if (destination == ic::StlEncoding::kBinary && !empty)
          f::CheckBinary(output.Bytes(), options.binary_header);
        auto reloaded = ic::ReadStl(output.Bytes(), destination,
                                    f::ReadLimits(), f::Work(), context);
        Require(reloaded.status == ic::StlStatus::kSuccess && reloaded.document,
                "actual serialization independently reloads");
        f::CheckDocument(*reloaded.document, empty);
      }
    }
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "all round-trip owners release their payload and workers");
}

void RejectedWire() {
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  std::size_t case_number = 0;
  const auto no_document = [&](std::span<const std::byte> bytes,
                               ic::StlEncoding encoding, ic::StlStatus status,
                               ic::StlDiagnosticCode code) {
    const auto result =
        ic::ReadStl(bytes, encoding, f::ReadLimits(), f::Work(), context);
    ++case_number;
    Require(result.status == status && result.diagnostic.code == code &&
                !result.document,
            "independent wire case " + std::to_string(case_number) +
                " expected status/code " +
                std::to_string(static_cast<int>(status)) + "/" +
                std::to_string(static_cast<int>(code)) + " got " +
                std::to_string(static_cast<int>(result.status)) + "/" +
                std::to_string(static_cast<int>(result.diagnostic.code)));
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "wire refusal releases all work");
  };
  auto binary = f::Binary();
  for (const auto size :
       {std::size_t{0}, f::kRecordOffset - 1, binary.size() - 1})
    no_document(std::span<const std::byte>(binary).first(size),
                ic::StlEncoding::kBinary, ic::StlStatus::kMalformed,
                ic::StlDiagnosticCode::kBinaryExtent);
  binary.push_back(std::byte{0});
  no_document(binary, ic::StlEncoding::kBinary,
              ic::StlStatus::kUnsupportedVariant,
              ic::StlDiagnosticCode::kTrailingBytes);
  binary = f::Binary();
  binary[f::kRecordOffset + f::kAttributeOffset] = std::byte{1};
  no_document(binary, ic::StlEncoding::kBinary,
              ic::StlStatus::kUnsupportedVariant,
              ic::StlDiagnosticCode::kAttributeWord);
  binary = f::Binary();
  constexpr std::uint32_t kPositiveInfinityBits = 0x7f800000;
  f::PutWord(binary, f::kRecordOffset, kPositiveInfinityBits);
  no_document(binary, ic::StlEncoding::kBinary, ic::StlStatus::kMalformed,
              ic::StlDiagnosticCode::kNonfinite);
  const std::array<std::string, 3> bad_syntax{
      f::Replace(std::string(f::kAscii), "outer loop", "OUTER loop"),
      f::Replace(std::string(f::kAscii), "endsolid original part",
                 "endsolid other"),
      f::Replace(std::string(f::kAscii), "endloop", "vertex 1 2 3\nendloop")};
  for (const auto& text : bad_syntax)
    no_document(f::Bytes(text), ic::StlEncoding::kAscii,
                ic::StlStatus::kMalformed, ic::StlDiagnosticCode::kSyntax);
  for (const auto tail :
       {"solid second\nendsolid second\n", "# unexpected comment\n"}) {
    const auto text = std::string(f::kAscii) + tail;
    no_document(f::Bytes(text), ic::StlEncoding::kAscii,
                ic::StlStatus::kUnsupportedVariant,
                ic::StlDiagnosticCode::kTrailingBytes);
  }
  for (const auto token : {"nan", "inf", "0x1p0", "--1"}) {
    const auto text = f::Replace(std::string(f::kAscii), "+0", token);
    no_document(f::Bytes(text), ic::StlEncoding::kAscii,
                ic::StlStatus::kMalformed,
                ic::StlDiagnosticCode::kNumericSyntax);
  }
  for (const auto token : {"1e9999", "1e-9999"}) {
    const auto text = f::Replace(std::string(f::kAscii), "+0", token);
    no_document(f::Bytes(text), ic::StlEncoding::kAscii,
                ic::StlStatus::kMalformed,
                ic::StlDiagnosticCode::kNumericRange);
  }
}

geo::MaterializedMesh MakeMesh(
    const geo::ExecutionContext& context, std::span<const double> positions,
    std::span<const geo::index_t> offsets,
    std::span<const geo::index_t> corners,
    std::span<const geo::AttributeMaterializationInput> attributes) {
  const geo::MeshMaterializationInput input{
      positions, geo::PolygonTopologyInput{offsets, corners}, attributes};
  auto result =
      geo::MaterializeMesh(input, f::MeshLimits(), f::Work().geometry, context);
  Require(result.status == geo::MaterializationStatus::kSuccess && result.mesh,
          "literal writer source is genuinely materialized");
  return *result.mesh;
}

void WriterControls() {
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  auto imported = ic::ReadStl(f::Bytes(f::kAscii), ic::StlEncoding::kAscii,
                              f::ReadLimits(), f::Work(), context);
  Require(imported.document.has_value(), "writer control has real source");
  const auto& document = *imported.document;
  auto options = f::Options(document, ic::StlEncoding::kBinary);
  options.acknowledge_corner_expansion = false;
  auto refused =
      ic::WriteStl(document.Mesh(), options, f::WriteLimits(), f::Work());
  Require(
      !refused.output && refused.diagnostic.code ==
                             ic::StlDiagnosticCode::kExpansionAcknowledgment,
      "shared vertex identity loss needs explicit acknowledgment");
  options = f::Options(document, ic::StlEncoding::kBinary);
  options.binary_header = options.binary_header.first(f::kHeaderBytes - 1);
  refused = ic::WriteStl(document.Mesh(), options, f::WriteLimits(), f::Work());
  Require(
      !refused.output &&
          refused.diagnostic.code == ic::StlDiagnosticCode::kMetadata,
      "binary metadata is exactly 80 bytes, never truncated/padded implicitly");
  const std::array<double, 12> positions{-0., 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
  const std::array<geo::index_t, 2> quad_offsets{0, 4};
  const std::array<geo::index_t, 4> quad_corners{0, 1, 3, 2};
  const std::array<double, 6> normal_values{0, 0, -2, 0, 0, 0};
  geo::AttributeMaterializationInput normal;
  normal.domain = geo::AttributeDomain::face;
  normal.name = "facet_normal";
  normal.semantic = "normal";
  normal.components = 3;
  normal.values = std::span(normal_values).first(3);
  auto quad = MakeMesh(context, positions, quad_offsets, quad_corners,
                       std::span(&normal, 1));
  options = f::Options(document, ic::StlEncoding::kBinary);
  options.source_document = nullptr;
  refused = ic::WriteStl(quad, options, f::WriteLimits(), f::Work());
  Require(!refused.output &&
              refused.diagnostic.code == ic::StlDiagnosticCode::kNotTriangles,
          "quad refuses output rather than implicit triangulation");
  const std::array<geo::index_t, 3> offsets{0, 3, 6};
  const std::array<geo::index_t, 6> corners{0, 1, 2, 0, 1, 2};
  auto missing =
      MakeMesh(context, std::span(positions).first(9), offsets, corners, {});
  refused = ic::WriteStl(missing, options, f::WriteLimits(), f::Work());
  Require(!refused.output && refused.diagnostic.code ==
                                 ic::StlDiagnosticCode::kNormalSelection,
          "missing normals are not synthesized");
  normal.values = std::span(normal_values);
  auto indexed = MakeMesh(context, std::span(positions).first(9), offsets,
                          corners, std::span(&normal, 1));
  auto written = ic::WriteStl(indexed, options, f::WriteLimits(), f::Work());
  Require(
      written.output && written.output->Facts().source_vertex_count == 3 &&
          written.output->Facts().source_face_count == 2 &&
          std::ranges::equal(written.output->CornerSourceVertices(), corners),
      "indexed triangle export records actual source corner expansion");
  f::CheckBinary(written.output->Bytes(), options.binary_header);
  const std::array<double, 6> uv_values{0, 0, 1, 0, 0, 1};
  auto uv = normal;
  uv.domain = geo::AttributeDomain::vertex;
  uv.name = "uv0";
  uv.semantic = "texcoord";
  uv.components = 2;
  uv.values = std::span(uv_values);
  const std::array attributes{normal, uv};
  auto textured = MakeMesh(context, std::span(positions).first(9), offsets,
                           corners, attributes);
  refused = ic::WriteStl(textured, options, f::WriteLimits(), f::Work());
  Require(!refused.output && refused.diagnostic.code ==
                                 ic::StlDiagnosticCode::kUnsupportedAttribute,
          "unrepresented UV rows are not silently dropped");
  const auto text =
      f::Replace(std::string(f::kAscii), "vertex 1 0 0", "vertex 0.1 0 0");
  auto precise = ic::ReadStl(f::Bytes(text), ic::StlEncoding::kAscii,
                             f::ReadLimits(), f::Work(), context);
  Require(precise.document.has_value(), "finite binary64 decimal admitted");
  options = f::Options(*precise.document, ic::StlEncoding::kBinary);
  refused = ic::WriteStl(precise.document->Mesh(), options, f::WriteLimits(),
                         f::Work());
  Require(
      refused.status == ic::StlStatus::kUnsupportedNumericConversion &&
          !refused.output &&
          refused.diagnostic.code == ic::StlDiagnosticCode::kNumericConversion,
      "binary32 output cannot silently quantize ordinary binary64 decimal");
  options = f::Options(*precise.document, ic::StlEncoding::kAscii);
  written = ic::WriteStl(precise.document->Mesh(), options, f::WriteLimits(),
                         f::Work());
  Require(written.output.has_value(), "binary64 ASCII output remains useful");
  auto reloaded = ic::ReadStl(written.output->Bytes(), ic::StlEncoding::kAscii,
                              f::ReadLimits(), f::Work(), context);
  Require(reloaded.document.has_value(), "full-digit ASCII output reloads");
  f::SameBits(precise.document->Mesh().View().positions[3],
              reloaded.document->Mesh().View().positions[3]);
}

void ReversedAndDegenerateTriangles() {
  constexpr std::string_view kSource =
      "solid reversed and degenerate\n"
      "facet normal 1 0 0\nouter loop\n"
      "vertex 0 1 0\nvertex 1 0 0\nvertex -0 0 0\n"
      "endloop\nendfacet\n"
      "facet normal 0 0 0\nouter loop\n"
      "vertex 2 0 0\nvertex 2 0 0\nvertex 2 0 0\n"
      "endloop\nendfacet\nendsolid reversed and degenerate\n";
  constexpr std::array<double, 18> kPositions{0, 1, 0, 1, 0, 0, -0., 0, 0,
                                              2, 0, 0, 2, 0, 0, 2,   0, 0};
  constexpr std::array<geo::index_t, 3> kOffsets{0, 3, 6};
  constexpr std::array<geo::index_t, 6> kCorners{0, 1, 2, 3, 4, 5};
  constexpr std::array<double, 6> kNormals{1, 0, 0, 0, 0, 0};
  const auto check = [&](const ic::StlDocument& document) {
    const auto view = document.Mesh().View();
    Require(document.Inspection().IsStorageValid() &&
                view.positions.size() == kPositions.size() &&
                std::ranges::equal(view.face_offsets, kOffsets) &&
                std::ranges::equal(view.corner_vertices, kCorners),
            "finite degeneracy and reversed winding retain all distinct "
            "triangle rows");
    for (std::size_t i = 0; i < kPositions.size(); ++i)
      f::SameBits(view.positions[i], kPositions[i]);
    Require(view.attributes.size() == 1,
            "reversed and degenerate triangles retain exactly one raw normal "
            "channel");
    const auto values =
        std::get<std::span<const double>>(view.attributes[0].values);
    Require(
        std::ranges::equal(values, kNormals),
        "raw finite normals on reversed/degenerate facets are not regenerated");
  };
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  {
    const auto source = ic::ReadStl(f::Bytes(kSource), ic::StlEncoding::kAscii,
                                    f::ReadLimits(), f::Work(), context);
    Require(source.document.has_value(),
            "finite reversed/degenerate source admitted");
    check(*source.document);
    for (const auto encoding :
         {ic::StlEncoding::kAscii, ic::StlEncoding::kBinary}) {
      const auto written = ic::WriteStl(source.document->Mesh(),
                                        f::Options(*source.document, encoding),
                                        f::WriteLimits(), f::Work());
      Require(written.output &&
                  std::ranges::equal(written.output->CornerSourceVertices(),
                                     kCorners),
              "serialization neither welds coincident degenerate rows nor "
              "flips winding");
      const auto reload = ic::ReadStl(written.output->Bytes(), encoding,
                                      f::ReadLimits(), f::Work(), context);
      Require(reload.document.has_value(),
              "actual reversed/degenerate output reloads");
      check(*reload.document);
    }
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "reversed/degenerate owners release all work");
}

void NumericFidelity(bool flush_subnormals, unsigned rounding_mode = 0) {
#if defined(_MSC_VER) && defined(_M_X64)
  struct RestoreControl {
    unsigned original = _mm_getcsr();
    ~RestoreControl() { _mm_setcsr(original); }
  } restore;
  constexpr unsigned kFlushAndDenormalsAreZero = 0x8040;
  if (flush_subnormals)
    _mm_setcsr((restore.original & ~_MM_ROUND_MASK) |
               kFlushAndDenormalsAreZero | rounding_mode);
  constexpr unsigned kCallerModeBits =
      _MM_ROUND_MASK | kFlushAndDenormalsAreZero;
  const unsigned selected_modes = _mm_getcsr() & kCallerModeBits;
  const auto check_modes = [&] {
    Require((_mm_getcsr() & kCallerModeBits) == selected_modes,
            "actual codec preserves caller rounding/FTZ/DAZ before fixture "
            "cleanup");
  };
#else
  Require(!flush_subnormals && rounding_mode == 0,
          "platform-specific flush fixture selected explicitly");
  const auto check_modes = [] {};
#endif
  // These independent binary64 encodings represent exactly widened binary32
  // minimum subnormals, largest finite value, and negative zero. No floating
  // conversion constructs the expected values under the altered caller mode.
  constexpr std::uint32_t kMinimumSubnormal = 1;
  constexpr std::uint32_t kNegativeMinimumSubnormal = 0x80000001;
  constexpr std::uint32_t kMaximumFinite = 0x7f7fffff;
  constexpr std::uint64_t kWidenedMinimum = 0x36a0000000000000;
  constexpr std::uint64_t kWidenedNegativeMinimum = 0xb6a0000000000000;
  constexpr std::uint64_t kWidenedMaximum = 0x47efffffe0000000;
  constexpr std::uint64_t kWidenedNegativeZero = 0x8000000000000000;
  auto wire = f::Binary();
  f::PutWord(wire, f::kRecordOffset, f::kNegativeZeroBits);
  constexpr std::size_t kPositionOffset =
      f::kRecordOffset + f::kNormalComponents * sizeof(std::uint32_t);
  f::PutWord(wire, kPositionOffset, kMinimumSubnormal);
  f::PutWord(wire, kPositionOffset + sizeof(std::uint32_t), kMaximumFinite);
  f::PutWord(wire, kPositionOffset + 2 * sizeof(std::uint32_t),
             kNegativeMinimumSubnormal);
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  {
    auto read = ic::ReadStl(wire, ic::StlEncoding::kBinary, f::ReadLimits(),
                            f::Work(), context);
    check_modes();
    Require(read.document.has_value(), "finite boundary wire is admitted");
    const auto check = [&](const ic::StlDocument& document) {
      const auto view = document.Mesh().View();
      const std::array<std::uint64_t, 3> expected{
          kWidenedMinimum, kWidenedMaximum, kWidenedNegativeMinimum};
      for (std::size_t i = 0; i < expected.size(); ++i)
        Require(std::bit_cast<std::uint64_t>(view.positions[i]) == expected[i],
                "binary32 position bits survive caller subnormal modes");
      const auto normals =
          std::get<std::span<const double>>(view.attributes[0].values);
      Require(std::bit_cast<std::uint64_t>(normals[0]) == kWidenedNegativeZero,
              "normal negative zero sign is retained");
    };
    check(*read.document);
    for (const auto encoding :
         {ic::StlEncoding::kAscii, ic::StlEncoding::kBinary}) {
      auto written = ic::WriteStl(read.document->Mesh(),
                                  f::Options(*read.document, encoding),
                                  f::WriteLimits(), f::Work());
      check_modes();
      Require(written.output.has_value(),
              "exact boundary serialization succeeds");
      if (encoding == ic::StlEncoding::kBinary)
        Require(std::ranges::equal(written.output->Bytes(), wire),
                "same-encoding boundary output equals independent packed wire");
      auto reload = ic::ReadStl(written.output->Bytes(), encoding,
                                f::ReadLimits(), f::Work(), context);
      check_modes();
      Require(reload.document.has_value(), "boundary output actually reloads");
      check(*reload.document);
    }
    const std::array<float, 6> float_normals{
        std::bit_cast<float>(kMinimumSubnormal),
        std::bit_cast<float>(kNegativeMinimumSubnormal),
        -0.0f,
        0,
        0,
        0};
    auto attribute = geo::AttributeMaterializationInput{};
    attribute.domain = geo::AttributeDomain::face;
    attribute.name = "facet_normal";
    attribute.semantic = "normal";
    attribute.components = 3;
    attribute.values = std::span(float_normals);
    const auto view = read.document->Mesh().View();
    auto mesh = MakeMesh(context, view.positions, view.face_offsets,
                         view.corner_vertices, std::span(&attribute, 1));
    auto options = f::Options(*read.document, ic::StlEncoding::kBinary);
    options.source_document = nullptr;
    auto written = ic::WriteStl(mesh, options, f::WriteLimits(), f::Work());
    check_modes();
    Require(written.output.has_value(),
            "canonical float32 normal export succeeds");
    const std::array<std::uint32_t, 3> expected{
        kMinimumSubnormal, kNegativeMinimumSubnormal, f::kNegativeZeroBits};
    for (std::size_t i = 0; i < expected.size(); ++i)
      Require(
          f::Word(written.output->Bytes(),
                  f::kRecordOffset + i * sizeof(std::uint32_t)) == expected[i],
          "float32 face-normal bits survive exact export");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "numeric boundary owners release all tracked work");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported = argc == 2 && argv[1] == kUnsupportedOption;
    Require(argc == 1 || unsupported, "unknown STL fixture argument");
    RejectedWire();
    RoundTrips(unsupported);
    if (!unsupported) {
      WriterControls();
      ReversedAndDegenerateTriangles();
      NumericFidelity(false);
#if defined(_MSC_VER) && defined(_M_X64)
      const unsigned original_control = _mm_getcsr();
      for (const unsigned rounding : {_MM_ROUND_NEAREST, _MM_ROUND_DOWN,
                                      _MM_ROUND_UP, _MM_ROUND_TOWARD_ZERO}) {
        NumericFidelity(true, rounding);
        Require(_mm_getcsr() == original_control,
                "fixture restores exact original caller floating-point state");
      }
#endif
    }
    std::cout << "STL literal encoding, no-weld, metadata, normals and "
                 "conversion fixtures passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
