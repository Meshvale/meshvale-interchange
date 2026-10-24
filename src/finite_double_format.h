// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_FINITE_DOUBLE_FORMAT_H_
#define MESHVALE_INTERCHANGE_SRC_FINITE_DOUBLE_FORMAT_H_

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <system_error>

namespace meshvale::interchange::numeric_format_detail {
enum class Phase { kScaleCoefficient, kExtractDigits };

// The callers admit finite binary64 and precision 1..max_digits10. Normal
// values and signed zero keep the standard-library spelling. A subnormal is
// formatted with integers because some libraries classify it as zero under
// DAZ before their decimal conversion. No floating control or heap is touched.
// checkpoint(phase, completed, total) observes actual coefficient passes or
// extracted decimal digits, then performs the owning operation's stop check.
// Scaling/digit work between observations is at most checkpoint_bytes plus
// one fixed-size coefficient pass (or one nine-digit limb). The final rounding
// and emission operate on at most max_digits10 significant characters and a
// fixed exponent buffer. Callers keep conversion pre/post stop checks,
// including the unchanged normal standard-library conversion.
template <class Checkpoint>
[[nodiscard]] std::to_chars_result FiniteDoubleToChars(
    std::span<char> output, double value, int precision,
    std::uint64_t checkpoint_bytes, Checkpoint&& checkpoint) {
  constexpr std::uint64_t kFractionMask = UINT64_C(0x000fffffffffffff);
  constexpr std::uint64_t kExponentMask = UINT64_C(0x7ff0000000000000);
  constexpr std::uint64_t kSignMask = UINT64_C(0x8000000000000000);
  constexpr std::size_t kSubnormalPower = 1074;
  constexpr std::uint32_t kDecimalRadix = 10;
  constexpr std::uint32_t kScaleMultiplier = 5;
  constexpr std::uint32_t kLimbRadix = 1000000000;
  constexpr std::size_t kLimbDigits = 9;
  // A 52-bit fraction is <10^16 and 5^1074 <10^1074. Thus at most
  // 16+1074 decimal digits, or ceil(1090/9)=122 base-10^9 limbs, suffice.
  constexpr std::size_t kFractionDecimalDigits = 16;
  constexpr std::size_t kCoefficientDigits =
      kFractionDecimalDigits + kSubnormalPower;
  constexpr std::size_t kCoefficientLimbs =
      (kCoefficientDigits + kLimbDigits - 1) / kLimbDigits;
  constexpr std::size_t kMaximumPrecision =
      std::numeric_limits<double>::max_digits10;
  constexpr char kZero = '0';
  constexpr char kOne = '1';
  constexpr char kNine = '9';
  constexpr char kHalfwayDigit = '5';
  constexpr char kDecimalPoint = '.';
  constexpr char kExponentMarker = 'e';
  constexpr char kMinus = '-';
  auto* const last =
      output.empty() ? output.data() : output.data() + output.size();
  const auto failure = [&](std::errc error) {
    return std::to_chars_result{last, error};
  };
  if (precision < 1 || precision > static_cast<int>(kMaximumPrecision) ||
      checkpoint_bytes == 0)
    return failure(std::errc::invalid_argument);
  if (output.empty()) return failure(std::errc::value_too_large);
  const auto bits = std::bit_cast<std::uint64_t>(value);
  auto fraction = bits & kFractionMask;
  if ((bits & kExponentMask) != 0 || fraction == 0)
    return std::to_chars(output.data(), last, value, std::chars_format::general,
                         precision);

  // value = fraction * 2^-1074 = (fraction * 5^1074) * 10^-1074.
  // (base-1)*5+carry <5*10^9, comfortably within uint64, at every step.
  std::array<std::uint32_t, kCoefficientLimbs> coefficient{};
  std::size_t used = 0;
  do {
    coefficient[used++] = static_cast<std::uint32_t>(fraction % kLimbRadix);
    fraction /= kLimbRadix;
  } while (fraction != 0);
  std::uint64_t work_bytes = 0;
  for (std::size_t pass = 0; pass < kSubnormalPower; ++pass) {
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < used; ++i) {
      const auto product =
          static_cast<std::uint64_t>(coefficient[i]) * kScaleMultiplier + carry;
      coefficient[i] = static_cast<std::uint32_t>(product % kLimbRadix);
      carry = product / kLimbRadix;
      work_bytes += sizeof(coefficient[i]);
    }
    if (carry != 0) {
      if (used == coefficient.size())
        return failure(std::errc::value_too_large);
      coefficient[used++] = static_cast<std::uint32_t>(carry);
      work_bytes += sizeof(coefficient[0]);
    }
    if (work_bytes >= checkpoint_bytes || pass + 1 == kSubnormalPower) {
      checkpoint(Phase::kScaleCoefficient, pass + 1, kSubnormalPower);
      work_bytes = 0;
    }
  }

  std::array<char, kMaximumPrecision> significant{};
  std::size_t retained = 0;
  std::size_t extracted = 0;
  char guard = kZero;
  bool has_guard = false;
  bool sticky = false;
  const auto digit = [&](char character) {
    if (retained < static_cast<std::size_t>(precision))
      significant[retained++] = character;
    else if (!has_guard) {
      guard = character;
      has_guard = true;
    } else if (character != kZero)
      sticky = true;
    ++extracted;
  };
  std::array<char, kLimbDigits> limb_text{};
  const auto limb_digits = [&](std::uint32_t limb) {
    std::size_t count = 1;
    for (auto remaining = limb; remaining >= kDecimalRadix;
         remaining /= kDecimalRadix)
      ++count;
    for (std::size_t i = count; i != 0; --i) {
      limb_text[i - 1] = static_cast<char>(kZero + limb % kDecimalRadix);
      limb /= kDecimalRadix;
    }
    return count;
  };
  const auto leading_digits = limb_digits(coefficient[used - 1]);
  const auto total_digits = (used - 1) * kLimbDigits + leading_digits;
  int exponent =
      static_cast<int>(total_digits) - 1 - static_cast<int>(kSubnormalPower);
  work_bytes = 0;
  for (std::size_t i = used; i != 0; --i) {
    const auto count = limb_digits(coefficient[i - 1]);
    if (i != used)
      for (std::size_t pad = count; pad < kLimbDigits; ++pad) digit(kZero);
    for (std::size_t j = 0; j < count; ++j) digit(limb_text[j]);
    work_bytes += i == used ? count : kLimbDigits;
    if (work_bytes >= checkpoint_bytes || i == 1) {
      checkpoint(Phase::kExtractDigits, extracted, total_digits);
      work_bytes = 0;
    }
  }

  // Decimal rounding is nearest/ties-even at the requested significant width.
  // A carry out changes only the scientific exponent, never the width.
  if (has_guard &&
      (guard > kHalfwayDigit ||
       (guard == kHalfwayDigit &&
        (sticky || (significant[retained - 1] - kZero) % 2 != 0)))) {
    std::size_t i = retained;
    while (i != 0 && significant[i - 1] == kNine) significant[--i] = kZero;
    if (i == 0) {
      significant[0] = kOne;
      ++exponent;
    } else
      ++significant[i - 1];
  }
  while (retained > 1 && significant[retained - 1] == kZero) --retained;

  constexpr std::size_t kExponentCharacters =
      std::numeric_limits<int>::digits10 + 2;
  std::array<char, kExponentCharacters> exponent_text{};
  auto* exponent_begin = exponent_text.data() + exponent_text.size();
  auto magnitude = static_cast<unsigned>(exponent < 0 ? -exponent : exponent);
  do {
    *--exponent_begin = static_cast<char>(kZero + magnitude % kDecimalRadix);
    magnitude /= kDecimalRadix;
  } while (magnitude != 0);
  if (exponent < 0) *--exponent_begin = kMinus;
  const auto exponent_size = static_cast<std::size_t>(
      exponent_text.data() + exponent_text.size() - exponent_begin);
  const bool negative = (bits & kSignMask) != 0;
  const auto required = static_cast<std::size_t>(negative) + retained +
                        static_cast<std::size_t>(retained > 1) + 1 +
                        exponent_size;
  if (required > output.size()) return failure(std::errc::value_too_large);
  auto* cursor = output.data();
  if (negative) *cursor++ = kMinus;
  *cursor++ = significant[0];
  if (retained > 1) {
    *cursor++ = kDecimalPoint;
    for (std::size_t i = 1; i < retained; ++i) *cursor++ = significant[i];
  }
  *cursor++ = kExponentMarker;
  for (std::size_t i = 0; i < exponent_size; ++i) *cursor++ = exponent_begin[i];
  return {cursor, std::errc{}};
}
}  // namespace meshvale::interchange::numeric_format_detail
#endif  // MESHVALE_INTERCHANGE_SRC_FINITE_DOUBLE_FORMAT_H_
