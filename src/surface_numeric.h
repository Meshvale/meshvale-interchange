// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_SURFACE_NUMERIC_H_
#define MESHVALE_INTERCHANGE_SRC_SURFACE_NUMERIC_H_

#include <meshvale/interchange/surface_write.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gltf_allocation.h"

namespace meshvale::interchange::surface_detail {
template <class T>
using Vector = std::vector<T, gltf_detail::Allocator<T>>;
using String = std::basic_string<char, std::char_traits<char>,
                                 gltf_detail::Allocator<char>>;
inline constexpr std::uint64_t kBinary64MagnitudeMask =
    UINT64_C(0x7fffffffffffffff);
inline constexpr std::uint64_t kBinary64SignMask = UINT64_C(0x8000000000000000);
inline constexpr std::uint64_t kBinary64ExponentMask =
    UINT64_C(0x7ff0000000000000);
inline constexpr std::uint32_t kBinary32MagnitudeMask = UINT32_C(0x7fffffff);
inline constexpr std::uint32_t kBinary32SignMask = UINT32_C(0x80000000);
inline constexpr std::uint32_t kBinary32ExponentMask = UINT32_C(0x7f800000);
inline constexpr std::size_t kPositionComponents = 3;
inline constexpr std::size_t kUvComponents = 2;
inline constexpr std::size_t kTriangleCorners = 3;
inline constexpr std::size_t kTrianglePositionScalars =
    kTriangleCorners * kPositionComponents;

[[nodiscard]] bool Finite64(std::uint64_t bits) noexcept;
[[nodiscard]] bool Finite32(std::uint32_t bits) noexcept;
[[nodiscard]] std::uint64_t Widen32(std::uint32_t bits) noexcept;
// Expected range, underflow and exactness failures use SurfaceCode; no floating
// cast, operation or ambient environment participates in choosing the target.
[[nodiscard]] std::uint32_t Round32(std::uint64_t bits,
                                    SurfaceNarrowing policy);
[[nodiscard]] std::uint64_t Equality64(std::uint64_t bits) noexcept;
[[nodiscard]] std::uint64_t Equality32(std::uint32_t bits) noexcept;
[[nodiscard]] SurfaceDelegateProfileFacts ObserveNumericProfile() noexcept;

// Magnitude with the actual request-limited allocator. The bit ceiling admits
// every temporary/reserved intermediate; the allocator independently accounts
// block headers, capacities and overlapping growth before the backend request.
class Natural {
 public:
  Natural(const gltf_detail::AllocationContext& allocation,
          std::uint64_t bit_limit);
  Natural(std::uint64_t value, const gltf_detail::AllocationContext& allocation,
          std::uint64_t bit_limit);
  [[nodiscard]] bool Zero() const noexcept;
  [[nodiscard]] std::size_t Bits() const noexcept;
  [[nodiscard]] std::size_t TrailingZeros() const;
  [[nodiscard]] bool Odd() const noexcept;
  [[nodiscard]] int Compare(const Natural& other) const;
  [[nodiscard]] std::uint64_t To64() const;
  [[nodiscard]] std::span<const std::uint32_t> Words() const noexcept;
  [[nodiscard]] const gltf_detail::AllocationContext& Allocation()
      const noexcept;
  [[nodiscard]] std::uint64_t BitLimit() const noexcept;
  void AddSmall(std::uint32_t value);
  void MultiplySmall(std::uint32_t value);
  void ShiftLeft(std::size_t bits);
  void ShiftRight(std::size_t bits);
  [[nodiscard]] std::uint32_t DivideSmall(std::uint32_t divisor);
  [[nodiscard]] Natural Add(const Natural& other) const;
  [[nodiscard]] Natural Subtract(const Natural& other) const;
  [[nodiscard]] Natural Multiply(const Natural& other) const;

 private:
  void AdmitBits(std::size_t bits) const;
  void Trim();
  gltf_detail::AllocationContext allocation_;
  Vector<std::uint32_t> words_;
  std::uint64_t bit_limit_;
};
struct Dyadic {
  bool negative = false;
  Natural magnitude;
  std::int32_t exponent2 = 0;
  explicit Dyadic(const gltf_detail::AllocationContext& allocation,
                  std::uint64_t bit_limit);
  void Normalize();
};
[[nodiscard]] Dyadic Decode64(std::uint64_t bits,
                              const gltf_detail::AllocationContext& allocation,
                              std::uint64_t bit_limit);
[[nodiscard]] Dyadic Add(const Dyadic& a, const Dyadic& b);
[[nodiscard]] Dyadic Subtract(const Dyadic& a, const Dyadic& b);
[[nodiscard]] Dyadic Multiply(const Dyadic& a, const Dyadic& b);
[[nodiscard]] int Compare(const Dyadic& a, const Dyadic& b);
[[nodiscard]] SurfaceDyadicError AbsoluteError(
    std::uint64_t original, std::uint64_t decoded,
    const gltf_detail::AllocationContext& allocation, std::uint64_t bit_limit);
[[nodiscard]] int CompareError(const SurfaceDyadicError& a,
                               const SurfaceDyadicError& b);
[[nodiscard]] bool WithinBound(const SurfaceDyadicError& error,
                               std::uint64_t bound_bits,
                               const gltf_detail::AllocationContext& allocation,
                               std::uint64_t bit_limit);
void AppendUnsigned(String& text, std::uint64_t value);
void AppendSigned(String& text, std::int64_t value);
// Exact finite dyadic text, including -0; no floating
// charconv/stream/promotion.
void AppendDecimal64(String& text, std::uint64_t bits,
                     const gltf_detail::AllocationContext& allocation,
                     std::uint64_t bit_limit, std::uint64_t token_limit);
// Actual wire decimal parser: bounded integer rational, nearest-even binary64.
// This is not the encoder's F64->F32 quantizer or an accepted native read
// result.
[[nodiscard]] std::uint64_t Decimal64(
    std::string_view token, const gltf_detail::AllocationContext& allocation,
    std::uint64_t bit_limit);
// Inspector-specific midpoint proof, not a call to Round32.
[[nodiscard]] bool IsRequired32(
    std::uint64_t source, std::uint32_t actual, SurfaceNarrowing mode,
    const gltf_detail::AllocationContext& allocation, std::uint64_t bit_limit);
// Exact source/decoded cross products and positive dot. No facet normal is
// generated or normalized; this predicate concerns geometry only.
[[nodiscard]] bool PreservesTriangle(
    std::span<const std::uint64_t, kTrianglePositionScalars> source,
    std::span<const std::uint64_t, kTrianglePositionScalars> decoded,
    const gltf_detail::AllocationContext& allocation, std::uint64_t bit_limit);
[[nodiscard]] SurfaceCode TriangleIssue(
    std::span<const std::uint64_t, kTrianglePositionScalars> source,
    std::span<const std::uint64_t, kTrianglePositionScalars> decoded,
    const gltf_detail::AllocationContext& allocation, std::uint64_t bit_limit);
}  // namespace meshvale::interchange::surface_detail
#endif  // MESHVALE_INTERCHANGE_SRC_SURFACE_NUMERIC_H_
