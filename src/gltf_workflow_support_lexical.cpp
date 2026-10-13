// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

#include "gltf_workflow_support_internal.h"

namespace meshvale::interchange::gltf_workflow_support_detail {
namespace {
constexpr std::size_t kNoNode = std::numeric_limits<std::size_t>::max();
constexpr std::uint64_t kUnsignedIntComponent = 5125;
constexpr std::string_view kByteLength = "byteLength";
constexpr std::string_view kBuffer = "buffer";
constexpr std::string_view kBufferView = "bufferView";
constexpr std::string_view kByteOffset = "byteOffset";
constexpr std::string_view kTarget = "target";
constexpr std::string_view kComponentType = "componentType";
constexpr std::string_view kCount = "count";
constexpr std::string_view kType = "type";
constexpr std::string_view kNormalized = "normalized";
constexpr std::string_view kUri = "uri";
constexpr std::string_view kScalarShape = "SCALAR";
constexpr std::string_view kFalseLiteral = "false";
constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::size_t kBase64InputBytes = 3;
constexpr std::size_t kBase64OutputBytes = 4;
constexpr std::size_t kBitsPerByte = 8;
constexpr std::size_t kBase64DigitBits = 6;
constexpr std::uint32_t kBase64DigitMask = (1U << kBase64DigitBits) - 1;
constexpr char kBase64Padding = '=';
struct Node {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::size_t key_begin = 0;
  std::size_t key_end = 0;
  std::size_t first = kNoNode;
  std::size_t next = kNoNode;
  std::size_t last = kNoNode;
  char kind = 0;
};
struct Frame {
  std::size_t node;
};
class LexicalIndex {
 public:
  LexicalIndex(const Operation& operation, std::span<const std::byte> bytes)
      : operation_(operation),
        text_(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        nodes_(gltf_detail::Allocator<Node>(operation.allocation)),
        stack_(gltf_detail::Allocator<Frame>(operation.allocation)) {
    Count(bytes.size(), operation.limits.max_json_bytes);
    Whitespace();
    Value(0, 0);
    while (!stack_.empty()) {
      Whitespace();
      auto parent = stack_.back().node;
      const char closer = nodes_[parent].kind == '{' ? '}' : ']';
      if (position_ >= text_.size()) Malformed();
      if (text_[position_] == closer) {
        nodes_[parent].end = ++position_;
        stack_.pop_back();
        continue;
      }
      if (nodes_[parent].first != kNoNode) {
        if (text_[position_] != ',') Malformed();
        ++position_;
        Whitespace();
      }
      std::size_t key_begin = 0;
      std::size_t key_end = 0;
      if (nodes_[parent].kind == '{') {
        key_begin = position_;
        String();
        key_end = position_;
        Whitespace();
        if (position_ >= text_.size() || text_[position_++] != ':') Malformed();
        Whitespace();
      }
      const auto child = nodes_.size();
      Value(key_begin, key_end);
      if (nodes_[parent].first == kNoNode)
        nodes_[parent].first = child;
      else
        nodes_[nodes_[parent].last].next = child;
      nodes_[parent].last = child;
    }
    Whitespace();
    if (position_ != text_.size()) Malformed();
  }
  const Node& At(std::size_t node) const { return nodes_[node]; }
  std::string_view Text(std::size_t begin, std::size_t end) const {
    return text_.substr(begin, end - begin);
  }
  std::string_view ValueText(std::size_t node) const {
    return Text(At(node).begin, At(node).end);
  }
  bool KeyIs(std::size_t node, std::string_view key) const {
    const auto& value = At(node);
    return DecodedEquals(value.key_begin, value.key_end, key);
  }
  bool StringIs(std::size_t node, std::string_view value) const {
    return At(node).kind == '"' &&
           DecodedEquals(At(node).begin, At(node).end, value);
  }
  bool DecodedEquals(std::size_t begin, std::size_t end,
                     std::string_view key) const {
    if (end < begin + 2) return false;
    auto position = begin + 1;
    std::size_t ordinal = 0;
    while (position < end - 1) {
      unsigned character = static_cast<unsigned char>(text_[position++]);
      if (character == '\\') {
        if (position >= end - 1) return false;
        character = static_cast<unsigned char>(text_[position++]);
        if (character == 'u') {
          character = 0;
          constexpr std::size_t kUnicodeHexDigits = 4;
          constexpr unsigned kHexadecimalRadix = 16;
          constexpr unsigned kHexLetterOffset = 10;
          for (std::size_t digit = 0; digit < kUnicodeHexDigits; ++digit) {
            if (position >= end - 1) return false;
            const auto hex = text_[position++];
            unsigned hex_value = 0;
            if (hex >= '0' && hex <= '9')
              hex_value = hex - '0';
            else if (hex >= 'a' && hex <= 'f')
              hex_value = hex - 'a' + kHexLetterOffset;
            else if (hex >= 'A' && hex <= 'F')
              hex_value = hex - 'A' + kHexLetterOffset;
            else
              return false;
            character = character * kHexadecimalRadix + hex_value;
          }
        } else {
          switch (character) {
            case 'b':
              character = '\b';
              break;
            case 'f':
              character = '\f';
              break;
            case 'n':
              character = '\n';
              break;
            case 'r':
              character = '\r';
              break;
            case 't':
              character = '\t';
              break;
            default:
              break;
          }
        }
      }
      if (ordinal >= key.size() ||
          character != static_cast<unsigned char>(key[ordinal++]))
        return false;
    }
    return ordinal == key.size();
  }
  std::optional<std::size_t> Member(std::size_t node,
                                    std::string_view key) const {
    for (auto child = At(node).first; child != kNoNode; child = At(child).next)
      if (KeyIs(child, key)) return child;
    return {};
  }
  std::optional<std::size_t> Child(std::size_t node,
                                   std::uint64_t ordinal) const {
    for (auto child = At(node).first; child != kNoNode;
         child = At(child).next) {
      if (ordinal == 0) return child;
      --ordinal;
    }
    return {};
  }
  std::size_t Children(std::size_t node) const {
    std::size_t count = 0;
    for (auto child = At(node).first; child != kNoNode; child = At(child).next)
      ++count;
    return count;
  }
  std::size_t Selected(const GltfPrimitiveHandle& primitive) const {
    auto meshes = Member(0, kMeshes);
    auto mesh = meshes ? Child(*meshes, primitive.MeshIndex()) : std::nullopt;
    auto primitives = mesh ? Member(*mesh, kPrimitives) : std::nullopt;
    auto selected = primitives ? Child(*primitives, primitive.PrimitiveIndex())
                               : std::nullopt;
    if (!selected) Malformed();
    return *selected;
  }

 private:
  [[noreturn]] void Malformed() const {
    Fail(Status::kOutputMismatch, Stage::kLexical, Code::kOutputJson, {}, {},
         {}, GltfSourceSpan{position_, 1});
  }
  void Tick() const {
    if (position_ % operation_.work.cancellation_block_bytes == 0)
      operation_.allocation.Checkpoint();
  }
  void Whitespace() {
    while (position_ < text_.size()) {
      const auto c = text_[position_];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
      Tick();
      ++position_;
    }
  }
  void String() {
    if (position_ >= text_.size() || text_[position_++] != '"') Malformed();
    bool escaped = false;
    while (position_ < text_.size()) {
      Tick();
      const auto c = text_[position_++];
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        return;
    }
    Malformed();
  }
  void Value(std::size_t key_begin, std::size_t key_end) {
    operation_.allocation.Checkpoint();
    Count(Add(nodes_.size(), 1), operation_.limits.max_json_nodes);
    Count(Add(stack_.size(), 1), operation_.limits.max_json_depth);
    if (position_ >= text_.size()) Malformed();
    const auto ordinal = nodes_.size();
    nodes_.push_back({position_, 0, key_begin, key_end});
    const auto kind = text_[position_];
    nodes_[ordinal].kind = kind;
    if (kind == '{' || kind == '[') {
      ++position_;
      stack_.push_back({ordinal});
      return;
    }
    if (kind == '"')
      String();
    else {
      const auto begin = position_;
      while (position_ < text_.size()) {
        const auto c = text_[position_];
        if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' ||
            c == '\n' || c == '\r')
          break;
        Tick();
        ++position_;
      }
      if (position_ == begin) Malformed();
    }
    nodes_[ordinal].end = position_;
  }
  const Operation& operation_;
  std::string_view text_;
  Vector<Node> nodes_;
  Vector<Frame> stack_;
  std::size_t position_ = 0;
};
struct Hole {
  std::size_t source_begin;
  std::size_t source_end;
  std::size_t output_begin;
  std::size_t output_end;
};
void BindingFailure() {
  Fail(Status::kOutputMismatch, Stage::kLexical, Code::kAppendedBinding);
}
std::uint64_t UInt(const LexicalIndex& index, std::size_t node) {
  const auto text = index.ValueText(node);
  std::uint64_t value = 0;
  auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    BindingFailure();
  return value;
}
std::uint64_t RequiredUInt(const LexicalIndex& index, std::size_t node,
                           std::string_view name) {
  const auto child = index.Member(node, name);
  if (!child) BindingFailure();
  return UInt(index, *child);
}
void DefaultUInt(const LexicalIndex& index, std::size_t node,
                 std::string_view name, std::uint64_t expected) {
  if (const auto child = index.Member(node, name);
      child && UInt(index, *child) != expected)
    BindingFailure();
}
void Fields(const LexicalIndex& index, std::size_t node,
            std::initializer_list<std::string_view> allowed) {
  if (index.At(node).kind != '{') BindingFailure();
  for (auto child = index.At(node).first; child != kNoNode;
       child = index.At(child).next) {
    bool admitted = false;
    for (auto key : allowed) admitted |= index.KeyIs(child, key);
    if (!admitted) BindingFailure();
  }
}
std::uint8_t IndexByte(geometry::MeshReadView candidate, std::size_t byte) {
  const auto value = candidate.corner_vertices[byte / kUint32Bytes];
  return static_cast<std::uint8_t>(value >>
                                   (kBitsPerByte * (byte % kUint32Bytes)));
}
struct UriCheck {
  const Operation& operation;
  std::string_view actual;
  static std::optional<Diagnostic> Check(void* opaque, std::size_t begin,
                                         std::size_t end, bool finalizing) {
    const auto& self = *static_cast<UriCheck*>(opaque);
    const auto candidate = self.operation.request.candidate.Source().View();
    const auto bytes = candidate.corner_vertices.size() * kUint32Bytes;
    for (auto ordinal = begin; ordinal < end; ++ordinal) {
      if (!finalizing &&
          ordinal % self.operation.work.cancellation_block_bytes == 0)
        self.operation.allocation.Checkpoint();
      char expected = kBase64Padding;
      if (ordinal < kDataUriPrefix.size())
        expected = kDataUriPrefix[ordinal];
      else {
        const auto position = ordinal - kDataUriPrefix.size();
        const auto group = position / kBase64OutputBytes * kBase64InputBytes;
        const auto digit = position % kBase64OutputBytes;
        const auto at = [&](std::size_t index) -> std::uint32_t {
          return index < bytes ? IndexByte(candidate, index) : 0;
        };
        const auto bits = (at(group) << (2 * kBitsPerByte)) |
                          (at(group + 1) << kBitsPerByte) | at(group + 2);
        if (digit < 2 || group + digit - 1 < bytes)
          expected =
              kBase64Alphabet[(bits >> ((kBase64InputBytes * kBitsPerByte) -
                                        kBase64DigitBits * (digit + 1))) &
                              kBase64DigitMask];
      }
      if (self.actual[ordinal] != expected)
        return Location(Stage::kLexical, Code::kAppendedBinding, {}, ordinal);
    }
    return {};
  }
};
std::size_t TableCount(const LexicalIndex& index, std::string_view key) {
  const auto table = index.Member(0, key);
  if (!table) return 0;
  if (index.At(*table).kind != '[') BindingFailure();
  return index.Children(*table);
}
void Appended(const Operation& operation, const LexicalIndex& source,
              const LexicalIndex& actual, Stats& stats) {
  const auto buffer_count = TableCount(source, kBuffers);
  const auto view_count = TableCount(source, kBufferViews);
  const auto accessor_count = TableCount(source, kAccessors);
  const auto buffer_table = actual.Member(0, kBuffers);
  const auto view_table = actual.Member(0, kBufferViews);
  const auto accessor_table = actual.Member(0, kAccessors);
  if (!buffer_table || !view_table || !accessor_table ||
      TableCount(actual, kBuffers) != buffer_count + 1 ||
      TableCount(actual, kBufferViews) != view_count + 1 ||
      TableCount(actual, kAccessors) != accessor_count + 1)
    BindingFailure();
  const auto buffer = *actual.Child(*buffer_table, buffer_count);
  const auto view = *actual.Child(*view_table, view_count);
  const auto accessor = *actual.Child(*accessor_table, accessor_count);
  const auto selected =
      actual.Selected(operation.request.source.SourcePrimitive());
  const auto count =
      operation.request.candidate.Source().View().corner_vertices.size();
  const auto bytes = count * kUint32Bytes;
  Fields(actual, buffer, {kByteLength, kUri});
  Fields(actual, view, {kBuffer, kByteLength, kByteOffset, kTarget});
  Fields(
      actual, accessor,
      {kBufferView, kComponentType, kCount, kType, kByteOffset, kNormalized});
  if (RequiredUInt(actual, buffer, kByteLength) != bytes ||
      RequiredUInt(actual, view, kByteLength) != bytes ||
      RequiredUInt(actual, view, kBuffer) != buffer_count ||
      RequiredUInt(actual, accessor, kBufferView) != view_count ||
      RequiredUInt(actual, accessor, kComponentType) != kUnsignedIntComponent ||
      RequiredUInt(actual, accessor, kCount) != count ||
      RequiredUInt(actual, selected, kIndices) != accessor_count)
    BindingFailure();
  DefaultUInt(actual, view, kByteOffset, 0);
  DefaultUInt(actual, accessor, kByteOffset, 0);
  if (operation.request.source.SourceExtraction().SourceMode() !=
          kTrianglesMode ||
      !source.Member(
          source.Selected(operation.request.source.SourcePrimitive()), kMode))
    DefaultUInt(actual, selected, kMode, kTrianglesMode);
  if (const auto normalized = actual.Member(accessor, kNormalized);
      normalized && actual.ValueText(*normalized) != kFalseLiteral)
    BindingFailure();
  if (const auto target = actual.Member(view, kTarget); target) {
    constexpr std::uint64_t kElementArrayBufferTarget = 34963;
    if (UInt(actual, *target) != kElementArrayBufferTarget) BindingFailure();
  }
  const auto type = actual.Member(accessor, kType);
  const auto uri = actual.Member(buffer, kUri);
  if (!type || !actual.StringIs(*type, kScalarShape) || !uri) BindingFailure();
  auto text = actual.ValueText(*uri);
  const auto groups = 1 + (bytes - 1) / kBase64InputBytes;
  const auto encoded =
      Add(kDataUriPrefix.size(),
          gltf_detail::CheckedProduct(groups, kBase64OutputBytes));
  if (text.size() != encoded + 2 || text.front() != '"' || text.back() != '"')
    BindingFailure();
  UriCheck check{operation, text.substr(1, text.size() - 2)};
  RunRanges(operation, encoded, UriCheck::Check, &check, stats);
}
void ValueHole(Vector<Hole>& holes, const LexicalIndex& original,
               const LexicalIndex& actual, std::size_t first,
               std::size_t second) {
  holes.push_back({original.At(first).begin, original.At(first).end,
                   actual.At(second).begin, actual.At(second).end});
}
void VerifyOldMembers(const LexicalIndex& source, const LexicalIndex& actual,
                      std::size_t first, std::size_t second,
                      std::initializer_list<std::string_view> allowed_new) {
  auto original = source.At(first).first;
  auto output = actual.At(second).first;
  while (original != kNoNode) {
    // Untouched raw key bytes belong to the ordered preservation byte plan.
    // This structure pass only admits the old prefix and possible appendages.
    if (output == kNoNode)
      Fail(Status::kOutputMismatch, Stage::kLexical,
           Code::kLexicalPreservation);
    original = source.At(original).next;
    output = actual.At(output).next;
  }
  while (output != kNoNode) {
    bool admitted = false;
    for (auto key : allowed_new) admitted |= actual.KeyIs(output, key);
    if (!admitted)
      Fail(Status::kOutputMismatch, Stage::kLexical,
           Code::kLexicalPreservation);
    output = actual.At(output).next;
  }
}
void InsertionHole(Vector<Hole>& holes, const LexicalIndex& source,
                   const LexicalIndex& actual, std::size_t first,
                   std::size_t second, std::size_t original_children) {
  const auto source_close = source.At(first).end - 1;
  const auto output_close = actual.At(second).end - 1;
  std::size_t output_begin = actual.At(second).begin + 1;
  std::size_t source_begin = source.At(first).begin + 1;
  if (original_children) {
    const auto old_source = *source.Child(first, original_children - 1);
    const auto old_output = actual.Child(second, original_children - 1);
    if (!old_output) BindingFailure();
    source_begin = source.At(old_source).end;
    output_begin = actual.At(*old_output).end;
  }
  output_begin = Add(output_begin, source_close - source_begin);
  if (output_begin > output_close) BindingFailure();
  holes.push_back({source_close, source_close, output_begin, output_close});
}
}  // namespace
void VerifyLexical(const Operation& operation, Stats& stats) {
  const auto& request = operation.request;
  // The real retained reload must be the exact bundle document; neither a
  // structurally equivalent JSON nor writer annotations establish this fact.
  BytePlan equality(operation);
  equality.Compare(request.output.Json(), request.reloaded_asset.OriginalJson(),
                   Location(Stage::kLexical, Code::kOutputJson));
  equality.Run(stats);
  const LexicalIndex source(operation,
                            request.source.SourceAsset().OriginalJson());
  const LexicalIndex actual(operation, request.output.Json());
  const LexicalIndex reload(operation, request.reloaded_asset.OriginalJson());
  (void)reload;
  if (!operation.changed) {
    BytePlan no_op(operation);
    auto diagnostic = Location(Stage::kLexical, Code::kLexicalPreservation);
    diagnostic.json_span = GltfSourceSpan{0, request.output.Json().size()};
    no_op.Compare(request.source.SourceAsset().OriginalJson(),
                  request.output.Json(), diagnostic);
    no_op.Run(stats);
    return;
  }
  Vector<Hole> holes{gltf_detail::Allocator<Hole>(operation.allocation)};
  const auto selected_source =
      source.Selected(request.source.SourcePrimitive());
  const auto selected_output =
      actual.Selected(request.source.SourcePrimitive());
  const auto source_indices = source.Member(selected_source, kIndices);
  const auto output_indices = actual.Member(selected_output, kIndices);
  const auto source_mode = source.Member(selected_source, kMode);
  const auto output_mode = actual.Member(selected_output, kMode);
  if (!output_indices) BindingFailure();
  if (source_indices)
    ValueHole(holes, source, actual, *source_indices, *output_indices);
  if (source_mode &&
      request.source.SourceExtraction().SourceMode() != kTrianglesMode) {
    if (!output_mode) BindingFailure();
    ValueHole(holes, source, actual, *source_mode, *output_mode);
  }
  VerifyOldMembers(source, actual, selected_source, selected_output,
                   {kIndices, kMode});
  if (actual.Children(selected_output) != source.Children(selected_source))
    InsertionHole(holes, source, actual, selected_source, selected_output,
                  source.Children(selected_source));
  for (auto table_key : {kBuffers, kBufferViews, kAccessors}) {
    const auto original = source.Member(0, table_key);
    const auto output = actual.Member(0, table_key);
    if (!output) BindingFailure();
    if (original) {
      if (source.At(*original).kind != '[' || actual.At(*output).kind != '[' ||
          actual.Children(*output) != source.Children(*original) + 1)
        BindingFailure();
      InsertionHole(holes, source, actual, *original, *output,
                    source.Children(*original));
    }
  }
  VerifyOldMembers(source, actual, 0, 0, {kBuffers, kBufferViews, kAccessors});
  if (actual.Children(0) != source.Children(0))
    InsertionHole(holes, source, actual, 0, 0, source.Children(0));
  std::sort(holes.begin(), holes.end(),
            [](const Hole& first, const Hole& second) {
              return first.source_begin < second.source_begin;
            });
  BytePlan preservation(operation);
  std::size_t source_position = 0;
  std::size_t output_position = 0;
  for (const auto& hole : holes) {
    if (hole.source_begin < source_position ||
        hole.output_begin < output_position)
      BindingFailure();
    auto diagnostic = Location(Stage::kLexical, Code::kLexicalPreservation);
    diagnostic.json_span =
        GltfSourceSpan{source_position, hole.source_begin - source_position};
    preservation.Compare(source.Text(source_position, hole.source_begin),
                         actual.Text(output_position, hole.output_begin),
                         diagnostic);
    source_position = hole.source_end;
    output_position = hole.output_end;
  }
  auto diagnostic = Location(Stage::kLexical, Code::kLexicalPreservation);
  diagnostic.json_span = GltfSourceSpan{
      source_position,
      request.source.SourceAsset().OriginalJson().size() - source_position};
  preservation.Compare(
      source.Text(source_position,
                  request.source.SourceAsset().OriginalJson().size()),
      actual.Text(output_position, request.output.Json().size()), diagnostic);
  preservation.Run(stats);
  Appended(operation, source, actual, stats);
}
}  // namespace meshvale::interchange::gltf_workflow_support_detail
