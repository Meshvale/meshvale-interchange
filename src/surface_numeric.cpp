// SPDX-License-Identifier: Apache-2.0
#include "surface_numeric.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_M_X64) || defined(__SSE__) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define MESHVALE_SURFACE_SSE_STATE 1
#endif

#include "gltf_allocation.h"
#include "surface_output_internal.h"

namespace meshvale::interchange::surface_detail {
namespace {
constexpr unsigned kFloatFractionBits = 23;
constexpr unsigned kDoubleFractionBits = 52;
constexpr unsigned kFloatSignShift = 31;
constexpr unsigned kDoubleSignShift = 63;
constexpr int kFloatExponentBias = 127;
constexpr int kDoubleExponentBias = 1023;
constexpr int kFloatMinimumNormalExponent = -126;
constexpr int kFloatSubnormalQuantumExponent = -149;
constexpr int kDoubleSubnormalQuantumExponent = -1074;
constexpr std::uint32_t kFloatFractionMask = UINT32_C(0x007fffff);
constexpr std::uint64_t kDoubleFractionMask = UINT64_C(0x000fffffffffffff);
constexpr std::uint32_t kFloatHiddenBit = UINT32_C(0x00800000);
constexpr std::uint64_t kDoubleHiddenBit = UINT64_C(0x0010000000000000);
constexpr std::uint32_t kMaximumFiniteFloatWord = UINT32_C(0x7f7fffff);
constexpr std::uint64_t kMaximumFiniteDoubleWord = UINT64_C(0x7fefffffffffffff);
constexpr std::uint32_t kDecimalRadix = 10;
constexpr std::uint32_t kDecimalDyadicFactor = 5;
constexpr char kDecimalZero = '0';
constexpr char kDecimalNine = '9';
constexpr char kDecimalMinus = '-';
constexpr char kDecimalPlus = '+';
constexpr char kDecimalPoint = '.';
constexpr char kExponentLower = 'e';
constexpr char kExponentUpper = 'E';
constexpr std::size_t kUnsignedDecimalCharacters =
    std::numeric_limits<std::uint64_t>::digits10 + 1;
constexpr unsigned kFloatExponentAllOnes = 255;
constexpr unsigned kUnsignedWordBits = 64;
constexpr unsigned kNaturalWordBits =
    std::numeric_limits<std::uint32_t>::digits;
constexpr std::size_t kNaturalWordsPerErrorWord =
    kUnsignedWordBits / kNaturalWordBits;
#if defined(MESHVALE_SURFACE_SSE_STATE)
constexpr std::uint32_t kMxcsrRoundingMask = UINT32_C(0x6000);
constexpr std::uint32_t kMxcsrFlushToZeroMask = UINT32_C(0x8000);
constexpr std::uint32_t kMxcsrDenormalsAreZeroMask = UINT32_C(0x0040);
#endif
}  // namespace

bool Finite64(std::uint64_t bits) noexcept {
  return (bits & kBinary64ExponentMask) != kBinary64ExponentMask;
}
bool Finite32(std::uint32_t bits) noexcept {
  return (bits & kBinary32ExponentMask) != kBinary32ExponentMask;
}
std::uint64_t Equality64(std::uint64_t bits) noexcept {
  return (bits & kBinary64MagnitudeMask) == 0 ? 0 : bits;
}
std::uint64_t Equality32(std::uint32_t bits) noexcept {
  return (bits & kBinary32MagnitudeMask) == 0 ? 0 : bits;
}
std::uint64_t Widen32(std::uint32_t bits) noexcept {
  const auto sign = static_cast<std::uint64_t>(bits >> kFloatSignShift)
                    << kDoubleSignShift;
  const auto exponent = (bits & kBinary32ExponentMask) >> kFloatFractionBits;
  const auto fraction = bits & kFloatFractionMask;
  if (exponent == kFloatExponentAllOnes) {
    return sign | kBinary64ExponentMask |
           (static_cast<std::uint64_t>(fraction)
            << (kDoubleFractionBits - kFloatFractionBits));
  }
  if (exponent == 0) {
    if (fraction == 0) return sign;
    const auto highest = static_cast<unsigned>(std::bit_width(fraction) - 1);
    const auto unbiased =
        kFloatSubnormalQuantumExponent + static_cast<int>(highest);
    const auto remainder = fraction - (std::uint32_t{1} << highest);
    return sign |
           (static_cast<std::uint64_t>(unbiased + kDoubleExponentBias)
            << kDoubleFractionBits) |
           (static_cast<std::uint64_t>(remainder)
            << (kDoubleFractionBits - highest));
  }
  return sign |
         (static_cast<std::uint64_t>(static_cast<int>(exponent) -
                                     kFloatExponentBias + kDoubleExponentBias)
          << kDoubleFractionBits) |
         (static_cast<std::uint64_t>(fraction)
          << (kDoubleFractionBits - kFloatFractionBits));
}

std::uint32_t Round32(std::uint64_t bits, SurfaceNarrowing policy) {
  const auto sign = static_cast<std::uint32_t>(bits >> kDoubleSignShift)
                    << kFloatSignShift;
  const auto magnitude = bits & kBinary64MagnitudeMask;
  if (!Finite64(bits))
    Fail(SurfaceStatus::kUnsupported, SurfaceStage::kNumeric,
         SurfaceCode::kNonfinite);
  if (magnitude > Widen32(kMaximumFiniteFloatWord))
    Fail(SurfaceStatus::kUnsupported, SurfaceStage::kNumeric,
         SurfaceCode::kNumericRange);
  if (magnitude == 0) return sign;
  const auto encoded_exponent = static_cast<unsigned>(
      (magnitude & kBinary64ExponentMask) >> kDoubleFractionBits);
  auto significand = magnitude & kDoubleFractionMask;
  int exponent = kDoubleSubnormalQuantumExponent;
  if (encoded_exponent != 0) {
    significand |= kDoubleHiddenBit;
    exponent = static_cast<int>(encoded_exponent) - kDoubleExponentBias -
               static_cast<int>(kDoubleFractionBits);
  }
  const auto highest = static_cast<int>(std::bit_width(significand)) - 1;
  const auto value_exponent = exponent + highest;
  auto quantum = value_exponent >= kFloatMinimumNormalExponent
                     ? value_exponent - static_cast<int>(kFloatFractionBits)
                     : kFloatSubnormalQuantumExponent;
  const auto shift = quantum - exponent;
  std::uint64_t rounded = 0;
  if (shift <= 0) {
    rounded = significand << static_cast<unsigned>(-shift);
  } else if (shift < static_cast<int>(kUnsignedWordBits)) {
    const auto distance = static_cast<unsigned>(shift);
    rounded = significand >> distance;
    const auto remainder = significand & ((UINT64_C(1) << distance) - 1);
    const auto halfway = UINT64_C(1) << (distance - 1);
    if (remainder > halfway || (remainder == halfway && (rounded & 1)))
      ++rounded;
  }
  if (rounded == 0)
    Fail(SurfaceStatus::kUnsupported, SurfaceStage::kNumeric,
         SurfaceCode::kNonzeroUnderflow);
  if (rounded >= (static_cast<std::uint64_t>(kFloatHiddenBit) << 1)) {
    rounded >>= 1;
    ++quantum;
  }
  std::uint32_t output;
  if (quantum == kFloatSubnormalQuantumExponent && rounded < kFloatHiddenBit) {
    output = sign | static_cast<std::uint32_t>(rounded);
  } else {
    const auto target_exponent =
        quantum + static_cast<int>(kFloatFractionBits) + kFloatExponentBias;
    if (target_exponent >= static_cast<int>(kFloatExponentAllOnes))
      Fail(SurfaceStatus::kUnsupported, SurfaceStage::kNumeric,
           SurfaceCode::kNumericRange);
    output =
        sign |
        (static_cast<std::uint32_t>(target_exponent) << kFloatFractionBits) |
        (static_cast<std::uint32_t>(rounded) - kFloatHiddenBit);
  }
  if (policy == SurfaceNarrowing::kExact32 && Widen32(output) != bits)
    Fail(SurfaceStatus::kUnsupported, SurfaceStage::kNumeric,
         SurfaceCode::kExactness);
  return output;
}

SurfaceDelegateProfileFacts ObserveNumericProfile() noexcept {
  SurfaceDelegateProfileFacts result;
  const auto rounding = std::fegetround();
  if (rounding != -1) result.nearest_even = rounding == FE_TONEAREST;
#if defined(MESHVALE_SURFACE_SSE_STATE)
  const auto control = static_cast<std::uint32_t>(_mm_getcsr());
  if (result.nearest_even)
    result.nearest_even =
        *result.nearest_even && (control & kMxcsrRoundingMask) == 0;
  result.ftz_disabled = (control & kMxcsrFlushToZeroMask) == 0;
  result.daz_disabled = (control & kMxcsrDenormalsAreZeroMask) == 0;
#endif
  if (!result.nearest_even || !result.ftz_disabled || !result.daz_disabled) {
    result.status = SurfaceDelegateProfileStatus::kUnqueryable;
  } else if (!*result.nearest_even) {
    result.status = SurfaceDelegateProfileStatus::kUnsupportedRounding;
  } else if (!*result.ftz_disabled || !*result.daz_disabled) {
    result.status = SurfaceDelegateProfileStatus::kUnsupportedFlushState;
  } else {
    result.status = SurfaceDelegateProfileStatus::kEstablishedBaseline;
  }
  return result;
}

Natural::Natural(const gltf_detail::AllocationContext& allocation,
                 std::uint64_t bit_limit)
    : allocation_(allocation),
      words_(gltf_detail::Allocator<std::uint32_t>(allocation)),
      bit_limit_(bit_limit) {
  if (bit_limit == 0)
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kRequiredFieldZero);
  if (bit_limit >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kConfigurationExtent);
}
Natural::Natural(std::uint64_t value,
                 const gltf_detail::AllocationContext& allocation,
                 std::uint64_t bit_limit)
    : Natural(allocation, bit_limit) {
  if (value == 0) return;
  AdmitBits(std::bit_width(value));
  words_.push_back(static_cast<std::uint32_t>(value));
  if (value >> kNaturalWordBits)
    words_.push_back(static_cast<std::uint32_t>(value >> kNaturalWordBits));
}
bool Natural::Zero() const noexcept { return words_.empty(); }
bool Natural::Odd() const noexcept {
  return !words_.empty() && (words_.front() & 1);
}
std::size_t Natural::Bits() const noexcept {
  return words_.empty() ? 0
                        : (words_.size() - 1) * kNaturalWordBits +
                              std::bit_width(words_.back());
}
std::span<const std::uint32_t> Natural::Words() const noexcept {
  return words_;
}
const gltf_detail::AllocationContext& Natural::Allocation() const noexcept {
  return allocation_;
}
std::uint64_t Natural::BitLimit() const noexcept { return bit_limit_; }
void Natural::AdmitBits(std::size_t bits) const {
  allocation_.Checkpoint();
  if (bits > bit_limit_)
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kNumeric,
         SurfaceCode::kCountLimit);
}
void Natural::Trim() {
  while (!words_.empty() && words_.back() == 0) {
    allocation_.Checkpoint();
    words_.pop_back();
  }
}
std::size_t Natural::TrailingZeros() const {
  if (Zero()) return 0;
  std::size_t zeros = 0;
  for (auto word : words_) {
    allocation_.Checkpoint();
    if (word != 0) return zeros + std::countr_zero(word);
    zeros += kNaturalWordBits;
  }
  return zeros;
}
int Natural::Compare(const Natural& other) const {
  if (words_.size() != other.words_.size())
    return words_.size() < other.words_.size() ? -1 : 1;
  for (auto index = words_.size(); index != 0;) {
    allocation_.Checkpoint();
    --index;
    if (words_[index] != other.words_[index])
      return words_[index] < other.words_[index] ? -1 : 1;
  }
  return 0;
}
std::uint64_t Natural::To64() const {
  if (Bits() > kUnsignedWordBits)
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kNumeric,
         SurfaceCode::kSizeOverflow);
  if (Zero()) return 0;
  return words_[0] | (words_.size() > 1 ? static_cast<std::uint64_t>(words_[1])
                                              << kNaturalWordBits
                                        : 0);
}
void Natural::AddSmall(std::uint32_t value) {
  if (value == 0) return;
  AdmitBits(std::max(Bits(), static_cast<std::size_t>(std::bit_width(value))) +
            1);
  std::uint64_t carry = value;
  for (auto& word : words_) {
    allocation_.Checkpoint();
    const auto sum = static_cast<std::uint64_t>(word) + carry;
    word = static_cast<std::uint32_t>(sum);
    carry = sum >> kNaturalWordBits;
    if (carry == 0) return;
  }
  if (carry) words_.push_back(static_cast<std::uint32_t>(carry));
}
void Natural::MultiplySmall(std::uint32_t value) {
  if (value == 0) {
    words_.clear();
    return;
  }
  if (value == 1 || Zero()) return;
  AdmitBits(Bits() + std::bit_width(value));
  std::uint64_t carry = 0;
  for (auto& word : words_) {
    allocation_.Checkpoint();
    const auto product = static_cast<std::uint64_t>(word) * value + carry;
    word = static_cast<std::uint32_t>(product);
    carry = product >> kNaturalWordBits;
  }
  if (carry) words_.push_back(static_cast<std::uint32_t>(carry));
}
void Natural::ShiftLeft(std::size_t bits) {
  if (bits == 0 || Zero()) return;
  if (bits > std::numeric_limits<std::size_t>::max() - Bits())
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kNumeric,
         SurfaceCode::kSizeOverflow);
  const auto total = Bits() + bits;
  AdmitBits(total);
  const auto count = total / kNaturalWordBits + (total % kNaturalWordBits != 0);
  Vector<std::uint32_t> next(
      count, 0, gltf_detail::Allocator<std::uint32_t>(allocation_));
  const auto whole = bits / kNaturalWordBits;
  const auto part = static_cast<unsigned>(bits % kNaturalWordBits);
  for (std::size_t index = 0; index < words_.size(); ++index) {
    allocation_.Checkpoint();
    const auto value = static_cast<std::uint64_t>(words_[index]) << part;
    next[index + whole] |= static_cast<std::uint32_t>(value);
    if (part && index + whole + 1 < next.size())
      next[index + whole + 1] |=
          static_cast<std::uint32_t>(value >> kNaturalWordBits);
  }
  words_.swap(next);
}
void Natural::ShiftRight(std::size_t bits) {
  if (bits == 0 || Zero()) return;
  if (bits >= Bits()) {
    words_.clear();
    return;
  }
  const auto whole = bits / kNaturalWordBits;
  const auto part = static_cast<unsigned>(bits % kNaturalWordBits);
  const auto count = words_.size() - whole;
  for (std::size_t index = 0; index < count; ++index) {
    allocation_.Checkpoint();
    const auto low = words_[index + whole] >> part;
    const auto high = part && index + whole + 1 < words_.size()
                          ? words_[index + whole + 1]
                                << (kNaturalWordBits - part)
                          : 0;
    words_[index] = low | high;
  }
  words_.resize(count);
  Trim();
}
std::uint32_t Natural::DivideSmall(std::uint32_t divisor) {
  if (divisor == 0)
    Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
         SurfaceCode::kConfigurationExtent);
  std::uint64_t remainder = 0;
  for (auto index = words_.size(); index != 0;) {
    allocation_.Checkpoint();
    --index;
    const auto value = (remainder << kNaturalWordBits) | words_[index];
    words_[index] = static_cast<std::uint32_t>(value / divisor);
    remainder = value % divisor;
  }
  Trim();
  return static_cast<std::uint32_t>(remainder);
}
Natural Natural::Add(const Natural& other) const {
  if (other.Zero()) return *this;
  if (Zero()) {
    Natural result(allocation_, bit_limit_);
    result.AdmitBits(other.Bits());
    result.words_.assign(other.words_.begin(), other.words_.end());
    return result;
  }
  AdmitBits(std::max(Bits(), other.Bits()) + 1);
  Natural result(*this);
  result.words_.resize(std::max(words_.size(), other.words_.size()), 0);
  std::uint64_t carry = 0;
  for (std::size_t index = 0; index < result.words_.size(); ++index) {
    allocation_.Checkpoint();
    const auto value = static_cast<std::uint64_t>(result.words_[index]) +
                       carry +
                       (index < other.words_.size() ? other.words_[index] : 0);
    result.words_[index] = static_cast<std::uint32_t>(value);
    carry = value >> kNaturalWordBits;
  }
  if (carry) result.words_.push_back(static_cast<std::uint32_t>(carry));
  return result;
}
Natural Natural::Subtract(const Natural& other) const {
  const auto comparison = Compare(other);
  if (comparison < 0)
    Fail(SurfaceStatus::kInvalidStorage, SurfaceStage::kNumeric,
         SurfaceCode::kSourceCorrespondence);
  if (comparison == 0) return Natural(allocation_, bit_limit_);
  Natural result(*this);
  std::uint64_t borrow = 0;
  for (std::size_t index = 0; index < words_.size(); ++index) {
    allocation_.Checkpoint();
    const auto subtrahend =
        (index < other.words_.size()
             ? static_cast<std::uint64_t>(other.words_[index])
             : 0) +
        borrow;
    const auto value = static_cast<std::uint64_t>(words_[index]);
    result.words_[index] = static_cast<std::uint32_t>(value - subtrahend);
    borrow = value < subtrahend;
  }
  result.Trim();
  return result;
}
Natural Natural::Multiply(const Natural& other) const {
  Natural result(allocation_, bit_limit_);
  if (Zero() || other.Zero()) return result;
  if (other.Bits() > std::numeric_limits<std::size_t>::max() - Bits())
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kNumeric,
         SurfaceCode::kSizeOverflow);
  AdmitBits(Bits() + other.Bits());
  result.words_.assign(words_.size() + other.words_.size(), 0);
  for (std::size_t left = 0; left < words_.size(); ++left) {
    std::uint64_t carry = 0;
    for (std::size_t right = 0; right < other.words_.size(); ++right) {
      allocation_.Checkpoint();
      const auto slot = left + right;
      const auto product =
          static_cast<std::uint64_t>(words_[left]) * other.words_[right] +
          result.words_[slot] + carry;
      result.words_[slot] = static_cast<std::uint32_t>(product);
      carry = product >> kNaturalWordBits;
    }
    result.words_[left + other.words_.size()] =
        static_cast<std::uint32_t>(carry);
  }
  result.Trim();
  return result;
}

Dyadic::Dyadic(const gltf_detail::AllocationContext& allocation,
               std::uint64_t bit_limit)
    : magnitude(allocation, bit_limit) {}
void Dyadic::Normalize() {
  if (magnitude.Zero()) {
    negative = false;
    exponent2 = 0;
    return;
  }
  const auto trailing = magnitude.TrailingZeros();
  if (trailing >
          static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
      static_cast<std::int64_t>(exponent2) +
              static_cast<std::int64_t>(trailing) >
          std::numeric_limits<std::int32_t>::max())
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kNumeric,
         SurfaceCode::kSizeOverflow);
  magnitude.ShiftRight(trailing);
  exponent2 += static_cast<std::int32_t>(trailing);
}
Dyadic Decode64(std::uint64_t bits,
                const gltf_detail::AllocationContext& allocation,
                std::uint64_t bit_limit) {
  if (!Finite64(bits))
    Fail(SurfaceStatus::kUnsupported, SurfaceStage::kNumeric,
         SurfaceCode::kNonfinite);
  Dyadic result(allocation, bit_limit);
  result.negative = (bits & kBinary64SignMask) != 0;
  const auto exponent = static_cast<unsigned>((bits & kBinary64ExponentMask) >>
                                              kDoubleFractionBits);
  auto significand = bits & kDoubleFractionMask;
  result.exponent2 = kDoubleSubnormalQuantumExponent;
  if (exponent) {
    significand |= kDoubleHiddenBit;
    result.exponent2 = static_cast<std::int32_t>(exponent) -
                       kDoubleExponentBias -
                       static_cast<std::int32_t>(kDoubleFractionBits);
  }
  result.magnitude = Natural(significand, allocation, bit_limit);
  result.Normalize();
  return result;
}
Dyadic Add(const Dyadic& a, const Dyadic& b) {
  if (a.magnitude.Zero()) return b;
  if (b.magnitude.Zero()) return a;
  const auto exponent = std::min(a.exponent2, b.exponent2);
  const auto left_shift = static_cast<std::int64_t>(a.exponent2) - exponent;
  const auto right_shift = static_cast<std::int64_t>(b.exponent2) - exponent;
  Dyadic result(a);
  Natural right(b.magnitude);
  result.magnitude.ShiftLeft(static_cast<std::size_t>(left_shift));
  right.ShiftLeft(static_cast<std::size_t>(right_shift));
  result.exponent2 = exponent;
  if (a.negative == b.negative) {
    result.magnitude = result.magnitude.Add(right);
  } else {
    const auto order = result.magnitude.Compare(right);
    if (order >= 0) {
      result.magnitude = result.magnitude.Subtract(right);
    } else {
      result.magnitude = right.Subtract(result.magnitude);
      result.negative = b.negative;
    }
  }
  result.Normalize();
  return result;
}
Dyadic Subtract(const Dyadic& a, const Dyadic& b) {
  Dyadic negative(b);
  if (!negative.magnitude.Zero()) negative.negative = !negative.negative;
  return Add(a, negative);
}
Dyadic Multiply(const Dyadic& a, const Dyadic& b) {
  Dyadic result(a.magnitude.Allocation(), a.magnitude.BitLimit());
  const auto exponent = static_cast<std::int64_t>(a.exponent2) + b.exponent2;
  if (exponent < std::numeric_limits<std::int32_t>::min() ||
      exponent > std::numeric_limits<std::int32_t>::max())
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kNumeric,
         SurfaceCode::kSizeOverflow);
  result.exponent2 = static_cast<std::int32_t>(exponent);
  result.negative = a.negative != b.negative;
  result.magnitude = a.magnitude.Multiply(b.magnitude);
  result.Normalize();
  return result;
}
int Compare(const Dyadic& a, const Dyadic& b) {
  if (a.magnitude.Zero() && b.magnitude.Zero()) return 0;
  if (a.negative != b.negative) return a.negative ? -1 : 1;
  const auto delta = Subtract(a, b);
  if (delta.magnitude.Zero()) return 0;
  return delta.negative ? -1 : 1;
}
SurfaceDyadicError AbsoluteError(
    std::uint64_t original, std::uint64_t decoded,
    const gltf_detail::AllocationContext& allocation, std::uint64_t bit_limit) {
  const auto source = Decode64(original, allocation, bit_limit);
  const auto actual = Decode64(decoded, allocation, bit_limit);
  const auto difference = Subtract(source, actual);
  SurfaceDyadicError result;
  if (difference.magnitude.Zero()) return result;
  const auto words = difference.magnitude.Words();
  const auto needed = words.size() / kNaturalWordsPerErrorWord +
                      (words.size() % kNaturalWordsPerErrorWord != 0);
  if (needed > result.magnitude.size())
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kNumeric,
         SurfaceCode::kCountLimit);
  for (std::size_t i = 0; i < words.size(); ++i) {
    allocation.Checkpoint();
    result.magnitude[i / kNaturalWordsPerErrorWord] |=
        static_cast<std::uint64_t>(words[i])
        << ((i % kNaturalWordsPerErrorWord) * kNaturalWordBits);
  }
  result.used_words = static_cast<std::uint32_t>(needed);
  result.exponent2 = difference.exponent2;
  return result;
}
namespace {
bool ErrorBit(const SurfaceDyadicError& value, std::int64_t exponent) noexcept {
  const auto bit = exponent - value.exponent2;
  if (bit < 0) return false;
  const auto word = static_cast<std::uint64_t>(bit) / kUnsignedWordBits;
  if (word >= value.used_words) return false;
  return ((value.magnitude[static_cast<std::size_t>(word)] >>
           (static_cast<unsigned>(bit) % kUnsignedWordBits)) &
          1) != 0;
}
std::int64_t ErrorTop(const SurfaceDyadicError& value) noexcept {
  if (value.used_words == 0) return std::numeric_limits<std::int64_t>::min();
  return static_cast<std::int64_t>(value.exponent2) +
         static_cast<std::int64_t>(value.used_words - 1) * kUnsignedWordBits +
         static_cast<std::int64_t>(
             std::bit_width(value.magnitude[value.used_words - 1])) -
         1;
}
}  // namespace
int CompareError(const SurfaceDyadicError& a, const SurfaceDyadicError& b) {
  const auto atop = ErrorTop(a), btop = ErrorTop(b);
  if (atop != btop) return atop < btop ? -1 : 1;
  if (a.used_words == 0) return 0;
  const auto bottom = std::min(a.exponent2, b.exponent2);
  for (auto exponent = atop; exponent >= bottom; --exponent) {
    const auto left = ErrorBit(a, exponent), right = ErrorBit(b, exponent);
    if (left != right) return left ? 1 : -1;
  }
  return 0;
}
bool WithinBound(const SurfaceDyadicError& error, std::uint64_t bound_bits,
                 const gltf_detail::AllocationContext& allocation,
                 std::uint64_t bit_limit) {
  const auto bound = AbsoluteError(0, bound_bits & kBinary64MagnitudeMask,
                                   allocation, bit_limit);
  return CompareError(error, bound) <= 0;
}

namespace {
bool EqualPoint(std::span<const std::uint64_t, kTrianglePositionScalars> points,
                std::size_t first, std::size_t second) noexcept {
  for (std::size_t c = 0; c < kPositionComponents; ++c) {
    if (Equality64(points[first * kPositionComponents + c]) !=
        Equality64(points[second * kPositionComponents + c]))
      return false;
  }
  return true;
}
Vector<Dyadic> Normal(
    std::span<const std::uint64_t, kTrianglePositionScalars> points,
    const gltf_detail::AllocationContext& allocation, std::uint64_t bit_limit) {
  Vector<Dyadic> a{gltf_detail::Allocator<Dyadic>(allocation)};
  Vector<Dyadic> b{gltf_detail::Allocator<Dyadic>(allocation)};
  a.reserve(kPositionComponents);
  b.reserve(kPositionComponents);
  constexpr auto kLastTriangleVertex = kTriangleCorners - 1;
  for (std::size_t c = 0; c < kPositionComponents; ++c) {
    const auto origin = Decode64(points[c], allocation, bit_limit);
    a.push_back(Subtract(
        Decode64(points[kPositionComponents + c], allocation, bit_limit),
        origin));
    b.push_back(
        Subtract(Decode64(points[kLastTriangleVertex * kPositionComponents + c],
                          allocation, bit_limit),
                 origin));
  }
  Vector<Dyadic> normal{gltf_detail::Allocator<Dyadic>(allocation)};
  normal.reserve(kPositionComponents);
  for (std::size_t c = 0; c < kPositionComponents; ++c) {
    allocation.Checkpoint();
    const auto next = (c + 1) % kPositionComponents;
    const auto previous = (c + kPositionComponents - 1) % kPositionComponents;
    normal.push_back(Subtract(Multiply(a[next], b[previous]),
                              Multiply(a[previous], b[next])));
  }
  return normal;
}
}  // namespace
SurfaceCode TriangleIssue(
    std::span<const std::uint64_t, kTrianglePositionScalars> source,
    std::span<const std::uint64_t, kTrianglePositionScalars> decoded,
    const gltf_detail::AllocationContext& allocation, std::uint64_t bit_limit) {
  for (std::size_t vertex = 0; vertex < kTriangleCorners; ++vertex) {
    allocation.Checkpoint();
    if (EqualPoint(source, vertex, (vertex + 1) % kTriangleCorners) ||
        EqualPoint(decoded, vertex, (vertex + 1) % kTriangleCorners))
      return SurfaceCode::kCollapsedEdge;
  }
  const auto original_normal = Normal(source, allocation, bit_limit);
  const auto actual_normal = Normal(decoded, allocation, bit_limit);
  bool original_zero = true, actual_zero = true;
  Dyadic dot(allocation, bit_limit);
  for (std::size_t c = 0; c < kPositionComponents; ++c) {
    original_zero = original_zero && original_normal[c].magnitude.Zero();
    actual_zero = actual_zero && actual_normal[c].magnitude.Zero();
    dot = Add(dot, Multiply(original_normal[c], actual_normal[c]));
  }
  if (original_zero || actual_zero) return SurfaceCode::kCollapsedTriangle;
  return dot.negative || dot.magnitude.Zero() ? SurfaceCode::kOrientation
                                              : SurfaceCode::kNone;
}
bool PreservesTriangle(
    std::span<const std::uint64_t, kTrianglePositionScalars> source,
    std::span<const std::uint64_t, kTrianglePositionScalars> decoded,
    const gltf_detail::AllocationContext& allocation, std::uint64_t bit_limit) {
  return TriangleIssue(source, decoded, allocation, bit_limit) ==
         SurfaceCode::kNone;
}

bool IsRequired32(std::uint64_t source, std::uint32_t actual,
                  SurfaceNarrowing mode,
                  const gltf_detail::AllocationContext& allocation,
                  std::uint64_t bit_limit) {
  if (!Finite64(source) || !Finite32(actual)) return false;
  if ((source >> kDoubleSignShift) != (actual >> kFloatSignShift)) return false;
  const auto source_magnitude = source & kBinary64MagnitudeMask;
  const auto actual_magnitude = actual & kBinary32MagnitudeMask;
  if (mode == SurfaceNarrowing::kExact32) return Widen32(actual) == source;
  if (source_magnitude == 0) return actual_magnitude == 0;
  if (actual_magnitude == 0 ||
      source_magnitude > Widen32(kMaximumFiniteFloatWord))
    return false;
  // Independent nearest-cell proof. Unlike Round32, this constructs no target
  // from source bits: the actual decoded target's two midpoint bounds decide
  // it.
  const auto value = Decode64(source_magnitude, allocation, bit_limit);
  const auto candidate =
      Decode64(Widen32(actual_magnitude), allocation, bit_limit);
  const auto lower =
      Decode64(Widen32(actual_magnitude - 1), allocation, bit_limit);
  auto lower_midpoint = Add(candidate, lower);
  --lower_midpoint.exponent2;
  lower_midpoint.Normalize();
  const auto lower_order = Compare(value, lower_midpoint);
  const bool even = (actual_magnitude & 1) == 0;
  if (lower_order < 0 || (lower_order == 0 && !even)) return false;
  if (actual_magnitude == kMaximumFiniteFloatWord) return true;
  const auto upper =
      Decode64(Widen32(actual_magnitude + 1), allocation, bit_limit);
  auto upper_midpoint = Add(candidate, upper);
  --upper_midpoint.exponent2;
  upper_midpoint.Normalize();
  const auto upper_order = Compare(value, upper_midpoint);
  return upper_order < 0 || (upper_order == 0 && even);
}

void AppendUnsigned(String& text, std::uint64_t value) {
  std::array<char, kUnsignedDecimalCharacters> buffer;
  const auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (result.ec != std::errc{})
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kEncode,
         SurfaceCode::kSizeOverflow);
  text.append(buffer.data(),
              static_cast<std::size_t>(result.ptr - buffer.data()));
}
void AppendSigned(String& text, std::int64_t value) {
  if (value < 0) {
    text.push_back(kDecimalMinus);
    AppendUnsigned(text, static_cast<std::uint64_t>(-(value + 1)) + 1);
  } else {
    AppendUnsigned(text, static_cast<std::uint64_t>(value));
  }
}
void AppendDecimal64(String& text, std::uint64_t bits,
                     const gltf_detail::AllocationContext& allocation,
                     std::uint64_t bit_limit, std::uint64_t token_limit) {
  auto value = Decode64(bits, allocation, bit_limit);
  String token{gltf_detail::Allocator<char>(allocation)};
  auto put = [&](char c) {
    allocation.Checkpoint();
    if (token.size() >= token_limit)
      Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kEncode,
           SurfaceCode::kOutputLimit);
    token.push_back(c);
  };
  if ((bits & kBinary64SignMask) != 0) put(kDecimalMinus);
  if (value.magnitude.Zero()) {
    put(kDecimalZero);
    text.append(token);
    return;
  }
  std::size_t scale = 0;
  if (value.exponent2 >= 0) {
    value.magnitude.ShiftLeft(static_cast<std::size_t>(value.exponent2));
  } else {
    scale =
        static_cast<std::size_t>(-static_cast<std::int64_t>(value.exponent2));
    for (std::size_t i = 0; i < scale; ++i) {
      allocation.Checkpoint();
      value.magnitude.MultiplySmall(kDecimalDyadicFactor);
    }
  }
  String reversed{gltf_detail::Allocator<char>(allocation)};
  do {
    allocation.Checkpoint();
    if (reversed.size() >= token_limit)
      Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kEncode,
           SurfaceCode::kOutputLimit);
    const auto digit = value.magnitude.DivideSmall(kDecimalRadix);
    reversed.push_back(static_cast<char>(kDecimalZero + digit));
  } while (!value.magnitude.Zero());
  if (scale >= reversed.size()) {
    put(kDecimalZero);
    put(kDecimalPoint);
    for (auto zeros = scale - reversed.size(); zeros != 0; --zeros)
      put(kDecimalZero);
  }
  for (auto i = reversed.size(); i != 0;) {
    --i;
    put(reversed[i]);
    if (scale != 0 && i == scale) put(kDecimalPoint);
  }
  text.append(token);
}
namespace {
int CompareRatio(const Natural& numerator, const Natural& denominator,
                 const Dyadic& value) {
  auto right = denominator.Multiply(value.magnitude);
  if (value.exponent2 >= 0) {
    right.ShiftLeft(static_cast<std::size_t>(value.exponent2));
    return numerator.Compare(right);
  }
  auto left = numerator;
  left.ShiftLeft(
      static_cast<std::size_t>(-static_cast<std::int64_t>(value.exponent2)));
  return left.Compare(right);
}
}  // namespace
std::uint64_t Decimal64(std::string_view token,
                        const gltf_detail::AllocationContext& allocation,
                        std::uint64_t bit_limit) {
  auto malformed = [] {
    Fail(SurfaceStatus::kVerificationFailed, SurfaceStage::kCompare,
         SurfaceCode::kWireValue);
  };
  if (token.empty()) malformed();
  std::size_t cursor = 0;
  bool negative = false;
  if (token[cursor] == kDecimalMinus || token[cursor] == kDecimalPlus) {
    negative = token[cursor] == kDecimalMinus;
    if (++cursor == token.size()) malformed();
  }
  Natural numerator(allocation, bit_limit);
  std::size_t digits = 0, fractional = 0;
  bool point = false;
  while (cursor < token.size()) {
    allocation.Checkpoint();
    const auto c = token[cursor];
    if (c >= kDecimalZero && c <= kDecimalNine) {
      numerator.MultiplySmall(kDecimalRadix);
      numerator.AddSmall(static_cast<std::uint32_t>(c - kDecimalZero));
      ++digits;
      if (point) ++fractional;
      ++cursor;
    } else if (c == kDecimalPoint && !point) {
      point = true;
      ++cursor;
    } else {
      break;
    }
  }
  if (digits == 0) malformed();
  std::int64_t exponent10 = 0;
  bool exponent_negative = false;
  if (cursor < token.size() &&
      (token[cursor] == kExponentLower || token[cursor] == kExponentUpper)) {
    if (++cursor == token.size()) malformed();
    if (token[cursor] == kDecimalMinus || token[cursor] == kDecimalPlus) {
      exponent_negative = token[cursor] == kDecimalMinus;
      if (++cursor == token.size()) malformed();
    }
    std::size_t exponent_digits = 0;
    while (cursor < token.size() && token[cursor] >= kDecimalZero &&
           token[cursor] <= kDecimalNine) {
      allocation.Checkpoint();
      const auto digit =
          static_cast<std::int64_t>(token[cursor] - kDecimalZero);
      if (exponent10 >
          (std::numeric_limits<std::int64_t>::max() - digit) / kDecimalRadix)
        Fail(SurfaceStatus::kVerificationFailed, SurfaceStage::kCompare,
             SurfaceCode::kNumericRange);
      exponent10 = exponent10 * kDecimalRadix + digit;
      ++cursor;
      ++exponent_digits;
    }
    if (exponent_digits == 0) malformed();
  }
  if (cursor != token.size()) malformed();
  const auto sign = negative ? kBinary64SignMask : 0;
  if (numerator.Zero()) return sign;
  if (fractional >
      static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
    malformed();
  if (exponent_negative) exponent10 = -exponent10;
  if (exponent10 < std::numeric_limits<std::int64_t>::min() +
                       static_cast<std::int64_t>(fractional))
    malformed();
  exponent10 -= static_cast<std::int64_t>(fractional);
  Natural denominator(1, allocation, bit_limit);
  const auto exponent_magnitude =
      exponent10 < 0 ? static_cast<std::uint64_t>(-(exponent10 + 1)) + 1
                     : static_cast<std::uint64_t>(exponent10);
  if (exponent_magnitude > bit_limit)
    Fail(SurfaceStatus::kLimitExceeded, SurfaceStage::kCompare,
         SurfaceCode::kCountLimit);
  for (std::uint64_t i = 0; i < exponent_magnitude; ++i) {
    allocation.Checkpoint();
    (exponent10 < 0 ? denominator : numerator).MultiplySmall(kDecimalRadix);
  }
  const auto maximum =
      Decode64(kMaximumFiniteDoubleWord, allocation, bit_limit);
  if (CompareRatio(numerator, denominator, maximum) > 0)
    Fail(SurfaceStatus::kVerificationFailed, SurfaceStage::kCompare,
         SurfaceCode::kNumericRange);
  std::uint64_t low = 0, high = kMaximumFiniteDoubleWord;
  while (low < high) {
    allocation.Checkpoint();
    const auto middle = low + (high - low) / 2 + (high - low) % 2;
    if (CompareRatio(numerator, denominator,
                     Decode64(middle, allocation, bit_limit)) >= 0)
      low = middle;
    else
      high = middle - 1;
  }
  if (low == kMaximumFiniteDoubleWord) return sign | low;
  const auto lower = Decode64(low, allocation, bit_limit);
  const auto upper = Decode64(low + 1, allocation, bit_limit);
  auto midpoint = Add(lower, upper);
  --midpoint.exponent2;
  midpoint.Normalize();
  const auto order = CompareRatio(numerator, denominator, midpoint);
  return sign | (order > 0 || (order == 0 && (low & 1)) ? low + 1 : low);
}
}  // namespace meshvale::interchange::surface_detail

namespace meshvale::interchange {
SurfaceNumericProfile SurfaceNumericSupport() noexcept {
  constexpr int kBinaryRadix = 2;
  constexpr int kBinary32SignificandBits = 24;
  constexpr int kBinary64SignificandBits = 53;
  constexpr int kWireByteBits = 8;
  constexpr bool supported =
      sizeof(float) == sizeof(std::uint32_t) &&
      sizeof(double) == sizeof(std::uint64_t) &&
      std::numeric_limits<float>::is_iec559 &&
      std::numeric_limits<double>::is_iec559 &&
      std::numeric_limits<float>::radix == kBinaryRadix &&
      std::numeric_limits<double>::radix == kBinaryRadix &&
      std::numeric_limits<float>::digits == kBinary32SignificandBits &&
      std::numeric_limits<double>::digits == kBinary64SignificandBits &&
      std::numeric_limits<unsigned char>::digits == kWireByteBits;
  return supported ? SurfaceNumericProfile::kIeeeBinary32Binary64
                   : SurfaceNumericProfile::kUnsupportedStorage;
}
}  // namespace meshvale::interchange

#if defined(MESHVALE_SURFACE_SSE_STATE)
#undef MESHVALE_SURFACE_SSE_STATE
#endif
