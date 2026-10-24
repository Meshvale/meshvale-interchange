// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/off.h"

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
#include <vector>

#include "finite_double_format.h"
#include "gltf_allocation.h"
#include "off_test_internal.h"

namespace meshvale::interchange::off_detail {
namespace allocation = gltf_detail;
template <class T>
using Vector = std::vector<T, allocation::Allocator<T>>;
struct DocumentState {
  explicit DocumentState(const allocation::AllocationContext& context)
      : allocation(context),
        source(allocation::Allocator<std::byte>(context)) {}
  allocation::AllocationContext allocation;
  Vector<std::byte> source;
  std::uint64_t declared_edges = 0;
  std::optional<geometry::MaterializedMesh> mesh;
  std::optional<geometry::MeshInspection> inspection;
};
struct OutputState {
  explicit OutputState(const allocation::AllocationContext& context)
      : allocation(context), bytes(allocation::Allocator<std::byte>(context)) {}
  allocation::AllocationContext allocation;
  Vector<std::byte> bytes;
  OffWriteFacts facts;
};
struct Access {
  static OffDocument Document(
      std::shared_ptr<const DocumentState> state) noexcept {
    return OffDocument(std::move(state));
  }
  static OffOutput Output(std::shared_ptr<const OutputState> state) noexcept {
    return OffOutput(std::move(state));
  }
};
}  // namespace meshvale::interchange::off_detail

namespace meshvale::interchange {
namespace {
namespace detail = off_detail;
namespace geo = geometry;
namespace allocation = gltf_detail;
using Status = OffStatus;
using Stage = OffStage;
using Code = OffDiagnosticCode;
constexpr std::size_t kComponents = 3;
constexpr std::size_t kMinimumCorners = 3;
constexpr unsigned char kPrintableFirst = 0x20;
constexpr unsigned char kPrintableLast = 0x7e;
constexpr std::uint64_t kDoubleExponentMask = UINT64_C(0x7ff0000000000000);
// Finite binary64 general/max_digits10 and uint64 decimal fit this buffer.
constexpr std::size_t kNumberBytes = 128;
constexpr std::string_view kMarker = "OFF";
constexpr std::string_view kBinary = "BINARY";

struct Failure {
  Status status;
  OffDiagnostic diagnostic;
};
[[noreturn]] void Fail(Status status, Stage stage, Code code,
                       std::optional<std::uint64_t> byte = {},
                       std::optional<std::uint64_t> vertex = {},
                       std::optional<std::uint64_t> face = {}) {
  OffDiagnostic diagnostic;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.byte_offset = byte;
  diagnostic.vertex = vertex;
  diagnostic.face = face;
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
void Validate(const OffWorkPolicy& work) {
  for (auto value :
       {work.cancellation_block_bytes, work.cancellation_block_tokens,
        work.cancellation_block_records, work.geometry.parallel_min_bytes,
        work.geometry.partition_bytes, work.geometry.cancellation_block_scalars,
        work.geometry.max_jobs})
    Positive(value);
}
void Validate(const OffReadLimits& limits, const OffWorkPolicy& work) {
  for (auto value :
       {limits.max_source_bytes, limits.max_vertices, limits.max_faces,
        limits.max_corners, limits.max_corners_per_face, limits.max_line_bytes,
        limits.max_token_bytes, limits.max_comment_bytes,
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
void Validate(const OffWriteLimits& limits, const OffWorkPolicy& work,
              const OffWriteOptions& options) {
  for (auto value :
       {limits.max_output_bytes, limits.max_vertices, limits.max_faces,
        limits.max_corners, limits.max_corners_per_face, limits.max_line_bytes,
        limits.max_token_bytes, limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.inspection);
  Validate(work);
  if (Add(limits.max_codec_new_requested_bytes,
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  if (options.text_policy != OffTextPolicy::kCanonicalAscii)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kTextPolicy);
  if (options.edge_count_policy != OffEdgeCountPolicy::kUnknownZero)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kEdgeCountPolicy);
}
struct Session {
  const OffWorkPolicy& work;
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
  void Record(Stage stage, std::size_t index) const {
    if (index % work.cancellation_block_records == 0) Check(stage, index);
  }
};
bool Space(char value) { return value == ' ' || value == '\t'; }
bool Finite(double value) noexcept {
  return (std::bit_cast<std::uint64_t>(value) & kDoubleExponentMask) !=
         kDoubleExponentMask;
}
std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
struct Token {
  std::string_view text;
  std::size_t offset = 0;
};
// A physical line is scanned once before token interpretation. This preserves
// the significant end-of-record boundary needed to refuse unmarked colors.
struct Record {
  std::string_view text;
  std::size_t offset;
  std::size_t cursor = 0;
  std::optional<Token> Next(const OffReadLimits& limits, const Session& session,
                            Stage stage, std::size_t& tokens) {
    while (cursor < text.size() && Space(text[cursor])) {
      session.Byte(stage, offset + cursor);
      ++cursor;
    }
    if (cursor == text.size()) return {};
    const auto begin = cursor;
    if (tokens++ % session.work.cancellation_block_tokens == 0)
      session.Check(stage, tokens);
    while (cursor < text.size() && !Space(text[cursor])) {
      session.Byte(stage, offset + cursor);
      if (cursor - begin >= limits.max_token_bytes)
        Fail(Status::kLimitExceeded, stage, Code::kTokenLimit, offset + cursor);
      ++cursor;
    }
    return Token{text.substr(begin, cursor - begin), offset + begin};
  }
};
std::uint64_t Unsigned(Token token, const Session& session, Stage stage) {
  std::uint64_t value = 0;
  constexpr std::uint64_t kDecimalBase = 10;
  for (std::size_t index = 0; index < token.text.size(); ++index) {
    session.Byte(stage, token.offset + index);
    const auto digit = token.text[index];
    if (digit < '0' || digit > '9')
      Fail(Status::kMalformed, stage, Code::kIntegerSyntax,
           token.offset + index);
    const auto number = static_cast<std::uint64_t>(digit - '0');
    if (value >
        (std::numeric_limits<std::uint64_t>::max() - number) / kDecimalBase)
      Fail(Status::kMalformed, stage, Code::kIntegerRange, token.offset);
    value = value * kDecimalBase + number;
  }
  return value;
}
double Decimal(Token token, const Session& session, Stage stage) {
  auto text = token.text;
  std::size_t cursor = 0;
  if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-'))
    ++cursor;
  auto digits = [&] {
    const auto start = cursor;
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
      session.Byte(stage, token.offset + cursor);
      ++cursor;
    }
    return cursor - start;
  };
  auto count = digits();
  if (cursor < text.size() && text[cursor] == '.') {
    ++cursor;
    count += digits();
  }
  if (!count)
    Fail(Status::kMalformed, stage, Code::kNumericSyntax, token.offset);
  if (cursor < text.size() && (text[cursor] == 'e' || text[cursor] == 'E')) {
    ++cursor;
    if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-'))
      ++cursor;
    if (!digits())
      Fail(Status::kMalformed, stage, Code::kNumericSyntax, token.offset);
  }
  if (cursor != text.size())
    Fail(Status::kMalformed, stage, Code::kNumericSyntax, token.offset);
  if (text.front() == '+') text.remove_prefix(1);
  double value = 0;
  // One already-admitted token bounds this indivisible standard conversion.
  session.Check(stage, token.offset);
  const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                      value, std::chars_format::general);
  session.Check(stage, token.offset);
  if (result.ec == std::errc::result_out_of_range)
    Fail(Status::kMalformed, stage, Code::kNumericRange, token.offset);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    Fail(Status::kMalformed, stage, Code::kNumericSyntax, token.offset);
  if (!Finite(value))
    Fail(Status::kMalformed, stage, Code::kNonfinite, token.offset);
  return value;
}
struct Counts {
  std::size_t vertices = 0;
  std::size_t faces = 0;
  std::size_t corners = 0;
  std::uint64_t edges = 0;
};
struct Decoded {
  explicit Decoded(const allocation::AllocationContext& context)
      : positions(allocation::Allocator<double>(context)),
        offsets(allocation::Allocator<geo::index_t>(context)),
        corners(allocation::Allocator<geo::index_t>(context)) {}
  detail::Vector<double> positions;
  detail::Vector<geo::index_t> offsets;
  detail::Vector<geo::index_t> corners;
  void Reserve(const Counts& counts) {
    positions.reserve(Product(counts.vertices, kComponents, Stage::kDecode,
                              Code::kVertexLimit));
    offsets.reserve(counts.faces + 1);
    corners.reserve(counts.corners);
    offsets.push_back(0);
  }
};
struct Parser {
  std::string_view text;
  const OffReadLimits& limits;
  const Session& session;
  Stage stage;
  std::size_t cursor = 0;
  std::size_t records = 0;
  std::size_t tokens = 0;
  std::optional<Record> Next() {
    while (cursor < text.size()) {
      const auto begin = cursor;
      auto data_end = begin;
      bool comment = false;
      std::uint64_t comment_bytes = 0;
      while (cursor < text.size() && text[cursor] != '\r' &&
             text[cursor] != '\n') {
        session.Byte(stage, cursor);
        if (cursor - begin >= limits.max_line_bytes)
          Fail(Status::kLimitExceeded, stage, Code::kLineLimit, cursor);
        const auto value = static_cast<unsigned char>(text[cursor]);
        if ((value < kPrintableFirst || value > kPrintableLast) &&
            value != '\t')
          Fail(Status::kMalformed, stage, Code::kSyntax, cursor);
        if (comment) {
          if (++comment_bytes > limits.max_comment_bytes)
            Fail(Status::kLimitExceeded, stage, Code::kCommentLimit, cursor);
        } else if (value == '#') {
          data_end = cursor;
          comment = true;
        }
        ++cursor;
      }
      if (!comment) data_end = cursor;
      if (cursor < text.size()) {
        session.Byte(stage, cursor);
        const auto delimiter = text[cursor++];
        if (delimiter == '\r' && cursor < text.size() && text[cursor] == '\n') {
          session.Byte(stage, cursor);
          ++cursor;
        }
      }
      // Determine whether a line has data without copying or unbounded trim.
      auto first = begin;
      while (first < data_end && Space(text[first])) {
        session.Byte(stage, first);
        ++first;
      }
      if (first != data_end) {
        session.Record(stage, records++);
        return Record{text.substr(begin, data_end - begin), begin};
      }
    }
    return {};
  }
  Record Required() {
    auto record = Next();
    if (!record) Fail(Status::kMalformed, stage, Code::kSyntax, cursor);
    return *record;
  }
  Token Field(Record& record) {
    auto token = record.Next(limits, session, stage, tokens);
    if (!token) Fail(Status::kMalformed, stage, Code::kSyntax, record.offset);
    return *token;
  }
  void End(Record& record, bool payload) {
    if (const auto extra = record.Next(limits, session, stage, tokens))
      Fail(payload ? Status::kUnsupportedVariant : Status::kMalformed, stage,
           payload ? Code::kUnsupportedPayload : Code::kSyntax, extra->offset);
  }
  Counts Parse(Decoded* decoded) {
    auto header = Required();
    const auto marker = Field(header);
    if (marker.text != kMarker) {
      if (marker.text.ends_with(kMarker) || marker.text == kBinary)
        Fail(Status::kUnsupportedVariant, stage, Code::kVariant, marker.offset);
      Fail(Status::kMalformed, stage, Code::kSyntax, marker.offset);
    }
    if (const auto extra = header.Next(limits, session, stage, tokens)) {
      if (extra->text == kBinary)
        Fail(Status::kUnsupportedVariant, stage, Code::kVariant, extra->offset);
      Fail(Status::kMalformed, stage, Code::kSyntax, extra->offset);
    }
    auto count_line = Required();
    const auto vertices = Unsigned(Field(count_line), session, stage);
    const auto faces = Unsigned(Field(count_line), session, stage);
    const auto edges = Unsigned(Field(count_line), session, stage);
    End(count_line, false);
    if (vertices > limits.max_vertices ||
        vertices > limits.materialization.max_vertices)
      Fail(Status::kLimitExceeded, stage, Code::kVertexLimit,
           count_line.offset);
    if (faces > limits.max_faces || faces > limits.materialization.max_faces ||
        faces >= limits.materialization.max_face_offsets)
      Fail(Status::kLimitExceeded, stage, Code::kFaceLimit, count_line.offset);
    Counts counts{Host(vertices), Host(faces), 0, edges};
    const auto scalars =
        Product(counts.vertices, kComponents, stage, Code::kVertexLimit);
    (void)Product(scalars, sizeof(double), stage, Code::kVertexLimit);
    (void)Product(counts.faces + 1, sizeof(geo::index_t), stage,
                  Code::kFaceLimit);
    for (std::size_t vertex = 0; vertex < counts.vertices; ++vertex) {
      session.Record(stage, vertex);
      auto record = Required();
      for (std::size_t component = 0; component < kComponents; ++component) {
        const auto value = Decimal(Field(record), session, stage);
        if (decoded) decoded->positions.push_back(value);
      }
      End(record, true);
    }
    for (std::size_t face = 0; face < counts.faces; ++face) {
      session.Record(stage, face);
      auto record = Required();
      const auto number = Field(record);
      const auto corners = Unsigned(number, session, stage);
      if (corners < kMinimumCorners)
        Fail(Status::kUnsupportedVariant, stage, Code::kFaceTooShort,
             number.offset, {}, face);
      if (corners > limits.max_corners_per_face)
        Fail(Status::kLimitExceeded, stage, Code::kPerFaceCornerLimit,
             number.offset, {}, face);
      if (corners > limits.max_corners - counts.corners ||
          corners > limits.materialization.max_corners - counts.corners)
        Fail(Status::kLimitExceeded, stage, Code::kCornerLimit, number.offset,
             {}, face);
      counts.corners += Host(corners);
      (void)Product(counts.corners, sizeof(geo::index_t), stage,
                    Code::kCornerLimit);
      for (std::size_t corner = 0; corner < corners; ++corner) {
        const auto field = Field(record);
        const auto index = Unsigned(field, session, stage);
        if (index >= vertices)
          Fail(Status::kMalformed, stage, Code::kVertexRange, field.offset,
               index, face);
        if (decoded) decoded->corners.push_back(index);
      }
      End(record, true);
      if (decoded) decoded->offsets.push_back(counts.corners);
    }
    if (const auto trailing = Next())
      Fail(Status::kUnsupportedVariant, stage, Code::kTrailingData,
           trailing->offset);
    return counts;
  }
};

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
  OffDiagnostic value;
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
    OffDiagnostic value;
    value.stage = Stage::kInspection;
    value.code = Code::kStorageIssues;
    value.geometry_status = result.status;
    value.geometry_diagnostic = result.diagnostic;
    if (!result.inspection->Issues().empty())
      value.storage_issue = result.inspection->Issues().front();
    throw Failure{Status::kInvalidStorage, value};
  }
}
void Admission(OffDiagnostic& diagnostic, Status& status, Stage stage,
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
OffReadResult Read(std::span<const std::byte> bytes,
                   const OffReadLimits& limits, const OffWorkPolicy& work,
                   const geo::ExecutionContext& context, std::stop_token stop,
                   const detail::TestOptions& options) {
  OffReadResult result;
  Stage stage = Stage::kConfiguration;
  try {
    Validate(limits, work);
    Session session{work, stop, options};
    session.Check(stage);
    if (bytes.size() > limits.max_source_bytes)
      Fail(Status::kLimitExceeded, Stage::kScan, Code::kSourceLimit);
    stage = Stage::kScan;
    Parser parser{Text(bytes), limits, session, stage};
    const auto counts = parser.Parse(nullptr);
    stage = Stage::kDecode;
    auto allocation =
        allocation::AllocationContext(context, stop, options.allocation_backend)
            .WithRequestLimit(Host(limits.max_codec_new_requested_bytes));
    auto state = std::allocate_shared<detail::DocumentState>(
        allocation::Allocator<detail::DocumentState>(allocation), allocation);
    state->declared_edges = counts.edges;
    state->source.reserve(bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      session.Byte(stage, index);
      state->source.push_back(bytes[index]);
    }
    {
      Decoded decoded(allocation);
      decoded.Reserve(counts);
      Parser owned{Text(state->source), limits, session, stage};
      (void)owned.Parse(&decoded);
      geo::MeshMaterializationInput input;
      input.positions = std::span<const double>(decoded.positions);
      input.topology =
          geo::PolygonTopologyInput{decoded.offsets, decoded.corners};
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
  const OffWriteLimits& limits;
  const Session& session;
  std::uint64_t line = 0;
  std::uint64_t tokens = 0;
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
      if (value == '\n')
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
    const auto result = numeric_format_detail::FiniteDoubleToChars(
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
    if (result.ec != std::errc{})
      Fail(Status::kInvalidStorage, Stage::kEncode, Code::kNumericRange);
    Token({bytes.data(), static_cast<std::size_t>(result.ptr - bytes.data())});
  }
  void Integer(std::uint64_t value) {
    std::array<char, kNumberBytes> bytes;
    session.Check(Stage::kEncode);
    const auto result =
        std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
    session.Check(Stage::kEncode);
    if (result.ec != std::errc{})
      Fail(Status::kInvalidStorage, Stage::kEncode, Code::kIntegerRange);
    Token({bytes.data(), static_cast<std::size_t>(result.ptr - bytes.data())});
  }
};
OffWriteResult Write(const geo::MaterializedMesh& mesh,
                     const OffWriteOptions& options,
                     const OffWriteLimits& limits, const OffWorkPolicy& work,
                     std::stop_token stop, const detail::TestOptions& test) {
  OffWriteResult result;
  Stage stage = Stage::kConfiguration;
  try {
    Validate(limits, work, options);
    Session session{work, stop, test};
    session.Check(stage);
    const auto view = mesh.View();
    const auto vertices = view.positions.size() / kComponents;
    const auto faces =
        view.face_offsets.empty() ? 0 : view.face_offsets.size() - 1;
    if (vertices > limits.max_vertices)
      Fail(Status::kLimitExceeded, Stage::kValidation, Code::kVertexLimit);
    if (faces > limits.max_faces)
      Fail(Status::kLimitExceeded, Stage::kValidation, Code::kFaceLimit);
    if (view.corner_vertices.size() > limits.max_corners)
      Fail(Status::kLimitExceeded, Stage::kValidation, Code::kCornerLimit);
    (void)Product(vertices, kComponents, Stage::kValidation,
                  Code::kVertexLimit);
    stage = Stage::kInspection;
    session.Check(stage);
    auto inspected = geo::InspectMaterializedMesh(mesh, limits.inspection,
                                                  work.geometry, stop);
    result.execution.inspection = inspected.execution;
    InspectionFailure(inspected);
    stage = Stage::kValidation;
    session.Check(stage);
    if (!view.attributes.empty())
      Fail(Status::kUnsupportedVariant, stage, Code::kUnsupportedAttribute);
    for (std::size_t face = 0; face < faces; ++face) {
      session.Record(stage, face);
      const auto width = view.face_offsets[face + 1] - view.face_offsets[face];
      if (width < kMinimumCorners)
        Fail(Status::kInvalidStorage, stage, Code::kFaceTooShort, {}, {}, face);
      if (width > limits.max_corners_per_face)
        Fail(Status::kLimitExceeded, stage, Code::kPerFaceCornerLimit, {}, {},
             face);
    }
    const auto context = mesh.ShareExecutionContext();
    auto allocation =
        allocation::AllocationContext(context, stop, test.allocation_backend)
            .WithRequestLimit(Host(limits.max_codec_new_requested_bytes));
    auto state = std::allocate_shared<detail::OutputState>(
        allocation::Allocator<detail::OutputState>(allocation), allocation);
    auto& facts = state->facts;
    facts.profile = OffProfile::kAsciiRecords;
    facts.vertex_count = vertices;
    facts.face_count = faces;
    facts.corner_count = view.corner_vertices.size();
    facts.text_policy = options.text_policy;
    facts.edge_count_policy = options.edge_count_policy;
    facts.correspondence = OffCorrespondence::kIdentity;
    if (options.source_document)
      facts.source_declared_edge_count =
          options.source_document->DeclaredEdgeCount();
    stage = Stage::kEncode;
    Writer writer{*state, limits, session};
    writer.Token(kMarker);
    writer.Append("\n");
    writer.Integer(vertices);
    writer.Append(" ");
    writer.Integer(faces);
    writer.Append(" ");
    writer.Integer(facts.emitted_edge_count);
    writer.Append("\n");
    for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
      session.Record(stage, vertex);
      for (std::size_t component = 0; component < kComponents; ++component) {
        if (component) writer.Append(" ");
        writer.Number(view.positions[vertex * kComponents + component]);
      }
      writer.Append("\n");
    }
    for (std::size_t face = 0; face < faces; ++face) {
      session.Record(stage, face);
      const auto begin = Host(view.face_offsets[face]);
      const auto end = Host(view.face_offsets[face + 1]);
      writer.Integer(end - begin);
      for (auto corner = begin; corner < end; ++corner) {
        writer.Append(" ");
        writer.Integer(view.corner_vertices[corner]);
      }
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

OffDocument::OffDocument(
    std::shared_ptr<const detail::DocumentState> state) noexcept
    : state_(std::move(state)) {}
OffDocument::OffDocument(const OffDocument&) noexcept = default;
OffDocument& OffDocument::operator=(const OffDocument&) noexcept = default;
OffDocument::~OffDocument() noexcept = default;
OffProfile OffDocument::Profile() const noexcept {
  return OffProfile::kAsciiRecords;
}
std::span<const std::byte> OffDocument::OriginalBytes() const noexcept {
  return state_->source;
}
std::uint64_t OffDocument::DeclaredEdgeCount() const noexcept {
  return state_->declared_edges;
}
const geo::MaterializedMesh& OffDocument::Mesh() const noexcept {
  return *state_->mesh;
}
const geo::MeshInspection& OffDocument::Inspection() const noexcept {
  return *state_->inspection;
}
OffRequestedBytes OffDocument::NewRequestedBytes() const noexcept {
  OffRequestedBytes value{state_->allocation.OperationActiveBytes(),
                          state_->mesh->NewRequestedBytes(),
                          state_->inspection->NewRequestedBytes(), 0};
  value.total = value.codec + value.materialization + value.inspection;
  return value;
}
OffOutput::OffOutput(std::shared_ptr<const detail::OutputState> state) noexcept
    : state_(std::move(state)) {}
OffOutput::OffOutput(const OffOutput&) noexcept = default;
OffOutput& OffOutput::operator=(const OffOutput&) noexcept = default;
OffOutput::~OffOutput() noexcept = default;
std::span<const std::byte> OffOutput::Bytes() const noexcept {
  return state_->bytes;
}
const OffWriteFacts& OffOutput::Facts() const noexcept { return state_->facts; }
OffRequestedBytes OffOutput::NewRequestedBytes() const noexcept {
  const auto bytes = state_->allocation.OperationActiveBytes();
  return {bytes, 0, 0, bytes};
}
OffReadResult ReadOff(std::span<const std::byte> bytes,
                      const OffReadLimits& limits, const OffWorkPolicy& work,
                      const geo::ExecutionContext& context,
                      std::stop_token stop) {
  return Read(bytes, limits, work, context, stop, {});
}
OffWriteResult WriteOff(const geo::MaterializedMesh& mesh,
                        const OffWriteOptions& options,
                        const OffWriteLimits& limits, const OffWorkPolicy& work,
                        std::stop_token stop) {
  return Write(mesh, options, limits, work, stop, {});
}
namespace off_detail {
OffReadResult ReadOffForTest(std::span<const std::byte> bytes,
                             const OffReadLimits& limits,
                             const OffWorkPolicy& work,
                             const geo::ExecutionContext& context,
                             std::stop_token stop, const TestOptions& options) {
  return Read(bytes, limits, work, context, stop, options);
}
OffWriteResult WriteOffForTest(const geo::MaterializedMesh& mesh,
                               const OffWriteOptions& options,
                               const OffWriteLimits& limits,
                               const OffWorkPolicy& work, std::stop_token stop,
                               const TestOptions& test) {
  return Write(mesh, options, limits, work, stop, test);
}
}  // namespace off_detail
static_assert(std::is_nothrow_copy_constructible_v<OffDocument>);
static_assert(std::is_nothrow_copy_assignable_v<OffDocument>);
static_assert(std::is_nothrow_destructible_v<OffDocument>);
static_assert(std::is_nothrow_copy_constructible_v<OffOutput>);
static_assert(std::is_nothrow_copy_assignable_v<OffOutput>);
static_assert(std::is_nothrow_destructible_v<OffOutput>);
}  // namespace meshvale::interchange
