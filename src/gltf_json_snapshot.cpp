// SPDX-License-Identifier: Apache-2.0
#include "gltf_json_snapshot.h"

#define JSONCONS_NO_DEPRECATED
#include <algorithm>
#include <cstdint>
#include <jsoncons/json_parser.hpp>
#include <jsoncons/json_visitor.hpp>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr auto kNone = std::numeric_limits<std::size_t>::max();
struct Range {
  std::size_t start = 0;
  std::size_t size = 0;
};
struct Node {
  JsonKind kind;
  std::size_t parent;
  std::size_t first_child = kNone;
  std::size_t last_child = kNone;
  std::size_t next_sibling = kNone;
  Range key;
  Range value;
  bool boolean = false;
};
struct CaptureFailure {
  JsonCaptureCode code;
};
[[noreturn]] void Malformed() {
  throw CaptureFailure{JsonCaptureCode::kMalformed};
}
void RequireExtent(std::size_t current, std::size_t extra, std::size_t limit) {
  if (current > limit || extra > limit - current)
    throw CaptureFailure{JsonCaptureCode::kSizeLimit};
}
// Reject overlong UTF8, surrogate code points, incomplete sequences and values
// above U+10FFFF. Decoded NUL is a scalar and remains part of the owned string.
void ValidateScalars(std::string_view text,
                     const AllocationContext& allocation) {
  // UTF8 lead/continuation fields and Unicode scalar boundaries.
  constexpr unsigned char kAsciiLimit = 0x80;
  constexpr unsigned char kTwoByteLeadMinimum = 0xc2;
  constexpr unsigned char kTwoByteLeadMaximum = 0xdf;
  constexpr unsigned char kThreeByteLeadMinimum = 0xe0;
  constexpr unsigned char kThreeByteLeadMaximum = 0xef;
  constexpr unsigned char kFourByteLeadMinimum = 0xf0;
  constexpr unsigned char kFourByteLeadMaximum = 0xf4;
  constexpr unsigned char kTwoByteLeadPayloadMask = 0x1f;
  constexpr unsigned char kThreeByteLeadPayloadMask = 0xf;
  constexpr unsigned char kFourByteLeadPayloadMask = 7;
  constexpr unsigned char kContinuationTagMask = 0xc0;
  constexpr unsigned char kContinuationTag = 0x80;
  constexpr unsigned char kContinuationPayloadMask = 0x3f;
  constexpr unsigned kContinuationPayloadBits = 6;
  constexpr std::size_t kThreeByteContinuationCount = 2;
  constexpr std::size_t kFourByteContinuationCount = 3;
  constexpr std::uint32_t kTwoByteScalarMinimum = 0x80;
  constexpr std::uint32_t kThreeByteScalarMinimum = 0x800;
  constexpr std::uint32_t kFourByteScalarMinimum = 0x10000;
  constexpr std::uint32_t kScalarMaximum = 0x10ffff;
  constexpr std::uint32_t kSurrogateMinimum = 0xd800;
  constexpr std::uint32_t kSurrogateMaximum = 0xdfff;
  for (std::size_t i = 0; i < text.size();) {
    allocation.Checkpoint();
    const auto first = static_cast<unsigned char>(text[i++]);
    if (first < kAsciiLimit) continue;
    std::size_t tails = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (first >= kTwoByteLeadMinimum && first <= kTwoByteLeadMaximum) {
      tails = 1;
      value = first & kTwoByteLeadPayloadMask;
      minimum = kTwoByteScalarMinimum;
    } else if (first >= kThreeByteLeadMinimum &&
               first <= kThreeByteLeadMaximum) {
      tails = kThreeByteContinuationCount;
      value = first & kThreeByteLeadPayloadMask;
      minimum = kThreeByteScalarMinimum;
    } else if (first >= kFourByteLeadMinimum && first <= kFourByteLeadMaximum) {
      tails = kFourByteContinuationCount;
      value = first & kFourByteLeadPayloadMask;
      minimum = kFourByteScalarMinimum;
    } else {
      Malformed();
    }
    if (tails > text.size() - i) Malformed();
    for (std::size_t t = 0; t < tails; ++t) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & kContinuationTagMask) != kContinuationTag) Malformed();
      value = (value << kContinuationPayloadBits) |
              (next & kContinuationPayloadMask);
    }
    if (value < minimum || value > kScalarMaximum ||
        (value >= kSurrogateMinimum && value <= kSurrogateMaximum))
      Malformed();
  }
}
unsigned Hex4(std::string_view text, std::size_t offset) {
  constexpr std::size_t kUnicodeEscapeHexDigits = 4;
  constexpr unsigned kHexLetterDigitOffset = 10;
  constexpr unsigned kHexRadix = 16;
  if (offset > text.size() || text.size() - offset < kUnicodeEscapeHexDigits)
    Malformed();
  unsigned value = 0;
  for (std::size_t i = 0; i < kUnicodeEscapeHexDigits; ++i) {
    const auto c = text[offset + i];
    unsigned digit = 0;
    if (c >= '0' && c <= '9')
      digit = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f')
      digit = static_cast<unsigned>(c - 'a' + kHexLetterDigitOffset);
    else if (c >= 'A' && c <= 'F')
      digit = static_cast<unsigned>(c - 'A' + kHexLetterDigitOffset);
    else
      Malformed();
    value = value * kHexRadix + digit;
  }
  return value;
}
std::optional<std::size_t> OptionalIndex(std::size_t index) {
  return index == kNone ? std::nullopt : std::optional(index);
}
}  // namespace

struct JsonSnapshotData {
  explicit JsonSnapshotData(const AllocationContext& allocation)
      : source(Allocator<char>(allocation)),
        strings(Allocator<char>(allocation)),
        nodes(Allocator<Node>(allocation)) {}
  std::vector<char, Allocator<char>> source;
  std::vector<char, Allocator<char>> strings;
  std::vector<Node, Allocator<Node>> nodes;
  [[nodiscard]] std::string_view Original() const noexcept {
    return {source.data(), source.size()};
  }
  [[nodiscard]] std::string_view Text(Range range) const noexcept {
    return range.size == 0
               ? std::string_view{}
               : std::string_view(strings.data() + range.start, range.size);
  }
  [[nodiscard]] const Node& At(std::size_t index) const {
    if (index >= nodes.size())
      throw AdmissionFailure(AdmissionCode::kInvalidObject);
    return nodes[index];
  }
};
namespace {
using Numbers = std::vector<Range, Allocator<Range>>;
// This scanner retains lexemes independently of parser numeric conversions;
// jsoncons still owns syntax validation. Quoted numeric-looking text is
// skipped.
void ScanOriginal(std::string_view source, Numbers& numbers, std::size_t limit,
                  const AllocationContext& allocation) {
  // JSON escapes comprise a backslash/tag, then four hex digits for Unicode.
  constexpr std::size_t kEscapePrefixBytes = 2;
  constexpr std::size_t kUnicodeEscapeBytes = 6;
  constexpr std::size_t kSurrogatePairEscapeBytes = 2 * kUnicodeEscapeBytes;
  constexpr unsigned kHighSurrogateMinimum = 0xd800;
  constexpr unsigned kHighSurrogateMaximum = 0xdbff;
  constexpr unsigned kLowSurrogateMinimum = 0xdc00;
  constexpr unsigned kLowSurrogateMaximum = 0xdfff;
  ValidateScalars(source, allocation);
  for (std::size_t i = 0; i < source.size();) {
    allocation.Checkpoint();
    if (source[i] == '"') {
      ++i;
      while (i < source.size() && source[i] != '"') {
        allocation.Checkpoint();
        if (source[i] != '\\') {
          ++i;
          continue;
        }
        if (source.size() - i < kEscapePrefixBytes) Malformed();
        if (source[i + 1] != 'u') {
          i += kEscapePrefixBytes;
          continue;
        }
        const auto code = Hex4(source, i + kEscapePrefixBytes);
        if (code >= kLowSurrogateMinimum && code <= kLowSurrogateMaximum)
          Malformed();
        if (code >= kHighSurrogateMinimum && code <= kHighSurrogateMaximum) {
          if (source.size() - i < kSurrogatePairEscapeBytes ||
              source[i + kUnicodeEscapeBytes] != '\\' ||
              source[i + kUnicodeEscapeBytes + 1] != 'u')
            Malformed();
          const auto low =
              Hex4(source, i + kUnicodeEscapeBytes + kEscapePrefixBytes);
          if (low < kLowSurrogateMinimum || low > kLowSurrogateMaximum)
            Malformed();
          i += kSurrogatePairEscapeBytes;
        } else {
          i += kUnicodeEscapeBytes;
        }
      }
      if (i == source.size()) Malformed();
      ++i;
    } else if (source[i] == '-' || (source[i] >= '0' && source[i] <= '9')) {
      const auto start = i++;
      while (i < source.size() &&
             ((source[i] >= '0' && source[i] <= '9') || source[i] == '.' ||
              source[i] == 'e' || source[i] == 'E' || source[i] == '+' ||
              source[i] == '-')) {
        allocation.Checkpoint();
        ++i;
      }
      RequireExtent(numbers.size(), 1, limit);
      numbers.push_back({start, i - start});
    } else {
      ++i;
    }
  }
}
class Builder : public jsoncons::basic_default_json_visitor<char> {
 public:
  Builder(JsonSnapshotData& data, const Numbers& numbers,
          const JsonCaptureLimits& limits, const AllocationContext& allocation)
      : data_(data),
        numbers_(numbers),
        limits_(limits),
        allocation_(allocation),
        stack_(Allocator<std::size_t>(allocation)) {}
  void Finish() const {
    if (!stack_.empty() || number_ != numbers_.size() || data_.nodes.empty())
      Malformed();
  }

 private:
  Range Copy(std::string_view text) {
    ValidateScalars(text, allocation_);
    RequireExtent(data_.strings.size(), text.size(), limits_.decoded_bytes);
    const Range range{data_.strings.size(), text.size()};
    data_.strings.insert(data_.strings.end(), text.begin(), text.end());
    return range;
  }
  bool Add(JsonKind kind, Range value = {}, bool boolean = false) {
    allocation_.Checkpoint();
    RequireExtent(data_.nodes.size(), 1, limits_.nodes);
    const auto parent = stack_.empty() ? kNone : stack_.back();
    const auto index = data_.nodes.size();
    data_.nodes.push_back(
        {kind, parent, kNone, kNone, kNone, key_, value, boolean});
    if (parent != kNone) {
      auto& owner = data_.nodes[parent];
      if (owner.last_child == kNone)
        owner.first_child = index;
      else
        data_.nodes[owner.last_child].next_sibling = index;
      owner.last_child = index;
    }
    key_ = {};
    return true;
  }
  bool Begin(JsonKind kind) {
    Add(kind);
    stack_.push_back(data_.nodes.size() - 1);
    return true;
  }
  bool End() {
    allocation_.Checkpoint();
    if (stack_.empty()) Malformed();
    stack_.pop_back();
    return true;
  }
  bool Number() {
    if (number_ == numbers_.size()) Malformed();
    return Add(JsonKind::kNumber, numbers_[number_++]);
  }
  bool visit_begin_object(jsoncons::semantic_tag, const jsoncons::ser_context&,
                          std::error_code&) override {
    return Begin(JsonKind::kObject);
  }
  bool visit_end_object(const jsoncons::ser_context&,
                        std::error_code&) override {
    return End();
  }
  bool visit_begin_array(jsoncons::semantic_tag, const jsoncons::ser_context&,
                         std::error_code&) override {
    return Begin(JsonKind::kArray);
  }
  bool visit_end_array(const jsoncons::ser_context&,
                       std::error_code&) override {
    return End();
  }
  bool visit_key(const jsoncons::string_view& text,
                 const jsoncons::ser_context&, std::error_code&) override {
    allocation_.Checkpoint();
    if (stack_.empty()) Malformed();
    const auto& object = data_.nodes[stack_.back()];
    const std::string_view decoded(text.data(), text.size());
    for (auto child = object.first_child; child != kNone;
         child = data_.nodes[child].next_sibling) {
      allocation_.Checkpoint();
      if (data_.Text(data_.nodes[child].key) == decoded)
        throw CaptureFailure{JsonCaptureCode::kDuplicateKey};
    }
    key_ = Copy(decoded);
    return true;
  }
  bool visit_string(const jsoncons::string_view& text,
                    jsoncons::semantic_tag tag, const jsoncons::ser_context&,
                    std::error_code&) override {
    if (tag == jsoncons::semantic_tag::bigint ||
        tag == jsoncons::semantic_tag::bigdec)
      return Number();
    return Add(JsonKind::kString, Copy({text.data(), text.size()}));
  }
  bool visit_uint64(std::uint64_t, jsoncons::semantic_tag,
                    const jsoncons::ser_context&, std::error_code&) override {
    return Number();
  }
  bool visit_int64(std::int64_t, jsoncons::semantic_tag,
                   const jsoncons::ser_context&, std::error_code&) override {
    return Number();
  }
  bool visit_double(double, jsoncons::semantic_tag,
                    const jsoncons::ser_context&, std::error_code&) override {
    return Number();
  }
  bool visit_bool(bool value, jsoncons::semantic_tag,
                  const jsoncons::ser_context&, std::error_code&) override {
    return Add(JsonKind::kBool, {}, value);
  }
  bool visit_null(jsoncons::semantic_tag, const jsoncons::ser_context&,
                  std::error_code&) override {
    return Add(JsonKind::kNull);
  }
  JsonSnapshotData& data_;
  const Numbers& numbers_;
  const JsonCaptureLimits& limits_;
  AllocationContext allocation_;
  std::vector<std::size_t, Allocator<std::size_t>> stack_;
  Range key_;
  std::size_t number_ = 0;
};
JsonCaptureCode AllocationCode(AdmissionCode code) {
  switch (code) {
    case AdmissionCode::kSizeLimit:
      return JsonCaptureCode::kSizeLimit;
    case AdmissionCode::kBudgetExceeded:
      return JsonCaptureCode::kBudgetExceeded;
    case AdmissionCode::kCanceled:
      return JsonCaptureCode::kCanceled;
    case AdmissionCode::kInvalidObject:
      break;
  }
  throw AdmissionFailure(code);
}
}  // namespace
JsonCaptureResult CaptureJsonSnapshot(std::string_view input,
                                      const JsonCaptureLimits& limits,
                                      const AllocationContext& allocation) {
  if (limits.source_bytes == 0 || limits.nodes == 0 ||
      limits.decoded_bytes == 0 || limits.nesting_depth <= 0)
    return {JsonCaptureCode::kInvalidLimits, std::nullopt};
  const auto host_extent =
      static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
  if (limits.source_bytes > host_extent || limits.decoded_bytes > host_extent ||
      limits.nodes > host_extent / sizeof(Node))
    return {JsonCaptureCode::kSizeLimit, std::nullopt};
  if (input.size() > limits.source_bytes)
    return {JsonCaptureCode::kSizeLimit, std::nullopt};
  try {
    allocation.Checkpoint();
    auto data = std::allocate_shared<JsonSnapshotData>(
        Allocator<JsonSnapshotData>(allocation), allocation);
    data->source.resize(input.size());
    // Bound the bytes copied between cooperative cancellation checkpoints.
    constexpr std::size_t kSourceCopyCheckpointBytes = 4096;
    for (std::size_t i = 0; i < input.size();) {
      allocation.Checkpoint();
      const auto count = std::min(input.size() - i, kSourceCopyCheckpointBytes);
      std::copy_n(input.data() + i, count, data->source.data() + i);
      i += count;
    }
    Numbers numbers{Allocator<Range>(allocation)};
    ScanOriginal(data->Original(), numbers, limits.nodes, allocation);
    Builder builder(*data, numbers, limits, allocation);
    jsoncons::json_options options;
    options.lossless_number(true)
        .lossless_bignum(true)
        .allow_comments(false)
        .allow_trailing_comma(false)
        .max_nesting_depth(limits.nesting_depth);
    jsoncons::basic_json_parser<char, Allocator<char>> parser(
        options, Allocator<char>(allocation));
    std::error_code error;
    parser.update(data->source.data(), data->source.size());
    parser.parse_some(builder, error);
    if (!error) parser.finish_parse(builder, error);
    if (!error) parser.check_done(error);
    if (error) {
      if (error == jsoncons::json_errc::max_nesting_depth_exceeded)
        throw CaptureFailure{JsonCaptureCode::kSizeLimit};
      Malformed();
    }
    builder.Finish();
    allocation.Checkpoint();
    return {JsonCaptureCode::kAccepted, JsonSnapshot(std::move(data))};
  } catch (const CaptureFailure& failure) {
    return {failure.code, std::nullopt};
  } catch (const AdmissionFailure& failure) {
    return {AllocationCode(failure.Code()), std::nullopt};
  }
}
JsonSnapshot::JsonSnapshot(std::shared_ptr<const JsonSnapshotData> data)
    : data_(std::move(data)) {}
const JsonSnapshotData& JsonSnapshot::Data() const {
  if (!data_) throw AdmissionFailure(AdmissionCode::kInvalidObject);
  return *data_;
}
std::string_view JsonSnapshot::Original() const { return Data().Original(); }
std::size_t JsonSnapshot::NodeCount() const { return Data().nodes.size(); }
JsonKind JsonSnapshot::Kind(std::size_t node) const {
  return Data().At(node).kind;
}
std::optional<std::size_t> JsonSnapshot::Parent(std::size_t node) const {
  return OptionalIndex(Data().At(node).parent);
}
std::optional<std::size_t> JsonSnapshot::FirstChild(std::size_t node) const {
  return OptionalIndex(Data().At(node).first_child);
}
std::optional<std::size_t> JsonSnapshot::NextSibling(std::size_t node) const {
  return OptionalIndex(Data().At(node).next_sibling);
}
std::string_view JsonSnapshot::Key(std::size_t node) const {
  return Data().Text(Data().At(node).key);
}
std::string_view JsonSnapshot::String(std::size_t node) const {
  const auto& value = Data().At(node);
  if (value.kind != JsonKind::kString)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  return Data().Text(value.value);
}
std::string_view JsonSnapshot::Number(std::size_t node) const {
  const auto& value = Data().At(node);
  if (value.kind != JsonKind::kNumber)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  return Data().Original().substr(value.value.start, value.value.size);
}
bool JsonSnapshot::Boolean(std::size_t node) const {
  const auto& value = Data().At(node);
  if (value.kind != JsonKind::kBool)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  return value.boolean;
}
}  // namespace meshvale::interchange::gltf_detail
