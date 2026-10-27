// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/surface_write.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_M_X64) || defined(__SSE__) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define MESHVALE_SURFACE_TEST_SSE 1
#endif

#include "gltf_allocation.h"
#include "surface_fixture.h"
#include "surface_numeric.h"
#include "surface_output_internal.h"

namespace {
namespace mv = meshvale::interchange;
namespace detail = mv::surface_detail;
namespace fixture = mv::surface_fixture;
constexpr std::uint64_t kMiB = 1024 * 1024, kOperationBytes = 16 * kMiB,
                        kIntegerBits = 8192;
#if defined(MESHVALE_SURFACE_TEST_SSE)
constexpr std::uint32_t kFtz = UINT32_C(0x8000), kDaz = UINT32_C(0x40),
                        kRounding = UINT32_C(0x6000);
#endif
constexpr std::size_t kWordBits = 64;
void Require(bool value, std::string_view why) {
  if (!value) throw std::runtime_error(std::string(why));
}

// Independent test arithmetic: repeated decimal long division, deliberately
// not the library's charged Natural, dyadic or quantizer implementation.
std::vector<std::uint64_t> DecimalWords(std::string_view decimal) {
  Require(!decimal.empty(), "literal decimal exists");
  std::string digits(decimal);
  std::vector<std::uint64_t> words;
  std::size_t bit = 0;
  while (digits != "0") {
    unsigned carry = 0;
    for (char& digit : digits) {
      Require(digit >= '0' && digit <= '9', "literal decimal digit");
      const unsigned value = carry * 10 + static_cast<unsigned>(digit - '0');
      digit = static_cast<char>('0' + value / 2);
      carry = value % 2;
    }
    if (bit / kWordBits >= words.size()) words.push_back(0);
    if (carry) words[bit / kWordBits] |= UINT64_C(1) << (bit % kWordBits);
    ++bit;
    const auto first = digits.find_first_not_of('0');
    digits = first == std::string::npos ? "0" : digits.substr(first);
  }
  while (!words.empty() && !words.back()) words.pop_back();
  return words;
}
mv::SurfaceDyadicError LiteralError(std::string_view fraction) {
  const auto slash = fraction.find('/');
  auto words = DecimalWords(fraction.substr(0, slash));
  const auto denominator = DecimalWords(slash == std::string_view::npos
                                            ? std::string_view{"1"}
                                            : fraction.substr(slash + 1));
  unsigned ones = 0;
  std::size_t power = 0;
  for (std::size_t i = 0; i < denominator.size(); ++i) {
    ones += std::popcount(denominator[i]);
    if (denominator[i])
      power = i * kWordBits + std::countr_zero(denominator[i]);
  }
  Require(ones == 1, "literal error denominator is dyadic");
  mv::SurfaceDyadicError result;
  if (words.empty()) return result;
  std::int32_t exponent = -static_cast<std::int32_t>(power);
  while (!(words.front() & 1)) {
    for (std::size_t i = 0; i < words.size(); ++i)
      words[i] = (words[i] >> 1) |
                 (i + 1 < words.size() ? words[i + 1] << (kWordBits - 1) : 0);
    while (!words.empty() && !words.back()) words.pop_back();
    ++exponent;
  }
  Require(words.size() <= result.magnitude.size(), "literal magnitude extent");
  std::copy(words.begin(), words.end(), result.magnitude.begin());
  result.used_words = static_cast<std::uint32_t>(words.size());
  result.exponent2 = exponent;
  return result;
}
bool SameError(const mv::SurfaceDyadicError& a,
               const mv::SurfaceDyadicError& b) {
  return a.used_words == b.used_words && a.exponent2 == b.exponent2 &&
         a.magnitude == b.magnitude;
}
void Scalars(const mv::gltf_detail::AllocationContext& allocation) {
  for (const auto& test : fixture::ScalarCases()) {
    const bool refused =
        test.nonfinite || test.outside_f32_finite_range || test.nonzero_to_zero;
    try {
      const auto actual =
          detail::Round32(test.source_f64, mv::SurfaceNarrowing::kBoundedRne32);
      Require(!refused && test.reference_f32 && actual == *test.reference_f32,
              "literal RNE32 word");
      const auto widened = detail::Widen32(actual);
      Require(test.absolute_error &&
                  SameError(detail::AbsoluteError(test.source_f64, widened,
                                                  allocation, kIntegerBits),
                            LiteralError(*test.absolute_error)),
              "independent literal rational error");
      Require(detail::IsRequired32(test.source_f64, actual,
                                   mv::SurfaceNarrowing::kBoundedRne32,
                                   allocation, kIntegerBits),
              "independent midpoint-cell accepts target");
      const bool exact = SameError(LiteralError(*test.absolute_error), {});
      Require(detail::IsRequired32(test.source_f64, actual,
                                   mv::SurfaceNarrowing::kExact32, allocation,
                                   kIntegerBits) == exact,
              "exact-cell disposition");
      const auto alternate = actual ^ UINT32_C(1);
      Require(!detail::IsRequired32(test.source_f64, alternate,
                                    mv::SurfaceNarrowing::kBoundedRne32,
                                    allocation, kIntegerBits),
              "wrong adjacent target is rejected");
      detail::String text{mv::gltf_detail::Allocator<char>(allocation)};
      detail::AppendDecimal64(text, widened, allocation, kIntegerBits, 4096);
      Require(detail::Decimal64(text, allocation, kIntegerBits) == widened,
              "exact integer-decimal round trip including signed zero");
    } catch (const detail::Failure& failure) {
      Require(refused && failure.Status() == mv::SurfaceStatus::kUnsupported,
              "typed scalar refusal");
    }
  }
}
void Triangles(const mv::gltf_detail::AllocationContext& allocation) {
  for (const auto& test : fixture::TriangleCases())
    for (std::size_t face = 0; face < test.faces.size(); ++face) {
      std::array<std::uint64_t, detail::kTrianglePositionScalars> original{},
          actual{};
      for (std::size_t corner = 0; corner < detail::kTriangleCorners; ++corner)
        for (std::size_t component = 0; component < detail::kPositionComponents;
             ++component) {
          const auto vertex = test.faces[face][corner],
                     at = corner * detail::kPositionComponents + component;
          original[at] = test.source_words[vertex][component];
          actual[at] = detail::Widen32(test.reference_words[vertex][component]);
        }
      const auto& oracle = test.face_oracles[face];
      const bool edge = std::any_of(oracle.reference_edges_squared.begin(),
                                    oracle.reference_edges_squared.end(),
                                    [](auto value) { return value == "0"; });
      const auto expected =
          edge               ? mv::SurfaceCode::kCollapsedEdge
          : oracle.collapsed ? mv::SurfaceCode::kCollapsedTriangle
          : oracle.normal_dot.starts_with('-') ? mv::SurfaceCode::kOrientation
                                               : mv::SurfaceCode::kNone;
      Require(detail::TriangleIssue(original, actual, allocation,
                                    kIntegerBits) == expected,
              "literal exact geometric predicate");
    }
}
struct RestoreEnvironment {
  int rounding = std::fegetround();
#if defined(MESHVALE_SURFACE_TEST_SSE)
  unsigned control = _mm_getcsr();
#endif
  ~RestoreEnvironment() {
    (void)std::fesetround(rounding);
#if defined(MESHVALE_SURFACE_TEST_SSE)
    _mm_setcsr(control);
#endif
  }
};
void Environment(const mv::gltf_detail::AllocationContext& allocation) {
  RestoreEnvironment restore;
  for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    Require(std::fesetround(mode) == 0,
            "actual caller rounding mode established");
    Scalars(allocation);
    Triangles(allocation);
  }
#if defined(MESHVALE_SURFACE_TEST_SSE)
  Require(std::fesetround(FE_TONEAREST) == 0, "baseline rounding established");
  for (unsigned flags : {0U, kFtz, kDaz, kFtz | kDaz}) {
    _mm_setcsr((restore.control & ~(kFtz | kDaz | kRounding)) | flags);
    Scalars(allocation);
    Triangles(allocation);
    const auto observed = detail::ObserveNumericProfile();
    Require(
        observed.status ==
            (flags ? mv::SurfaceDelegateProfileStatus::kUnsupportedFlushState
                   : mv::SurfaceDelegateProfileStatus::kEstablishedBaseline),
        "actual profile facts");
  }
#else
  Require(detail::ObserveNumericProfile().status ==
              mv::SurfaceDelegateProfileStatus::kUnqueryable,
          "unknown flush state is refused, never fabricated");
#endif
}
}  // namespace
int main() {
  try {
    meshvale::geometry::ExecutionContext context({4, 1, 64 * kMiB});
    {
      mv::gltf_detail::AllocationContext root(context, {});
      const auto allocation = root.WithRequestLimit(kOperationBytes);
      Scalars(allocation);
      Triangles(allocation);
      Environment(allocation);
      try {
        detail::Natural limited(1, allocation, 1);
        limited.ShiftLeft(1);
        Require(false, "bit cap must refuse");
      } catch (const detail::Failure& failure) {
        Require(failure.Status() == mv::SurfaceStatus::kLimitExceeded &&
                    failure.Diagnostic().code == mv::SurfaceCode::kCountLimit,
                "actual intermediate cap");
      }
    }
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "all numeric owners and workers released");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
#if defined(MESHVALE_SURFACE_TEST_SSE)
#undef MESHVALE_SURFACE_TEST_SSE
#endif
