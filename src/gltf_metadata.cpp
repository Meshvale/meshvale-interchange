// SPDX-License-Identifier: Apache-2.0
#include "gltf_metadata.h"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <system_error>

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr std::uint64_t kDecimalRadix = 10;
struct Decimal {
  std::string_view text;
  std::size_t start;
  std::size_t end;
  std::size_t significant_digits;
  std::int64_t power;
  bool negative;
  bool zero;
};
Decimal ParseDecimal(std::string_view text) {
  Decimal result{text, 0, 0, 0, 0, false, true};
  if (text.empty())
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kExactInteger);
  std::size_t at = 0;
  result.negative = text[at] == '-';
  if (result.negative) ++at;
  bool fraction = false;
  std::int64_t fraction_digits = 0;
  std::size_t trailing_zeros = 0;
  for (; at < text.size() && text[at] != 'e' && text[at] != 'E'; ++at) {
    if (text[at] == '.') {
      fraction = true;
      continue;
    }
    if (fraction) ++fraction_digits;
    if (text[at] != '0') {
      if (result.zero) result.start = at;
      result.zero = false;
      trailing_zeros = 0;
      result.end = at + 1;
    } else if (!result.zero) {
      ++trailing_zeros;
    }
    if (!result.zero) ++result.significant_digits;
  }
  std::int64_t exponent = 0;
  bool exponent_negative = false;
  if (at < text.size()) {
    ++at;
    if (text[at] == '+' || text[at] == '-') {
      exponent_negative = text[at] == '-';
      ++at;
    }
    for (; at < text.size(); ++at) {
      const auto digit = static_cast<std::int64_t>(text[at] - '0');
      // Zero has the same exact value even with an unrepresentable exponent.
      if (result.zero) continue;
      constexpr auto kExponentMaximum =
          std::numeric_limits<std::int64_t>::max();
      if (exponent >
          (kExponentMaximum - digit) / static_cast<std::int64_t>(kDecimalRadix))
        Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kUnsupportedNumberRepresentation);
      exponent = exponent * static_cast<std::int64_t>(kDecimalRadix) + digit;
    }
  }
  if (result.zero) return result;
  result.significant_digits -= trailing_zeros;
  const auto zeros = static_cast<std::int64_t>(trailing_zeros);
  if (exponent_negative) {
    if (exponent > std::numeric_limits<std::int64_t>::max() - fraction_digits)
      Fail(GltfStatus::kUnsupportedProfile,
           GltfDiagnosticCode::kUnsupportedNumberRepresentation);
    result.power = -exponent - fraction_digits + zeros;
  } else {
    if (zeros > std::numeric_limits<std::int64_t>::max() - exponent)
      Fail(GltfStatus::kUnsupportedProfile,
           GltfDiagnosticCode::kUnsupportedNumberRepresentation);
    result.power = exponent + zeros - fraction_digits;
  }
  return result;
}
std::uint64_t Magnitude(const Decimal& value, std::uint64_t maximum) {
  if (value.zero) return 0;
  if (value.power < 0)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kExactInteger);
  constexpr std::size_t kUint64DecimalDigits = 20;
  if (value.power > static_cast<std::int64_t>(kUint64DecimalDigits) ||
      value.significant_digits > kUint64DecimalDigits - value.power)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow);
  std::uint64_t output = 0;
  auto append = [&](std::uint64_t digit) {
    if (output > (maximum - digit) / kDecimalRadix)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow);
    output = output * kDecimalRadix + digit;
  };
  for (auto at = value.start; at < value.end; ++at)
    if (value.text[at] != '.')
      append(static_cast<std::uint64_t>(value.text[at] - '0'));
  for (std::int64_t count = 0; count < value.power; ++count) append(0);
  return output;
}
}  // namespace
std::uint64_t ExactUnsigned(std::string_view text) {
  const auto value = ParseDecimal(text);
  if (value.negative && !value.zero)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
  return Magnitude(value, std::numeric_limits<std::uint64_t>::max());
}
std::int64_t ExactSigned(std::string_view text) {
  const auto value = ParseDecimal(text);
  constexpr auto kPositiveMaximum = std::numeric_limits<std::int64_t>::max();
  const auto maximum = static_cast<std::uint64_t>(kPositiveMaximum) +
                       static_cast<std::uint64_t>(value.negative);
  const auto magnitude = Magnitude(value, maximum);
  if (!value.negative) return static_cast<std::int64_t>(magnitude);
  if (magnitude == maximum) return std::numeric_limits<std::int64_t>::min();
  return -static_cast<std::int64_t>(magnitude);
}
int CompareDecimal(std::string_view left, std::string_view right) {
  const auto a = ParseDecimal(left);
  const auto b = ParseDecimal(right);
  if (a.zero && b.zero) return 0;
  const bool a_negative = a.negative && !a.zero;
  const bool b_negative = b.negative && !b.zero;
  if (a_negative != b_negative) return a_negative ? -1 : 1;
  int magnitude = 0;
  if (a.zero || b.zero) {
    magnitude = a.zero ? -1 : 1;
  } else {
    auto order = [](const Decimal& value) {
      const auto digits = static_cast<std::int64_t>(value.significant_digits);
      if (value.power > std::numeric_limits<std::int64_t>::max() - digits)
        Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kUnsupportedNumberRepresentation);
      return value.power + digits;
    };
    const auto a_order = order(a);
    const auto b_order = order(b);
    if (a_order != b_order) {
      magnitude = a_order < b_order ? -1 : 1;
    } else {
      auto ai = a.start;
      auto bi = b.start;
      while (ai < a.end || bi < b.end) {
        if (ai < a.end && a.text[ai] == '.') ++ai;
        if (bi < b.end && b.text[bi] == '.') ++bi;
        const auto ad = ai < a.end ? a.text[ai++] : '0';
        const auto bd = bi < b.end ? b.text[bi++] : '0';
        if (ad != bd) {
          magnitude = ad < bd ? -1 : 1;
          break;
        }
      }
    }
  }
  return a_negative ? -magnitude : magnitude;
}
std::uint64_t ReadExactUnsigned(const JsonSnapshot& document,
                                std::size_t node) {
  if (document.Kind(node) != JsonKind::kNumber)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldType);
  return ExactUnsigned(document.Number(node));
}
std::int64_t ReadExactSigned(const JsonSnapshot& document, std::size_t node) {
  if (document.Kind(node) != JsonKind::kNumber)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldType);
  return ExactSigned(document.Number(node));
}
std::uint32_t ReadFloatBits(const JsonSnapshot& document, std::size_t node) {
  if (document.Kind(node) != JsonKind::kNumber)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldType);
  const auto text = document.Number(node);
  const auto decimal = ParseDecimal(text);
  if (decimal.zero)
    return std::bit_cast<std::uint32_t>(decimal.negative ? -0.0f : 0.0f);
  // Largest finite binary32, expressed exactly so range checks precede
  // rounding.
  constexpr std::string_view kBinary32Maximum =
      "340282346638528859811704183484516925440";
  auto positive = text;
  if (decimal.negative) positive.remove_prefix(1);
  if (CompareDecimal(positive, kBinary32Maximum) > 0)
    Fail(GltfStatus::kUnsupportedProfile,
         GltfDiagnosticCode::kUnsupportedNumberRepresentation);
  float value = 0;
  const auto converted = std::from_chars(text.data(), text.data() + text.size(),
                                         value, std::chars_format::general);
  if (converted.ec != std::errc{} ||
      converted.ptr != text.data() + text.size() || !std::isfinite(value) ||
      value == 0)
    Fail(GltfStatus::kUnsupportedProfile,
         GltfDiagnosticCode::kUnsupportedNumberRepresentation);
  return std::bit_cast<std::uint32_t>(value);
}
void CheckMetadataRelationships(AssetState& state) {
  constexpr std::string_view kAccessorsKey = "accessors";
  constexpr std::string_view kTypeKey = "type";
  constexpr std::string_view kComponentTypeKey = "componentType";
  constexpr std::array<std::string_view, 2> kBoundKeys = {"min", "max"};
  constexpr std::array<std::string_view, 7> kShapes = {
      "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"};
  constexpr std::array<std::uint64_t, 7> kWidths = {1, 2, 3, 4, 4, 9, 16};
  constexpr std::uint64_t kInt8Component = 5120;
  constexpr std::uint64_t kUint8Component = 5121;
  constexpr std::uint64_t kInt16Component = 5122;
  constexpr std::uint64_t kUint16Component = 5123;
  constexpr std::uint64_t kUint32Component = 5125;
  constexpr std::uint64_t kFloatComponent = 5126;
  struct IntegerComponentRange {
    std::uint64_t component;
    std::string_view minimum;
    std::string_view maximum;
  };
  constexpr std::array<IntegerComponentRange, 5> kIntegerComponentRanges = {
      {{kInt8Component, "-128", "127"},
       {kUint8Component, "0", "255"},
       {kInt16Component, "-32768", "32767"},
       {kUint16Component, "0", "65535"},
       {kUint32Component, "0", "4294967295"}}};
  constexpr std::string_view kCamerasKey = "cameras";
  constexpr std::string_view kNearKey = "znear";
  constexpr std::string_view kFarKey = "zfar";
  constexpr std::string_view kOrthographic = "orthographic";
  constexpr std::string_view kPerspective = "perspective";
  constexpr std::array<std::string_view, 2> kMagnificationKeys = {"xmag",
                                                                  "ymag"};
  const auto& doc = state.document;
  const auto accessors = FindMember(doc, 0, kAccessorsKey);
  if (accessors)
    for (auto row = doc.FirstChild(*accessors); row;
         row = doc.NextSibling(*row)) {
      state.allocation.Checkpoint();
      const auto shape = doc.String(*FindMember(doc, *row, kTypeKey));
      std::uint64_t width = 0;
      for (std::size_t i = 0; i < kShapes.size(); ++i)
        if (shape == kShapes[i]) width = kWidths[i];
      const auto component =
          ReadExactUnsigned(doc, *FindMember(doc, *row, kComponentTypeKey));
      for (const auto key : kBoundKeys) {
        const auto bound = FindMember(doc, *row, key);
        if (!bound) continue;
        if (ChildCount(doc, *bound) != width)
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreArrayLength);
        for (auto number = doc.FirstChild(*bound); number;
             number = doc.NextSibling(*number)) {
          const auto value = doc.Number(*number);
          std::string_view lower;
          std::string_view upper;
          for (const auto& range : kIntegerComponentRanges)
            if (component == range.component) {
              lower = range.minimum;
              upper = range.maximum;
              break;
            }
          if (component == kFloatComponent) {
            (void)ReadFloatBits(doc, *number);
            continue;
          }
          if (CompareDecimal(value, lower) < 0 ||
              CompareDecimal(value, upper) > 0)
            Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
          (void)ReadExactSigned(doc, *number);
        }
      }
      const auto minimum = FindMember(doc, *row, kBoundKeys[0]);
      const auto maximum = FindMember(doc, *row, kBoundKeys[1]);
      if (minimum && maximum)
        for (std::uint64_t i = 0; i < width; ++i)
          if (CompareDecimal(doc.Number(*ChildAt(doc, *minimum, i)),
                             doc.Number(*ChildAt(doc, *maximum, i))) > 0)
            Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
    }
  const auto cameras = FindMember(doc, 0, kCamerasKey);
  if (cameras)
    for (auto row = doc.FirstChild(*cameras); row;
         row = doc.NextSibling(*row)) {
      const auto type = doc.String(*FindMember(doc, *row, kTypeKey));
      const auto block_node = FindMember(doc, *row, type);
      if (!block_node)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired);
      if (FindMember(doc, *row, kOrthographic) &&
          FindMember(doc, *row, kPerspective))
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
      const auto block = *block_node;
      const auto far = FindMember(doc, block, kFarKey);
      const auto near = *FindMember(doc, block, kNearKey);
      if (far && CompareDecimal(doc.Number(*far), doc.Number(near)) <= 0)
        Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
      if (type == kOrthographic)
        for (const auto key : kMagnificationKeys)
          if (CompareDecimal(doc.Number(*FindMember(doc, block, key)), "0") ==
              0)
            Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
    }
}
}  // namespace meshvale::interchange::gltf_detail
