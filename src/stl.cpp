// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/stl.h"

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

#include "finite_double_format.h"
#include "gltf_allocation.h"
#include "stl_test_internal.h"

namespace meshvale::interchange::stl_detail {
namespace allocation = gltf_detail;
template <class T>
using Vector = std::vector<T, allocation::Allocator<T>>;
struct DocumentState {
  explicit DocumentState(const allocation::AllocationContext& context)
      : allocation(context),
        source(allocation::Allocator<std::byte>(context)) {}
  allocation::AllocationContext allocation;
  Vector<std::byte> source;
  StlEncoding encoding = StlEncoding::kInvalid;
  std::size_t name_offset = 0;
  std::size_t name_size = 0;
  std::optional<geometry::MaterializedMesh> mesh;
  std::optional<geometry::MeshInspection> inspection;
};
struct OutputState {
  explicit OutputState(const allocation::AllocationContext& context)
      : allocation(context),
        bytes(allocation::Allocator<std::byte>(context)),
        corners(allocation::Allocator<geometry::index_t>(context)) {}
  allocation::AllocationContext allocation;
  Vector<std::byte> bytes;
  Vector<geometry::index_t> corners;
  StlWriteFacts facts;
};
struct Access {
  static StlDocument Document(
      std::shared_ptr<const DocumentState> state) noexcept {
    return StlDocument(std::move(state));
  }
  static StlOutput Output(std::shared_ptr<const OutputState> state) noexcept {
    return StlOutput(std::move(state));
  }
};
}  // namespace meshvale::interchange::stl_detail

namespace meshvale::interchange {
namespace {
namespace detail = stl_detail;
namespace geo = geometry;
namespace allocation = gltf_detail;
using Status = StlStatus;
using Stage = StlStage;
using Code = StlDiagnosticCode;
constexpr std::size_t kHeaderBytes = 80;
constexpr std::size_t kCountBytes = 4;
constexpr std::size_t kPrefixBytes = kHeaderBytes + kCountBytes;
constexpr std::size_t kFacetBytes = 50;
constexpr std::size_t kComponents = 3;
constexpr std::size_t kPositionScalars = 9;
constexpr std::size_t kFacetScalars = kComponents + kPositionScalars;
constexpr std::size_t kFloatBytes = 4;
constexpr std::size_t kAttributeWordBytes = 2;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xff;
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
constexpr unsigned char kPrintableAsciiFirst = 0x20;
constexpr unsigned char kPrintableAsciiLast = 0x7e;
// Finite binary64 general/max_digits10 output is shorter than this buffer.
constexpr std::size_t kNumberBytes = 128;
constexpr std::string_view kNormalName = "facet_normal";
constexpr std::string_view kNormalSemantic = "normal";
constexpr std::string_view kSolid = "solid";
constexpr std::string_view kEndSolid = "endsolid";
constexpr std::string_view kFacet = "facet";
constexpr std::string_view kNormal = "normal";
constexpr std::string_view kOuter = "outer";
constexpr std::string_view kLoop = "loop";
constexpr std::string_view kVertex = "vertex";
constexpr std::string_view kEndLoop = "endloop";
constexpr std::string_view kEndFacet = "endfacet";

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
  StlDiagnostic diagnostic;
};
[[noreturn]] void Fail(Status status, Stage stage, Code code,
                       std::optional<std::uint64_t> byte = {},
                       std::optional<std::uint64_t> facet = {}) {
  StlDiagnostic diagnostic;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.byte_offset = byte;
  diagnostic.facet = facet;
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
std::size_t Product(std::size_t count, std::size_t width) {
  if (count >
      static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
          width)
    Fail(Status::kLimitExceeded, Stage::kValidation, Code::kFacetLimit);
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
void Validate(const StlWorkPolicy& work) {
  for (auto value :
       {work.cancellation_block_bytes, work.cancellation_block_tokens,
        work.cancellation_block_facets, work.geometry.parallel_min_bytes,
        work.geometry.partition_bytes, work.geometry.cancellation_block_scalars,
        work.geometry.max_jobs})
    Positive(value);
}
void Encoding(StlEncoding encoding) {
  if (encoding != StlEncoding::kAscii && encoding != StlEncoding::kBinary)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration, Code::kEncoding);
}
void Validate(const StlReadLimits& limits, const StlWorkPolicy& work,
              StlEncoding encoding) {
  Encoding(encoding);
  for (auto value :
       {limits.max_source_bytes, limits.max_facets, limits.max_name_bytes,
        limits.max_line_bytes, limits.max_token_bytes,
        limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.materialization);
  Validate(limits.inspection);
  Validate(work);
  if (Add(Add(limits.max_codec_new_requested_bytes,
              limits.materialization.max_new_requested_bytes),
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const StlWriteLimits& limits, const StlWorkPolicy& work,
              StlEncoding encoding) {
  Encoding(encoding);
  for (auto value :
       {limits.max_output_bytes, limits.max_facets, limits.max_name_bytes,
        limits.max_line_bytes, limits.max_token_bytes,
        limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.inspection);
  Validate(work);
  if (Add(limits.max_codec_new_requested_bytes,
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
struct Session {
  const StlWorkPolicy& work;
  std::stop_token stop;
  const detail::TestOptions& test;
  void Check(Stage stage, std::uint64_t ordinal = 0) const {
    if (test.checkpoint) test.checkpoint(stage, ordinal, test.state);
    if (stop.stop_requested())
      Fail(Status::kCanceled, stage, Code::kStopRequested);
  }
  void Byte(Stage stage, std::size_t offset) const {
    if (offset % work.cancellation_block_bytes == 0) Check(stage, offset);
  }
  void Facet(Stage stage, std::size_t index) const {
    if (index % work.cancellation_block_facets == 0) Check(stage, index);
  }
};
bool Space(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}
std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
bool EqualBytes(std::string_view first, std::string_view second,
                const Session& session, Stage stage) {
  if (first.size() != second.size()) return false;
  for (std::size_t index = 0; index < first.size(); ++index) {
    session.Byte(stage, index);
    if (first[index] != second[index]) return false;
  }
  return true;
}
void Lines(std::span<const std::byte> bytes, std::uint64_t limit,
           const Session& session) {
  std::size_t line = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    session.Byte(Stage::kScan, index);
    const char value = static_cast<char>(bytes[index]);
    if (value == '\r' || value == '\n')
      line = 0;
    else if (++line > limit)
      Fail(Status::kLimitExceeded, Stage::kScan, Code::kLineLimit, index);
  }
}
struct Decoded {
  explicit Decoded(const allocation::AllocationContext& allocation)
      : positions(allocation::Allocator<double>(allocation)),
        normals(allocation::Allocator<double>(allocation)),
        offsets(allocation::Allocator<geo::index_t>(allocation)),
        corners(allocation::Allocator<geo::index_t>(allocation)) {}
  detail::Vector<double> positions;
  detail::Vector<double> normals;
  detail::Vector<geo::index_t> offsets;
  detail::Vector<geo::index_t> corners;
  void Reserve(std::size_t facets) {
    positions.reserve(Product(facets, kPositionScalars));
    normals.reserve(Product(facets, kComponents));
    offsets.reserve(Host(Add(facets, 1)));
    corners.reserve(Product(facets, kComponents));
    offsets.push_back(0);
  }
  void Store(std::size_t facet,
             const std::array<double, kFacetScalars>& values) {
    for (std::size_t component = 0; component < kComponents; ++component) {
      normals.push_back(values[component]);
      corners.push_back(facet * kComponents + component);
    }
    for (std::size_t component = kComponents; component < values.size();
         ++component)
      positions.push_back(values[component]);
    offsets.push_back(corners.size());
  }
};
void Counts(std::size_t facets, const StlReadLimits& limits) {
  const auto corners = Product(facets, kComponents);
  if (facets > limits.max_facets || facets > limits.materialization.max_faces ||
      corners > limits.materialization.max_vertices ||
      corners > limits.materialization.max_corners ||
      Add(facets, 1) > limits.materialization.max_face_offsets ||
      Product(facets, kComponents) >
          limits.materialization.max_attribute_scalars)
    Fail(Status::kLimitExceeded, Stage::kScan, Code::kFacetLimit);
  (void)Product(Product(facets, kPositionScalars), sizeof(double));
  (void)Product(corners, sizeof(geo::index_t));
}
double Decimal(std::string_view token, std::size_t offset,
               const Session& session) {
  std::size_t cursor = 0;
  if (cursor < token.size() && (token[cursor] == '+' || token[cursor] == '-'))
    ++cursor;
  auto digits = [&] {
    const auto start = cursor;
    while (cursor < token.size() && token[cursor] >= '0' &&
           token[cursor] <= '9') {
      session.Byte(Stage::kScan, offset + cursor);
      ++cursor;
    }
    return cursor - start;
  };
  auto count = digits();
  if (cursor < token.size() && token[cursor] == '.') {
    ++cursor;
    count += digits();
  }
  if (!count)
    Fail(Status::kMalformed, Stage::kScan, Code::kNumericSyntax, offset);
  if (cursor < token.size() && (token[cursor] == 'e' || token[cursor] == 'E')) {
    ++cursor;
    if (cursor < token.size() && (token[cursor] == '+' || token[cursor] == '-'))
      ++cursor;
    if (!digits())
      Fail(Status::kMalformed, Stage::kScan, Code::kNumericSyntax, offset);
  }
  if (cursor != token.size())
    Fail(Status::kMalformed, Stage::kScan, Code::kNumericSyntax, offset);
  if (token.front() == '+') token.remove_prefix(1);
  double value = 0;
  // The standard conversion has no callback. Its indivisible work is bounded
  // by the admitted token; observe cancellation immediately around it.
  session.Check(Stage::kScan, offset);
  const auto converted =
      std::from_chars(token.data(), token.data() + token.size(), value,
                      std::chars_format::general);
  session.Check(Stage::kScan, offset);
  if (converted.ec == std::errc::result_out_of_range)
    Fail(Status::kMalformed, Stage::kScan, Code::kNumericRange, offset);
  if (converted.ec != std::errc{} ||
      converted.ptr != token.data() + token.size())
    Fail(Status::kMalformed, Stage::kScan, Code::kNumericSyntax, offset);
  if (!Finite(value))
    Fail(Status::kMalformed, Stage::kScan, Code::kNonfinite, offset);
  return value;
}
struct Ascii {
  std::string_view text;
  const StlReadLimits& limits;
  const Session& session;
  std::size_t cursor = 0;
  std::size_t tokens = 0;
  std::size_t name_offset = 0;
  std::size_t name_size = 0;
  void Skip() {
    while (cursor < text.size() && Space(text[cursor])) {
      session.Byte(Stage::kScan, cursor);
      ++cursor;
    }
  }
  std::string_view Token() {
    Skip();
    if (tokens++ % session.work.cancellation_block_tokens == 0)
      session.Check(Stage::kScan, tokens);
    const auto begin = cursor;
    while (cursor < text.size() && !Space(text[cursor])) {
      session.Byte(Stage::kScan, cursor);
      if (cursor - begin >= limits.max_token_bytes)
        Fail(Status::kLimitExceeded, Stage::kScan, Code::kTokenLimit, cursor);
      ++cursor;
    }
    if (begin == cursor)
      Fail(Status::kMalformed, Stage::kScan, Code::kSyntax, cursor);
    return text.substr(begin, cursor - begin);
  }
  void Word(std::string_view word) {
    if (Token() != word)
      Fail(Status::kMalformed, Stage::kScan, Code::kSyntax, cursor);
  }
  std::string_view Name(std::string_view keyword) {
    Word(keyword);
    if (cursor < text.size() && text[cursor] == ' ') ++cursor;
    const auto begin = cursor;
    while (cursor < text.size() && text[cursor] != '\r' &&
           text[cursor] != '\n') {
      session.Byte(Stage::kScan, cursor);
      const auto value = static_cast<unsigned char>(text[cursor]);
      if (value < kPrintableAsciiFirst || value > kPrintableAsciiLast)
        Fail(Status::kMalformed, Stage::kScan, Code::kSyntax, cursor);
      if (cursor - begin >= limits.max_name_bytes)
        Fail(Status::kLimitExceeded, Stage::kScan, Code::kNameLimit, cursor);
      ++cursor;
    }
    return text.substr(begin, cursor - begin);
  }
  std::size_t Parse(Decoded* decoded) {
    auto name = Name(kSolid);
    name_offset = static_cast<std::size_t>(name.data() - text.data());
    name_size = name.size();
    std::size_t facets = 0;
    for (;;) {
      Skip();
      if (text.substr(cursor, kEndSolid.size()) == kEndSolid) {
        if (!EqualBytes(Name(kEndSolid), name, session, Stage::kScan))
          Fail(Status::kMalformed, Stage::kScan, Code::kSyntax, cursor);
        Skip();
        if (cursor != text.size())
          Fail(Status::kUnsupportedVariant, Stage::kScan, Code::kTrailingBytes,
               cursor);
        return facets;
      }
      session.Facet(Stage::kDecode, facets);
      Word(kFacet);
      Word(kNormal);
      std::array<double, kFacetScalars> values{};
      for (std::size_t i = 0; i < kComponents; ++i) {
        const auto value = Token();
        values[i] = Decimal(value, cursor - value.size(), session);
      }
      Word(kOuter);
      Word(kLoop);
      for (std::size_t vertex = 0; vertex < kComponents; ++vertex) {
        Word(kVertex);
        for (std::size_t component = 0; component < kComponents; ++component) {
          const auto value = Token();
          values[kComponents + vertex * kComponents + component] =
              Decimal(value, cursor - value.size(), session);
        }
      }
      Word(kEndLoop);
      Word(kEndFacet);
      Counts(Host(Add(facets, 1)), limits);
      if (decoded) decoded->Store(facets, values);
      ++facets;
    }
  }
};
std::uint32_t Little(std::span<const std::byte> bytes, std::size_t offset,
                     std::size_t count, const Session& session) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < count; ++index) {
    session.Byte(Stage::kDecode, offset + index);
    value |= std::to_integer<std::uint32_t>(bytes[offset + index])
             << (index * kByteBits);
  }
  return value;
}
std::size_t Binary(std::span<const std::byte> bytes,
                   const StlReadLimits& limits, const Session& session,
                   Decoded* decoded) {
  if (bytes.size() < kPrefixBytes)
    Fail(Status::kMalformed, Stage::kScan, Code::kBinaryExtent);
  const auto facets = static_cast<std::size_t>(
      Little(bytes, kHeaderBytes, kCountBytes, session));
  const auto size = Add(kPrefixBytes, Product(facets, kFacetBytes));
  if (bytes.size() < size)
    Fail(Status::kMalformed, Stage::kScan, Code::kBinaryExtent);
  if (bytes.size() > size)
    Fail(Status::kUnsupportedVariant, Stage::kScan, Code::kTrailingBytes);
  Counts(facets, limits);
  for (std::size_t facet = 0; facet < facets; ++facet) {
    session.Facet(Stage::kDecode, facet);
    const auto begin = kPrefixBytes + facet * kFacetBytes;
    if (Little(bytes, begin + kFacetScalars * kFloatBytes, kAttributeWordBytes,
               session))
      Fail(Status::kUnsupportedVariant, Stage::kDecode, Code::kAttributeWord,
           begin, facet);
    std::array<double, kFacetScalars> values{};
    for (std::size_t component = 0; component < values.size(); ++component) {
      const auto offset = begin + component * kFloatBytes;
      const auto bits = Little(bytes, offset, kFloatBytes, session);
      if ((bits & kFloatExponentMask) == kFloatExponentMask)
        Fail(Status::kMalformed, Stage::kDecode, Code::kNonfinite, offset,
             facet);
      values[component] = Widen(bits);
    }
    if (decoded) decoded->Store(facet, values);
  }
  return facets;
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
  StlDiagnostic value;
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
    StlDiagnostic value;
    value.stage = Stage::kInspection;
    value.code = Code::kStorageIssues;
    value.geometry_status = result.status;
    value.geometry_diagnostic = result.diagnostic;
    if (!result.inspection->Issues().empty())
      value.storage_issue = result.inspection->Issues().front();
    throw Failure{Status::kInvalidStorage, value};
  }
}
void Admission(StlDiagnostic& diagnostic, Status& status, Stage stage,
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
StlReadResult Read(std::span<const std::byte> bytes, StlEncoding encoding,
                   const StlReadLimits& limits, const StlWorkPolicy& work,
                   const geo::ExecutionContext& context, std::stop_token stop,
                   const detail::TestOptions& options) {
  StlReadResult result;
  Stage stage = Stage::kConfiguration;
  try {
    Validate(limits, work, encoding);
    Session session{work, stop, options};
    session.Check(stage);
    if (bytes.size() > limits.max_source_bytes)
      Fail(Status::kLimitExceeded, Stage::kScan, Code::kSourceLimit);
    stage = Stage::kScan;
    if (encoding == StlEncoding::kAscii)
      Lines(bytes, limits.max_line_bytes, session);
    Ascii parser{Text(bytes), limits, session};
    const auto facets = encoding == StlEncoding::kAscii
                            ? parser.Parse(nullptr)
                            : Binary(bytes, limits, session, nullptr);
    Counts(facets, limits);
    stage = Stage::kDecode;
    auto allocation =
        allocation::AllocationContext(context, stop, options.allocation_backend)
            .WithRequestLimit(Host(limits.max_codec_new_requested_bytes));
    auto state = std::allocate_shared<detail::DocumentState>(
        allocation::Allocator<detail::DocumentState>(allocation), allocation);
    state->encoding = encoding;
    state->name_offset = parser.name_offset;
    state->name_size = parser.name_size;
    state->source.reserve(bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      session.Byte(Stage::kDecode, index);
      state->source.push_back(bytes[index]);
    }
    stage = Stage::kDecode;
    {
      Decoded decoded(allocation);
      decoded.Reserve(facets);
      if (encoding == StlEncoding::kAscii) {
        Ascii owned{Text(state->source), limits, session};
        (void)owned.Parse(&decoded);
      } else
        (void)Binary(state->source, limits, session, &decoded);
      geo::AttributeMaterializationInput normal;
      normal.domain = geo::AttributeDomain::face;
      normal.name = kNormalName;
      normal.semantic = kNormalSemantic;
      normal.components = kComponents;
      normal.values = std::span<const double>(decoded.normals);
      const std::array attributes{normal};
      geo::MeshMaterializationInput input;
      input.positions = std::span<const double>(decoded.positions);
      input.topology =
          geo::PolygonTopologyInput{decoded.offsets, decoded.corners};
      input.attributes = attributes;
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

struct Writer {
  detail::OutputState& state;
  const StlWriteLimits& limits;
  const Session& session;
  std::size_t line = 0;
  std::size_t tokens = 0;
  void GrowOutput() {
    auto& bytes = state.bytes;
    const auto host_limit = Host(limits.max_output_bytes);
    const auto extent =
        static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    const auto maximum = host_limit < extent ? host_limit : extent;
    if (bytes.size() >= maximum)
      Fail(Status::kLimitExceeded, Stage::kEncode, Code::kOutputLimit);
    const auto capacity = bytes.capacity();
    const auto next =
        capacity == 0 ? 1 : (capacity <= maximum / 2 ? capacity * 2 : maximum);
    detail::Vector<std::byte> replacement(bytes.get_allocator());
    session.Check(Stage::kEncode);
    // Empty reserve does not relocate existing bytes. Both real blocks remain
    // charged during the explicit checkpointed transfer.
    replacement.reserve(next);
    const auto checkpoint = [&](std::size_t copied) {
      if (session.test.relocation_checkpoint)
        session.test.relocation_checkpoint(Stage::kEncode, copied, bytes.size(),
                                           session.test.state);
      session.Check(Stage::kEncode, copied);
    };
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      if (i % session.work.cancellation_block_bytes == 0) checkpoint(i);
      replacement.push_back(bytes[i]);
    }
    checkpoint(bytes.size());
    static_assert(noexcept(bytes.swap(replacement)));
    bytes.swap(replacement);
  }
  void Append(std::string_view text) {
    if (text.size() > limits.max_output_bytes - state.bytes.size())
      Fail(Status::kLimitExceeded, Stage::kEncode, Code::kOutputLimit);
    for (char value : text) {
      session.Byte(Stage::kEncode, state.bytes.size());
      if (value == '\r' || value == '\n')
        line = 0;
      else if (++line > limits.max_line_bytes)
        Fail(Status::kLimitExceeded, Stage::kEncode, Code::kLineLimit);
      if (state.bytes.size() == state.bytes.capacity()) GrowOutput();
      state.bytes.push_back(static_cast<std::byte>(value));
    }
  }
  void Token(std::string_view text) {
    if (text.size() > limits.max_token_bytes)
      Fail(Status::kLimitExceeded, Stage::kEncode, Code::kTokenLimit);
    if (tokens++ % session.work.cancellation_block_tokens == 0)
      session.Check(Stage::kEncode, tokens);
    Append(text);
  }
  void Number(double value) {
    std::array<char, kNumberBytes> bytes;
    session.Check(Stage::kEncode);
    const auto text = numeric_format_detail::FiniteDoubleToChars(
        bytes, value, std::numeric_limits<double>::max_digits10,
        session.work.cancellation_block_bytes,
        [&](numeric_format_detail::Phase phase, std::uint64_t completed,
            std::uint64_t total) {
          if (session.test.numeric_format_checkpoint)
            session.test.numeric_format_checkpoint(
                Stage::kEncode, phase, completed, total, session.test.state);
          session.Check(Stage::kEncode);
        });
    session.Check(Stage::kEncode);
    if (text.ec != std::errc{})
      Fail(Status::kUnsupportedNumericConversion, Stage::kEncode,
           Code::kNumericConversion);
    Token({bytes.data(), static_cast<std::size_t>(text.ptr - bytes.data())});
  }
};
void PutLittle(detail::Vector<std::byte>& bytes, std::size_t offset,
               std::uint32_t value, std::size_t count, const Session& session) {
  for (std::size_t index = 0; index < count; ++index) {
    session.Byte(Stage::kEncode, offset + index);
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * kByteBits)) & kByteMask);
  }
}
std::uint32_t Narrow(double value, std::size_t facet) {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  const auto exponent = (bits & kDoubleExponentMask) >> kDoubleFractionBits;
  const auto fraction = bits & kDoubleFractionMask;
  const auto sign =
      static_cast<std::uint32_t>(bits >> kWidenSignShift) & kFloatSignMask;
  if (!Finite(value))
    Fail(Status::kInvalidStorage, Stage::kValidation, Code::kNonfinite, {},
         facet);
  if (exponent == 0 && fraction == 0) return sign;
  const auto unbiased = static_cast<int>(exponent) - kDoubleExponentBias;
  if (exponent != 0 && unbiased >= kFloatMinimumExponent &&
      unbiased <= kFloatMaximumExponent &&
      (fraction & ((UINT64_C(1) << kFractionShift) - 1)) == 0) {
    return sign |
           (static_cast<std::uint32_t>(unbiased + kFloatExponentBias)
            << kFloatFractionBits) |
           static_cast<std::uint32_t>(fraction >> kFractionShift);
  }
  if (exponent != 0 && unbiased >= kFloatSubnormalExponent &&
      unbiased < kFloatMinimumExponent) {
    const auto significand = (UINT64_C(1) << kDoubleFractionBits) | fraction;
    const auto shift =
        static_cast<unsigned>(static_cast<int>(kDoubleFractionBits) +
                              kFloatSubnormalExponent - unbiased);
    if ((significand & ((UINT64_C(1) << shift) - 1)) == 0)
      return sign | static_cast<std::uint32_t>(significand >> shift);
  }
  Fail(Status::kUnsupportedNumericConversion, Stage::kEncode,
       Code::kNumericConversion, {}, facet);
}
void Metadata(const StlWriteOptions& options, const StlWriteLimits& limits,
              const Session& session) {
  if (!options.acknowledge_corner_expansion)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kExpansionAcknowledgment);
  if (options.encoding == StlEncoding::kBinary) {
    if (options.binary_header.size() != kHeaderBytes ||
        !options.ascii_name.empty())
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kMetadata);
  } else {
    if (!options.binary_header.empty())
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kMetadata);
    if (options.ascii_name.size() > limits.max_name_bytes)
      Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kNameLimit);
    for (std::size_t index = 0; index < options.ascii_name.size(); ++index) {
      session.Byte(Stage::kValidation, index);
      const auto value = static_cast<unsigned char>(options.ascii_name[index]);
      if (value < kPrintableAsciiFirst || value > kPrintableAsciiLast)
        Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
             Code::kMetadata);
    }
  }
}
StlWriteResult Write(const geo::MaterializedMesh& mesh,
                     const StlWriteOptions& options,
                     const StlWriteLimits& limits, const StlWorkPolicy& work,
                     std::stop_token stop, const detail::TestOptions& test) {
  StlWriteResult result;
  Stage stage = Stage::kConfiguration;
  try {
    Validate(limits, work, options.encoding);
    Session session{work, stop, test};
    session.Check(stage);
    Metadata(options, limits, session);
    stage = Stage::kInspection;
    session.Check(stage);
    auto inspected = geo::InspectMaterializedMesh(mesh, limits.inspection,
                                                  work.geometry, stop);
    result.execution.inspection = inspected.execution;
    InspectionFailure(inspected);
    stage = Stage::kValidation;
    const auto view = mesh.View();
    const auto facets = view.face_offsets.size() - 1;
    if (facets > limits.max_facets)
      Fail(Status::kLimitExceeded, stage, Code::kFacetLimit);
    if (options.face_normal_attribute >= view.attributes.size())
      Fail(Status::kUnsupportedVariant, stage, Code::kNormalSelection);
    const auto& normal = view.attributes[Host(options.face_normal_attribute)];
    if (view.attributes.size() != 1)
      Fail(Status::kUnsupportedVariant, stage, Code::kUnsupportedAttribute);
    if (normal.domain != geo::AttributeDomain::face ||
        normal.semantic != kNormalSemantic ||
        normal.components != kComponents || normal.set_index ||
        normal.offsets || normal.present || !normal.metadata.empty() ||
        (!std::holds_alternative<std::span<const float>>(normal.values) &&
         !std::holds_alternative<std::span<const double>>(normal.values)))
      Fail(Status::kUnsupportedVariant, stage, Code::kNormalSelection);
    const auto normal_value = [&](std::size_t index) {
      if (const auto* single =
              std::get_if<std::span<const float>>(&normal.values))
        return Widen(std::bit_cast<std::uint32_t>((*single)[index]));
      return std::get<std::span<const double>>(normal.values)[index];
    };
    for (std::size_t facet = 0; facet < facets; ++facet) {
      session.Facet(stage, facet);
      if (view.face_offsets[facet + 1] - view.face_offsets[facet] !=
          kComponents)
        Fail(Status::kUnsupportedVariant, stage, Code::kNotTriangles, {},
             facet);
      for (std::size_t component = 0; component < kComponents; ++component)
        if (!Finite(normal_value(facet * kComponents + component)))
          Fail(Status::kInvalidStorage, stage, Code::kNonfinite, {}, facet);
    }
    const auto context = mesh.ShareExecutionContext();
    auto allocation =
        allocation::AllocationContext(context, stop, test.allocation_backend)
            .WithRequestLimit(Host(limits.max_codec_new_requested_bytes));
    auto state = std::allocate_shared<detail::OutputState>(
        allocation::Allocator<detail::OutputState>(allocation), allocation);
    auto& facts = state->facts;
    facts.encoding = options.encoding;
    facts.face_normal_attribute = options.face_normal_attribute;
    facts.source_vertex_count = view.positions.size() / kComponents;
    facts.source_face_count = facets;
    facts.corner_expansion_acknowledged = true;
    if (options.source_document) {
      facts.source_encoding = options.source_document->Encoding();
      const bool retained =
          *facts.source_encoding == options.encoding &&
          (options.encoding == StlEncoding::kBinary
               ? EqualBytes(Text(options.binary_header),
                            Text(options.source_document->BinaryHeader()),
                            session, Stage::kValidation)
               : EqualBytes(options.ascii_name,
                            options.source_document->AsciiName(), session,
                            Stage::kValidation));
      facts.metadata_policy = retained
                                  ? StlMetadataPolicy::kRetainedSourceMetadata
                                  : StlMetadataPolicy::kReplacedSourceMetadata;
    }
    state->corners.reserve(view.corner_vertices.size());
    for (std::size_t corner = 0; corner < view.corner_vertices.size();
         ++corner) {
      session.Facet(stage, corner / kComponents);
      state->corners.push_back(view.corner_vertices[corner]);
    }
    stage = Stage::kEncode;
    if (options.encoding == StlEncoding::kBinary) {
      if (facets > std::numeric_limits<std::uint32_t>::max())
        Fail(Status::kUnsupportedVariant, stage, Code::kFacetLimit);
      const auto size = Add(kPrefixBytes, Product(facets, kFacetBytes));
      if (size > limits.max_output_bytes)
        Fail(Status::kLimitExceeded, stage, Code::kOutputLimit);
      state->bytes.reserve(Host(size));
      for (std::size_t index = 0; index < size; ++index) {
        session.Byte(stage, index);
        state->bytes.push_back(std::byte{0});
      }
      for (std::size_t index = 0; index < kHeaderBytes; ++index) {
        session.Byte(stage, index);
        state->bytes[index] = options.binary_header[index];
      }
      PutLittle(state->bytes, kHeaderBytes, static_cast<std::uint32_t>(facets),
                kCountBytes, session);
    }
    Writer writer{*state, limits, session};
    if (options.encoding == StlEncoding::kAscii) {
      writer.Token(kSolid);
      writer.Append(" ");
      writer.Append(options.ascii_name);
      writer.Append("\n");
    }
    for (std::size_t facet = 0; facet < facets; ++facet) {
      session.Facet(stage, facet);
      std::array<double, kFacetScalars> values{};
      for (std::size_t component = 0; component < kComponents; ++component)
        values[component] = normal_value(facet * kComponents + component);
      for (std::size_t corner = 0; corner < kComponents; ++corner) {
        const auto vertex =
            Host(view.corner_vertices[Host(view.face_offsets[facet]) + corner]);
        for (std::size_t component = 0; component < kComponents; ++component)
          values[kComponents + corner * kComponents + component] =
              view.positions[vertex * kComponents + component];
      }
      if (options.encoding == StlEncoding::kBinary) {
        const auto begin = kPrefixBytes + facet * kFacetBytes;
        for (std::size_t component = 0; component < values.size(); ++component)
          PutLittle(state->bytes, begin + component * kFloatBytes,
                    Narrow(values[component], facet), kFloatBytes, session);
      } else {
        writer.Append("  ");
        writer.Token(kFacet);
        writer.Append(" ");
        writer.Token(kNormal);
        for (std::size_t component = 0; component < kComponents; ++component) {
          writer.Append(" ");
          writer.Number(values[component]);
        }
        writer.Append("\n    ");
        writer.Token(kOuter);
        writer.Append(" ");
        writer.Token(kLoop);
        writer.Append("\n");
        for (std::size_t corner = 0; corner < kComponents; ++corner) {
          writer.Append("      ");
          writer.Token(kVertex);
          for (std::size_t component = 0; component < kComponents;
               ++component) {
            writer.Append(" ");
            writer.Number(
                values[kComponents + corner * kComponents + component]);
          }
          writer.Append("\n");
        }
        writer.Append("    ");
        writer.Token(kEndLoop);
        writer.Append("\n  ");
        writer.Token(kEndFacet);
        writer.Append("\n");
      }
    }
    if (options.encoding == StlEncoding::kAscii) {
      writer.Token(kEndSolid);
      writer.Append(" ");
      writer.Append(options.ascii_name);
      writer.Append("\n");
    }
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

StlDocument::StlDocument(
    std::shared_ptr<const detail::DocumentState> state) noexcept
    : state_(std::move(state)) {}
StlDocument::StlDocument(const StlDocument&) noexcept = default;
StlDocument& StlDocument::operator=(const StlDocument&) noexcept = default;
StlDocument::~StlDocument() noexcept = default;
StlEncoding StlDocument::Encoding() const noexcept { return state_->encoding; }
std::span<const std::byte> StlDocument::OriginalBytes() const noexcept {
  return state_->source;
}
std::span<const std::byte> StlDocument::BinaryHeader() const noexcept {
  return state_->encoding == StlEncoding::kBinary
             ? std::span<const std::byte>(state_->source).first(kHeaderBytes)
             : std::span<const std::byte>{};
}
std::string_view StlDocument::AsciiName() const noexcept {
  return state_->encoding == StlEncoding::kAscii
             ? Text(state_->source)
                   .substr(state_->name_offset, state_->name_size)
             : std::string_view{};
}
const geo::MaterializedMesh& StlDocument::Mesh() const noexcept {
  return *state_->mesh;
}
const geo::MeshInspection& StlDocument::Inspection() const noexcept {
  return *state_->inspection;
}
StlRequestedBytes StlDocument::NewRequestedBytes() const noexcept {
  StlRequestedBytes value{state_->allocation.OperationActiveBytes(),
                          state_->mesh->NewRequestedBytes(),
                          state_->inspection->NewRequestedBytes(), 0};
  value.total = value.codec + value.materialization + value.inspection;
  return value;
}
StlOutput::StlOutput(std::shared_ptr<const detail::OutputState> state) noexcept
    : state_(std::move(state)) {}
StlOutput::StlOutput(const StlOutput&) noexcept = default;
StlOutput& StlOutput::operator=(const StlOutput&) noexcept = default;
StlOutput::~StlOutput() noexcept = default;
std::span<const std::byte> StlOutput::Bytes() const noexcept {
  return state_->bytes;
}
const StlWriteFacts& StlOutput::Facts() const noexcept { return state_->facts; }
std::span<const geo::index_t> StlOutput::CornerSourceVertices() const noexcept {
  return state_->corners;
}
StlRequestedBytes StlOutput::NewRequestedBytes() const noexcept {
  const auto bytes = state_->allocation.OperationActiveBytes();
  return {bytes, 0, 0, bytes};
}
StlReadResult ReadStl(std::span<const std::byte> bytes, StlEncoding encoding,
                      const StlReadLimits& limits, const StlWorkPolicy& work,
                      const geo::ExecutionContext& context,
                      std::stop_token stop) {
  return Read(bytes, encoding, limits, work, context, stop, {});
}
StlWriteResult WriteStl(const geo::MaterializedMesh& mesh,
                        const StlWriteOptions& options,
                        const StlWriteLimits& limits, const StlWorkPolicy& work,
                        std::stop_token stop) {
  return Write(mesh, options, limits, work, stop, {});
}
namespace stl_detail {
StlReadResult ReadStlForTest(std::span<const std::byte> bytes,
                             StlEncoding encoding, const StlReadLimits& limits,
                             const StlWorkPolicy& work,
                             const geo::ExecutionContext& context,
                             std::stop_token stop, const TestOptions& options) {
  return Read(bytes, encoding, limits, work, context, stop, options);
}
StlWriteResult WriteStlForTest(const geo::MaterializedMesh& mesh,
                               const StlWriteOptions& options,
                               const StlWriteLimits& limits,
                               const StlWorkPolicy& work, std::stop_token stop,
                               const TestOptions& test) {
  return Write(mesh, options, limits, work, stop, test);
}
}  // namespace stl_detail
static_assert(std::is_nothrow_copy_constructible_v<StlDocument>);
static_assert(std::is_nothrow_copy_assignable_v<StlDocument>);
static_assert(std::is_nothrow_destructible_v<StlDocument>);
static_assert(std::is_nothrow_copy_constructible_v<StlOutput>);
static_assert(std::is_nothrow_copy_assignable_v<StlOutput>);
static_assert(std::is_nothrow_destructible_v<StlOutput>);
}  // namespace meshvale::interchange
