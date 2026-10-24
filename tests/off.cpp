// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#if defined(_MSC_VER) && defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "off_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::off_fixture;
using f::Require;
constexpr std::uint64_t kContextBytes = 64 * f::kPhysicalLimit;
constexpr std::string_view kUnsupportedOption = "--unsupported-profile";

void RoundTrip(std::string_view text, bool unsupported,
               std::span<const double> positions = f::kPositions,
               std::span<const geo::index_t> offsets = f::kOffsets,
               std::span<const geo::index_t> corners = f::kCorners,
               std::uint64_t edges = f::kOriginalEdges) {
  geo::ExecutionContext context({4, 1, kContextBytes});
  {
    auto read =
        ic::ReadOff(f::Bytes(text), f::ReadLimits(), f::Work(), context);
    Require(read.execution.codec.launched_workers == 0 &&
                read.execution.codec.joined_workers == 0 &&
                read.execution.materialization.launched_workers ==
                    read.execution.materialization.joined_workers,
            "codec is serial and genuine delegated workers join");
    if (unsupported && !positions.empty()) {
      Require(read.status == ic::OffStatus::kGeometryFailure &&
                  !read.document &&
                  read.diagnostic.stage == ic::OffStage::kMaterialization &&
                  read.diagnostic.geometry_status ==
                      geo::MaterializationStatus::kUnsupportedAllocationProfile,
              "nonempty input proves actual unsupported numerical allocation "
              "profile");
    } else {
      Require(read.status == ic::OffStatus::kSuccess && read.document,
              "literal OFF read succeeds");
      f::Check(*read.document, positions, offsets, corners);
      Require(read.document->DeclaredEdgeCount() == edges &&
                  std::ranges::equal(read.document->OriginalBytes(),
                                     f::Bytes(text)),
              "physical source and original edge count retained without "
              "inferring edges");
      auto written =
          ic::WriteOff(read.document->Mesh(), f::Options(&*read.document),
                       f::WriteLimits(), f::Work());
      Require(written.status == ic::OffStatus::kSuccess && written.output,
              "explicit canonical write succeeds");
      f::CheckFacts(written.output->Facts(), positions.size() / 3,
                    offsets.size() - 1, corners.size());
      Require(written.output->Facts().source_declared_edge_count == edges &&
                  f::Text(written.output->Bytes()).find('#') ==
                      std::string_view::npos,
              "canonical rewrite explicitly omits comments but records "
              "metadata provenance");
      auto reload = ic::ReadOff(written.output->Bytes(), f::ReadLimits(),
                                f::Work(), context);
      Require(reload.status == ic::OffStatus::kSuccess && reload.document,
              "actual encoded OFF independently reloads");
      f::Check(*reload.document, positions, offsets, corners);
      Require(reload.document->DeclaredEdgeCount() == 0,
              "canonical edge count is unknown zero");
      Require(
          std::ranges::equal(read.document->OriginalBytes(), f::Bytes(text)),
          "source bytes unchanged after serialization");
    }
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "roundtrip owners and workers all release");
}

void LiteralGeometry(bool unsupported) {
  RoundTrip(f::kWire, unsupported);
  constexpr std::array<geo::index_t, 1> kEmptyOffsets{0};
  RoundTrip("OFF\r\n0 0 99", unsupported, {}, kEmptyOffsets, {}, 99);
  constexpr std::array<double, 6> kPoints{-0., 2, 3, 9, 8, 7};
  RoundTrip("OFF\n2 0 3\n-0 2 3\n9 8 7", unsupported, kPoints, kEmptyOffsets,
            {}, 3);
  const auto huge_edge =
      f::Replace(std::string(f::kWire), "8 3 17", "8 3 18446744073709551615");
  RoundTrip(huge_edge, unsupported, f::kPositions, f::kOffsets, f::kCorners,
            UINT64_MAX);
  constexpr std::string_view kOther =
      "OFF\n5 3 0\n0 0 0\n2 0 0\n1 .5 0\n2 2 1\n0 2 0\n"
      "5 0 1 2 3 4\n4 4 3 1 0\n3 0 0 1\n";
  constexpr std::array<double, 15> kOtherPositions{0, 0, 0, 2, 0, 0, 1, .5,
                                                   0, 2, 2, 1, 0, 2, 0};
  constexpr std::array<geo::index_t, 4> kOtherOffsets{0, 5, 9, 12};
  constexpr std::array<geo::index_t, 12> kOtherCorners{0, 1, 2, 3, 4, 4,
                                                       3, 1, 0, 0, 0, 1};
  RoundTrip(kOther, unsupported, kOtherPositions, kOtherOffsets, kOtherCorners,
            0);
}

void RefusedWire() {
  geo::ExecutionContext context({4, 1, kContextBytes});
  std::size_t ordinal = 0;
  const auto refuse = [&](std::string_view text, ic::OffStatus expected) {
    auto result =
        ic::ReadOff(f::Bytes(text), f::ReadLimits(), f::Work(), context);
    ++ordinal;
    Require(result.status == expected && !result.document &&
                result.diagnostic.code != ic::OffDiagnosticCode::kNone,
            "literal refusal case " + std::to_string(ordinal) +
                " actual status " +
                std::to_string(static_cast<int>(result.status)));
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "refused wire leaves no partial owner/work");
  };
  for (const auto marker :
       {"COFF", "NOFF", "CNOFF", "STOFF", "4OFF", "nOFF", "OFF BINARY"}) {
    const auto text = std::string(marker) + "\n0 0 0\n";
    refuse(text, ic::OffStatus::kUnsupportedVariant);
  }
  for (const auto text :
       {"OFF\n-1 0 0\n", "OFF\n18446744073709551616 0 0\n", "OFF\n1.0 0 0\n",
        "OFF\n1 0 0\n0 0\n", "OFF\n1 1 0\n0 0 0\n3 0 0 1\n"})
    refuse(text, ic::OffStatus::kMalformed);
  refuse("OFF\n1 0 0\n0 0 0 1\n", ic::OffStatus::kUnsupportedVariant);
  refuse(f::Replace(std::string(f::kWire), "3 4 5 6", "3 4 5 6 1 0 0"),
         ic::OffStatus::kUnsupportedVariant);
  refuse("OFF\n1 1 0\n0 0 0\n2 0 0\n", ic::OffStatus::kUnsupportedVariant);
  for (const auto token : {"nan", "inf", "0x1p0", "--1", "1e9999", "1e-9999"})
    refuse(f::Replace(std::string(f::kWire), "99 -0 5",
                      std::string(token) + " -0 5"),
           ic::OffStatus::kMalformed);
  for (const auto text : {"0 0 0\n", "OFF\n0 0", "OFF\n0 0 0\n0 0 0\n",
                          "OFF\n0 0 0\nOFF\n0 0 0\n"}) {
    const auto result =
        ic::ReadOff(f::Bytes(text), f::ReadLimits(), f::Work(), context);
    Require(result.status != ic::OffStatus::kSuccess && !result.document &&
                result.diagnostic.code != ic::OffDiagnosticCode::kNone,
            "missing/truncated/wrapped/concatenated data is never admitted");
  }
  for (const auto bytes : {std::string("\xef\xbb\xbfOFF\n0 0 0\n"),
                           std::string("OFF\n0 0 0\n#\xc3\xa9\n"),
                           std::string("OFF\n0 0 0\n#") + char{0} + "\n"}) {
    const auto result =
        ic::ReadOff(f::Bytes(bytes), f::ReadLimits(), f::Work(), context);
    Require(
        result.status != ic::OffStatus::kSuccess && !result.document,
        "BOM, non-ASCII comment and embedded NUL cannot be silently discarded");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "all wire refusals release actual owners/workers");
}

void WriterControls(bool unsupported) {
  // Empty numerical storage has no numerical profile dependency. An authored
  // empty channel still must not be silently dropped by the codec.
  geo::ExecutionContext context({4, 1, kContextBytes});
  constexpr std::array<geo::index_t, 1> kEmptyOffsets{0};
  for (const auto semantic :
       {"normal", "color", "texcoord", "label", "custom"}) {
    geo::AttributeMaterializationInput attribute;
    attribute.domain = geo::AttributeDomain::vertex;
    attribute.name = "authored empty channel";
    attribute.semantic = semantic;
    attribute.components = 1;
    attribute.values = std::span<const double>{};
    const geo::MeshMaterializationInput input{
        std::span<const double>{}, geo::PolygonTopologyInput{kEmptyOffsets, {}},
        std::span(&attribute, 1)};
    auto source = geo::MaterializeMesh(input, f::MeshLimits(),
                                       f::Work().geometry, context);
    Require(source.mesh.has_value(),
            "authored empty channel genuinely materializes on every profile");
    const auto refused =
        ic::WriteOff(*source.mesh, f::Options(), f::WriteLimits(), f::Work());
    Require(!refused.output &&
                refused.status == ic::OffStatus::kUnsupportedVariant &&
                refused.diagnostic.code ==
                    ic::OffDiagnosticCode::kUnsupportedAttribute,
            "every authored empty channel is explicitly refused");
  }
  if (unsupported) return;
  auto source =
      ic::ReadOff(f::Bytes(f::kWire), f::ReadLimits(), f::Work(), context);
  Require(source.document.has_value(),
          "policy controls use real strong source");
  const std::array<double, 8> labels{0, 1, 2, 3, 4, 5, 6, 7};
  const std::array<geo::MetadataEntryView, 1> metadata{
      {{"author", "original"}}};
  geo::AttributeMaterializationInput authored;
  authored.domain = geo::AttributeDomain::vertex;
  authored.name = "original labels";
  authored.semantic = "label";
  authored.components = 1;
  authored.values = std::span(labels);
  authored.metadata = metadata;
  const geo::MeshMaterializationInput input{
      std::span(f::kPositions),
      geo::PolygonTopologyInput{f::kOffsets, f::kCorners},
      std::span(&authored, 1)};
  const auto with_metadata =
      geo::MaterializeMesh(input, f::MeshLimits(), f::Work().geometry, context);
  Require(with_metadata.mesh.has_value(),
          "populated metadata-bearing channel genuinely materializes");
  const auto refused = ic::WriteOff(*with_metadata.mesh, f::Options(),
                                    f::WriteLimits(), f::Work());
  Require(refused.status == ic::OffStatus::kUnsupportedVariant &&
              !refused.output &&
              refused.diagnostic.code ==
                  ic::OffDiagnosticCode::kUnsupportedAttribute,
          "populated authored rows and metadata cannot be silently discarded");
  for (const auto policy :
       {ic::OffTextPolicy::kInvalid, static_cast<ic::OffTextPolicy>(999)}) {
    auto options = f::Options();
    options.text_policy = policy;
    const auto result = ic::WriteOff(source.document->Mesh(), options,
                                     f::WriteLimits(), f::Work());
    Require(result.status == ic::OffStatus::kInvalidConfiguration &&
                !result.output &&
                result.diagnostic.code == ic::OffDiagnosticCode::kTextPolicy,
            "missing/foreign rewrite policy refuses");
  }
  for (const auto policy : {ic::OffEdgeCountPolicy::kInvalid,
                            static_cast<ic::OffEdgeCountPolicy>(999)}) {
    auto options = f::Options();
    options.edge_count_policy = policy;
    const auto result = ic::WriteOff(source.document->Mesh(), options,
                                     f::WriteLimits(), f::Work());
    Require(
        result.status == ic::OffStatus::kInvalidConfiguration &&
            !result.output &&
            result.diagnostic.code == ic::OffDiagnosticCode::kEdgeCountPolicy,
        "missing/foreign edge policy refuses");
  }
}

void NumericBits(bool flush = false, unsigned rounding = 0) {
#if defined(_MSC_VER) && defined(_M_X64)
  struct Restore {
    unsigned original = _mm_getcsr();
    ~Restore() { _mm_setcsr(original); }
  } restore;
  constexpr unsigned kFlushAndDenormalsAreZero = 0x8040;
  constexpr unsigned kModeBits = _MM_ROUND_MASK | kFlushAndDenormalsAreZero;
  if (flush)
    _mm_setcsr((restore.original & ~_MM_ROUND_MASK) | rounding |
               kFlushAndDenormalsAreZero);
  const unsigned selected = _mm_getcsr() & kModeBits;
  const auto modes = [&] {
    Require((_mm_getcsr() & kModeBits) == selected,
            "actual OFF codec preserves caller modes before cleanup");
  };
#else
  Require(!flush && rounding == 0, "platform mode control is explicit");
  const auto modes = [] {};
#endif
  constexpr std::string_view kNumeric =
      "OFF\n2 0 0\n4.9406564584124654e-324 -4.9406564584124654e-324 -0\n"
      "1.7976931348623157e308 -1.7976931348623157e308 .1\n";
  constexpr std::array<std::uint64_t, 6> kBits{1,
                                               0x8000000000000001,
                                               0x8000000000000000,
                                               0x7fefffffffffffff,
                                               0xffefffffffffffff,
                                               0x3fb999999999999a};
  const auto check = [&](const ic::OffDocument& document) {
    const auto positions = document.Mesh().View().positions;
    Require(positions.size() == kBits.size(), "numeric literal cardinality");
    for (std::size_t i = 0; i < kBits.size(); ++i)
      Require(std::bit_cast<std::uint64_t>(positions[i]) == kBits[i],
              "independent binary64 extrema/subnormal/signedzero/decimal bits");
  };
  geo::ExecutionContext context({4, 1, kContextBytes});
  auto source =
      ic::ReadOff(f::Bytes(kNumeric), f::ReadLimits(), f::Work(), context);
  modes();
  Require(source.document.has_value(), "finite numeric boundaries admitted");
  check(*source.document);
  const auto output = ic::WriteOff(source.document->Mesh(), f::Options(),
                                   f::WriteLimits(), f::Work());
  modes();
  Require(output.output.has_value(), "numeric output succeeds");
  const auto reload =
      ic::ReadOff(output.output->Bytes(), f::ReadLimits(), f::Work(), context);
  modes();
  Require(reload.document.has_value(), "actual numeric output reloads");
  check(*reload.document);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported = argc == 2 && argv[1] == kUnsupportedOption;
    Require(argc == 1 || unsupported, "unknown OFF test argument");
    RefusedWire();
    LiteralGeometry(unsupported);
    WriterControls(unsupported);
    if (!unsupported) {
      NumericBits();
#if defined(_MSC_VER) && defined(_M_X64)
      const unsigned original = _mm_getcsr();
      for (const unsigned rounding : {_MM_ROUND_NEAREST, _MM_ROUND_DOWN,
                                      _MM_ROUND_UP, _MM_ROUND_TOWARD_ZERO}) {
        NumericBits(true, rounding);
        Require(_mm_getcsr() == original,
                "fixture restores exact original caller control");
      }
#endif
    }
    std::cout << "OFF literal polygons, identity, rewrite, refusals and "
                 "numeric fixtures passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
