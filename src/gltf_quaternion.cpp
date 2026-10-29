// SPDX-License-Identifier: Apache-2.0
#include "gltf_quaternion.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "gltf_numerical_profile.h"

namespace meshvale::interchange::gltf_detail {
namespace {
using Words = std::span<std::uint32_t>;
using ConstWords = std::span<const std::uint32_t>;
constexpr std::uint64_t kLimbCheckpoint = 16;
constexpr unsigned kWordBits = 32;
constexpr unsigned kDyadicScale = 298;
constexpr unsigned kFloatBandFirstPower = 23;
constexpr unsigned kFloatBandSecondPower = 48;
constexpr unsigned kFloatFilterErrorPower = 47;
constexpr unsigned kNodeFilterErrorPower = 21;
constexpr unsigned kByteComponentBits = 8;
constexpr unsigned kShortComponentBits = 16;
constexpr unsigned kDoubleExponentMaximum = 2047;
constexpr unsigned kDoubleSignBit = 63;
constexpr unsigned kDoubleWordBits = 64;
constexpr std::uint64_t kNodeBandDenominator = 1000000000000ULL;
constexpr std::uint64_t kNodeBandNumerator = 2000001;
constexpr std::uint64_t kFloatBandDenominator = std::uint64_t{1}
                                                << kFloatBandSecondPower;
constexpr std::uint64_t kFloatBandNumerator = 33554433;
constexpr std::uint32_t kFloatExponentMask = 0x7f800000U;
constexpr std::uint32_t kFloatMagnitudeMask = 0x7fffffffU;
constexpr std::uint32_t kFloatMagnitudeTwo = 0x40000000U;
constexpr std::uint32_t kFloatFractionMask = 0x007fffffU;
constexpr unsigned kFloatFractionBits = 23;
constexpr unsigned kDoubleFractionBits = 52;
constexpr int kDoubleExponentBias = 1023;
constexpr std::uint64_t kDoubleFractionMask =
    (std::uint64_t{1} << kDoubleFractionBits) - 1;
constexpr std::size_t kDecimalDigits = kQuaternionCanonicalDigits;
constexpr std::int64_t kDecimalScale = kQuaternionDecimalPower;
constexpr std::uint32_t kDecimalRadix = 10;

void Increment(std::uint64_t& value) {
  if (value == std::numeric_limits<std::uint64_t>::max())
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  ++value;
}
void Clear(Words words, QuaternionWork& work) {
  for (auto& word : words) {
    work.IntegerStep();
    word = 0;
  }
}
void Set(Words words, std::uint64_t value, QuaternionWork& work) {
  Clear(words, work);
  words[0] = static_cast<std::uint32_t>(value);
  words[1] = static_cast<std::uint32_t>(value >> kWordBits);
  work.IntegerStep();
  work.IntegerStep();
}
void Copy(Words to, ConstWords from, QuaternionWork& work) {
  for (std::size_t i = 0; i < to.size(); ++i) {
    work.IntegerStep();
    to[i] = from[i];
  }
}
std::size_t Used(ConstWords words, QuaternionWork& work) {
  for (std::size_t i = words.size(); i != 0; --i) {
    work.IntegerStep();
    if (words[i - 1]) return i;
  }
  return 0;
}
int Compare(ConstWords left, ConstWords right, QuaternionWork& work) {
  for (std::size_t i = left.size(); i != 0; --i) {
    work.IntegerStep();
    if (left[i - 1] != right[i - 1]) return left[i - 1] < right[i - 1] ? -1 : 1;
  }
  return 0;
}
bool Add(Words to, ConstWords left, ConstWords right, QuaternionWork& work) {
  std::uint64_t carry = 0;
  for (std::size_t i = 0; i < to.size(); ++i) {
    work.IntegerStep();
    const auto sum = std::uint64_t{left[i]} + right[i] + carry;
    to[i] = static_cast<std::uint32_t>(sum);
    carry = sum >> kWordBits;
  }
  return carry == 0;
}
void Subtract(Words left, ConstWords right, QuaternionWork& work) {
  std::uint64_t borrow = 0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    work.IntegerStep();
    const auto subtrahend = std::uint64_t{right[i]} + borrow;
    const auto value = std::uint64_t{left[i]};
    left[i] = static_cast<std::uint32_t>(value - subtrahend);
    borrow = value < subtrahend;
  }
  if (borrow) throw AdmissionFailure(AdmissionCode::kInvalidObject);
}
bool Shift(Words words, unsigned shift, QuaternionWork& work) {
  const auto used = Used(words, work);
  if (!used || !shift) return true;
  const auto significant =
      (used - 1) * kWordBits + (kWordBits - std::countl_zero(words[used - 1]));
  if (shift > words.size() * kWordBits - significant) return false;
  const auto whole = shift / kWordBits;
  const auto part = shift % kWordBits;
  for (std::size_t end = words.size(); end != 0; --end) {
    work.IntegerStep();
    const auto i = end - 1;
    std::uint32_t value = i >= whole ? words[i - whole] << part : 0;
    if (part && i > whole) value |= words[i - whole - 1] >> (kWordBits - part);
    words[i] = value;
  }
  return true;
}
bool MultiplySmall(Words words, std::uint32_t factor, std::uint32_t addition,
                   QuaternionWork& work) {
  std::uint64_t carry = addition;
  for (auto& word : words) {
    work.IntegerStep();
    const auto product = std::uint64_t{word} * factor + carry;
    word = static_cast<std::uint32_t>(product);
    carry = product >> kWordBits;
  }
  return carry == 0;
}
bool Multiply(Words to, ConstWords left, ConstWords right,
              QuaternionWork& work) {
  Clear(to, work);
  const auto a = Used(left, work);
  const auto b = Used(right, work);
  for (std::size_t i = 0; i < a; ++i) {
    std::uint64_t carry = 0;
    for (std::size_t j = 0; j < b; ++j) {
      work.IntegerStep();
      if (i + j >= to.size()) return false;
      const auto product =
          std::uint64_t{left[i]} * right[j] + to[i + j] + carry;
      to[i + j] = static_cast<std::uint32_t>(product);
      carry = product >> kWordBits;
    }
    auto at = i + b;
    while (carry) {
      work.IntegerStep();
      if (at >= to.size()) return false;
      const auto sum = std::uint64_t{to[at]} + carry;
      to[at++] = static_cast<std::uint32_t>(sum);
      carry = sum >> kWordBits;
    }
  }
  return true;
}
void Bit(Words words, unsigned bit, QuaternionWork& work) {
  work.IntegerStep();
  if (bit >= words.size() * kWordBits)
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  words[bit / kWordBits] |= std::uint32_t{1} << (bit % kWordBits);
}
void Require(bool fits) {
  if (!fits) throw AdmissionFailure(AdmissionCode::kSizeLimit);
}
QuaternionPredicateResult Refuse(QuaternionDecision decision,
                                 GltfQuaternionDiagnosticRule rule,
                                 std::optional<std::uint32_t> component = {}) {
  return {decision, rule, component};
}
struct Band {
  std::uint64_t denominator;
  std::uint64_t numerator;
  unsigned error_power;
};
enum class FilterResult { kInside, kOutside, kOverlap };

// s3 is a nonnegative multiple of2^-298. Bounded binary32 components
// and direct normalized divisions (smallest nonzero magnitude >=1/65535)
// cannot introduce finer bits through separate binary64 products/additions.
void ScaledFilterSum(Words to, double sum, QuaternionWork& work) {
  const auto bits = std::bit_cast<std::uint64_t>(sum);
  const auto exponent =
      static_cast<int>((bits >> kDoubleFractionBits) & kDoubleExponentMaximum);
  if ((bits >> kDoubleSignBit) || exponent == kDoubleExponentMaximum)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  if (exponent == 0) {
    if (bits) throw AdmissionFailure(AdmissionCode::kInvalidObject);
    Clear(to, work);
    return;
  }
  auto significand =
      (bits & kDoubleFractionMask) | (std::uint64_t{1} << kDoubleFractionBits);
  const auto shift = exponent - kDoubleExponentBias -
                     static_cast<int>(kDoubleFractionBits) +
                     static_cast<int>(kDyadicScale);
  if (shift < 0) {
    const auto right = static_cast<unsigned>(-shift);
    if (right >= kDoubleWordBits ||
        (significand & ((std::uint64_t{1} << right) - 1)))
      throw AdmissionFailure(AdmissionCode::kInvalidObject);
    significand >>= right;
  }
  Set(to, significand, work);
  if (shift > 0) Require(Shift(to, static_cast<unsigned>(shift), work));
}
FilterResult CertifiedFilter(double sum, Band band, QuaternionWorkspace& space,
                             QuaternionWork& work) {
  auto& [source_sum, error, lower_sum, upper_sum, upper_cross, lower_bound,
         denominator, lower_cross] = space.filter;
  ScaledFilterSum(source_sum, sum, work);
  Clear(error, work);
  Bit(error, kDyadicScale - band.error_power, work);
  Copy(lower_sum, source_sum, work);
  const bool negative_low = Compare(lower_sum, error, work) < 0;
  if (!negative_low) Subtract(lower_sum, error, work);
  Require(Add(upper_sum, source_sum, error, work));
  Set(denominator, band.denominator, work);
  Require(Multiply(upper_cross, upper_sum, denominator, work));
  if (!negative_low)
    Require(Multiply(lower_cross, lower_sum, denominator, work));
  Set(lower_bound, band.denominator - band.numerator, work);
  Require(Shift(lower_bound, kDyadicScale, work));
  Set(error, band.denominator + band.numerator, work);
  Require(Shift(error, kDyadicScale, work));
  if (Compare(upper_cross, lower_bound, work) < 0 ||
      (!negative_low && Compare(lower_cross, error, work) > 0))
    return FilterResult::kOutside;
  if (!negative_low && Compare(lower_cross, lower_bound, work) >= 0 &&
      Compare(upper_cross, error, work) <= 0)
    return FilterResult::kInside;
  return FilterResult::kOverlap;
}
double FilterSum(const std::array<double, kQuaternionComponents>& values) {
  // Separate volatile stores plus the required actual strict compilation
  // prevent contraction/reassociation. No external rounding/flush/mask state is
  // changed.
  volatile double p0 = values[0] * values[0];
  volatile double p1 = values[1] * values[1];
  volatile double p2 = values[2] * values[2];
  volatile double p3 = values[3] * values[3];
  volatile double s1 = p0 + p1;
  volatile double s2 = s1 + p2;
  volatile double s3 = s2 + p3;
  return s3;
}
bool ExactFloat(std::span<const std::uint32_t, kQuaternionComponents> words,
                QuaternionWorkspace& space, QuaternionWork& work) {
  auto& [source_sum, lower_bound, upper_bound, term] = space.floating;
  Clear(source_sum, work);
  for (const auto word : words) {
    work.IntegerStep();
    const auto exponent = (word & kFloatExponentMask) >> kFloatFractionBits;
    const auto mantissa =
        (word & kFloatFractionMask) |
        (exponent ? std::uint32_t{1} << kFloatFractionBits : 0);
    Set(term, std::uint64_t{mantissa} * mantissa, work);
    Require(Shift(term, exponent ? 2 * (exponent - 1) : 0, work));
    Require(Add(source_sum, source_sum, term, work));
  }
  Clear(lower_bound, work);
  Bit(lower_bound, kDyadicScale, work);
  Clear(term, work);
  Bit(term, kDyadicScale - kFloatBandFirstPower, work);
  Subtract(lower_bound, term, work);
  Clear(term, work);
  Bit(term, kDyadicScale - kFloatBandSecondPower, work);
  Subtract(lower_bound, term, work);
  Clear(upper_bound, work);
  Bit(upper_bound, kDyadicScale, work);
  Bit(upper_bound, kDyadicScale - kFloatBandFirstPower, work);
  Bit(upper_bound, kDyadicScale - kFloatBandSecondPower, work);
  return Compare(source_sum, lower_bound, work) >= 0 &&
         Compare(source_sum, upper_bound, work) <= 0;
}
struct Decimal {
  std::string_view text;
  std::size_t first = 0, last = 0, digits = 0;
  std::int64_t power = 0;
  bool zero = true;
  bool exponent_overflow = false;
  bool exponent_negative = false;
};
Decimal Parse(std::string_view text, QuaternionWork& work) {
  Decimal out{text};
  bool fractional = false, exponent_part = false;
  std::int64_t fraction = 0, trailing = 0, exponent = 0;
  for (std::size_t at = 0; at < text.size(); ++at) {
    work.ByteVisit();
    const auto c = text[at];
    if (c == 'e' || c == 'E') {
      exponent_part = true;
    } else if (c == '+' || c == '-') {
      if (exponent_part) out.exponent_negative = c == '-';
    } else if (c == '.') {
      fractional = true;
    } else if (c >= '0' && c <= '9') {
      const auto digit = static_cast<std::int64_t>(c - '0');
      if (exponent_part) {
        if (exponent >
            (std::numeric_limits<std::int64_t>::max() - digit) / kDecimalRadix)
          out.exponent_overflow = true;
        if (!out.exponent_overflow) exponent = exponent * kDecimalRadix + digit;
      } else {
        if (fractional) {
          if (fraction == std::numeric_limits<std::int64_t>::max())
            throw AdmissionFailure(AdmissionCode::kSizeLimit);
          ++fraction;
        }
        if (digit) {
          if (out.zero) out.first = at;
          out.zero = false;
          out.last = at + 1;
          trailing = 0;
        } else if (!out.zero) {
          ++trailing;
        }
        if (!out.zero) ++out.digits;
      }
    } else {
      out.exponent_overflow = true;
    }
  }
  if (out.zero) return out;
  out.digits -= static_cast<std::size_t>(trailing);
  if (out.exponent_overflow) return out;
  if (out.exponent_negative) {
    if (exponent > std::numeric_limits<std::int64_t>::max() - fraction) {
      out.exponent_overflow = true;
      return out;
    }
    out.power = -exponent - fraction + trailing;
  } else {
    if (exponent > std::numeric_limits<std::int64_t>::max() - trailing) {
      out.exponent_overflow = true;
      return out;
    }
    out.power = exponent + trailing - fraction;
  }
  return out;
}
bool OutsideNodeRange(const Decimal& value) {
  if (value.zero) return false;
  if (value.exponent_overflow) return !value.exponent_negative;
  if (value.power > 0) return true;
  // Avoid signed addition/negation overflow when classifying tiny decimals.
  const auto fractional = static_cast<std::uint64_t>(-(value.power + 1)) + 1;
  if (value.power == 0)
    return value.digits != 1 || value.text[value.first] != '1';
  return value.digits > fractional;
}
bool ExactNode(const std::array<Decimal, kQuaternionComponents>& values,
               QuaternionWorkspace& space, QuaternionWork& work) {
  auto& [source_sum, coefficient, square, denominator_squared, scaled_sum,
         lower_bound, upper_bound, factor] = space.decimal;
  std::int64_t scale = 0;
  for (const auto& value : values)
    if (!value.zero) scale = std::max(scale, -value.power);
  Clear(source_sum, work);
  for (const auto& value : values) {
    Clear(coefficient, work);
    if (value.zero) continue;
    for (auto at = value.first; at < value.last; ++at) {
      work.ByteVisit();
      const auto c = value.text[at];
      if (c != '.')
        Require(MultiplySmall(coefficient, kDecimalRadix,
                              static_cast<std::uint32_t>(c - '0'), work));
    }
    for (std::int64_t i = 0; i < scale + value.power; ++i)
      Require(MultiplySmall(coefficient, kDecimalRadix, 0, work));
    Require(Multiply(square, coefficient, coefficient, work));
    Require(Add(source_sum, source_sum, square, work));
  }
  Set(denominator_squared, 1, work);
  for (std::int64_t i = 0; i < 2 * scale; ++i)
    Require(MultiplySmall(denominator_squared, kDecimalRadix, 0, work));
  Set(factor, kNodeBandDenominator, work);
  Require(Multiply(scaled_sum, source_sum, factor, work));
  Set(factor, kNodeBandDenominator - kNodeBandNumerator, work);
  Require(Multiply(lower_bound, denominator_squared, factor, work));
  Set(factor, kNodeBandDenominator + kNodeBandNumerator, work);
  Require(Multiply(upper_bound, denominator_squared, factor, work));
  return Compare(scaled_sum, lower_bound, work) >= 0 &&
         Compare(scaled_sum, upper_bound, work) <= 0;
}
}  // namespace

std::uint64_t QuaternionNodeByteVisitBound(
    std::span<const GltfRawNumberView, kQuaternionComponents> authored) {
  auto bound = kQuaternionNodeExtraByteVisits;
  for (const auto& number : authored) {
    if (number.lexeme.size() >
        std::numeric_limits<std::uint64_t>::max() - bound)
      throw AdmissionFailure(AdmissionCode::kSizeLimit);
    bound += static_cast<std::uint64_t>(number.lexeme.size());
  }
  return bound;
}

void QuaternionWork::IntegerStep() {
  const auto interval =
      std::min(kLimbCheckpoint, policy.max_relationship_block);
  if (!interval) throw AdmissionFailure(AdmissionCode::kInvalidObject);
  Increment(integer_steps);
  if (integer_steps % interval == 0) allocation.Checkpoint();
}
void QuaternionWork::ByteVisit() {
  if (!policy.cancellation_block_bytes)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  Increment(byte_visits);
  if (byte_visits % policy.cancellation_block_bytes == 0)
    allocation.Checkpoint();
}
void QuaternionWork::Checkpoint() const { allocation.Checkpoint(); }

QuaternionPredicateResult EvaluateQuaternionWords(
    std::span<const std::uint32_t, kQuaternionComponents> words,
    GltfQuaternionEncoding encoding, GltfQuaternionRole role,
    QuaternionWorkspace& space, QuaternionWork& work,
    GltfDeformationNumericObservation& observation) {
  work.Checkpoint();
  RecordGltfExactBlock(observation);
  if (role != GltfQuaternionRole::kAnimationValue &&
      role != GltfQuaternionRole::kCubicTangent)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  const bool floating = encoding == GltfQuaternionEncoding::kFloat32;
  const bool signed_code = encoding == GltfQuaternionEncoding::kSnorm8 ||
                           encoding == GltfQuaternionEncoding::kSnorm16;
  const bool small = encoding == GltfQuaternionEncoding::kSnorm8 ||
                     encoding == GltfQuaternionEncoding::kUnorm8;
  if (!floating && encoding != GltfQuaternionEncoding::kSnorm8 &&
      encoding != GltfQuaternionEncoding::kUnorm8 &&
      encoding != GltfQuaternionEncoding::kSnorm16 &&
      encoding != GltfQuaternionEncoding::kUnorm16)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  const std::uint32_t width = small ? kByteComponentBits : kShortComponentBits;
  const std::uint32_t maximum_code = (std::uint32_t{1} << width) - 1;
  const std::uint32_t denominator =
      signed_code ? maximum_code >> 1 : maximum_code;
  std::array<std::int64_t, kQuaternionComponents> numerators{};
  std::optional<std::uint32_t> large;
  std::uint64_t exact_sum = 0;
  for (std::uint32_t i = 0; i < words.size(); ++i) {
    work.IntegerStep();
    const auto word = words[i];
    if (floating) {
      if ((word & kFloatExponentMask) == kFloatExponentMask)
        return Refuse(QuaternionDecision::kNonfinite,
                      GltfQuaternionDiagnosticRule::kAnimationNonfinite, i);
      if (!large && (word & kFloatMagnitudeMask) >= kFloatMagnitudeTwo)
        large = i;
    } else {
      if (word > maximum_code)
        throw AdmissionFailure(AdmissionCode::kInvalidObject);
      auto value = static_cast<std::int64_t>(word);
      if (signed_code && word > denominator) value -= std::int64_t{1} << width;
      value = std::max(value, -static_cast<std::int64_t>(denominator));
      numerators[i] = value;
      space.normalized[i] = static_cast<std::uint64_t>(value * value);
      exact_sum += space.normalized[i];
    }
  }
  if (role == GltfQuaternionRole::kCubicTangent) {
    work.Checkpoint();
    return {};
  }
  if (large)
    return Refuse(QuaternionDecision::kNonunit,
                  GltfQuaternionDiagnosticRule::kFloatMagnitudeNonunit, *large);
  if (!EstablishGltfNumericalProfile(observation, work.partition, work.caller))
    return {QuaternionDecision::kNumericRefusal, {}, {}};
  work.Checkpoint();
  std::array<double, kQuaternionComponents> values{};
  for (std::size_t i = 0; i < values.size(); ++i) {
    work.IntegerStep();
    if (floating) {
      values[i] = static_cast<double>(std::bit_cast<float>(words[i]));
    } else {
      volatile double converted =
          static_cast<double>(numerators[i]) / static_cast<double>(denominator);
      values[i] = converted;
    }
  }
  const auto sum = FilterSum(values);
  const auto den_squared = std::uint64_t{denominator} * denominator;
  const auto radius = std::uint64_t{2} * denominator + 1;
  const Band band = floating
                        ? Band{kFloatBandDenominator, kFloatBandNumerator,
                               kFloatFilterErrorPower}
                        : Band{den_squared, radius, kFloatFilterErrorPower};
  const auto filter = CertifiedFilter(sum, band, space, work);
  bool admitted = filter == FilterResult::kInside;
  if (filter == FilterResult::kOverlap) {
    RecordGltfExactBlock(observation);
    admitted = floating ? ExactFloat(words, space, work)
                        : exact_sum >= den_squared - radius &&
                              exact_sum <= den_squared + radius;
  }
  work.Checkpoint();
  if (admitted) return {};
  return Refuse(QuaternionDecision::kNonunit,
                filter == FilterResult::kOutside
                    ? GltfQuaternionDiagnosticRule::kFilterBandNonunit
                    : GltfQuaternionDiagnosticRule::kExactBandNonunit);
}

QuaternionPredicateResult EvaluateNodeQuaternion(
    std::span<const GltfRawNumberView, kQuaternionComponents> authored,
    std::span<const std::uint32_t, kQuaternionComponents>
        actual_projected_words,
    QuaternionWorkspace& space, QuaternionWork& work,
    GltfDeformationNumericObservation& observation) {
  work.Checkpoint();
  RecordGltfExactBlock(observation);
  std::array<Decimal, kQuaternionComponents> values;
  for (std::uint32_t i = 0; i < authored.size(); ++i) {
    values[i] = Parse(authored[i].lexeme, work);
    if (OutsideNodeRange(values[i]))
      return Refuse(QuaternionDecision::kRange,
                    GltfQuaternionDiagnosticRule::kNodeExactRange, i);
    if (!values[i].zero &&
        (values[i].exponent_overflow || values[i].digits > kDecimalDigits ||
         values[i].power < -kDecimalScale))
      return Refuse(QuaternionDecision::kRepresentation,
                    GltfQuaternionDiagnosticRule::kRepresentationEnvelope, i);
    work.IntegerStep();
    if ((actual_projected_words[i] & kFloatExponentMask) == kFloatExponentMask)
      throw AdmissionFailure(AdmissionCode::kInvalidObject);
  }
  if (!EstablishGltfNumericalProfile(observation, work.partition, work.caller))
    return {QuaternionDecision::kNumericRefusal, {}, {}};
  work.Checkpoint();
  std::array<double, kQuaternionComponents> projected{};
  for (std::size_t i = 0; i < projected.size(); ++i) {
    work.IntegerStep();
    projected[i] =
        static_cast<double>(std::bit_cast<float>(actual_projected_words[i]));
  }
  const auto filter = CertifiedFilter(
      FilterSum(projected),
      {kNodeBandDenominator, kNodeBandNumerator, kNodeFilterErrorPower}, space,
      work);
  bool admitted = filter == FilterResult::kInside;
  if (filter == FilterResult::kOverlap) {
    RecordGltfExactBlock(observation);
    admitted = ExactNode(values, space, work);
  }
  work.Checkpoint();
  if (admitted) return {};
  return Refuse(QuaternionDecision::kNonunit,
                filter == FilterResult::kOutside
                    ? GltfQuaternionDiagnosticRule::kFilterBandNonunit
                    : GltfQuaternionDiagnosticRule::kExactBandNonunit);
}
}  // namespace meshvale::interchange::gltf_detail
