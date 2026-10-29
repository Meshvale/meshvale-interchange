// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_QUATERNION_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_QUATERNION_H_

#include <meshvale/interchange/gltf.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_detail {
inline constexpr std::size_t kQuaternionComponents =
    GltfNodeRotationSourceView::kQuaternionComponents;
inline constexpr std::size_t kQuaternionDecimalSlots = 8;
inline constexpr std::size_t kQuaternionFilterSlots = 8;
inline constexpr std::size_t kQuaternionFloatWords = 10;
inline constexpr std::size_t kQuaternionDecimalWords = 128;
inline constexpr std::size_t kQuaternionFilterWords = 16;
inline constexpr std::uint64_t kQuaternionCanonicalDigits = 256;
inline constexpr std::uint64_t kQuaternionDecimalPower = 512;
// Conservative scheduling extents of this implementation, never observations.
// A W-word product has at most W^2 pair steps and W^2 carry steps, plus
// clear/two used-word passes. No token scan or arithmetic is needed to plan.
inline constexpr std::uint64_t kQuaternionFilterProductSteps =
    2 * kQuaternionFilterWords * kQuaternionFilterWords +
    3 * kQuaternionFilterWords;
inline constexpr std::uint64_t kQuaternionDecimalProductSteps =
    2 * kQuaternionDecimalWords * kQuaternionDecimalWords +
    3 * kQuaternionDecimalWords;
inline constexpr std::uint64_t kQuaternionFilterMaxIntegerSteps =
    19 * kQuaternionFilterWords + 9 + 2 * kQuaternionFilterProductSteps;
inline constexpr std::uint64_t kQuaternionFloatExactMaxIntegerSteps =
    kQuaternionFloatWords +
    kQuaternionComponents * (4 * kQuaternionFloatWords + 3) +
    8 * kQuaternionFloatWords + 6;
inline constexpr std::uint64_t kQuaternionDecimalExactMaxIntegerSteps =
    kQuaternionDecimalWords +
    kQuaternionComponents *
        ((2 + kQuaternionCanonicalDigits + kQuaternionDecimalPower) *
             kQuaternionDecimalWords +
         kQuaternionDecimalProductSteps) +
    (kQuaternionDecimalWords + 2) +
    2 * kQuaternionDecimalPower * kQuaternionDecimalWords +
    3 * (kQuaternionDecimalWords + 2 + kQuaternionDecimalProductSteps) +
    2 * kQuaternionDecimalWords;
inline constexpr std::uint64_t kQuaternionNodeMaxIntegerSteps =
    2 * kQuaternionComponents + kQuaternionFilterMaxIntegerSteps +
    kQuaternionDecimalExactMaxIntegerSteps;
inline constexpr std::uint64_t kQuaternionNodeExtraByteVisits =
    kQuaternionComponents * (kQuaternionCanonicalDigits + 1);
[[nodiscard]] constexpr std::uint64_t QuaternionWordsIntegerStepBound(
    GltfQuaternionEncoding encoding, GltfQuaternionRole role) noexcept {
  if (role == GltfQuaternionRole::kCubicTangent) return kQuaternionComponents;
  return 2 * kQuaternionComponents + kQuaternionFilterMaxIntegerSteps +
         (encoding == GltfQuaternionEncoding::kFloat32
              ? kQuaternionFloatExactMaxIntegerSteps
              : 0);
}
// Reads only the four actual view lengths, with typed checked addition; it does
// not inspect tokens or create source/proof authority. The extra pass visits at
// most256 canonical digits and one decimal point per nonzero coefficient.
[[nodiscard]] std::uint64_t QuaternionNodeByteVisitBound(
    std::span<const GltfRawNumberView, kQuaternionComponents> authored);
// Per-worker scratch, allocated by the actual caller with Allocator<...> under
// its existing complete descriptor/request parents. No new context/cap. Keep
// this storage, source backing and work configuration alive through every join.
struct QuaternionWorkspace {
  std::array<std::uint64_t, kQuaternionComponents> normalized;
  std::array<std::array<std::uint32_t, kQuaternionFloatWords>,
             kQuaternionComponents>
      floating;
  std::array<std::array<std::uint32_t, kQuaternionDecimalWords>,
             kQuaternionDecimalSlots>
      decimal;
  std::array<std::array<std::uint32_t, kQuaternionFilterWords>,
             kQuaternionFilterSlots>
      filter;
};
struct QuaternionWork {
  const AllocationContext& allocation;
  const GltfDeformationWorkPolicy& policy;
  // Actual repeated32-bit word/word-pair visits inside this helper, including
  // source-word classification/conversion passes; exclude them from caller C.
  std::uint64_t integer_steps = 0;
  std::uint64_t byte_visits = 0;
  std::optional<std::uint64_t> partition;
  bool caller = true;
  void IntegerStep();
  void ByteVisit();
  void Checkpoint() const;
};
enum class QuaternionDecision {
  kAdmitted,
  kRange,
  kNonfinite,
  kNonunit,
  kRepresentation,
  kNumericRefusal
};
struct QuaternionPredicateResult {
  QuaternionDecision decision = QuaternionDecision::kAdmitted;
  std::optional<GltfQuaternionDiagnosticRule> rule;
  std::optional<std::uint32_t> component;
};
// Integer encodings use their low wire bits (e.g. snorm8 -128 is 0x80), never
// sign-extended uint32 values. FLOAT supplies actual IEEE binary32 words. These
// predicates create no asset/proof authority and never rewrite the inputs.
[[nodiscard]] QuaternionPredicateResult EvaluateQuaternionWords(
    std::span<const std::uint32_t, kQuaternionComponents> words,
    GltfQuaternionEncoding encoding, GltfQuaternionRole role,
    QuaternionWorkspace& workspace, QuaternionWork& work,
    GltfDeformationNumericObservation& observation);
// Authored lexemes are syntax-admitted immutable source views. Projected words
// must be the actual successful reader projection of that same tuple. Earlier
// reader range/representation/projection failures leave this helper unreached.
[[nodiscard]] QuaternionPredicateResult EvaluateNodeQuaternion(
    std::span<const GltfRawNumberView, kQuaternionComponents> authored,
    std::span<const std::uint32_t, kQuaternionComponents>
        actual_projected_words,
    QuaternionWorkspace& workspace, QuaternionWork& work,
    GltfDeformationNumericObservation& observation);
}  // namespace meshvale::interchange::gltf_detail

#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_QUATERNION_H_
