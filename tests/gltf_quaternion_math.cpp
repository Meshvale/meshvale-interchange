// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#if defined(_MSC_VER) && defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "gltf_allocation.h"
#include "gltf_numerical_profile.h"
#include "gltf_quaternion.h"

namespace {
namespace mv = meshvale::interchange;
namespace detail = mv::gltf_detail;
namespace geo = meshvale::geometry;
using Encoding = mv::GltfQuaternionEncoding;
using Role = mv::GltfQuaternionRole;
using Rule = mv::GltfQuaternionDiagnosticRule;
using Decision = detail::QuaternionDecision;
constexpr std::size_t kMiB = 1024 * 1024;
constexpr std::size_t kRootBytes = 8 * kMiB;
constexpr std::size_t kRequestBytes = kMiB;
constexpr std::size_t kTestWorkers = 2;
// A bounded private test configuration, not a public preset or measured policy.
constexpr mv::GltfDeformationWorkPolicy kWork{256, 64, 32, 256, 64, 32, 2, 16};
constexpr std::array<std::uint32_t, 4> kUnit{0, 0, 0, 0x3f800000U};
constexpr std::array<std::uint32_t, 4> kZero{};
struct Case {
  std::string_view id;
  Encoding encoding;
  Role role;
  std::array<std::uint32_t, 4> words;
  std::array<std::string_view, 4> tokens;
  Decision decision;
  std::optional<Rule> rule;
  bool absent;
  bool fallback;
};
// Exact source literals and independently derived rational classifications.
// Projected node words here are mathematical test inputs; this executable
// creates no asset/read/proof authority and does not replace the reader.
constexpr std::array<Case, 55> kCases{{
    {"baseline/axis-unit",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x00000000U, 0x00000000U, 0x00000000U, 0x3f800000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/unit-four-halves",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x3f000000U, 0x3f000000U, 0x3f000000U, 0x3f000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/one-half-next-up",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x3f000000U, 0x3f000000U, 0x3f000000U, 0x3f000001U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/one-half-next-down",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x3f000000U, 0x3f000000U, 0x3f000000U, 0x3effffffU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/decimal-json-0.6-0.8-projected-f32",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x3f19999aU, 0x3f4ccccdU, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/signed-zero-negative-unit",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x80000000U, 0x80000000U, 0x80000000U, 0xbf800000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/subnormal-plus-unit",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x00000001U, 0x00000000U, 0x00000000U, 0x3f800000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/zero-value-or-legal-zero-tangent",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"baseline-zero-as-tangent",
     Encoding::kFloat32,
     Role::kCubicTangent,
     {0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/nonfinite-payload",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x7f800001U, 0x00000000U, 0x00000000U, 0x3f800000U},
     {"", "", "", ""},
     Decision::kNonfinite,
     Rule::kAnimationNonfinite,
     false,
     false},
    {"baseline/snorm8-min-axis",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x00000080U, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/snorm8-minus127-axis",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x00000081U, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/snorm8-nearest-even-four-halves",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x00000040U, 0x00000040U, 0x00000040U, 0x00000040U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"baseline/snorm16-min-axis",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x00008000U, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/snorm16-minus32767-axis",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x00008001U, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/snorm16-nearest-even-four-halves",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x00004000U, 0x00004000U, 0x00004000U, 0x00004000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"baseline/unorm8-unit-axis",
     Encoding::kUnorm8,
     Role::kAnimationValue,
     {0x00000000U, 0x00000000U, 0x00000000U, 0x000000ffU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/unorm8-nearest-even-four-halves",
     Encoding::kUnorm8,
     Role::kAnimationValue,
     {0x00000080U, 0x00000080U, 0x00000080U, 0x00000080U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"baseline/unorm16-unit-axis",
     Encoding::kUnorm16,
     Role::kAnimationValue,
     {0x00000000U, 0x00000000U, 0x00000000U, 0x0000ffffU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"baseline/unorm16-nearest-even-four-halves",
     Encoding::kUnorm16,
     Role::kAnimationValue,
     {0x00008000U, 0x00008000U, 0x00008000U, 0x00008000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"snorm8/upper-exact",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x00000040U, 0x00000040U, 0x00000040U, 0x00000040U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"snorm8/upper-inside",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x00000040U, 0x00000040U, 0x00000040U, 0x0000003fU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"snorm8/upper-outside",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x00000040U, 0x00000040U, 0x00000040U, 0x00000041U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"snorm8/lower-exact",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x0000007dU, 0x00000002U, 0x00000007U, 0x0000000eU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"snorm8/lower-outside",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x0000007cU, 0x00000002U, 0x00000007U, 0x0000000eU},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"snorm8/negative-q",
     Encoding::kSnorm8,
     Role::kAnimationValue,
     {0x000000c0U, 0x000000c0U, 0x000000c0U, 0x000000c0U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"unorm8/upper-exact",
     Encoding::kUnorm8,
     Role::kAnimationValue,
     {0x00000080U, 0x00000080U, 0x00000080U, 0x00000080U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"unorm8/upper-inside",
     Encoding::kUnorm8,
     Role::kAnimationValue,
     {0x00000080U, 0x00000080U, 0x00000080U, 0x0000007fU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"unorm8/upper-outside",
     Encoding::kUnorm8,
     Role::kAnimationValue,
     {0x00000080U, 0x00000080U, 0x00000080U, 0x00000081U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"unorm8/lower-exact",
     Encoding::kUnorm8,
     Role::kAnimationValue,
     {0x000000fdU, 0x00000000U, 0x00000008U, 0x00000015U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"unorm8/lower-outside",
     Encoding::kUnorm8,
     Role::kAnimationValue,
     {0x000000fcU, 0x00000000U, 0x00000008U, 0x00000015U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"snorm16/upper-exact",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x00004000U, 0x00004000U, 0x00004000U, 0x00004000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"snorm16/upper-inside",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x00004000U, 0x00004000U, 0x00004000U, 0x00003fffU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"snorm16/upper-outside",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x00004000U, 0x00004000U, 0x00004000U, 0x00004001U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"snorm16/lower-exact",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x00007ffdU, 0x00000000U, 0x0000002dU, 0x000000fcU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"snorm16/lower-outside",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x00007ffcU, 0x00000000U, 0x0000002dU, 0x000000fcU},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"snorm16/negative-q",
     Encoding::kSnorm16,
     Role::kAnimationValue,
     {0x0000c000U, 0x0000c000U, 0x0000c000U, 0x0000c000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"unorm16/upper-exact",
     Encoding::kUnorm16,
     Role::kAnimationValue,
     {0x00008000U, 0x00008000U, 0x00008000U, 0x00008000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"unorm16/upper-inside",
     Encoding::kUnorm16,
     Role::kAnimationValue,
     {0x00008000U, 0x00008000U, 0x00008000U, 0x00007fffU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"unorm16/upper-outside",
     Encoding::kUnorm16,
     Role::kAnimationValue,
     {0x00008000U, 0x00008000U, 0x00008000U, 0x00008001U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"unorm16/lower-exact",
     Encoding::kUnorm16,
     Role::kAnimationValue,
     {0x0000fffdU, 0x00000004U, 0x0000003cU, 0x00000165U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"unorm16/lower-outside",
     Encoding::kUnorm16,
     Role::kAnimationValue,
     {0x0000fffcU, 0x00000004U, 0x0000003cU, 0x00000165U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFilterBandNonunit,
     false,
     false},
    {"float32/upper-exact",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x3f800000U, 0x39800000U, 0x39800000U, 0x33800000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"float32/upper-last-word--1",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x3f800000U, 0x39800000U, 0x39800000U, 0x337fffffU},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"float32/upper-last-word-1",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x3f800000U, 0x39800000U, 0x39800000U, 0x33800001U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kExactBandNonunit,
     false,
     true},
    {"float32/lower-exact",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x3f7ffffeU, 0x33800000U, 0x39353000U, 0x399cb800U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"float32/largest-finite-tangent",
     Encoding::kFloat32,
     Role::kCubicTangent,
     {0x7f7fffffU, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"float32/largest-finite-value",
     Encoding::kFloat32,
     Role::kAnimationValue,
     {0x7f7fffffU, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kNonunit,
     Rule::kFloatMagnitudeNonunit,
     false,
     false},
    {"json/upper-exact",
     Encoding::kJsonDecimal,
     Role::kAnimationValue,
     {0x3f000008U, 0x3f000008U, 0x3f000008U, 0x3f000008U},
     {"0.5000005", "0.5000005", "0.5000005", "0.5000005"},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"json/upper-inside",
     Encoding::kJsonDecimal,
     Role::kAnimationValue,
     {0x3f000008U, 0x3f000008U, 0x3f000008U, 0x3f000008U},
     {"0.5000005", "0.5000005", "0.5000005",
      "0.500000499999999999999999999999"},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"json/upper-outside",
     Encoding::kJsonDecimal,
     Role::kAnimationValue,
     {0x3f000008U, 0x3f000008U, 0x3f000008U, 0x3f000008U},
     {"0.5000005", "0.5000005", "0.5000005",
      "0.500000500000000000000000000001"},
     Decision::kNonunit,
     Rule::kExactBandNonunit,
     false,
     true},
    {"json/lower-exact",
     Encoding::kJsonDecimal,
     Role::kAnimationValue,
     {0x3f7fffdeU, 0x358637bdU, 0x39e51d26U, 0x3ab04ab6U},
     {"0.999998", "0.000001", "0.000437", "0.001345"},
     Decision::kAdmitted,
     std::nullopt,
     false,
     true},
    {"json/0.6-0.8-exact-source",
     Encoding::kJsonDecimal,
     Role::kAnimationValue,
     {0x3f19999aU, 0x80000000U, 0x00000000U, 0x3f4ccccdU},
     {"0.6", "-0", "0", "0.8"},
     Decision::kAdmitted,
     std::nullopt,
     false,
     false},
    {"json/component-out-of-range",
     Encoding::kJsonDecimal,
     Role::kAnimationValue,
     {0x3f800000U, 0x00000000U, 0x00000000U, 0x00000000U},
     {"1.0000000000000000001", "0", "0", "0"},
     Decision::kRange,
     Rule::kNodeExactRange,
     false,
     false},
    {"json/absence",
     Encoding::kJsonDecimal,
     Role::kAnimationValue,
     {0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U},
     {"", "", "", ""},
     Decision::kAdmitted,
     std::nullopt,
     true,
     false},
}};
void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
std::array<mv::GltfRawNumberView, 4> NumberViews(
    const std::array<std::string_view, 4>& tokens) {
  std::array<mv::GltfRawNumberView, 4> views{};
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto kind = tokens[i].find_first_of(".eE") == std::string_view::npos
                          ? mv::GltfJsonNumberKind::kIntegerSpelling
                          : mv::GltfJsonNumberKind::kDecimalSpelling;
    views[i] = {{0, tokens[i].size()}, kind, tokens[i]};
  }
  return views;
}
void SourceCases(detail::QuaternionWorkspace& workspace,
                 const detail::AllocationContext& allocation) {
  const std::array<std::string_view, 4> spellings{"-0", "1", "0.0", "1E-2"};
  const auto typed = NumberViews(spellings);
  Require(typed[0].kind == mv::GltfJsonNumberKind::kIntegerSpelling &&
              typed[1].kind == mv::GltfJsonNumberKind::kIntegerSpelling &&
              typed[2].kind == mv::GltfJsonNumberKind::kDecimalSpelling &&
              typed[3].kind == mv::GltfJsonNumberKind::kDecimalSpelling,
          "literal syntax selects both actual JSON number kinds");
  for (std::size_t i = 0; i < spellings.size(); ++i)
    Require(typed[i].lexeme == spellings[i], "original spelling/sign retained");
  std::uint64_t integer_steps = 0, byte_visits = 0;
  std::size_t predicates = 0, absences = 0, fallbacks = 0;
  for (const auto& value : kCases) {
    mv::GltfDeformationNumericObservation numeric{};
    detail::QuaternionWork work{allocation, kWork};
    if (value.absent) {
      // Absence is a caller decision: do not invent an authored quaternion row.
      ++absences;
      Require(numeric.guard_calls == 0 && numeric.exact_blocks == 0 &&
                  work.integer_steps == 0 && work.byte_visits == 0,
              value.id);
      continue;
    }
    const auto words_before = value.words;
    const auto result = value.encoding == Encoding::kJsonDecimal
                            ? detail::EvaluateNodeQuaternion(
                                  NumberViews(value.tokens), value.words,
                                  workspace, work, numeric)
                            : detail::EvaluateQuaternionWords(
                                  value.words, value.encoding, value.role,
                                  workspace, work, numeric);
    Require(result.decision == value.decision && result.rule == value.rule,
            value.id);
    if (value.encoding == Encoding::kJsonDecimal) {
      Require(work.integer_steps <= detail::kQuaternionNodeMaxIntegerSteps &&
                  work.byte_visits <= detail::QuaternionNodeByteVisitBound(
                                          NumberViews(value.tokens)),
              value.id);
    } else {
      Require(work.integer_steps <= detail::QuaternionWordsIntegerStepBound(
                                        value.encoding, value.role) &&
                  work.byte_visits == 0,
              value.id);
    }
    Require(value.words == words_before && !numeric.refusal, value.id);
    if (value.rule == Rule::kFloatMagnitudeNonunit ||
        value.rule == Rule::kAnimationNonfinite)
      Require(result.component == 0 && numeric.guard_calls == 0, value.id);
    if (value.fallback) {
      ++fallbacks;
      Require(numeric.exact_blocks >= 2, value.id);
    }
    if (numeric.guard_calls) {
      Require(numeric.guard_calls == numeric.established_calls &&
                  numeric.compiled_profile.has_value() &&
                  numeric.exception_masks.required_units == 1 &&
                  numeric.exception_masks.sse2_mask == 63 &&
                  !numeric.exception_masks.x87_mask &&
                  numeric.exception_masks.sample_caller == true,
              value.id);
    }
    if (value.encoding == Encoding::kJsonDecimal &&
        value.decision == Decision::kRange) {
      Require(numeric.guard_calls == 0 && result.component == 0 &&
                  work.byte_visits == value.tokens[0].size(),
              value.id);
    }
    integer_steps += work.integer_steps;
    byte_visits += work.byte_visits;
    ++predicates;
  }
  Require(predicates == 54 && absences == 1 && fallbacks >= 12 &&
              integer_steps > 0 && byte_visits > 0,
          "source inventory/work");
  std::cout << "source-controls=55 predicates=" << predicates
            << " uncalled-absence=" << absences
            << " exact-fallbacks=" << fallbacks
            << " integer-steps=" << integer_steps
            << " lexical-byte-visits=" << byte_visits << '\n';
}
void Roles(detail::QuaternionWorkspace& workspace,
           const detail::AllocationContext& allocation) {
  mv::GltfDeformationNumericObservation numeric{};
  detail::QuaternionWork work{allocation, kWork};
  const auto evaluate = [&](const std::array<std::uint32_t, 4>& words,
                            Role role) {
    const auto before_i = work.integer_steps;
    const auto before_b = work.byte_visits;
    const auto result = detail::EvaluateQuaternionWords(
        words, Encoding::kFloat32, role, workspace, work, numeric);
    Require(work.integer_steps - before_i <=
                    detail::QuaternionWordsIntegerStepBound(Encoding::kFloat32,
                                                            role) &&
                work.byte_visits == before_b,
            "role-specific work bound");
    return result;
  };
  // The same expanded implicit/sparse zero changes admissibility with its use.
  Require(evaluate(kZero, Role::kCubicTangent).decision == Decision::kAdmitted,
          "sparse zero tangent");
  Require(evaluate(kZero, Role::kAnimationValue).decision == Decision::kNonunit,
          "shared zero value: no easiest-use cache");
  const std::array<std::array<std::uint32_t, 4>, 6> cubic{
      kZero, kUnit, kZero, kZero, {0x80000000U, 0, 0, 0xbf800000U}, kZero};
  for (std::size_t row = 0; row < cubic.size(); ++row) {
    const auto role =
        row % 3 == 1 ? Role::kAnimationValue : Role::kCubicTangent;
    Require(evaluate(cubic[row], role).decision == Decision::kAdmitted,
            "finite cubic derivatives, antipodal values");
  }
  Require(
      evaluate(cubic[0], Role::kAnimationValue).decision == Decision::kNonunit,
      "same stream LINEAR row0");
  const std::array<std::uint32_t, 4> signaling_nan{0x7f800001U, 0, 0, 0};
  Require(evaluate(signaling_nan, Role::kCubicTangent).decision ==
              Decision::kNonfinite,
          "nonfinite derivative: bit classification only");
  const std::array<std::uint32_t, 4> nonfinite_after_large{0x7f7fffffU,
                                                           0x7f800001U, 0, 0};
  const auto nonfinite = evaluate(nonfinite_after_large, Role::kAnimationValue);
  Require(nonfinite.decision == Decision::kNonfinite &&
              nonfinite.rule == Rule::kAnimationNonfinite &&
              nonfinite.component == 1,
          "all finite checks precede the magnitude shortcut");
}
void DecimalEnvelope(detail::QuaternionWorkspace& workspace,
                     const detail::AllocationContext& allocation) {
  // Exact upper endpoint with two nonzero components. Adding the tiny third
  // component is strictly outside even though the guarded filter overlaps.
  // This 256-digit canonical coefficient remains binary32-projectable (word7).
  const std::string tiny =
      "0." + std::string(43, '0') + "1" + std::string(254, '0') + "1";
  const std::array<std::string_view, 4> tokens{"0.999999", "0.002", tiny, "0"};
  const std::array<std::uint32_t, 4> projected{0x3f7fffefU, 0x3b03126fU, 7, 0};
  mv::GltfDeformationNumericObservation numeric{};
  detail::QuaternionWork work{allocation, kWork};
  const auto result = detail::EvaluateNodeQuaternion(
      NumberViews(tokens), projected, workspace, work, numeric);
  Require(result.decision == Decision::kNonunit &&
              result.rule == Rule::kExactBandNonunit &&
              numeric.exact_blocks >= 2 && work.byte_visits > tiny.size(),
          "long exact coefficient must not disappear in projection");
  Require(work.integer_steps <= detail::kQuaternionNodeMaxIntegerSteps &&
              work.byte_visits <=
                  detail::QuaternionNodeByteVisitBound(NumberViews(tokens)),
          "long decimal work bound");
  const std::string too_many = "0." + std::string(257, '3');
  const std::array<std::string_view, 4> over{too_many, "0", "0", "1"};
  mv::GltfDeformationNumericObservation refused{};
  detail::QuaternionWork other{allocation, kWork};
  const auto overflow = detail::EvaluateNodeQuaternion(
      NumberViews(over),
      std::array<std::uint32_t, 4>{0x3eaaaaabU, 0, 0, 0x3f800000U}, workspace,
      other, refused);
  Require(overflow.decision == Decision::kRepresentation &&
              overflow.rule == Rule::kRepresentationEnvelope &&
              overflow.component == 0 && refused.guard_calls == 0,
          "representation envelope before unproved filter");
  Require(other.integer_steps <= detail::kQuaternionNodeMaxIntegerSteps &&
              other.byte_visits <=
                  detail::QuaternionNodeByteVisitBound(NumberViews(over)),
          "refused decimal work bound");
}
void DivisionWitnesses(detail::QuaternionWorkspace& workspace,
                       const detail::AllocationContext& allocation) {
  struct Witness {
    Encoding encoding;
    std::uint32_t numerator, denominator;
    std::uint64_t direct, reciprocal;
    Decision decision;
  };
  constexpr std::array<Witness, 4> witnesses{{
      {Encoding::kSnorm8, 17, 127, 0x3fc1224489122449ULL, 0x3fc1224489122448ULL,
       Decision::kNonunit},
      {Encoding::kUnorm8, 33, 255, 0x3fc0909090909091ULL, 0x3fc0909090909090ULL,
       Decision::kNonunit},
      {Encoding::kSnorm16, 257, 32767, 0x3f80102020404081ULL,
       0x3f80102020404080ULL, Decision::kNonunit},
      {Encoding::kUnorm16, 33, 65535, 0x3f40801080108011ULL,
       0x3f40801080108010ULL, Decision::kAdmitted},
  }};
  mv::GltfDeformationNumericObservation numeric{};
  Require(detail::EstablishGltfNumericalProfile(numeric, {}, true),
          "actual caller before witness arithmetic");
  for (const auto& witness : witnesses) {
    volatile double numerator = static_cast<double>(witness.numerator);
    volatile double denominator = static_cast<double>(witness.denominator);
    volatile double direct = numerator / denominator;
    volatile double reciprocal = 1.0 / denominator;
    volatile double multiplied = numerator * reciprocal;
    const double direct_value = direct, multiplied_value = multiplied;
    Require(std::bit_cast<std::uint64_t>(direct_value) == witness.direct &&
                std::bit_cast<std::uint64_t>(multiplied_value) ==
                    witness.reciprocal &&
                witness.direct != witness.reciprocal,
            "actual RN64 division differs from cached reciprocal");
    const std::array<std::uint32_t, 4> words{witness.numerator, 0, 0,
                                             witness.denominator};
    detail::QuaternionWork work{allocation, kWork};
    Require(detail::EvaluateQuaternionWords(words, witness.encoding,
                                            Role::kAnimationValue, workspace,
                                            work, numeric)
                    .decision == witness.decision,
            "normalized division-witness quaternion");
    Require(
        work.integer_steps <= detail::QuaternionWordsIntegerStepBound(
                                  witness.encoding, Role::kAnimationValue) &&
            work.byte_visits == 0,
        "division-witness work bound");
  }
}
void Checkpoints(const geo::ExecutionContext& context) {
  std::stop_source stop;
  detail::AllocationContext stopped(context, stop.get_token());
  stop.request_stop();
  detail::QuaternionWork integer{stopped, kWork};
  integer.integer_steps = kWork.max_relationship_block - 1;
  detail::QuaternionWork lexical{stopped, kWork};
  lexical.byte_visits = kWork.cancellation_block_bytes - 1;
  for (auto* value : {&integer, &lexical}) {
    bool canceled = false;
    try {
      if (value == &integer)
        value->IntegerStep();
      else
        value->ByteVisit();
    } catch (const detail::AdmissionFailure& failure) {
      canceled = failure.Code() == detail::AdmissionCode::kCanceled;
    }
    Require(canceled, "genuine inner checkpoint cancellation");
  }
}
void Overflow(const detail::AllocationContext& allocation) {
  detail::QuaternionWork work{allocation, kWork};
  work.integer_steps = std::numeric_limits<std::uint64_t>::max();
  bool overflow = false;
  try {
    work.IntegerStep();
  } catch (const detail::AdmissionFailure& failure) {
    overflow = failure.Code() == detail::AdmissionCode::kSizeLimit;
  }
  Require(overflow, "checked helper word-step counter");
  work.byte_visits = std::numeric_limits<std::uint64_t>::max();
  overflow = false;
  try {
    work.ByteVisit();
  } catch (const detail::AdmissionFailure& failure) {
    overflow = failure.Code() == detail::AdmissionCode::kSizeLimit;
  }
  Require(overflow, "checked lexical byte-visit counter");
}
void ReciprocalWorkers(const geo::ExecutionContext& context,
                       const detail::AllocationContext& allocation) {
#if defined(_MSC_VER) && defined(_M_X64)
  constexpr unsigned kStatusFlags = 0x3f;
  constexpr unsigned kExceptionMasks = 0x1f80;
  constexpr unsigned kRoundingAndFlush = 0xe040;
  constexpr unsigned kFirstMaskBit = 7;
  constexpr unsigned kExceptionMaskCount = 6;
  constexpr std::uint32_t kRequiredSse2UnitMask = 1;
  constexpr std::uint32_t kAllSse2ExceptionMasks = 63;
  constexpr std::size_t kUnsafeControlModeCount = 5;
  constexpr unsigned kRoundDown = 0x2000;
  constexpr unsigned kRoundUp = 0x4000;
  constexpr unsigned kRoundTowardZero = 0x6000;
  constexpr unsigned kFlushToZero = 0x8000;
  constexpr unsigned kDenormalsAreZero = 0x40;
  constexpr std::string_view kReciprocalWorkerPrefix = "reciprocal-worker=";
  constexpr std::string_view kCallerKey = " caller=";
  constexpr std::string_view kWorkerKey = " worker=";
  constexpr std::string_view kPartitionKey = " partition=";
  constexpr std::string_view kGuardsKey = " guards=";
  constexpr std::string_view kEstablishedKey = " established=";
  constexpr std::string_view kIntegerStepsKey = " integer_steps=";
  constexpr std::string_view kExactBlocksKey = " exact_blocks=";
  constexpr std::string_view kDirectBitsKey = " direct_bits=";
  constexpr std::string_view kRealLeaseKey = " real-lease=";
  // Geometry intentionally leases zero for a single desired slot. This test
  // owns two genuine workers, as required by its public lease contract.
  constexpr std::size_t kReciprocalWorkers = 2;
  constexpr std::array<unsigned, kUnsafeControlModeCount> kUnsafeModes{
      kRoundDown, kRoundUp, kRoundTowardZero, kFlushToZero, kDenormalsAreZero};
  constexpr std::array<std::uint32_t, detail::kQuaternionComponents>
      kDivisionUnit{33, 0, 0, 65535};
  constexpr std::uint64_t kDirectDivisionBits = 0x3f40801080108011ULL;
  struct Restore {
    unsigned saved;
    ~Restore() noexcept { _mm_setcsr(saved); }
  };
  struct ActualWorker {
    std::shared_ptr<detail::QuaternionWorkspace> workspace;
    mv::GltfDeformationNumericObservation observation{};
    detail::QuaternionPredicateResult result;
    std::exception_ptr error;
    std::uint64_t integer_steps = 0, byte_visits = 0, direct_bits = 0;
    unsigned entry = 0, established = 0, after_helper = 0, restored = 0;
    bool distinct_thread = false;
  };
  const auto initial = _mm_getcsr();
  const auto safe =
      (initial & ~(kRoundingAndFlush | kStatusFlags)) | kExceptionMasks;
  const auto caller_id = std::this_thread::get_id();
  const auto baseline = context.ActiveTrackedPayload();
  for (unsigned scenario = 0;
       scenario < kUnsafeModes.size() + kExceptionMaskCount; ++scenario) {
    const auto unsafe =
        scenario < kUnsafeModes.size()
            ? safe | kUnsafeModes[scenario]
            : safe & ~(1U << (kFirstMaskBit + scenario - kUnsafeModes.size()));
    {
      auto lease = geo::TryReserveWorkers(context, kReciprocalWorkers);
      Require(lease.Count() == kReciprocalWorkers,
              "real reciprocal worker lease");
      std::array<ActualWorker, kReciprocalWorkers> actual;
      for (auto& record : actual)
        record.workspace = std::allocate_shared<detail::QuaternionWorkspace>(
            detail::Allocator<detail::QuaternionWorkspace>(allocation));
      Require(context.ActiveTrackedPayload() > baseline,
              "reciprocal scratch charged under original request/ledger");
      unsigned caller_during = 0;
      {
        Restore caller_restore{initial};
        _mm_setcsr(unsafe);
        // Only isolated test workers establish controls. Neither caller state
        // nor an override is passed to the production predicate.
        std::array<std::jthread, kReciprocalWorkers> workers;
        for (std::size_t i = 0; i < workers.size(); ++i) {
          workers[i] = std::jthread([&, i] {
            auto& record = actual[i];
            record.entry = _mm_getcsr();
            {
              Restore worker_restore{record.entry};
              _mm_setcsr(safe);
              record.established = _mm_getcsr();
              try {
                record.distinct_thread =
                    std::this_thread::get_id() != caller_id;
                const auto partition =
                    static_cast<std::uint64_t>(scenario) * kReciprocalWorkers +
                    i;
                detail::QuaternionWork work{allocation, kWork,     0,
                                            0,          partition, false};
                record.result = detail::EvaluateQuaternionWords(
                    kDivisionUnit, Encoding::kUnorm16, Role::kAnimationValue,
                    *record.workspace, work, record.observation);
                record.integer_steps = work.integer_steps;
                record.byte_visits = work.byte_visits;
                // Independent direct division on this same actual worker.
                // The normalized animation predicate necessarily executes its
                // four volatile divisions and filter multiply/sum above.
                volatile double numerator = 33;
                volatile double denominator = 65535;
                volatile double quotient = numerator / denominator;
                record.direct_bits =
                    std::bit_cast<std::uint64_t>(static_cast<double>(quotient));
                record.after_helper = _mm_getcsr();
              } catch (...) {
                record.error = std::current_exception();
              }
            }
            record.restored = _mm_getcsr();
          });
        }
        for (auto& worker : workers) worker.join();
        caller_during = _mm_getcsr();
      }
      for (std::size_t i = 0; i < actual.size(); ++i) {
        const auto& record = actual[i];
        const auto& observed = record.observation;
        if (record.error) std::rethrow_exception(record.error);
        const auto partition =
            static_cast<std::uint64_t>(scenario) * kReciprocalWorkers + i;
        Require(
            record.distinct_thread && caller_during == unsafe &&
                _mm_getcsr() == initial && record.restored == record.entry &&
                record.established == safe &&
                (record.after_helper & ~kStatusFlags) == (safe & ~kStatusFlags),
            "actual worker controls restored; caller stays unsafe until "
            "its own test scope ends");
        Require(
            record.result.decision == Decision::kAdmitted &&
                !record.result.rule && observed.guard_calls == 1 &&
                observed.established_calls == 1 && observed.compiled_profile &&
                !observed.refusal &&
                observed.exception_masks.required_units ==
                    kRequiredSse2UnitMask &&
                observed.exception_masks.sse2_mask == kAllSse2ExceptionMasks &&
                !observed.exception_masks.x87_mask &&
                observed.exception_masks.sample_partition == partition &&
                observed.exception_masks.sample_caller == false &&
                record.integer_steps > 0 &&
                record.integer_steps <=
                    detail::QuaternionWordsIntegerStepBound(
                        Encoding::kUnorm16, Role::kAnimationValue) &&
                record.byte_visits == 0 &&
                record.direct_bits == kDirectDivisionBits &&
                context.ActiveWorkers() == kReciprocalWorkers,
            "actual worker division/filter admission with own guard, "
            "partition/sample and retained real lease");
        std::cout << kReciprocalWorkerPrefix << scenario << ':' << i
                  << kCallerKey << std::hex << unsafe << '/' << caller_during
                  << kWorkerKey << record.entry << '/' << record.established
                  << '/' << record.after_helper << '/' << record.restored
                  << std::dec << kPartitionKey << partition << kGuardsKey
                  << observed.guard_calls << kEstablishedKey
                  << observed.established_calls << kIntegerStepsKey
                  << record.integer_steps << kExactBlocksKey
                  << observed.exact_blocks << kDirectBitsKey << std::hex
                  << record.direct_bits << std::dec << kRealLeaseKey
                  << lease.Count() << '\n';
      }
      lease.Reset();
      Require(context.ActiveWorkers() == 0,
              "both joined worker slots released");
    }
    Require(context.ActiveTrackedPayload() == baseline,
            "each charged pair returns original payload baseline");
  }
  Require(_mm_getcsr() == initial && context.ActiveWorkers() == 0,
          "all reciprocal caller/worker states and joins restored");
#else
  (void)context;
  (void)allocation;
  throw std::runtime_error("reciprocal controls require actual MSVC x64 tuple");
#endif
}

void Workers(const geo::ExecutionContext& context,
             const detail::AllocationContext& allocation) {
  auto lease = geo::TryReserveWorkers(context, kTestWorkers);
  Require(lease.Count() == kTestWorkers, "real worker lease");
  std::array<std::shared_ptr<detail::QuaternionWorkspace>, kTestWorkers> spaces;
  std::array<mv::GltfDeformationNumericObservation, kTestWorkers>
      observations{};
  std::array<std::exception_ptr, kTestWorkers> errors{};
  std::array<detail::QuaternionPredicateResult, kTestWorkers> results{};
  std::array<std::jthread, kTestWorkers> workers;
  for (std::size_t i = 0; i < kTestWorkers; ++i) {
    spaces[i] = std::allocate_shared<detail::QuaternionWorkspace>(
        detail::Allocator<detail::QuaternionWorkspace>(allocation));
    workers[i] = std::jthread([&, i] {
      try {
        detail::QuaternionWork work{allocation, kWork, 0, 0, i, false};
        results[i] = detail::EvaluateQuaternionWords(
            kUnit, Encoding::kFloat32, Role::kAnimationValue, *spaces[i], work,
            observations[i]);
      } catch (...) {
        errors[i] = std::current_exception();
      }
    });
  }
  for (auto& worker : workers) worker.join();
  for (std::size_t i = 0; i < kTestWorkers; ++i) {
    if (errors[i]) std::rethrow_exception(errors[i]);
    Require(results[i].decision == Decision::kAdmitted &&
                observations[i].guard_calls == 1 &&
                observations[i].established_calls == 1 &&
                observations[i].compiled_profile.has_value() &&
                observations[i].exception_masks.required_units == 1 &&
                observations[i].exception_masks.sse2_mask == 63 &&
                !observations[i].exception_masks.x87_mask &&
                observations[i].exception_masks.sample_partition == i &&
                observations[i].exception_masks.sample_caller == false,
            "actual executing worker guard");
  }
  Require(context.ActiveWorkers() == kTestWorkers,
          "lease retained through joins");
  lease.Reset();
  Require(context.ActiveWorkers() == 0, "joined worker release");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const std::string_view argument = argc == 2 ? argv[1] : "";
    const bool expect_supported = argument == "--expect-supported";
    const bool expect_unsupported = argument == "--expect-unsupported";
    Require(
        argc == 1 || (argc == 2 && (expect_supported || expect_unsupported)),
        "unknown math-test expectation");
    mv::GltfDeformationNumericObservation candidate{};
    if (!detail::EstablishGltfNumericalProfile(candidate, {}, true)) {
      Require(!expect_supported && candidate.refusal &&
                  candidate.refusal->reason ==
                      mv::GltfDeformationNumericRefusalReason::
                          kCompiledUnsupported &&
                  !candidate.compiled_profile && candidate.guard_calls == 1 &&
                  candidate.established_calls == 0 &&
                  candidate.exact_blocks == 0,
              "unsupported scope must be a genuine compiled refusal");
      mv::GltfDeformationNumericObservation repeated{};
      Require(!detail::EstablishGltfNumericalProfile(repeated, 7, true) &&
                  repeated.refusal && repeated.refusal->partition == 7,
              "first actual unsupported gate");
      Require(!detail::EstablishGltfNumericalProfile(repeated, 9, true) &&
                  repeated.refusal->partition == 7,
              "later partition must not overwrite first refusal");
      Require(
          !detail::EstablishGltfNumericalProfile(repeated, 2, true) &&
              repeated.refusal->partition == 2 && repeated.refusal->caller &&
              repeated.refusal->reason ==
                  mv::GltfDeformationNumericRefusalReason::
                      kCompiledUnsupported &&
              repeated.guard_calls == 3 && repeated.established_calls == 0 &&
              !repeated.refusal->nearest_even &&
              !repeated.refusal->ftz_disabled &&
              !repeated.refusal->daz_disabled && !repeated.refusal->unit &&
              !repeated.refusal->exception_masks.required_units &&
              !repeated.refusal->exception_masks.sse2_mask &&
              !repeated.refusal->exception_masks.x87_mask &&
              !repeated.refusal->exception_masks.sample_partition &&
              !repeated.refusal->exception_masks.sample_caller &&
              !repeated.exception_masks.required_units &&
              !repeated.exception_masks.sse2_mask &&
              !repeated.exception_masks.x87_mask &&
              !repeated.exception_masks.sample_partition &&
              !repeated.exception_masks.sample_caller,
          "lowest actual partition and unavailable state remain truthful");
      std::cout
          << "compiled-unsupported gate/refusal-order PASS; "
             "55-control math and reciprocal11 worker cohort NOT EXERCISED\n";
      return 0;
    }
    Require(!expect_unsupported,
            "compiled tuple is supported, contrary to expectation");
    std::cout << "compiled-profile=" << *candidate.compiled_profile << '\n';
    geo::ExecutionContext context({kTestWorkers, 1, kRootBytes});
    {
      const detail::AllocationContext root(context, {});
      const auto allocation = root.WithRequestLimit(kRequestBytes);
      auto workspace = std::allocate_shared<detail::QuaternionWorkspace>(
          detail::Allocator<detail::QuaternionWorkspace>(allocation));
      Require(context.ActiveTrackedPayload() > 0, "actual charged workspace");
      std::cout << "workspace-bytes=" << sizeof(detail::QuaternionWorkspace)
                << " alignment=" << alignof(detail::QuaternionWorkspace)
                << " charged-root-active=" << context.ActiveTrackedPayload()
                << '\n';
      SourceCases(*workspace, allocation);
      Roles(*workspace, allocation);
      DecimalEnvelope(*workspace, allocation);
      DivisionWitnesses(*workspace, allocation);
      Checkpoints(context);
      Overflow(allocation);
      Workers(context, allocation);
      ReciprocalWorkers(context, allocation);
    }
    Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
            "all joined workspaces/request owners release original ledger");
    std::cout << "roles/decimal-envelope/division4/checkpoints/overflow/"
                 "leased-workers PASS; "
                 "workers=0 payload=0; production FP controls unchanged; test "
                 "scopes restored\n";
  } catch (const std::exception& error) {
    std::cerr << "quaternion math failure: " << error.what() << '\n';
    return 1;
  }
}
