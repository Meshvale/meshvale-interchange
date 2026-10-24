// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/ply.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_allocation.h"
#include "ply_test_internal.h"

namespace meshvale::interchange::ply_detail {
namespace allocation = gltf_detail;
template <class T>
using Vector = std::vector<T, allocation::Allocator<T>>;
struct Element {
  PlyElementKind kind = PlyElementKind::kVertex;
  std::uint64_t count = 0;
  std::size_t begin = 0;
  std::size_t size = 0;
};
struct DocumentState {
  explicit DocumentState(const allocation::AllocationContext& context)
      : allocation(context),
        source(allocation::Allocator<std::byte>(context)),
        properties(allocation::Allocator<PlyPropertyView>(context)) {}
  allocation::AllocationContext allocation;
  Vector<std::byte> source;
  Vector<PlyPropertyView> properties;
  std::array<Element, 2> elements{};
  std::array<PlyElementView, 2> views{};
  std::size_t element_count = 0;
  PlyEncoding encoding = PlyEncoding::kInvalid;
  std::optional<geometry::MaterializedMesh> mesh;
  std::optional<geometry::MeshInspection> inspection;
};
struct OutputState {
  explicit OutputState(const allocation::AllocationContext& context)
      : allocation(context), bytes(allocation::Allocator<std::byte>(context)) {}
  allocation::AllocationContext allocation;
  Vector<std::byte> bytes;
  PlyWriteFacts facts;
};
struct Access {
  static PlyDocument Document(
      std::shared_ptr<const DocumentState> state) noexcept {
    return PlyDocument(std::move(state));
  }
  static PlyOutput Output(std::shared_ptr<const OutputState> state) noexcept {
    return PlyOutput(std::move(state));
  }
};
}  // namespace meshvale::interchange::ply_detail

namespace meshvale::interchange {
namespace {
namespace detail = ply_detail;
namespace allocation = gltf_detail;
namespace geo = geometry;
using Status = PlyStatus;
using Stage = PlyStage;
using Code = PlyDiagnosticCode;
using Type = PlyScalarType;
using Binding = PlyBinding;
using Number = std::variant<std::int32_t, std::uint32_t, float, double>;
constexpr std::size_t kComponents = 3;
constexpr std::size_t kMinimumCorners = 3;
constexpr std::size_t kNumberBytes = 128;
constexpr unsigned char kPrintableFirst = 0x20;
constexpr unsigned char kPrintableLast = 0x7e;
constexpr unsigned kBitsPerByte = 8;
constexpr std::uint64_t kDecimalBase = 10;
constexpr std::string_view kMarker = "ply";
constexpr std::string_view kFormat = "format";
constexpr std::string_view kVersion = "1.0";
constexpr std::string_view kElement = "element";
constexpr std::string_view kProperty = "property";
constexpr std::string_view kList = "list";
constexpr std::string_view kComment = "comment";
constexpr std::string_view kEndHeader = "end_header";
constexpr std::string_view kVertex = "vertex";
constexpr std::string_view kFace = "face";
constexpr std::string_view kIndices = "vertex_indices";
constexpr std::array<std::string_view, kComponents> kCoordinates{"x", "y", "z"};
constexpr std::array<std::string_view, 9> kTypes{
    "", "char", "uchar", "short", "ushort", "int", "uint", "float", "double"};
constexpr std::array<std::string_view, 4> kEncodings{
    "", "ascii", "binary_little_endian", "binary_big_endian"};
constexpr std::uint32_t kFloatExponentMask = 0x7f800000;
constexpr std::uint32_t kFloatSignMask = 0x80000000;
constexpr std::uint32_t kFloatFractionMask = 0x007fffff;
constexpr std::uint64_t kDoubleExponentMask = 0x7ff0000000000000;
constexpr std::uint64_t kDoubleFractionMask = 0x000fffffffffffff;
constexpr unsigned kFloatFractionBits = 23;
constexpr unsigned kDoubleFractionBits = 52;
constexpr unsigned kWidenSignShift = 32;
constexpr unsigned kFractionShift = kDoubleFractionBits - kFloatFractionBits;
constexpr int kFloatExponentBias = 127;
constexpr int kDoubleExponentBias = 1023;
constexpr int kFloatMinimumExponent = -126;
constexpr int kFloatMaximumExponent = 127;
constexpr int kFloatSubnormalExponent = -149;
// Integer encoding avoids ambient FTZ/DAZ or rounding-mode conversions. In
// particular, binary32 subnormals widen to normal binary64 bit patterns.
double Widen(std::uint32_t bits) noexcept {
  auto result = static_cast<std::uint64_t>(bits & kFloatSignMask)
                << kWidenSignShift;
  const auto exponent = (bits & kFloatExponentMask) >> kFloatFractionBits;
  const auto fraction = bits & kFloatFractionMask;
  if ((bits & kFloatExponentMask) == kFloatExponentMask) {
    result |= kDoubleExponentMask |
              (static_cast<std::uint64_t>(fraction) << kFractionShift);
  } else if (exponent != 0) {
    const auto widened =
        static_cast<int>(exponent) - kFloatExponentBias + kDoubleExponentBias;
    result |= (static_cast<std::uint64_t>(widened) << kDoubleFractionBits) |
              (static_cast<std::uint64_t>(fraction) << kFractionShift);
  } else if (fraction != 0) {
    const auto leading = static_cast<unsigned>(std::bit_width(fraction) - 1);
    const auto widened = static_cast<int>(leading) + kFloatSubnormalExponent +
                         kDoubleExponentBias;
    result |= (static_cast<std::uint64_t>(widened) << kDoubleFractionBits) |
              ((static_cast<std::uint64_t>(fraction)
                << (kDoubleFractionBits - leading)) &
               kDoubleFractionMask);
  }
  return std::bit_cast<double>(result);
}
bool Finite(double value) noexcept {
  return (std::bit_cast<std::uint64_t>(value) & kDoubleExponentMask) !=
         kDoubleExponentMask;
}
struct Failure {
  Status status;
  PlyDiagnostic diagnostic;
};
[[noreturn]] void Fail(Status status, Stage stage, Code code) {
  PlyDiagnostic diagnostic;
  diagnostic.stage = stage;
  diagnostic.code = code;
  throw Failure{status, diagnostic};
}
std::size_t Host(std::uint64_t value) {
  if (value > std::numeric_limits<std::size_t>::max())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  return static_cast<std::size_t>(value);
}
void Positive(std::uint64_t value) {
  if (!value)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kRequiredFieldZero);
  (void)Host(value);
}
std::uint64_t Add(std::uint64_t first, std::uint64_t second) {
  if (second > std::numeric_limits<std::uint64_t>::max() - first)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  return first + second;
}
std::size_t Product(std::size_t count, std::size_t width, Stage stage,
                    Code code) {
  if (count >
      static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
          width)
    Fail(Status::kLimitExceeded, stage, code);
  return count * width;
}
void Validate(const geo::MeshInspectionLimits& limits) {
  Positive(limits.max_issues);
  Positive(limits.max_new_requested_bytes);
}
void Validate(const geo::MeshMaterializationLimits& limits) {
  for (auto value :
       {limits.max_vertices, limits.max_faces, limits.max_corners,
        limits.max_face_offsets, limits.max_attributes,
        limits.max_attribute_scalars, limits.max_attribute_offsets,
        limits.max_presence_bytes, limits.max_metadata_entries,
        limits.max_descriptor_bytes, limits.max_new_requested_bytes})
    Positive(value);
}
void Validate(const PlyWorkPolicy& work) {
  for (auto value :
       {work.cancellation_block_bytes, work.cancellation_block_values,
        work.cancellation_block_records, work.geometry.parallel_min_bytes,
        work.geometry.partition_bytes, work.geometry.cancellation_block_scalars,
        work.geometry.max_jobs})
    Positive(value);
}
template <class Limits>
void CommonLimits(const Limits& limits) {
  for (auto value :
       {limits.max_header_bytes, limits.max_header_lines, limits.max_line_bytes,
        limits.max_token_bytes, limits.max_name_bytes,
        limits.max_total_name_bytes, limits.max_properties, limits.max_vertices,
        limits.max_faces, limits.max_corners, limits.max_corners_per_face,
        limits.max_list_rows, limits.max_list_items_per_row,
        limits.max_property_values, limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.inspection);
}
void Validate(const PlyReadLimits& limits, const PlyWorkPolicy& work) {
  CommonLimits(limits);
  Positive(limits.max_source_bytes);
  Positive(limits.max_comment_bytes);
  Validate(limits.materialization);
  Validate(work);
  if (Add(Add(limits.max_codec_new_requested_bytes,
              limits.materialization.max_new_requested_bytes),
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const PlyWriteLimits& limits, const PlyWorkPolicy& work,
              const PlyWriteOptions& options) {
  CommonLimits(limits);
  Positive(limits.max_output_bytes);
  Validate(work);
  if (Add(limits.max_codec_new_requested_bytes,
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  if (options.encoding != PlyEncoding::kAscii &&
      options.encoding != PlyEncoding::kBinaryLittleEndian &&
      options.encoding != PlyEncoding::kBinaryBigEndian)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration, Code::kEncoding);
  if (options.rewrite_policy != PlyRewritePolicy::kCanonical)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kRewritePolicy);
}
struct Session {
  const PlyWorkPolicy& work;
  std::stop_token stop;
  const detail::TestOptions& test;
  PlyDiagnostic location;
  [[noreturn]] void Reject(Status status, Stage stage, Code code,
                           std::optional<std::uint64_t> offset = {}) const {
    auto diagnostic = location;
    diagnostic.stage = stage;
    diagnostic.code = code;
    diagnostic.byte_offset = offset;
    throw Failure{status, diagnostic};
  }
  void Check(Stage stage, std::uint64_t ordinal = 0) const {
    if (test.checkpoint) test.checkpoint(stage, ordinal, test.state);
    if (stop.stop_requested())
      Reject(Status::kCanceled, stage, Code::kStopRequested);
  }
  void Byte(Stage stage, std::size_t index) const {
    if (index % work.cancellation_block_bytes == 0) Check(stage, index);
  }
  void Value(Stage stage, std::uint64_t index) const {
    if (index % work.cancellation_block_values == 0) Check(stage, index);
  }
  void Record(Stage stage, std::uint64_t index) const {
    if (index % work.cancellation_block_records == 0) Check(stage, index);
  }
  void Cap(std::uint64_t value, std::uint64_t cap, Stage stage,
           Code code) const {
    if (value > cap) Reject(Status::kLimitExceeded, stage, code);
  }
};
// Grow only plain cells. Reserving an empty replacement performs no hidden
// relocation; explicit transfer observes the selected work interval. Both
// blocks stay charged until the allocation-free swap releases the old buffer.
template <class T>
void AppendBounded(detail::Vector<T>& values, T value, std::uint64_t limit,
                   Stage stage, Code code, detail::RelocationKind kind,
                   const Session& session) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (values.size() >= limit)
    session.Reject(Status::kLimitExceeded, stage, code);
  if (values.size() == values.capacity()) {
    const auto host_limit = Host(limit);
    const auto cell_limit =
        static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
        sizeof(T);
    const auto maximum = host_limit < cell_limit ? host_limit : cell_limit;
    if (values.size() >= maximum)
      session.Reject(Status::kLimitExceeded, stage, code);
    const auto capacity = values.capacity();
    const auto next =
        capacity == 0 ? 1 : (capacity <= maximum / 2 ? capacity * 2 : maximum);
    detail::Vector<T> replacement(values.get_allocator());
    session.Check(stage);
    replacement.reserve(next);
    const auto block = kind == detail::RelocationKind::kOutputBytes
                           ? session.work.cancellation_block_bytes
                           : (kind == detail::RelocationKind::kProperties
                                  ? session.work.cancellation_block_records
                                  : session.work.cancellation_block_values);
    const auto checkpoint = [&](std::size_t copied) {
      if (session.test.relocation_checkpoint)
        session.test.relocation_checkpoint(kind, stage, copied, values.size(),
                                           session.test.state);
      session.Check(stage, copied);
    };
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i % block == 0) checkpoint(i);
      replacement.push_back(values[i]);
    }
    checkpoint(values.size());
    static_assert(noexcept(values.swap(replacement)));
    values.swap(replacement);
  }
  values.push_back(value);
}
std::uint64_t Accumulate(std::uint64_t current, std::uint64_t count,
                         std::uint64_t cap, const Session& session, Stage stage,
                         Code code) {
  if (count > cap || current > cap - count)
    session.Reject(Status::kLimitExceeded, stage, code);
  return current + count;
}
bool Space(char value) { return value == ' ' || value == '\t'; }
bool Equal(std::string_view first, std::string_view second,
           const Session& session, Stage stage) {
  if (first.size() != second.size()) return false;
  for (std::size_t i = 0; i < first.size(); ++i) {
    session.Byte(stage, i);
    if (first[i] != second[i]) return false;
  }
  return true;
}
std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::size_t Width(Type type) {
  switch (type) {
    case Type::kChar:
    case Type::kUchar:
      return 1;
    case Type::kShort:
    case Type::kUshort:
      return 2;
    case Type::kInt:
    case Type::kUint:
    case Type::kFloat:
      return 4;
    case Type::kDouble:
      return 8;
    default:
      return 0;
  }
}
bool Integer(Type type) { return type >= Type::kChar && type <= Type::kUint; }
bool Signed(Type type) {
  return type == Type::kChar || type == Type::kShort || type == Type::kInt;
}
std::uint64_t Maximum(Type type) {
  const auto bits = Width(type) * kBitsPerByte;
  return (UINT64_C(1) << (bits - (Signed(type) ? 1 : 0))) - 1;
}
Type ParseType(std::string_view text, const Session& session) {
  for (std::size_t i = 1; i < kTypes.size(); ++i)
    if (text == kTypes[i]) return static_cast<Type>(i);
  session.Reject(Status::kUnsupportedVariant, Stage::kScan, Code::kVariant);
}
struct Token {
  std::string_view text;
  std::size_t offset = 0;
};
std::uint64_t Magnitude(Token token, const Session& session, Stage stage) {
  if (token.text.empty())
    session.Reject(Status::kMalformed, stage, Code::kIntegerSyntax,
                   token.offset);
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < token.text.size(); ++i) {
    session.Byte(stage, token.offset + i);
    const auto digit = token.text[i];
    if (digit < '0' || digit > '9')
      session.Reject(Status::kMalformed, stage, Code::kIntegerSyntax,
                     token.offset + i);
    const auto number = static_cast<std::uint64_t>(digit - '0');
    if (value >
        (std::numeric_limits<std::uint64_t>::max() - number) / kDecimalBase)
      session.Reject(Status::kMalformed, stage, Code::kIntegerRange,
                     token.offset);
    value = value * kDecimalBase + number;
  }
  return value;
}

struct Header {
  detail::DocumentState& state;
  const PlyReadLimits& limits;
  Session& session;
  std::size_t cursor = 0;
  std::uint64_t lines = 0;
  std::uint64_t names = 0;
  std::uint64_t attributes = 0;
  std::optional<std::size_t> vertex = {};
  std::optional<std::size_t> face = {};
  Token Line() {
    session.Record(Stage::kScan, lines);
    session.Cap(++lines, limits.max_header_lines, Stage::kScan,
                Code::kHeaderLineLimit);
    const auto begin = cursor;
    const auto text = Text(state.source);
    while (cursor < text.size() && text[cursor] != '\r' &&
           text[cursor] != '\n') {
      session.Byte(Stage::kScan, cursor);
      session.Cap(cursor + 1, limits.max_header_bytes, Stage::kScan,
                  Code::kHeaderLimit);
      session.Cap(cursor - begin + 1, limits.max_line_bytes, Stage::kScan,
                  Code::kLineLimit);
      const auto byte = static_cast<unsigned char>(text[cursor]);
      if ((byte < kPrintableFirst || byte > kPrintableLast) && byte != '\t')
        session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax, cursor);
      ++cursor;
    }
    const auto end = cursor;
    if (cursor == text.size())
      session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax, cursor);
    session.Byte(Stage::kScan, cursor);
    if (text[cursor] == '\r') {
      ++cursor;
      if (cursor == text.size() || text[cursor] != '\n')
        session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax, cursor);
    }
    session.Byte(Stage::kScan, cursor);
    ++cursor;
    session.Cap(cursor, limits.max_header_bytes, Stage::kScan,
                Code::kHeaderLimit);
    return {text.substr(begin, end - begin), begin};
  }
  std::size_t Words(Token line, std::array<Token, 5>& words) {
    std::size_t i = 0, count = 0;
    while (i < line.text.size()) {
      while (i < line.text.size() && Space(line.text[i])) {
        session.Byte(Stage::kScan, line.offset + i);
        ++i;
      }
      if (i == line.text.size()) break;
      const auto begin = i;
      while (i < line.text.size() && !Space(line.text[i])) {
        session.Byte(Stage::kScan, line.offset + i);
        session.Cap(i - begin + 1, limits.max_token_bytes, Stage::kScan,
                    Code::kTokenLimit);
        ++i;
      }
      if (count == words.size())
        session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax,
                       line.offset + begin);
      words[count++] = {line.text.substr(begin, i - begin),
                        line.offset + begin};
    }
    return count;
  }
  void Name(Token token) {
    session.Cap(token.text.size(), limits.max_name_bytes, Stage::kScan,
                Code::kNameLimit);
    names = Accumulate(names, token.text.size(), limits.max_total_name_bytes,
                       session, Stage::kScan, Code::kTotalNameLimit);
    for (std::size_t i = 0; i < token.text.size(); ++i) {
      session.Byte(Stage::kScan, token.offset + i);
      const char value = token.text[i];
      const bool letter = (value >= 'A' && value <= 'Z') ||
                          (value >= 'a' && value <= 'z') || value == '_';
      if (!letter && !(i && value >= '0' && value <= '9'))
        session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax,
                       token.offset + i);
    }
  }
  void Parse() {
    if (Line().text != kMarker)
      session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax, 0);
    std::array<Token, 5> fields{};
    auto count = Words(Line(), fields);
    if (count != 3 || fields[0].text != kFormat)
      session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax);
    if (fields[2].text != kVersion)
      session.Reject(Status::kUnsupportedVariant, Stage::kScan, Code::kVariant,
                     fields[2].offset);
    for (std::size_t i = 1; i < kEncodings.size(); ++i)
      if (fields[1].text == kEncodings[i])
        state.encoding = static_cast<PlyEncoding>(i);
    if (state.encoding == PlyEncoding::kInvalid)
      session.Reject(Status::kUnsupportedVariant, Stage::kScan, Code::kVariant,
                     fields[1].offset);
    while (true) {
      const auto line = Line();
      std::size_t begin = 0;
      while (begin < line.text.size() && Space(line.text[begin])) {
        session.Byte(Stage::kScan, line.offset + begin);
        ++begin;
      }
      const auto word_begin = begin;
      while (begin < line.text.size() && !Space(line.text[begin])) {
        session.Byte(Stage::kScan, line.offset + begin);
        ++begin;
      }
      if (line.text.substr(word_begin, begin - word_begin) == kComment) {
        session.Cap(begin - word_begin, limits.max_token_bytes, Stage::kScan,
                    Code::kTokenLimit);
        const auto content = begin < line.text.size() ? begin + 1 : begin;
        session.Cap(line.text.size() - content, limits.max_comment_bytes,
                    Stage::kScan, Code::kCommentLimit);
        continue;
      }
      count = Words(line, fields);
      if (!count)
        session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax,
                       line.offset);
      const auto directive = fields[0].text;
      if (directive == kEndHeader) {
        if (count != 1)
          session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax,
                         line.offset);
        break;
      }
      if (directive == kFormat)
        session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax,
                       line.offset);
      if (directive == kElement) {
        if (count != 3)
          session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax,
                         line.offset);
        Name(fields[1]);
        const bool is_vertex = fields[1].text == kVertex;
        const bool is_face = fields[1].text == kFace;
        if (!is_vertex && !is_face)
          session.Reject(Status::kUnsupportedVariant, Stage::kScan,
                         Code::kVariant, fields[1].offset);
        if ((is_vertex && vertex) || (is_face && face))
          session.Reject(Status::kMalformed, Stage::kScan, Code::kDuplicateName,
                         fields[1].offset);
        const auto rows = Magnitude(fields[2], session, Stage::kScan);
        session.Cap(rows, is_vertex ? limits.max_vertices : limits.max_faces,
                    Stage::kScan,
                    is_vertex ? Code::kVertexLimit : Code::kFaceLimit);
        const auto index = state.element_count++;
        session.location.element = index;
        session.location.property.reset();
        state.elements[index] = {
            is_vertex ? PlyElementKind::kVertex : PlyElementKind::kFace, rows,
            state.properties.size(), 0};
        (is_vertex ? vertex : face) = index;
      } else if (directive == kProperty) {
        if (!state.element_count)
          session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax,
                         line.offset);
        auto& element = state.elements[state.element_count - 1];
        session.location.property = element.size;
        const bool list = count > 1 && fields[1].text == kList;
        if (count != (list ? 5u : 3u))
          session.Reject(Status::kMalformed, Stage::kScan, Code::kSyntax,
                         line.offset);
        session.Cap(state.properties.size() + 1, limits.max_properties,
                    Stage::kScan, Code::kPropertyLimit);
        const auto name = fields[list ? 4 : 2];
        Name(name);
        for (std::size_t i = 0; i < element.size; ++i) {
          session.Record(Stage::kScan, i);
          if (Equal(name.text, state.properties[element.begin + i].name,
                    session, Stage::kScan))
            session.Reject(Status::kMalformed, Stage::kScan,
                           Code::kDuplicateName, name.offset);
        }
        PlyPropertyView property;
        property.name = name.text;
        property.kind =
            list ? PlyPropertyKind::kList : PlyPropertyKind::kScalar;
        property.value_type = ParseType(fields[list ? 3 : 1].text, session);
        if (list) {
          property.count_type = ParseType(fields[2].text, session);
          if (!Integer(*property.count_type))
            session.Reject(Status::kUnsupportedVariant, Stage::kScan,
                           Code::kVariant, fields[2].offset);
        }
        bool core = false;
        if (element.kind == PlyElementKind::kVertex) {
          for (std::size_t component = 0; component < kComponents; ++component)
            if (name.text == kCoordinates[component]) {
              if (list || (property.value_type != Type::kFloat &&
                           property.value_type != Type::kDouble))
                session.Reject(Status::kUnsupportedVariant, Stage::kScan,
                               Code::kMissingCoreProperty, name.offset);
              property.binding = static_cast<Binding>(component);
              core = true;
            }
        } else if (name.text == kIndices) {
          if (!list || !Integer(property.value_type))
            session.Reject(Status::kUnsupportedVariant, Stage::kScan,
                           Code::kMissingCoreProperty, name.offset);
          property.binding = Binding::kVertexIndices;
          core = true;
        }
        if (!core) {
          property.attribute = attributes++;
          property.binding = Binding::kAttribute;
        }
        AppendBounded(state.properties, property, limits.max_properties,
                      Stage::kScan, Code::kPropertyLimit,
                      detail::RelocationKind::kProperties, session);
        ++element.size;
      } else
        session.Reject(Status::kUnsupportedVariant, Stage::kScan,
                       Code::kVariant, line.offset);
    }
    if (!vertex)
      session.Reject(Status::kUnsupportedVariant, Stage::kScan,
                     Code::kMissingCoreProperty);
    for (std::size_t e = 0; e < state.element_count; ++e) {
      session.location.element = e;
      session.location.property.reset();
      const auto& element = state.elements[e];
      const auto properties = std::span<const PlyPropertyView>(state.properties)
                                  .subspan(element.begin, element.size);
      std::array<bool, kComponents> coordinates{};
      std::optional<Type> coordinate_type;
      bool indices = false;
      for (std::size_t i = 0; i < properties.size(); ++i) {
        session.Record(Stage::kScan, i);
        const auto& property = properties[i];
        if (property.binding <= Binding::kZ) {
          coordinates[static_cast<std::size_t>(property.binding)] = true;
          if (coordinate_type && *coordinate_type != property.value_type)
            session.Reject(Status::kUnsupportedVariant, Stage::kScan,
                           Code::kMissingCoreProperty);
          coordinate_type = property.value_type;
        } else if (property.binding == Binding::kVertexIndices)
          indices = true;
      }
      if ((element.kind == PlyElementKind::kVertex &&
           (!coordinates[0] || !coordinates[1] || !coordinates[2])) ||
          (element.kind == PlyElementKind::kFace && !indices))
        session.Reject(Status::kUnsupportedVariant, Stage::kScan,
                       Code::kMissingCoreProperty);
      state.views[e] = {element.kind, element.count, properties};
    }
    session.location = {};
  }
};

template <class Float>
bool FiniteValue(Float value) noexcept {
  if constexpr (std::is_same_v<Float, float>)
    return (std::bit_cast<std::uint32_t>(value) & kFloatExponentMask) !=
           kFloatExponentMask;
  else
    return Finite(value);
}
template <class Float>
Float Decimal(Token token, const Session& session) {
  auto text = token.text;
  std::size_t cursor = 0;
  bool nonzero = false;
  if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-'))
    ++cursor;
  auto digits = [&](bool significand) {
    const auto start = cursor;
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
      session.Byte(Stage::kDecode, token.offset + cursor);
      if (significand && text[cursor] != '0') nonzero = true;
      ++cursor;
    }
    return cursor - start;
  };
  auto count = digits(true);
  if (cursor < text.size() && text[cursor] == '.') {
    ++cursor;
    count += digits(true);
  }
  if (!count)
    session.Reject(Status::kMalformed, Stage::kDecode, Code::kNumericSyntax,
                   token.offset);
  if (cursor < text.size() && (text[cursor] == 'e' || text[cursor] == 'E')) {
    ++cursor;
    if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-'))
      ++cursor;
    if (!digits(false))
      session.Reject(Status::kMalformed, Stage::kDecode, Code::kNumericSyntax,
                     token.offset);
  }
  if (cursor != text.size())
    session.Reject(Status::kMalformed, Stage::kDecode, Code::kNumericSyntax,
                   token.offset);
  if (text.front() == '+') text.remove_prefix(1);
  Float value = 0;
  session.Check(Stage::kDecode, token.offset);
  const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                      value, std::chars_format::general);
  session.Check(Stage::kDecode, token.offset);
  if (result.ec == std::errc::result_out_of_range)
    session.Reject(Status::kMalformed, Stage::kDecode, Code::kNumericRange,
                   token.offset);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    session.Reject(Status::kMalformed, Stage::kDecode, Code::kNumericSyntax,
                   token.offset);
  if (!FiniteValue(value))
    session.Reject(Status::kMalformed, Stage::kDecode, Code::kNonfinite,
                   token.offset);
  bool zero;
  if constexpr (std::is_same_v<Float, float>)
    zero = (std::bit_cast<std::uint32_t>(value) & ~kFloatSignMask) == 0;
  else
    zero = (std::bit_cast<std::uint64_t>(value) & ~(UINT64_C(1) << 63)) == 0;
  if (nonzero && zero)
    session.Reject(Status::kMalformed, Stage::kDecode, Code::kNumericRange,
                   token.offset);
  return value;
}
struct Body {
  std::span<const std::byte> bytes;
  PlyEncoding encoding;
  const PlyReadLimits& limits;
  Session& session;
  std::size_t cursor;
  std::size_t line = 0;
  std::uint64_t values = 0;
  void Ordinary() {
    session.Byte(Stage::kDecode, cursor);
    session.Cap(++line, limits.max_line_bytes, Stage::kDecode,
                Code::kLineLimit);
    const auto byte = std::to_integer<unsigned char>(bytes[cursor]);
    if ((byte < kPrintableFirst || byte > kPrintableLast) && byte != '\t')
      session.Reject(Status::kMalformed, Stage::kDecode, Code::kSyntax, cursor);
    ++cursor;
  }
  void Whitespace() {
    while (cursor < bytes.size()) {
      const auto value = static_cast<char>(bytes[cursor]);
      if (Space(value))
        Ordinary();
      else if (value == '\n') {
        session.Byte(Stage::kDecode, cursor++);
        line = 0;
      } else if (value == '\r') {
        session.Byte(Stage::kDecode, cursor++);
        if (cursor == bytes.size() || bytes[cursor] != std::byte{'\n'})
          session.Reject(Status::kMalformed, Stage::kDecode, Code::kSyntax,
                         cursor);
        session.Byte(Stage::kDecode, cursor++);
        line = 0;
      } else
        break;
    }
  }
  Token Next() {
    Whitespace();
    const auto begin = cursor;
    while (cursor < bytes.size()) {
      const auto value = static_cast<char>(bytes[cursor]);
      if (Space(value) || value == '\r' || value == '\n') break;
      session.Cap(cursor - begin + 1, limits.max_token_bytes, Stage::kDecode,
                  Code::kTokenLimit);
      Ordinary();
    }
    if (cursor == begin)
      session.Reject(Status::kMalformed, Stage::kDecode, Code::kSyntax, begin);
    return {Text(bytes).substr(begin, cursor - begin), begin};
  }
  Number Read(Type type, bool count_or_index = false) {
    session.Value(Stage::kDecode, values++);
    if (encoding != PlyEncoding::kAscii) {
      const auto width = Width(type);
      if (width > bytes.size() - cursor)
        session.Reject(Status::kMalformed, Stage::kDecode, Code::kSyntax,
                       cursor);
      std::uint64_t bits = 0;
      const auto begin = cursor;
      for (std::size_t i = 0; i < width; ++i) {
        session.Byte(Stage::kDecode, cursor);
        const auto shift =
            (encoding == PlyEncoding::kBinaryLittleEndian ? i : width - 1 - i) *
            kBitsPerByte;
        bits |= static_cast<std::uint64_t>(
                    std::to_integer<unsigned char>(bytes[cursor++]))
                << shift;
      }
      if (type == Type::kFloat) {
        const auto value =
            std::bit_cast<float>(static_cast<std::uint32_t>(bits));
        if (!FiniteValue(value))
          session.Reject(Status::kMalformed, Stage::kDecode, Code::kNonfinite,
                         begin);
        return value;
      }
      if (type == Type::kDouble) {
        const auto value = std::bit_cast<double>(bits);
        if (!Finite(value))
          session.Reject(Status::kMalformed, Stage::kDecode, Code::kNonfinite,
                         begin);
        return value;
      }
      if (Signed(type)) {
        const auto sign = UINT64_C(1) << (width * kBitsPerByte - 1);
        const auto signed_value =
            static_cast<std::int64_t>(bits) -
            ((bits & sign) ? static_cast<std::int64_t>(sign * 2) : 0);
        if (count_or_index && signed_value < 0)
          session.Reject(Status::kMalformed, Stage::kDecode,
                         Code::kIntegerRange, begin);
        return static_cast<std::int32_t>(signed_value);
      }
      return static_cast<std::uint32_t>(bits);
    }
    const auto token = Next();
    if (type == Type::kFloat) return Decimal<float>(token, session);
    if (type == Type::kDouble) return Decimal<double>(token, session);
    auto magnitude = token;
    bool negative = false;
    if (Signed(type) && !count_or_index && !magnitude.text.empty() &&
        (magnitude.text.front() == '+' || magnitude.text.front() == '-')) {
      negative = magnitude.text.front() == '-';
      magnitude.text.remove_prefix(1);
      ++magnitude.offset;
    }
    const auto value = Magnitude(magnitude, session, Stage::kDecode);
    if (value > Maximum(type) + (negative ? 1 : 0))
      session.Reject(Status::kMalformed, Stage::kDecode, Code::kIntegerRange,
                     token.offset);
    if (Signed(type))
      return static_cast<std::int32_t>(negative
                                           ? -static_cast<std::int64_t>(value)
                                           : static_cast<std::int64_t>(value));
    return static_cast<std::uint32_t>(value);
  }
  std::uint64_t Count(Type type) {
    const auto number = Read(type, true);
    if (const auto* value = std::get_if<std::int32_t>(&number))
      return static_cast<std::uint64_t>(*value);
    return std::get<std::uint32_t>(number);
  }
  void End() {
    if (encoding == PlyEncoding::kAscii) Whitespace();
    if (cursor != bytes.size())
      session.Reject(Status::kUnsupportedVariant, Stage::kDecode,
                     Code::kTrailingData, cursor);
  }
};
using Values =
    std::variant<detail::Vector<std::int32_t>, detail::Vector<std::uint8_t>,
                 detail::Vector<std::uint16_t>, detail::Vector<std::uint32_t>,
                 detail::Vector<float>, detail::Vector<double>>;
struct Attribute {
  Attribute(const allocation::AllocationContext& context,
            const PlyPropertyView& property, geo::AttributeDomain domain,
            std::size_t rows)
      : descriptor{domain, property.name, {}, {}, 1, {}, {}, {}, {}},
        values(std::in_place_type<detail::Vector<std::int32_t>>,
               allocation::Allocator<std::int32_t>(context)),
        offsets(allocation::Allocator<geo::index_t>(context)) {
    switch (property.value_type) {
      case Type::kUchar:
        values.emplace<detail::Vector<std::uint8_t>>(
            allocation::Allocator<std::uint8_t>(context));
        break;
      case Type::kUshort:
        values.emplace<detail::Vector<std::uint16_t>>(
            allocation::Allocator<std::uint16_t>(context));
        break;
      case Type::kUint:
        values.emplace<detail::Vector<std::uint32_t>>(
            allocation::Allocator<std::uint32_t>(context));
        break;
      case Type::kFloat:
        values.emplace<detail::Vector<float>>(
            allocation::Allocator<float>(context));
        break;
      case Type::kDouble:
        values.emplace<detail::Vector<double>>(
            allocation::Allocator<double>(context));
        break;
      default:
        break;
    }
    if (property.kind == PlyPropertyKind::kList) {
      if (rows == std::numeric_limits<std::size_t>::max())
        Fail(Status::kLimitExceeded, Stage::kDecode, Code::kListRowLimit);
      (void)Product(rows + 1, sizeof(geo::index_t), Stage::kDecode,
                    Code::kListRowLimit);
      offsets.reserve(rows + 1);
      offsets.push_back(0);
    } else
      std::visit(
          [&](auto& data) {
            (void)Product(
                rows, sizeof(typename std::decay_t<decltype(data)>::value_type),
                Stage::kDecode, Code::kPropertyValueLimit);
            data.reserve(rows);
          },
          values);
  }
  geo::AttributeMaterializationInput descriptor;
  Values values;
  detail::Vector<geo::index_t> offsets;
  void Push(const Number& value, std::uint64_t limit, const Session& session) {
    std::visit(
        [&](auto& data) {
          using T = typename std::decay_t<decltype(data)>::value_type;
          if constexpr (std::is_same_v<T, std::uint8_t> ||
                        std::is_same_v<T, std::uint16_t>)
            AppendBounded(data, static_cast<T>(std::get<std::uint32_t>(value)),
                          limit, Stage::kDecode, Code::kPropertyValueLimit,
                          detail::RelocationKind::kAttributeValues, session);
          else
            AppendBounded(data, std::get<T>(value), limit, Stage::kDecode,
                          Code::kPropertyValueLimit,
                          detail::RelocationKind::kAttributeValues, session);
        },
        values);
  }
  geo::AttributeMaterializationInput View() const {
    auto result = descriptor;
    result.values = std::visit(
        [](const auto& data) -> geo::ScalarIngress {
          using T = typename std::decay_t<decltype(data)>::value_type;
          return std::span<const T>(data);
        },
        values);
    if (!offsets.empty()) result.offsets = offsets;
    return result;
  }
  std::size_t Size() const {
    return std::visit([](const auto& data) { return data.size(); }, values);
  }
};
struct Decoded {
  explicit Decoded(const allocation::AllocationContext& context)
      : positions(std::in_place_type<detail::Vector<double>>,
                  allocation::Allocator<double>(context)),
        offsets(allocation::Allocator<geo::index_t>(context)),
        corners(allocation::Allocator<geo::index_t>(context)),
        attributes(allocation::Allocator<Attribute>(context)),
        views(allocation::Allocator<geo::AttributeMaterializationInput>(
            context)) {}
  std::variant<detail::Vector<float>, detail::Vector<double>> positions;
  detail::Vector<geo::index_t> offsets;
  detail::Vector<geo::index_t> corners;
  detail::Vector<Attribute> attributes;
  detail::Vector<geo::AttributeMaterializationInput> views;
};

void Decode(detail::DocumentState& state, const PlyReadLimits& limits,
            Session& session, std::size_t cursor, Decoded& decoded) {
  std::uint64_t vertices = 0, faces = 0, list_rows = 0, property_values = 0;
  Type coordinate_type = Type::kInvalid;
  std::size_t attribute_count = 0;
  for (std::size_t e = 0; e < state.element_count; ++e) {
    const auto& element = state.views[e];
    if (element.kind == PlyElementKind::kVertex)
      vertices = element.original_count;
    else
      faces = element.original_count;
    for (const auto& property : element.properties) {
      session.Record(Stage::kDecode, &property - element.properties.data());
      if (property.binding == Binding::kX)
        coordinate_type = property.value_type;
      if (property.binding == Binding::kAttribute) ++attribute_count;
      if (property.kind == PlyPropertyKind::kList)
        list_rows =
            Accumulate(list_rows, element.original_count, limits.max_list_rows,
                       session, Stage::kDecode, Code::kListRowLimit);
      else if (property.binding == Binding::kAttribute)
        property_values = Accumulate(property_values, element.original_count,
                                     limits.max_property_values, session,
                                     Stage::kDecode, Code::kPropertyValueLimit);
    }
  }
  // Every scalar needs at least one ASCII byte, or its fixed binary width.
  // A list needs its count prefix; structural lists need at least three items.
  // This lower bound refuses impossible header counts before numeric
  // allocation.
  const auto remaining = state.source.size() - cursor;
  std::size_t required = 0;
  for (std::size_t e = 0; e < state.element_count; ++e) {
    const auto& element = state.views[e];
    for (std::size_t i = 0; i < element.properties.size(); ++i) {
      session.Record(Stage::kDecode, i);
      const auto& property = element.properties[i];
      const auto scalar_width = state.encoding == PlyEncoding::kAscii
                                    ? 1
                                    : Width(property.value_type);
      auto row_bytes = scalar_width;
      if (property.kind == PlyPropertyKind::kList) {
        row_bytes = state.encoding == PlyEncoding::kAscii
                        ? 1
                        : Width(*property.count_type);
        if (property.binding == Binding::kVertexIndices)
          row_bytes += kMinimumCorners * scalar_width;
      }
      if (element.original_count > (remaining - required) / row_bytes)
        session.Reject(Status::kMalformed, Stage::kDecode, Code::kSyntax,
                       cursor);
      required += Host(element.original_count) * row_bytes;
    }
  }
  const auto position_count =
      Product(Host(vertices), kComponents, Stage::kDecode, Code::kVertexLimit);
  if (coordinate_type == Type::kFloat)
    decoded.positions.emplace<detail::Vector<float>>(
        allocation::Allocator<float>(state.allocation));
  std::visit(
      [&](auto& positions) {
        using T = typename std::decay_t<decltype(positions)>::value_type;
        (void)Product(position_count, sizeof(T), Stage::kDecode,
                      Code::kVertexLimit);
        positions.reserve(position_count);
        for (std::size_t i = 0; i < position_count; ++i) {
          session.Value(Stage::kDecode, i);
          positions.push_back(T{0});
        }
      },
      decoded.positions);
  if (faces == std::numeric_limits<std::size_t>::max())
    session.Reject(Status::kLimitExceeded, Stage::kDecode, Code::kFaceLimit);
  (void)Product(Host(faces) + 1, sizeof(geo::index_t), Stage::kDecode,
                Code::kFaceLimit);
  decoded.offsets.reserve(Host(faces) + 1);
  decoded.offsets.push_back(0);
  (void)Product(attribute_count, sizeof(Attribute), Stage::kDecode,
                Code::kPropertyLimit);
  decoded.attributes.reserve(attribute_count);
  for (std::size_t e = 0; e < state.element_count; ++e) {
    const auto& element = state.views[e];
    for (std::size_t i = 0; i < element.properties.size(); ++i) {
      session.Record(Stage::kDecode, i);
      const auto& property = element.properties[i];
      if (property.binding == Binding::kAttribute)
        decoded.attributes.emplace_back(state.allocation, property,
                                        element.kind == PlyElementKind::kVertex
                                            ? geo::AttributeDomain::vertex
                                            : geo::AttributeDomain::face,
                                        Host(element.original_count));
    }
  }
  Body body{state.source, state.encoding, limits, session, cursor};
  for (std::size_t e = 0; e < state.element_count; ++e) {
    const auto& element = state.views[e];
    session.location.element = e;
    for (std::uint64_t row = 0; row < element.original_count; ++row) {
      session.location.row = row;
      session.Record(Stage::kDecode, row);
      for (std::size_t i = 0; i < element.properties.size(); ++i) {
        const auto& property = element.properties[i];
        session.location.property = i;
        session.location.attribute = property.attribute;
        session.location.item.reset();
        if (property.binding <= Binding::kZ) {
          const auto number = body.Read(property.value_type);
          const auto index = Host(row) * kComponents +
                             static_cast<std::size_t>(property.binding);
          std::visit(
              [&](auto& positions) {
                using T =
                    typename std::decay_t<decltype(positions)>::value_type;
                positions[index] = std::get<T>(number);
              },
              decoded.positions);
        } else if (property.binding == Binding::kVertexIndices) {
          const auto count = body.Count(*property.count_type);
          session.Cap(count, limits.max_list_items_per_row, Stage::kDecode,
                      Code::kListItemLimit);
          session.Cap(count, limits.max_corners_per_face, Stage::kDecode,
                      Code::kPerFaceCornerLimit);
          if (count < kMinimumCorners)
            session.Reject(Status::kMalformed, Stage::kDecode,
                           Code::kFaceTooShort, body.cursor);
          (void)Accumulate(decoded.corners.size(), count, limits.max_corners,
                           session, Stage::kDecode, Code::kCornerLimit);
          const auto minimum_width = state.encoding == PlyEncoding::kAscii
                                         ? 1
                                         : Width(property.value_type);
          if (count > (body.bytes.size() - body.cursor) / minimum_width)
            session.Reject(Status::kMalformed, Stage::kDecode, Code::kSyntax,
                           body.cursor);
          for (std::uint64_t item = 0; item < count; ++item) {
            session.location.item = item;
            const auto index = body.Count(property.value_type);
            if (index >= vertices)
              session.Reject(Status::kMalformed, Stage::kDecode,
                             Code::kVertexRange, body.cursor);
            AppendBounded(decoded.corners, static_cast<geo::index_t>(index),
                          limits.max_corners, Stage::kDecode,
                          Code::kCornerLimit, detail::RelocationKind::kCorners,
                          session);
          }
          decoded.offsets.push_back(decoded.corners.size());
        } else {
          auto& attribute = decoded.attributes[Host(*property.attribute)];
          if (property.kind == PlyPropertyKind::kScalar)
            attribute.Push(body.Read(property.value_type),
                           limits.max_property_values, session);
          else {
            const auto count = body.Count(*property.count_type);
            session.Cap(count, limits.max_list_items_per_row, Stage::kDecode,
                        Code::kListItemLimit);
            property_values =
                Accumulate(property_values, count, limits.max_property_values,
                           session, Stage::kDecode, Code::kPropertyValueLimit);
            const auto minimum_width = state.encoding == PlyEncoding::kAscii
                                           ? 1
                                           : Width(property.value_type);
            if (count > (body.bytes.size() - body.cursor) / minimum_width)
              session.Reject(Status::kMalformed, Stage::kDecode, Code::kSyntax,
                             body.cursor);
            for (std::uint64_t item = 0; item < count; ++item) {
              session.location.item = item;
              attribute.Push(body.Read(property.value_type),
                             limits.max_property_values, session);
            }
            attribute.offsets.push_back(attribute.Size());
          }
        }
      }
    }
    session.location.row.reset();
    session.location.property.reset();
    session.location.item.reset();
    session.location.attribute.reset();
  }
  body.End();
  session.location = {};
  decoded.views.reserve(decoded.attributes.size());
  for (std::size_t i = 0; i < decoded.attributes.size(); ++i) {
    session.Record(Stage::kDecode, i);
    decoded.views.push_back(decoded.attributes[i].View());
  }
}
void GeometryFailure(Stage stage, geo::MaterializationStatus status,
                     const geo::MaterializationDiagnostic& diagnostic) {
  Status mapped = Status::kGeometryFailure;
  if (status == geo::MaterializationStatus::kCanceled)
    mapped = Status::kCanceled;
  else if (status == geo::MaterializationStatus::kBudgetExceeded)
    mapped = Status::kBudgetExceeded;
  else if (status == geo::MaterializationStatus::kLimitExceeded)
    mapped = Status::kLimitExceeded;
  else if (status == geo::MaterializationStatus::kInvalidSourceStorage)
    mapped = Status::kInvalidStorage;
  PlyDiagnostic value;
  value.stage = stage;
  value.code = Code::kGeometry;
  value.geometry_status = status;
  value.geometry_diagnostic = diagnostic;
  value.storage_issue = diagnostic.storage_issue;
  throw Failure{mapped, value};
}
void InspectionFailure(const geo::MeshInspectionResult& result) {
  if (!result.inspection)
    GeometryFailure(Stage::kInspection, result.status, result.diagnostic);
  if (!result.inspection->IsStorageValid()) {
    PlyDiagnostic value;
    value.stage = Stage::kInspection;
    value.code = Code::kStorageIssues;
    value.geometry_status = result.status;
    value.geometry_diagnostic = result.diagnostic;
    if (!result.inspection->Issues().empty())
      value.storage_issue = result.inspection->Issues().front();
    throw Failure{Status::kInvalidStorage, value};
  }
}
void Admission(PlyDiagnostic& diagnostic, Status& status, Stage stage,
               const allocation::AdmissionFailure& failure) {
  diagnostic.stage = stage;
  if (failure.Code() == allocation::AdmissionCode::kCanceled) {
    status = Status::kCanceled;
    diagnostic.code = Code::kStopRequested;
  } else if (failure.Code() == allocation::AdmissionCode::kSizeLimit) {
    status = Status::kLimitExceeded;
    diagnostic.code = Code::kRequestBudget;
  } else if (failure.Code() == allocation::AdmissionCode::kBudgetExceeded) {
    status = Status::kBudgetExceeded;
    diagnostic.code = Code::kRequestBudget;
  } else {
    status = Status::kInvalidConfiguration;
    diagnostic.code = Code::kConfigurationExtent;
  }
}

PlyReadResult Read(std::span<const std::byte> bytes,
                   const PlyReadLimits& limits, const PlyWorkPolicy& work,
                   const geo::ExecutionContext& context, std::stop_token stop,
                   const detail::TestOptions& options) {
  PlyReadResult result;
  Stage stage = Stage::kConfiguration;
  try {
    Validate(limits, work);
    Session session{work, stop, options, {}};
    session.Check(stage);
    session.Cap(bytes.size(), limits.max_source_bytes, Stage::kScan,
                Code::kSourceLimit);
    stage = Stage::kScan;
    auto allocation =
        allocation::AllocationContext(context, stop, options.allocation_backend)
            .WithRequestLimit(Host(limits.max_codec_new_requested_bytes));
    auto state = std::allocate_shared<detail::DocumentState>(
        allocation::Allocator<detail::DocumentState>(allocation), allocation);
    state->source.reserve(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      session.Byte(stage, i);
      state->source.push_back(bytes[i]);
    }
    Header header{*state, limits, session};
    header.Parse();
    stage = Stage::kDecode;
    {
      Decoded decoded(allocation);
      Decode(*state, limits, session, header.cursor, decoded);
      geo::MeshMaterializationInput input;
      input.positions = std::visit(
          [](const auto& positions) -> geo::PositionIngress {
            using T = typename std::decay_t<decltype(positions)>::value_type;
            return std::span<const T>(positions);
          },
          decoded.positions);
      input.topology =
          geo::PolygonTopologyInput{decoded.offsets, decoded.corners};
      input.attributes = decoded.views;
      stage = Stage::kMaterialization;
      session.Check(stage);
      auto materialized = geo::MaterializeMesh(input, limits.materialization,
                                               work.geometry, context, stop);
      result.execution.materialization = materialized.execution;
      if (!materialized.mesh)
        GeometryFailure(stage, materialized.status, materialized.diagnostic);
      state->mesh.emplace(*materialized.mesh);
    }
    stage = Stage::kInspection;
    session.Check(stage);
    auto inspected = geo::InspectMaterializedMesh(
        *state->mesh, limits.inspection, work.geometry, stop);
    result.execution.inspection = inspected.execution;
    InspectionFailure(inspected);
    state->inspection.emplace(*inspected.inspection);
    stage = Stage::kPublication;
    session.Check(stage);
    result.document.emplace(detail::Access::Document(std::move(state)));
    result.status = Status::kSuccess;
  } catch (const Failure& failure) {
    result.status = failure.status;
    result.diagnostic = failure.diagnostic;
  } catch (const allocation::AdmissionFailure& failure) {
    Admission(result.diagnostic, result.status, stage, failure);
  }
  return result;
}

std::uint32_t Narrow(double value, const Session& session) {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  const auto exponent = (bits & kDoubleExponentMask) >> kDoubleFractionBits;
  const auto fraction = bits & kDoubleFractionMask;
  const auto sign =
      static_cast<std::uint32_t>(bits >> kWidenSignShift) & kFloatSignMask;
  if (!Finite(value))
    session.Reject(Status::kInvalidStorage, Stage::kValidation,
                   Code::kNonfinite);
  if (exponent == 0 && fraction == 0) return sign;
  const auto unbiased = static_cast<int>(exponent) - kDoubleExponentBias;
  if (exponent != 0 && unbiased >= kFloatMinimumExponent &&
      unbiased <= kFloatMaximumExponent &&
      (fraction & ((UINT64_C(1) << kFractionShift) - 1)) == 0)
    return sign |
           (static_cast<std::uint32_t>(unbiased + kFloatExponentBias)
            << kFloatFractionBits) |
           static_cast<std::uint32_t>(fraction >> kFractionShift);
  if (exponent != 0 && unbiased >= kFloatSubnormalExponent &&
      unbiased < kFloatMinimumExponent) {
    const auto significand = (UINT64_C(1) << kDoubleFractionBits) | fraction;
    const auto shift =
        static_cast<unsigned>(static_cast<int>(kDoubleFractionBits) +
                              kFloatSubnormalExponent - unbiased);
    if ((significand & ((UINT64_C(1) << shift) - 1)) == 0)
      return sign | static_cast<std::uint32_t>(significand >> shift);
  }
  session.Reject(Status::kUnsupportedNumericConversion, Stage::kValidation,
                 Code::kNumericConversion);
}
Number AttributeValue(const geo::AttributeReadView& attribute,
                      std::size_t index) {
  return std::visit(
      [&](const auto& values) -> Number {
        using T = typename std::decay_t<decltype(values)>::value_type;
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double> ||
                      std::is_same_v<T, std::int32_t>)
          return values[index];
        else
          return static_cast<std::uint32_t>(values[index]);
      },
      attribute.values);
}
bool Matches(Type type, const geo::ScalarReadView& values) {
  switch (type) {
    case Type::kChar:
    case Type::kShort:
    case Type::kInt:
      return std::holds_alternative<std::span<const std::int32_t>>(values);
    case Type::kUchar:
      return std::holds_alternative<std::span<const std::uint8_t>>(values);
    case Type::kUshort:
      return std::holds_alternative<std::span<const std::uint16_t>>(values);
    case Type::kUint:
      return std::holds_alternative<std::span<const std::uint32_t>>(values);
    case Type::kFloat:
      return std::holds_alternative<std::span<const float>>(values);
    case Type::kDouble:
      return std::holds_alternative<std::span<const double>>(values);
    default:
      return false;
  }
}
void ValidateValue(const Number& number, Type type, const Session& session) {
  if (const auto* single_value = std::get_if<float>(&number)) {
    if (!FiniteValue(*single_value))
      session.Reject(Status::kInvalidStorage, Stage::kValidation,
                     Code::kNonfinite);
  } else if (const auto* double_value = std::get_if<double>(&number)) {
    if (!Finite(*double_value))
      session.Reject(Status::kInvalidStorage, Stage::kValidation,
                     Code::kNonfinite);
  } else if (const auto* signed_value = std::get_if<std::int32_t>(&number)) {
    const auto maximum = static_cast<std::int64_t>(Maximum(type));
    if (*signed_value < -maximum - 1 || *signed_value > maximum)
      session.Reject(Status::kUnsupportedNumericConversion, Stage::kValidation,
                     Code::kNumericConversion);
  } else if (std::get<std::uint32_t>(number) > Maximum(type))
    session.Reject(Status::kUnsupportedNumericConversion, Stage::kValidation,
                   Code::kNumericConversion);
}
void SchemaCaps(std::span<const PlyElementView> schema,
                const geo::MeshReadView& view, const PlyWriteLimits& limits,
                Session& session) {
  session.Cap(view.attributes.size(), limits.max_properties, Stage::kValidation,
              Code::kPropertyLimit);
  std::uint64_t properties = 0, names = 0;
  for (std::size_t e = 0; e < schema.size(); ++e) {
    session.Record(Stage::kValidation, e);
    properties = Accumulate(properties, schema[e].properties.size(),
                            limits.max_properties, session, Stage::kValidation,
                            Code::kPropertyLimit);
  }
  for (std::size_t e = 0; e < schema.size(); ++e) {
    session.location.element = e;
    const auto element_name =
        schema[e].kind == PlyElementKind::kVertex ? kVertex : kFace;
    session.Cap(element_name.size(), limits.max_name_bytes, Stage::kValidation,
                Code::kNameLimit);
    names = Accumulate(names, element_name.size(), limits.max_total_name_bytes,
                       session, Stage::kValidation, Code::kTotalNameLimit);
    for (std::size_t i = 0; i < schema[e].properties.size(); ++i) {
      session.Record(Stage::kValidation, i);
      session.location.property = i;
      const auto& property = schema[e].properties[i];
      session.Cap(property.name.size(), limits.max_name_bytes,
                  Stage::kValidation, Code::kNameLimit);
      names =
          Accumulate(names, property.name.size(), limits.max_total_name_bytes,
                     session, Stage::kValidation, Code::kTotalNameLimit);
    }
  }
  std::uint64_t supplied_names = 0, supplied_values = 0, supplied_list_rows = 0;
  for (std::size_t a = 0; a < view.attributes.size(); ++a) {
    session.Record(Stage::kValidation, a);
    session.location.attribute = a;
    const auto& attribute = view.attributes[a];
    session.Cap(attribute.name.size(), limits.max_name_bytes,
                Stage::kValidation, Code::kNameLimit);
    supplied_names = Accumulate(supplied_names, attribute.name.size(),
                                limits.max_total_name_bytes, session,
                                Stage::kValidation, Code::kTotalNameLimit);
    if (attribute.present || attribute.set_index ||
        !attribute.semantic.empty() || !attribute.metadata.empty() ||
        attribute.components != 1)
      session.Reject(Status::kUnsupportedBinding, Stage::kValidation,
                     Code::kSchemaBinding);
    const auto count = std::visit(
        [](const auto& values) { return values.size(); }, attribute.values);
    supplied_values =
        Accumulate(supplied_values, count, limits.max_property_values, session,
                   Stage::kValidation, Code::kPropertyValueLimit);
    if (attribute.offsets && !attribute.offsets->empty())
      supplied_list_rows =
          Accumulate(supplied_list_rows, attribute.offsets->size() - 1,
                     limits.max_list_rows, session, Stage::kValidation,
                     Code::kListRowLimit);
  }
  session.location = {};
}
void BindingCheck(std::span<const PlyElementView> schema,
                  const geo::MeshReadView& view, const PlyWriteLimits& limits,
                  Session& session) {
  const auto vertices = view.positions.size() / kComponents;
  const auto faces = view.face_offsets.size() - 1;
  std::size_t attributes = 0;
  std::uint64_t list_rows = 0, property_values = 0;
  bool face_element = false;
  Type coordinate_type = Type::kInvalid;
  for (std::size_t e = 0; e < schema.size(); ++e) {
    session.location.element = e;
    const auto& element = schema[e];
    const auto rows =
        element.kind == PlyElementKind::kVertex ? vertices : faces;
    if (element.kind == PlyElementKind::kFace) face_element = true;
    for (std::size_t i = 0; i < element.properties.size(); ++i) {
      session.Record(Stage::kValidation, i);
      session.location.property = i;
      const auto& property = element.properties[i];
      session.location.attribute = property.attribute;
      if (property.binding == Binding::kX)
        coordinate_type = property.value_type;
      if (property.kind == PlyPropertyKind::kList) {
        list_rows = Accumulate(list_rows, rows, limits.max_list_rows, session,
                               Stage::kValidation, Code::kListRowLimit);
      }
      if (property.binding != Binding::kAttribute) continue;
      if (attributes >= view.attributes.size())
        session.Reject(Status::kUnsupportedBinding, Stage::kValidation,
                       Code::kSchemaBinding);
      const auto& attribute = view.attributes[attributes++];
      const auto domain = element.kind == PlyElementKind::kVertex
                              ? geo::AttributeDomain::vertex
                              : geo::AttributeDomain::face;
      if (attribute.domain != domain || attribute.components != 1 ||
          !Matches(property.value_type, attribute.values) ||
          attribute.offsets.has_value() !=
              (property.kind == PlyPropertyKind::kList) ||
          attribute.present || attribute.set_index ||
          !attribute.semantic.empty() || !attribute.metadata.empty() ||
          !Equal(property.name, attribute.name, session, Stage::kValidation))
        session.Reject(Status::kUnsupportedBinding, Stage::kValidation,
                       Code::kSchemaBinding);
      const auto count = std::visit(
          [](const auto& values) { return values.size(); }, attribute.values);
      property_values =
          Accumulate(property_values, count, limits.max_property_values,
                     session, Stage::kValidation, Code::kPropertyValueLimit);
      for (std::size_t value = 0; value < count; ++value) {
        session.Value(Stage::kValidation, value);
        session.location.item = value;
        ValidateValue(AttributeValue(attribute, value), property.value_type,
                      session);
      }
      if (attribute.offsets) {
        for (std::size_t row = 0; row < rows; ++row) {
          session.Record(Stage::kValidation, row);
          session.location.row = row;
          const auto width =
              (*attribute.offsets)[row + 1] - (*attribute.offsets)[row];
          session.Cap(width, limits.max_list_items_per_row, Stage::kValidation,
                      Code::kListItemLimit);
          if (width > Maximum(*property.count_type))
            session.Reject(Status::kUnsupportedNumericConversion,
                           Stage::kValidation, Code::kNumericConversion);
        }
      }
      session.location.row.reset();
      session.location.item.reset();
    }
  }
  session.location = {};
  if (attributes != view.attributes.size() || (!face_element && faces))
    session.Reject(Status::kUnsupportedBinding, Stage::kValidation,
                   Code::kSchemaBinding);
  for (std::size_t i = 0; i < view.positions.size(); ++i) {
    session.Value(Stage::kValidation, i);
    session.location.row = i / kComponents;
    if (!Finite(view.positions[i]))
      session.Reject(Status::kInvalidStorage, Stage::kValidation,
                     Code::kNonfinite);
    if (coordinate_type == Type::kFloat)
      (void)Narrow(view.positions[i], session);
  }
  for (std::size_t e = 0; e < schema.size(); ++e) {
    if (schema[e].kind != PlyElementKind::kFace) continue;
    session.location.element = e;
    for (std::size_t i = 0; i < schema[e].properties.size(); ++i) {
      const auto& property = schema[e].properties[i];
      if (property.binding != Binding::kVertexIndices) continue;
      session.location.property = i;
      for (std::size_t face = 0; face < faces; ++face) {
        session.Record(Stage::kValidation, face);
        session.location.row = face;
        const auto width =
            view.face_offsets[face + 1] - view.face_offsets[face];
        session.Cap(width, limits.max_corners_per_face, Stage::kValidation,
                    Code::kPerFaceCornerLimit);
        session.Cap(width, limits.max_list_items_per_row, Stage::kValidation,
                    Code::kListItemLimit);
        if (width > Maximum(*property.count_type))
          session.Reject(Status::kUnsupportedNumericConversion,
                         Stage::kValidation, Code::kNumericConversion);
      }
      for (std::size_t corner = 0; corner < view.corner_vertices.size();
           ++corner) {
        session.Value(Stage::kValidation, corner);
        session.location.item = corner;
        if (view.corner_vertices[corner] > Maximum(property.value_type))
          session.Reject(Status::kUnsupportedNumericConversion,
                         Stage::kValidation, Code::kNumericConversion);
      }
    }
  }
  session.location = {};
}
struct Writer {
  detail::OutputState& state;
  const PlyWriteLimits& limits;
  Session& session;
  bool header = true;
  std::uint64_t line = 0, lines = 0, values = 0;
  bool first = true;
  void Byte(std::byte value, bool text) {
    session.Byte(Stage::kEncode, state.bytes.size());
    if (state.bytes.size() >= limits.max_output_bytes)
      session.Reject(Status::kLimitExceeded, Stage::kEncode,
                     Code::kOutputLimit);
    if (header && state.bytes.size() >= limits.max_header_bytes)
      session.Reject(Status::kLimitExceeded, Stage::kEncode,
                     Code::kHeaderLimit);
    if (text) {
      if (value == std::byte{'\n'}) {
        line = 0;
        if (header)
          session.Cap(++lines, limits.max_header_lines, Stage::kEncode,
                      Code::kHeaderLineLimit);
      } else
        session.Cap(++line, limits.max_line_bytes, Stage::kEncode,
                    Code::kLineLimit);
    }
    AppendBounded(state.bytes, value, limits.max_output_bytes, Stage::kEncode,
                  Code::kOutputLimit, detail::RelocationKind::kOutputBytes,
                  session);
  }
  void Append(std::string_view text) {
    for (char value : text) Byte(static_cast<std::byte>(value), true);
  }
  void Token(std::string_view text) {
    session.Cap(text.size(), limits.max_token_bytes, Stage::kEncode,
                Code::kTokenLimit);
    Append(text);
  }
  template <class T>
  void IntegerToken(T value) {
    std::array<char, kNumberBytes> bytes;
    session.Check(Stage::kEncode);
    const auto result =
        std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
    session.Check(Stage::kEncode);
    if (result.ec != std::errc{})
      session.Reject(Status::kInvalidStorage, Stage::kEncode,
                     Code::kIntegerRange);
    Token({bytes.data(), static_cast<std::size_t>(result.ptr - bytes.data())});
  }
  void Field(Type type, const Number& number) {
    session.Value(Stage::kEncode, values++);
    if (state.facts.encoding != PlyEncoding::kAscii) {
      std::uint64_t bits = std::visit(
          [](const auto& value) -> std::uint64_t {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, float>)
              return std::bit_cast<std::uint32_t>(value);
            else if constexpr (std::is_same_v<T, double>)
              return std::bit_cast<std::uint64_t>(value);
            else
              return static_cast<std::uint64_t>(value);
          },
          number);
      const auto width = Width(type);
      for (std::size_t i = 0; i < width; ++i) {
        const auto shift =
            (state.facts.encoding == PlyEncoding::kBinaryLittleEndian
                 ? i
                 : width - 1 - i) *
            kBitsPerByte;
        Byte(static_cast<std::byte>((bits >> shift) & UINT64_C(0xff)), false);
      }
      return;
    }
    if (!first) Append(" ");
    first = false;
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_floating_point_v<T>) {
            double wide;
            if constexpr (std::is_same_v<T, float>)
              wide = Widen(std::bit_cast<std::uint32_t>(value));
            else
              wide = value;
            std::array<char, kNumberBytes> bytes;
            session.Check(Stage::kEncode);
            const auto result =
                std::to_chars(bytes.data(), bytes.data() + bytes.size(), wide,
                              std::chars_format::general,
                              std::numeric_limits<T>::max_digits10);
            session.Check(Stage::kEncode);
            if (result.ec != std::errc{})
              session.Reject(Status::kInvalidStorage, Stage::kEncode,
                             Code::kNumericRange);
            Token({bytes.data(),
                   static_cast<std::size_t>(result.ptr - bytes.data())});
          } else
            IntegerToken(value);
        },
        number);
  }
  void Count(Type type, std::uint64_t value) {
    if (value > Maximum(type))
      session.Reject(Status::kUnsupportedNumericConversion, Stage::kEncode,
                     Code::kNumericConversion);
    if (Signed(type))
      Field(type, static_cast<std::int32_t>(value));
    else
      Field(type, static_cast<std::uint32_t>(value));
  }
  void Header(std::span<const PlyElementView> schema,
              const geo::MeshReadView& mesh) {
    Token(kMarker);
    Append("\n");
    Token(kFormat);
    Append(" ");
    Token(kEncodings[static_cast<std::size_t>(state.facts.encoding)]);
    Append(" ");
    Token(kVersion);
    Append("\n");
    for (std::size_t e = 0; e < schema.size(); ++e) {
      session.Record(Stage::kEncode, e);
      Token(kElement);
      Append(" ");
      Token(schema[e].kind == PlyElementKind::kVertex ? kVertex : kFace);
      Append(" ");
      IntegerToken(schema[e].kind == PlyElementKind::kVertex
                       ? mesh.positions.size() / kComponents
                       : mesh.face_offsets.size() - 1);
      Append("\n");
      for (std::size_t i = 0; i < schema[e].properties.size(); ++i) {
        session.Record(Stage::kEncode, i);
        const auto& property = schema[e].properties[i];
        Token(kProperty);
        Append(" ");
        if (property.kind == PlyPropertyKind::kList) {
          Token(kList);
          Append(" ");
          Token(kTypes[static_cast<std::size_t>(*property.count_type)]);
          Append(" ");
        }
        Token(kTypes[static_cast<std::size_t>(property.value_type)]);
        Append(" ");
        Token(property.name);
        Append("\n");
      }
    }
    Token(kEndHeader);
    Append("\n");
    header = false;
  }
  void Body(std::span<const PlyElementView> schema,
            const geo::MeshReadView& mesh) {
    for (std::size_t e = 0; e < schema.size(); ++e) {
      session.location.element = e;
      const auto& element = schema[e];
      const auto rows = element.kind == PlyElementKind::kVertex
                            ? mesh.positions.size() / kComponents
                            : mesh.face_offsets.size() - 1;
      for (std::size_t row = 0; row < rows; ++row) {
        session.Record(Stage::kEncode, row);
        session.location.row = row;
        first = true;
        for (std::size_t i = 0; i < element.properties.size(); ++i) {
          const auto& property = element.properties[i];
          session.location.property = i;
          session.location.attribute = property.attribute;
          session.location.item.reset();
          if (property.binding <= Binding::kZ) {
            const auto value =
                mesh.positions[row * kComponents +
                               static_cast<std::size_t>(property.binding)];
            if (property.value_type == Type::kFloat)
              Field(property.value_type,
                    std::bit_cast<float>(Narrow(value, session)));
            else
              Field(property.value_type, value);
          } else if (property.binding == Binding::kVertexIndices) {
            const auto begin = Host(mesh.face_offsets[row]),
                       end = Host(mesh.face_offsets[row + 1]);
            Count(*property.count_type, end - begin);
            for (auto corner = begin; corner < end; ++corner) {
              session.location.item = corner - begin;
              Count(property.value_type, mesh.corner_vertices[corner]);
            }
          } else {
            const auto& attribute = mesh.attributes[Host(*property.attribute)];
            if (property.kind == PlyPropertyKind::kScalar)
              Field(property.value_type, AttributeValue(attribute, row));
            else {
              const auto begin = Host((*attribute.offsets)[row]),
                         end = Host((*attribute.offsets)[row + 1]);
              Count(*property.count_type, end - begin);
              for (auto item = begin; item < end; ++item) {
                session.location.item = item - begin;
                Field(property.value_type, AttributeValue(attribute, item));
              }
            }
          }
        }
        if (state.facts.encoding == PlyEncoding::kAscii) Append("\n");
      }
    }
    session.location = {};
  }
};

PlyWriteResult Write(const geo::MaterializedMesh& mesh,
                     const PlyDocument& schema_source,
                     const PlyWriteOptions& options,
                     const PlyWriteLimits& limits, const PlyWorkPolicy& work,
                     std::stop_token stop, const detail::TestOptions& test) {
  PlyWriteResult result;
  Stage stage = Stage::kConfiguration;
  try {
    Validate(limits, work, options);
    Session session{work, stop, test, {}};
    session.Check(stage);
    stage = Stage::kValidation;
    const auto view = mesh.View();
    const auto schema = schema_source.Schema();
    const auto vertices = view.positions.size() / kComponents;
    const auto faces =
        view.face_offsets.empty() ? 0 : view.face_offsets.size() - 1;
    session.Cap(vertices, limits.max_vertices, stage, Code::kVertexLimit);
    session.Cap(faces, limits.max_faces, stage, Code::kFaceLimit);
    session.Cap(view.corner_vertices.size(), limits.max_corners, stage,
                Code::kCornerLimit);
    SchemaCaps(schema, view, limits, session);
    stage = Stage::kInspection;
    session.Check(stage);
    auto inspected = geo::InspectMaterializedMesh(mesh, limits.inspection,
                                                  work.geometry, stop);
    result.execution.inspection = inspected.execution;
    InspectionFailure(inspected);
    stage = Stage::kValidation;
    session.Check(stage);
    BindingCheck(schema, view, limits, session);
    const auto context = mesh.ShareExecutionContext();
    auto allocation =
        allocation::AllocationContext(context, stop, test.allocation_backend)
            .WithRequestLimit(Host(limits.max_codec_new_requested_bytes));
    auto state = std::allocate_shared<detail::OutputState>(
        allocation::Allocator<detail::OutputState>(allocation), allocation);
    auto& facts = state->facts;
    facts.source_encoding = schema_source.Encoding();
    facts.encoding = options.encoding;
    facts.rewrite_policy = options.rewrite_policy;
    facts.vertex_count = vertices;
    facts.face_count = faces;
    facts.corner_count = view.corner_vertices.size();
    facts.correspondence = PlyCorrespondence::kIdentity;
    for (const auto& element : schema) {
      if (element.kind == PlyElementKind::kVertex)
        facts.source_vertex_count = element.original_count;
      else
        facts.source_face_count = element.original_count;
      facts.property_count += element.properties.size();
    }
    stage = Stage::kEncode;
    session.Check(stage);
    Writer writer{*state, limits, session};
    writer.Header(schema, view);
    writer.Body(schema, view);
    inspected.inspection.reset();
    stage = Stage::kPublication;
    session.Check(stage);
    result.output.emplace(detail::Access::Output(std::move(state)));
    result.status = Status::kSuccess;
  } catch (const Failure& failure) {
    result.status = failure.status;
    result.diagnostic = failure.diagnostic;
  } catch (const allocation::AdmissionFailure& failure) {
    Admission(result.diagnostic, result.status, stage, failure);
  }
  return result;
}
}  // namespace

PlyDocument::PlyDocument(
    std::shared_ptr<const detail::DocumentState> state) noexcept
    : state_(std::move(state)) {}
PlyDocument::PlyDocument(const PlyDocument&) noexcept = default;
PlyDocument& PlyDocument::operator=(const PlyDocument&) noexcept = default;
PlyDocument::~PlyDocument() noexcept = default;
PlyEncoding PlyDocument::Encoding() const noexcept { return state_->encoding; }
std::span<const std::byte> PlyDocument::OriginalBytes() const noexcept {
  return state_->source;
}
std::span<const PlyElementView> PlyDocument::Schema() const noexcept {
  return std::span<const PlyElementView>(state_->views)
      .first(state_->element_count);
}
const geo::MaterializedMesh& PlyDocument::Mesh() const noexcept {
  return *state_->mesh;
}
const geo::MeshInspection& PlyDocument::Inspection() const noexcept {
  return *state_->inspection;
}
PlyRequestedBytes PlyDocument::NewRequestedBytes() const noexcept {
  PlyRequestedBytes value{state_->allocation.OperationActiveBytes(),
                          state_->mesh->NewRequestedBytes(),
                          state_->inspection->NewRequestedBytes(), 0};
  value.total = value.codec + value.materialization + value.inspection;
  return value;
}
PlyOutput::PlyOutput(std::shared_ptr<const detail::OutputState> state) noexcept
    : state_(std::move(state)) {}
PlyOutput::PlyOutput(const PlyOutput&) noexcept = default;
PlyOutput& PlyOutput::operator=(const PlyOutput&) noexcept = default;
PlyOutput::~PlyOutput() noexcept = default;
std::span<const std::byte> PlyOutput::Bytes() const noexcept {
  return state_->bytes;
}
const PlyWriteFacts& PlyOutput::Facts() const noexcept { return state_->facts; }
PlyRequestedBytes PlyOutput::NewRequestedBytes() const noexcept {
  const auto bytes = state_->allocation.OperationActiveBytes();
  return {bytes, 0, 0, bytes};
}
PlyReadResult ReadPly(std::span<const std::byte> bytes,
                      const PlyReadLimits& limits, const PlyWorkPolicy& work,
                      const geo::ExecutionContext& context,
                      std::stop_token stop) {
  return Read(bytes, limits, work, context, stop, {});
}
PlyWriteResult WritePly(const geo::MaterializedMesh& mesh,
                        const PlyDocument& schema_source,
                        const PlyWriteOptions& options,
                        const PlyWriteLimits& limits, const PlyWorkPolicy& work,
                        std::stop_token stop) {
  return Write(mesh, schema_source, options, limits, work, stop, {});
}
namespace ply_detail {
PlyReadResult ReadPlyForTest(std::span<const std::byte> bytes,
                             const PlyReadLimits& limits,
                             const PlyWorkPolicy& work,
                             const geo::ExecutionContext& context,
                             std::stop_token stop, const TestOptions& options) {
  return Read(bytes, limits, work, context, stop, options);
}
PlyWriteResult WritePlyForTest(const geo::MaterializedMesh& mesh,
                               const PlyDocument& schema_source,
                               const PlyWriteOptions& options,
                               const PlyWriteLimits& limits,
                               const PlyWorkPolicy& work, std::stop_token stop,
                               const TestOptions& test) {
  return Write(mesh, schema_source, options, limits, work, stop, test);
}
}  // namespace ply_detail
static_assert(std::is_nothrow_copy_constructible_v<PlyDocument>);
static_assert(std::is_nothrow_copy_assignable_v<PlyDocument>);
static_assert(std::is_nothrow_destructible_v<PlyDocument>);
static_assert(std::is_nothrow_copy_constructible_v<PlyOutput>);
static_assert(std::is_nothrow_copy_assignable_v<PlyOutput>);
static_assert(std::is_nothrow_destructible_v<PlyOutput>);
}  // namespace meshvale::interchange
