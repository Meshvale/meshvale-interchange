// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "gltf_workflow_test_internal.h"

namespace {
namespace mv = meshvale::interchange;
namespace geom = meshvale::geometry;
namespace detail = mv::gltf_detail;
constexpr std::uint64_t kGenerousRequestedBytes = 64 * 1024 * 1024;
constexpr std::uint64_t kDocumentByteLimit = 1024 * 1024;
constexpr std::uint64_t kResourceByteLimit = 1024 * 1024;
constexpr std::uint64_t kDocumentNodeLimit = 65536;
constexpr std::uint64_t kDocumentDepthLimit = 64;
constexpr std::uint64_t kNumericTokenByteLimit = 128;
constexpr std::uint64_t kDecimalCoefficientDigitLimit = 128;
constexpr std::uint64_t kDecimalExponentDigitLimit = 32;
constexpr std::uint64_t kTableRowLimit = 65536;
constexpr std::uint64_t kGraphEdgeLimit = 1024;
constexpr std::uint64_t kInstanceLimit = 1024;
constexpr std::uint64_t kAccessorElementLimit = 65536;
constexpr std::uint64_t kComponentLimit = 1024 * 1024;
constexpr std::uint64_t kTriangleCornerLimit = 65536;
constexpr std::size_t kParallelWorkerBudget = 4;
constexpr std::size_t kParallelFixtureVertices = 12288;
constexpr std::size_t kShortVectorComponents = 2;
constexpr std::size_t kBytesPerShortVector = 4;
constexpr std::size_t kResourceTailBytes = 4;
constexpr std::size_t kPositionComponents = 3;
constexpr std::uint32_t kLargeSourceIndex = 16777217;
constexpr std::uint64_t kLargeIndexCount = 3;
constexpr std::uint64_t kLargeVertexCount =
    std::uint64_t{kLargeSourceIndex} + 1;
constexpr std::uint64_t kBytesPerMebibyte = 1024 * 1024;
constexpr std::string_view kLargeCaseOption = "--large-source-index";
constexpr std::string_view kResourceBudgetOption = "--resource-budget-mib";
constexpr std::string_view kParallelShortSemantic = "_SHORT";
constexpr std::size_t kCallerOnlyWorkerBudget = 1;
constexpr std::size_t kMinimumParallelVertices = 1;
constexpr std::uint64_t kParallelResourceThresholdBytes = 1024;
constexpr std::uint64_t kResourcePartitionBytes = 4096;
constexpr std::uint64_t kParallelChannelThresholdComponents = 1024;
constexpr std::uint64_t kChannelPartitionElements = 512;
constexpr std::size_t kHexDigitsPerByte = 2;
constexpr std::size_t kBitsPerHexDigit = 4;
constexpr unsigned kAlphabeticHexDigitValue = 10;
constexpr std::uint64_t kTriangleMode = 4;
constexpr std::uint64_t kStripMode = 5;
constexpr std::uint64_t kFanMode = 6;
constexpr std::string_view kModePlaceholder = "@MODE@";
constexpr std::string_view kIndexCountPlaceholder = "@COUNT@";
constexpr std::string_view kPositionSemantic = "POSITION";
constexpr std::string_view kSignedByteSemantic = "_I8";
constexpr std::string_view kUnsignedByteSemantic = "_U8";
constexpr std::string_view kSignedShortSemantic = "_I16";
constexpr std::string_view kUnsignedShortSemantic = "_U16";
constexpr std::string_view kSparseSemantic = "_SPARSE";

void Require(bool condition, std::string_view detail,
             std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()) + ": " +
                             std::string(detail));
}

void RequireExtraction(
    const mv::GltfExtractionResult& result, std::string_view detail,
    std::source_location location = std::source_location::current()) {
  if (!result.extraction)
    Require(false,
            std::string(detail) + ": actual status=" +
                std::to_string(static_cast<int>(result.diagnostic.status)) +
                " code=" +
                std::to_string(static_cast<int>(result.diagnostic.code)) +
                " rule=" + std::to_string(result.diagnostic.rule_id),
            location);
}

mv::GltfReadLimits SourceLimits() {
  mv::GltfReadLimits limits;
  limits.max_json_bytes = kDocumentByteLimit;
  limits.max_resource_bytes = kResourceByteLimit;
  limits.max_owned_source_resource_bytes = kResourceByteLimit;
  limits.max_document_nodes = kDocumentNodeLimit;
  limits.max_document_depth = kDocumentDepthLimit;
  limits.max_decoded_string_bytes = kDocumentByteLimit;
  limits.max_numeric_token_bytes = kNumericTokenByteLimit;
  limits.max_decimal_coefficient_digits = kDecimalCoefficientDigitLimit;
  limits.max_decimal_exponent_digits = kDecimalExponentDigitLimit;
  limits.max_table_rows = kTableRowLimit;
  limits.max_graph_edges = kGraphEdgeLimit;
  limits.max_instances = kInstanceLimit;
  limits.max_accessor_elements = kAccessorElementLimit;
  limits.max_projection_bytes = kGenerousRequestedBytes;
  return limits;
}
mv::GltfReadOperationLimits ReadLimits(
    std::uint64_t cap = kGenerousRequestedBytes) {
  return {SourceLimits(), cap};
}
mv::GltfExtractionLimits ExtractLimits(
    std::uint64_t cap = kGenerousRequestedBytes) {
  return {kComponentLimit, kTriangleCornerLimit, cap};
}
mv::GltfWorkPolicy WorkPolicy() {
  return {kParallelResourceThresholdBytes, kResourcePartitionBytes,
          kParallelChannelThresholdComponents, kChannelPartitionElements};
}
geom::ExecutionContext Context(std::size_t workers = kCallerOnlyWorkerBudget) {
  return geom::ExecutionContext(
      {workers, kMinimumParallelVertices, kGenerousRequestedBytes});
}
std::span<const std::byte> JsonBytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}
unsigned HexDigit(char value) {
  if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
  Require(value >= 'a' && value <= 'f', "invalid independent hex fixture");
  return static_cast<unsigned>(value - 'a') + kAlphabeticHexDigitValue;
}
std::vector<std::byte> DecodeHex(std::string_view text) {
  Require(text.size() % kHexDigitsPerByte == 0, "hex fixture length");
  std::vector<std::byte> result(text.size() / kHexDigitsPerByte);
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = static_cast<std::byte>(
        (HexDigit(text[i * kHexDigitsPerByte]) << kBitsPerHexDigit) |
        HexDigit(text[i * kHexDigitsPerByte + 1]));
  return result;
}
void Replace(std::string& text, std::string_view marker,
             std::string_view replacement) {
  const auto where = text.find(marker);
  Require(where != std::string::npos, "fixture marker");
  text.replace(where, marker.size(), replacement);
}

// These authored wire spellings are independent protocol expectations. The
// supplied buffer retains four trailing bytes beyond its logical byteLength.
constexpr std::string_view kFixtureTemplate =
    R"({"asset":{"version":"2.0","extras":{"opaque":1.2300e+02}},"buffers":[{"uri":"fixture.bin","byteLength":136}],"bufferViews":[{"buffer":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":64,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":80,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":96,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":112,"byteLength":16},{"buffer":0,"byteOffset":128,"byteLength":2},{"buffer":0,"byteOffset":132,"byteLength":4}],"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},{"bufferView":1,"componentType":5120,"count":4,"type":"SCALAR","normalized":true},{"bufferView":2,"componentType":5121,"count":4,"type":"SCALAR","normalized":true},{"bufferView":3,"componentType":5122,"count":4,"type":"SCALAR","normalized":true},{"bufferView":4,"componentType":5123,"count":4,"type":"SCALAR","normalized":true},{"componentType":5123,"count":4,"type":"SCALAR","sparse":{"count":2,"indices":{"bufferView":6,"componentType":5121},"values":{"bufferView":7}}},{"bufferView":5,"componentType":5125,"count":@COUNT@,"type":"SCALAR"}],"meshes":[{"primitives":[{"mode":@MODE@,"indices":6,"attributes":{"POSITION":0,"_I8":1,"_U8":2,"_I16":3,"_U16":4,"_SPARSE":5}}]}],"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],"scene":0})";
constexpr std::string_view kFixtureBufferHex =
    "0000008000000000000000000000803f0000000000000000000000000000803f0000000000"
    "000000000000000000803f80aabbcc7faabbccffaabbcc00aabbcc00aabbccffaabbcc80aa"
    "bbcc01aabbcc0080aabbff7faabbffffaabb0000aabb0000aabbffffaabb0080aabb0100aa"
    "bb000000000100000002000000030000000103aabbffff0080deadbeef";
struct Fixture {
  std::string json;
  std::vector<std::byte> buffer;
};
Fixture LiteralFixture(std::uint64_t mode = kTriangleMode) {
  Fixture fixture{std::string(kFixtureTemplate), DecodeHex(kFixtureBufferHex)};
  Replace(fixture.json, kModePlaceholder, std::to_string(mode));
  Replace(fixture.json, kIndexCountPlaceholder,
          mode == kTriangleMode ? "3" : "4");
  return fixture;
}
std::array<mv::SuppliedGltfResource, 1> Bind(const Fixture& fixture) {
  return {{{mv::GltfResourceKind::kBuffer, 0, fixture.buffer}}};
}
mv::GltfReadResult Read(const Fixture& fixture,
                        const geom::ExecutionContext& context, bool capped,
                        std::uint64_t cap = kGenerousRequestedBytes,
                        std::stop_token stop = {}) {
  const auto supplied = Bind(fixture);
  if (capped)
    return mv::ReadGltfAssetCapped(JsonBytes(fixture.json), supplied,
                                   ReadLimits(cap), WorkPolicy(), context,
                                   stop);
  return mv::ReadGltfAsset(JsonBytes(fixture.json), supplied, SourceLimits(),
                           WorkPolicy(), context, stop);
}
mv::GltfExtractionResult Extract(const mv::GltfAsset& asset, bool capped,
                                 std::uint64_t cap = kGenerousRequestedBytes,
                                 std::stop_token stop = {},
                                 std::uint64_t mode = kTriangleMode) {
  const auto primitive = asset.Primitive(0);
  Require(primitive.has_value(), "strong selected primitive");
  const mv::GltfExtractionRequest request{
      mode == kTriangleMode ? mv::GltfTriangleRequest::kRequireTriangles
                            : mv::GltfTriangleRequest::kConvertStripOrFan,
      mv::GltfValueRequest::kRawAndNormalized};
  if (capped)
    return mv::ExtractGltfPrimitiveCapped(asset, *primitive, request,
                                          ExtractLimits(cap), stop);
  return mv::ExtractGltfPrimitive(asset, *primitive, request,
                                  ExtractLimits(cap), stop);
}
const mv::GltfChannelView& Channel(
    const mv::GltfPrimitiveExtraction& extraction, std::string_view semantic) {
  for (const auto& channel : extraction.Channels())
    if (channel.semantic == semantic) return channel;
  throw std::runtime_error("missing expected semantic");
}
template <class T, std::size_t N>
void CheckRaw(const mv::GltfChannelView& channel,
              const std::array<T, N>& expected) {
  const auto* actual = std::get_if<std::span<const T>>(&channel.raw);
  Require(actual != nullptr && std::ranges::equal(*actual, expected),
          "exact typed raw fixture values");
}
template <std::size_t N>
void CheckNormalized(const mv::GltfChannelView& channel,
                     const std::array<std::uint64_t, N>& expected_bits) {
  Require(channel.normalized_values.size() == expected_bits.size(),
          "complete requested normalized output");
  for (std::size_t i = 0; i < expected_bits.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(channel.normalized_values[i]) ==
                expected_bits[i],
            "independent exact normalized value bits");
}
void CheckLiteralExtraction(const mv::GltfPrimitiveExtraction& extraction,
                            std::uint64_t mode) {
  Require(extraction.SourceMode() == mode, "original primitive mode");
  Require(extraction.Channels().size() == 6, "complete channel inventory");
  CheckRaw(Channel(extraction, kSignedByteSemantic),
           std::array<std::int8_t, 4>{-128, 127, -1, 0});
  CheckRaw(Channel(extraction, kUnsignedByteSemantic),
           std::array<std::uint8_t, 4>{0, 255, 128, 1});
  CheckRaw(Channel(extraction, kSignedShortSemantic),
           std::array<std::int16_t, 4>{-32768, 32767, -1, 0});
  CheckRaw(Channel(extraction, kUnsignedShortSemantic),
           std::array<std::uint16_t, 4>{0, 65535, 32768, 1});
  CheckRaw(Channel(extraction, kSparseSemantic),
           std::array<std::uint16_t, 4>{0, 65535, 0, 32768});
  const auto& positions = Channel(extraction, kPositionSemantic);
  const auto raw_positions = std::get<std::span<const float>>(positions.raw);
  constexpr std::array<std::uint32_t, 12> kPositionBits{
      0x80000000, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000};
  Require(raw_positions.size() == kPositionBits.size(), "complete XYZ values");
  for (std::size_t i = 0; i < kPositionBits.size(); ++i)
    Require(std::bit_cast<std::uint32_t>(raw_positions[i]) == kPositionBits[i],
            "position bits including negative zero");
  const auto& signed_bytes = Channel(extraction, kSignedByteSemantic);
  Require(signed_bytes.base_byte_stride == 4 && signed_bytes.normalized,
          "interleaved normalized declaration");
  const auto& sparse = Channel(extraction, kSparseSemantic);
  Require(!sparse.base_buffer_view && sparse.sparse_source.has_value() &&
              std::ranges::equal(sparse.sparse_destinations,
                                 std::array<std::uint64_t, 2>{1, 3}),
          "sparse source and destination correspondence");
  CheckNormalized(Channel(extraction, kSignedByteSemantic),
                  std::array<std::uint64_t, 4>{
                      0xbff0000000000000ULL, 0x3ff0000000000000ULL,
                      0xbf80204081020408ULL, 0x0000000000000000ULL});
  CheckNormalized(Channel(extraction, kUnsignedByteSemantic),
                  std::array<std::uint64_t, 4>{
                      0x0000000000000000ULL, 0x3ff0000000000000ULL,
                      0x3fe0101010101010ULL, 0x3f70101010101010ULL});
  CheckNormalized(Channel(extraction, kSignedShortSemantic),
                  std::array<std::uint64_t, 4>{
                      0xbff0000000000000ULL, 0x3ff0000000000000ULL,
                      0xbf00002000400080ULL, 0x0000000000000000ULL});
  CheckNormalized(Channel(extraction, kUnsignedShortSemantic),
                  std::array<std::uint64_t, 4>{
                      0x0000000000000000ULL, 0x3ff0000000000000ULL,
                      0x3fe0001000100010ULL, 0x3ef0001000100010ULL});
  const auto indices = extraction.SourceIndices();
  Require(indices.has_value(), "retained index accessor");
  const auto raw_indices =
      std::get<std::span<const std::uint32_t>>(indices->raw);
  const std::size_t index_count = mode == kTriangleMode ? 3 : 4;
  Require(raw_indices.size() == index_count, "original index count");
  for (std::size_t i = 0; i < index_count; ++i)
    Require(raw_indices[i] == i, "exact unsigned32 source index");
  const std::array<std::uint32_t, 6> expected =
      mode == kFanMode ? std::array<std::uint32_t, 6>{0, 1, 2, 0, 2, 3}
                       : std::array<std::uint32_t, 6>{0, 1, 2, 2, 1, 3};
  const std::size_t corner_count = mode == kTriangleMode ? 3 : 6;
  Require(std::ranges::equal(extraction.TriangleIndices(),
                             std::span(expected).first(corner_count)),
          "directed topology conversion");
  Require(extraction.SourceCorners().size() == corner_count,
          "complete original source corners");
  for (std::size_t i = 0; i < corner_count; ++i)
    Require(
        extraction.SourceCorners()[i].primitive_stream_corner == expected[i] &&
            extraction.SourceCorners()[i].accessor_row == expected[i],
        "literal stream and accessor provenance");
}

void LegacyAndCappedSemantics() {
  for (const auto mode : {kTriangleMode, kStripMode, kFanMode}) {
    auto fixture = LiteralFixture(mode);
    auto context = Context();
    for (const bool capped_read : {false, true}) {
      {
        auto captured = Read(fixture, context, capped_read);
        Require(captured.asset.has_value(), "public fixture read");
        Require(captured.asset->NewRequestedBytes().has_value() == capped_read,
                "read-route counter presence; absent is not zero");
        Require(std::ranges::equal(captured.asset->OriginalJson(),
                                   JsonBytes(fixture.json)),
                "exact JSON retention");
        Require(captured.asset->Resources().size() == 1 &&
                    std::ranges::equal(captured.asset->Resources()[0].bytes,
                                       fixture.buffer) &&
                    captured.asset->Resources()[0].logical_bytes == 136 &&
                    captured.asset->InstanceCount() == 2,
                "full resource trailing bytes and unrelated instances");
        for (const bool capped_extract : {false, true}) {
          auto result = Extract(*captured.asset, capped_extract,
                                kGenerousRequestedBytes, {}, mode);
          RequireExtraction(result, "explicit extraction route");
          CheckLiteralExtraction(*result.extraction, mode);
          const auto bytes = result.extraction->NewRequestedBytes();
          Require(bytes > 0, "actual live extraction counter in either scope");
          const auto ledger_before_copy = context.ActiveTrackedPayload();
          auto copy = *result.extraction;
          Require(copy.NewRequestedBytes() == bytes &&
                      context.ActiveTrackedPayload() == ledger_before_copy,
                  "copy shares one extraction counter without request");
          result.extraction.reset();
          CheckLiteralExtraction(copy, mode);
        }
      }
      Require(
          context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "complete legacy/capped lifecycle cleanup");
    }
  }
}

void RetainedOwnersAndStrongAssociation() {
  auto monitor = Context();
  std::optional<mv::GltfAsset> retained_asset;
  std::optional<mv::GltfPrimitiveExtraction> retained_extraction;
  std::uint64_t read_bytes = 0;
  std::uint64_t extraction_bytes = 0;
  {
    const auto temporary_context = monitor;
    auto fixture = LiteralFixture(kStripMode);
    auto original = Read(fixture, temporary_context, true);
    Require(original.asset.has_value(), "retained source read");
    retained_asset = *original.asset;
    read_bytes = *retained_asset->NewRequestedBytes();
    auto selected = original.asset->Primitive(0);
    auto equal_other = Read(fixture, temporary_context, true);
    Require(equal_other.asset.has_value(), "independent equal-byte source");
    auto wrong = mv::ExtractGltfPrimitiveCapped(
        *equal_other.asset, *selected,
        {mv::GltfTriangleRequest::kConvertStripOrFan,
         mv::GltfValueRequest::kRawAndNormalized},
        ExtractLimits(), {});
    Require(!wrong.extraction &&
                wrong.diagnostic.status == mv::GltfStatus::kForeignOwner &&
                wrong.diagnostic.code == mv::GltfDiagnosticCode::kForeignOwner,
            "same ledger and equal bytes cannot replace strong owner");
    const auto charge = monitor.ActiveTrackedPayload();
    auto result =
        Extract(*original.asset, true, kGenerousRequestedBytes, {}, kStripMode);
    RequireExtraction(result, "retained extraction capture");
    retained_extraction = *result.extraction;
    extraction_bytes = retained_extraction->NewRequestedBytes();
    Require(monitor.ActiveTrackedPayload() == charge + extraction_bytes,
            "distinct extraction operation excludes retained source bytes");
  }
  Require(*retained_asset->NewRequestedBytes() == read_bytes &&
              retained_extraction->NewRequestedBytes() == extraction_bytes &&
              monitor.ActiveTrackedPayload() == read_bytes + extraction_bytes,
          "live distinct owner counters after caller teardown");
  const auto expected = LiteralFixture(kStripMode);
  Require(std::ranges::equal(retained_asset->Resources()[0].bytes,
                             expected.buffer) &&
              std::ranges::equal(retained_asset->OriginalJson(),
                                 JsonBytes(expected.json)),
          "read copied retained source after original buffer/context teardown");
  retained_asset.reset();
  CheckLiteralExtraction(*retained_extraction, kStripMode);
  Require(monitor.ActiveTrackedPayload() == read_bytes + extraction_bytes,
          "extraction strongly retains the source operation");
  retained_extraction.reset();
  Require(monitor.ActiveTrackedPayload() == 0 && monitor.ActiveWorkers() == 0,
          "last retained owner releases source and extraction");
}

void FreshCaptureTokenAndBinaryFailure() {
  auto fixture = LiteralFixture();
  auto context = Context();
  for (const bool capped : {false, true}) {
    std::stop_source capture_stop;
    auto source = Read(fixture, context, capped, kGenerousRequestedBytes,
                       capture_stop.get_token());
    Require(source.asset.has_value(), "source before capture-token release");
    capture_stop.request_stop();
    auto result = Extract(*source.asset, true);
    RequireExtraction(result,
                      "extraction uses fresh token, not retained capture stop");
    CheckLiteralExtraction(*result.extraction, kTriangleMode);
  }
  Require(context.ActiveTrackedPayload() == 0,
          "fresh-token scopes release all owners");
  constexpr std::array<std::byte, 4> kQuietNanPositionBytes{
      std::byte{0x00}, std::byte{0x00}, std::byte{0xc0}, std::byte{0x7f}};
  std::ranges::copy(kQuietNanPositionBytes, fixture.buffer.begin());
  auto source = Read(fixture, context, true);
  Require(source.asset.has_value(),
          "metadata read does not silently decode binary");
  const auto source_charge = context.ActiveTrackedPayload();
  for (const bool capped : {false, true}) {
    auto result = Extract(*source.asset, capped);
    Require(
        !result.extraction &&
            result.diagnostic.status == mv::GltfStatus::kMalformed &&
            result.diagnostic.code == mv::GltfDiagnosticCode::kBinaryValue &&
            context.ActiveTrackedPayload() == source_charge,
        "unchanged decoder rejects NaN and restores only new requests");
  }
  source.asset.reset();
  Require(context.ActiveTrackedPayload() == 0, "binary-failure source cleanup");
}

void AdmissionAndCancellation() {
  auto fixture = LiteralFixture();
  auto context = Context();
  auto invalid = Read(fixture, context, true, 0);
  Require(!invalid.asset &&
              invalid.diagnostic.status == mv::GltfStatus::kInvalidLimits &&
              invalid.diagnostic.code == mv::GltfDiagnosticCode::kZeroLimit &&
              context.ActiveTrackedPayload() == 0,
          "zero new cap rejects configuration before seed allocation");
  auto seed_limited = Read(fixture, context, true, 1);
  Require(!seed_limited.asset &&
              seed_limited.diagnostic.status ==
                  mv::GltfStatus::kUnsupportedProfile &&
              seed_limited.diagnostic.code ==
                  mv::GltfDiagnosticCode::kLimitExceeded,
          "insufficient capped-read seed");
  Require(context.ActiveTrackedPayload() == 0, "seed rejection rollback");
  std::stop_source canceled;
  canceled.request_stop();
  auto stopped = Read(fixture, context, true, kGenerousRequestedBytes,
                      canceled.get_token());
  Require(
      !stopped.asset && stopped.diagnostic.status == mv::GltfStatus::kCanceled,
      "fresh read token observes prior stop");
  auto source = Read(fixture, context, true);
  Require(source.asset.has_value(), "source for fresh extraction stop");
  const auto retained_charge = context.ActiveTrackedPayload();
  auto stop_extract = Extract(*source.asset, true, kGenerousRequestedBytes,
                              canceled.get_token());
  Require(!stop_extract.extraction &&
              stop_extract.diagnostic.status == mv::GltfStatus::kCanceled &&
              context.ActiveTrackedPayload() == retained_charge,
          "canceled extraction preserves source charge");
  auto limited_extract = Extract(*source.asset, true, 1);
  Require(!limited_extract.extraction &&
              limited_extract.diagnostic.status ==
                  mv::GltfStatus::kUnsupportedProfile &&
              limited_extract.diagnostic.code ==
                  mv::GltfDiagnosticCode::kLimitExceeded &&
              context.ActiveTrackedPayload() == retained_charge,
          "extraction seed cap and unchanged source ledger");
  const auto foreign_bytes = context.TrackedPayloadBudget() - retained_charge;
  auto foreign = geom::TryReservePayload(context, foreign_bytes);
  Require(foreign.status == geom::ReservationStatus::kAccepted,
          "foreign operation lease");
  auto budget_limited = Extract(*source.asset, true);
  Require(
      !budget_limited.extraction &&
          budget_limited.diagnostic.status == mv::GltfStatus::kBudgetExceeded &&
          budget_limited.diagnostic.code ==
              mv::GltfDiagnosticCode::kPayloadBudget &&
          context.ActiveTrackedPayload() == context.TrackedPayloadBudget(),
      "foreign overlap consumes shared ledger independently of fresh cap");
  foreign.lease.Reset();
  source.asset.reset();
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "cancellation and foreign-budget cleanup");
}

Fixture ParallelFixture() {
  const auto bytes = kParallelFixtureVertices * kBytesPerShortVector;
  Fixture fixture;
  fixture.json =
      R"({"asset":{"version":"2.0"},"buffers":[{"uri":"parallel.bin","byteLength":)" +
      std::to_string(bytes) + R"(}],"bufferViews":[{"buffer":0,"byteLength":)" +
      std::to_string(bytes) +
      R"(}],"accessors":[{"componentType":5126,"count":)" +
      std::to_string(kParallelFixtureVertices) +
      R"(,"type":"VEC3","min":[0,0,0],"max":[0,0,0]},{"bufferView":0,"componentType":5122,"count":)" +
      std::to_string(kParallelFixtureVertices) +
      R"(,"type":"VEC2","normalized":true}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"_SHORT":1}}]}]})";
  fixture.buffer.resize(bytes + kResourceTailBytes, std::byte{0xad});
  constexpr std::array<std::byte, kBytesPerShortVector> kSignedShortRow{
      std::byte{0x00}, std::byte{0x80}, std::byte{0xff}, std::byte{0x7f}};
  for (std::size_t row = 0; row < kParallelFixtureVertices; ++row)
    std::ranges::copy(kSignedShortRow,
                      fixture.buffer.begin() + row * kBytesPerShortVector);
  return fixture;
}
void CheckParallelExtraction(const mv::GltfPrimitiveExtraction& extraction) {
  const auto positions = std::get<std::span<const float>>(
      Channel(extraction, kPositionSemantic).raw);
  Require(positions.size() == kParallelFixtureVertices * kPositionComponents,
          "whole substantial zero-position output");
  for (const float value : positions)
    Require(std::bit_cast<std::uint32_t>(value) == 0, "implicit zero bits");
  const auto& short_channel = Channel(extraction, kParallelShortSemantic);
  const auto raw = std::get<std::span<const std::int16_t>>(short_channel.raw);
  Require(raw.size() == kParallelFixtureVertices * kShortVectorComponents &&
              short_channel.normalized_values.size() == raw.size(),
          "whole substantial short-vector output");
  for (std::size_t row = 0; row < kParallelFixtureVertices; ++row) {
    const auto component = row * kShortVectorComponents;
    Require(raw[component] == -32768 && raw[component + 1] == 32767 &&
                short_channel.normalized_values[component] == -1.0 &&
                short_channel.normalized_values[component + 1] == 1.0,
            "literal signed extrema in each serial/parallel row");
  }
  Require(extraction.TriangleIndices().size() == kParallelFixtureVertices &&
              extraction.SourceCorners().size() == kParallelFixtureVertices,
          "substantial nonindexed triangle stream");
  for (std::size_t row = 0; row < kParallelFixtureVertices; ++row)
    Require(
        extraction.TriangleIndices()[row] == row &&
            extraction.SourceCorners()[row].primitive_stream_corner == row &&
            extraction.SourceCorners()[row].accessor_row == row,
        "independent full triangle/provenance ordinals");
}
void SerialParallelSemantics() {
  const auto fixture = ParallelFixture();
  for (const std::size_t workers :
       {kCallerOnlyWorkerBudget, kParallelWorkerBudget}) {
    auto context = Context(workers);
    {
      auto source = Read(fixture, context, true);
      Require(source.asset.has_value(), "substantial capped read");
      Require(std::ranges::equal(source.asset->Resources()[0].bytes,
                                 fixture.buffer),
              "substantial full resource bytes");
      auto extraction = Extract(*source.asset, true);
      RequireExtraction(extraction, "substantial capped extraction");
      CheckParallelExtraction(*extraction.extraction);
      Require(context.ActiveWorkers() == 0,
              "public operation joins before return");
      Require(
          (context.PeakWorkers() > 0) == (workers == kParallelWorkerBudget),
          "real-worker use versus serial policy; no estimated launch count");
    }
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "substantial serial/parallel complete cleanup");
  }
}

// A separate qualification invocation is mandatory before claiming >2^24 index
// support. The finite default suite does not execute or stand in for this case.
void LargeSourceIndex(std::uint64_t resource_budget_mib) {
  Require(resource_budget_mib > 0 &&
              resource_budget_mib <=
                  std::numeric_limits<std::size_t>::max() / kBytesPerMebibyte,
          "positive host-representable large-case budget");
  const auto budget = resource_budget_mib * kBytesPerMebibyte;
  geom::ExecutionContext context({kParallelWorkerBudget,
                                  kMinimumParallelVertices,
                                  static_cast<std::size_t>(budget)});
  Fixture fixture{
      R"({"asset":{"version":"2.0"},"buffers":[{"uri":"large.bin","byteLength":12}],"bufferViews":[{"buffer":0,"byteLength":12}],"accessors":[{"componentType":5126,"count":16777218,"type":"VEC3","min":[0,0,0],"max":[0,0,0]},{"bufferView":0,"componentType":5125,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"indices":1,"attributes":{"POSITION":0}}]}]})",
      DecodeHex("000000000100000001000001")};
  auto limits = ReadLimits(budget);
  limits.source.max_accessor_elements = kLargeVertexCount;
  const auto supplied = Bind(fixture);
  auto source = mv::ReadGltfAssetCapped(JsonBytes(fixture.json), supplied,
                                        limits, WorkPolicy(), context, {});
  Require(source.asset.has_value(), "large index source public read");
  auto extract_limits = ExtractLimits(budget);
  extract_limits.max_components =
      kLargeVertexCount * kPositionComponents + kLargeIndexCount;
  auto result = mv::ExtractGltfPrimitiveCapped(
      *source.asset, *source.asset->Primitive(0),
      {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw},
      extract_limits, {});
  RequireExtraction(result, "large index public capped extraction");
  constexpr std::array<std::uint32_t, 3> kExpectedIndices{0, 1,
                                                          kLargeSourceIndex};
  Require(std::ranges::equal(result.extraction->TriangleIndices(),
                             kExpectedIndices),
          "exact UInt32 index above binary32 integer precision");
  const auto raw_indices = std::get<std::span<const std::uint32_t>>(
      result.extraction->SourceIndices()->raw);
  Require(std::ranges::equal(raw_indices, kExpectedIndices),
          "retained UInt32 raw index");
  Require(
      result.extraction->SourceCorners()[2].accessor_row == kLargeSourceIndex,
      "exact large source accessor row");
  const auto positions = std::get<std::span<const float>>(
      Channel(*result.extraction, kPositionSemantic).raw);
  Require(positions.size() == kLargeVertexCount * kPositionComponents &&
              std::bit_cast<std::uint32_t>(
                  positions[std::uint64_t{kLargeSourceIndex} *
                            kPositionComponents]) == 0,
          "actual large canonical channel is present");
  result.extraction.reset();
  source.asset.reset();
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "large source complete cleanup");
}

// Fixed, nonallocating observation of the owning private backend's original
// requests. No RequestPlan, allocation header layout, replacement global new,
// ledger-delta peak estimate or payload-only lower bound is used here.
constexpr std::size_t kMaximumRecordedLiveBlocks = 4096;
constexpr std::size_t kFailSecondLaunchOrdinal = 2;
struct RecordedBlock {
  void* pointer = nullptr;
  std::size_t bytes = 0;
  std::size_t alignment = 0;
};
struct RequestRecorder {
  explicit RequestRecorder(const geom::ExecutionContext& context)
      : context(context), baseline(context.ActiveTrackedPayload()) {}
  const geom::ExecutionContext& context;
  std::size_t baseline;
  detail::AllocationBackend original = detail::DefaultAllocationBackend();
  std::array<RecordedBlock, kMaximumRecordedLiveBlocks> blocks{};
  std::mutex mutex;
  std::size_t calls = 0;
  std::size_t live = 0;
  std::size_t peak = 0;
  std::size_t fail_ordinal = 0;
  bool fault_entered = false;
  bool covered = true;
  bool matching = true;
  bool freed_before_release = true;
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<RequestRecorder*>(state);
    std::lock_guard lock(self.mutex);
    ++self.calls;
    Require(bytes <= std::numeric_limits<std::size_t>::max() - self.live,
            "recorded request overflow");
    const auto new_live = self.live + bytes;
    self.covered &=
        self.context.ActiveTrackedPayload() >= self.baseline + new_live;
    if (self.fail_ordinal == self.calls) {
      self.fault_entered = true;
      throw std::bad_alloc();
    }
    auto free = std::ranges::find_if(
        self.blocks,
        [](const RecordedBlock& block) { return block.pointer == nullptr; });
    Require(free != self.blocks.end(), "fixed request recorder capacity");
    // Preserve the actual argument sizes/alignment and original default route.
    void* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    Require(pointer != nullptr, "original allocation returned null");
    *free = {pointer, bytes, alignment};
    self.live = new_live;
    self.peak = std::max(self.peak, new_live);
    return pointer;
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<RequestRecorder*>(state);
    std::lock_guard lock(self.mutex);
    auto block = std::ranges::find_if(self.blocks,
                                      [pointer](const RecordedBlock& entry) {
                                        return entry.pointer == pointer;
                                      });
    self.matching &= block != self.blocks.end();
    if (block == self.blocks.end()) return;
    self.matching &= block->bytes == bytes && block->alignment == alignment;
    self.freed_before_release &=
        self.context.ActiveTrackedPayload() >= self.baseline + self.live;
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    // The original free has finished; its charge still exists until this
    // callback returns and the owning AllocationContext releases the lease.
    self.freed_before_release &=
        self.context.ActiveTrackedPayload() >= self.baseline + self.live;
    self.live -= block->bytes;
    *block = {};
  }
  detail::AllocationBackend Backend() noexcept {
    return {Allocate, Free, this};
  }
  void CheckLive(std::uint64_t owner_bytes) const {
    Require(covered && matching && freed_before_release,
            "complete original request admission/matched free/lease ordering");
    Require(live == owner_bytes &&
                context.ActiveTrackedPayload() == baseline + live,
            "exact retained owner count versus live original requests: live=" +
                std::to_string(live) + " owner=" + std::to_string(owner_bytes) +
                " ledger=" + std::to_string(context.ActiveTrackedPayload()) +
                " baseline=" + std::to_string(baseline) +
                " calls=" + std::to_string(calls));
  }
  void CheckEmpty() const {
    CheckLive(0);
    Require(
        context.ActiveWorkers() == 0,
        "all worker reservations restored before request recorder teardown");
  }
};
struct WorkerRecorder {
  std::atomic<std::size_t> launched{0};
  std::atomic<std::size_t> joined{0};
  std::atomic<std::size_t> entered{0};
  std::stop_source* stop_on_entry = nullptr;
  static void Launched(std::size_t, void* state) noexcept {
    static_cast<WorkerRecorder*>(state)->launched.fetch_add(1);
  }
  static void Joined(std::size_t, void* state) noexcept {
    static_cast<WorkerRecorder*>(state)->joined.fetch_add(1);
  }
  static void Entered(std::size_t, void* state) noexcept {
    auto& self = *static_cast<WorkerRecorder*>(state);
    self.entered.fetch_add(1);
    if (self.stop_on_entry) self.stop_on_entry->request_stop();
  }
  detail::OperationWorkerObserver Observer() noexcept {
    return {Launched, Joined, Entered, this};
  }
  void CheckJoined(bool require_positive) const {
    Require(launched.load() == joined.load(),
            "actual native launches equal joins");
    if (require_positive)
      Require(launched.load() > 0 && entered.load() > 0,
              "actual entered worker witness, not planned work");
  }
};
class InjectedLaunchFailure : public std::exception {
 public:
  const char* what() const noexcept override {
    return "independent second launch fault";
  }
};
struct FailSecondLaunch {
  detail::NativeWorkerBackend original = detail::DefaultNativeWorkerBackend();
  std::size_t calls = 0;
  static std::uintptr_t Launch(detail::NativeWorkerRecord* record,
                               void* state) {
    auto& self = *static_cast<FailSecondLaunch*>(state);
    if (++self.calls == kFailSecondLaunchOrdinal) throw InjectedLaunchFailure();
    return self.original.launch(record, self.original.state);
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& self = *static_cast<FailSecondLaunch*>(state);
    self.original.join(handle, self.original.state);
  }
  detail::NativeWorkerBackend Backend() noexcept {
    return {Launch, Join, this};
  }
};
mv::GltfReadResult ObservedRead(
    const Fixture& fixture, const geom::ExecutionContext& context,
    RequestRecorder& recorder, std::uint64_t cap = kGenerousRequestedBytes,
    std::stop_token stop = {},
    detail::NativeWorkerBackend backend = detail::DefaultNativeWorkerBackend(),
    const detail::OperationWorkerObserver* observer = nullptr) {
  const auto supplied = Bind(fixture);
  return detail::TestReadGltfAssetCapped(
      JsonBytes(fixture.json), supplied, ReadLimits(cap), WorkPolicy(), context,
      stop, recorder.Backend(), backend, observer);
}
mv::GltfExtractionResult ObservedExtract(
    const mv::GltfAsset& asset, RequestRecorder& recorder,
    std::uint64_t cap = kGenerousRequestedBytes, std::stop_token stop = {},
    detail::NativeWorkerBackend backend = detail::DefaultNativeWorkerBackend(),
    const detail::OperationWorkerObserver* observer = nullptr,
    std::uint64_t mode = kTriangleMode) {
  return detail::TestExtractGltfPrimitiveCapped(
      asset, *asset.Primitive(0),
      {mode == kTriangleMode ? mv::GltfTriangleRequest::kRequireTriangles
                             : mv::GltfTriangleRequest::kConvertStripOrFan,
       mv::GltfValueRequest::kRawAndNormalized},
      ExtractLimits(cap), stop, recorder.Backend(), backend, observer);
}
void OriginalReadRequests() {
  const auto fixture = LiteralFixture();
  auto context = Context();
  std::size_t peak = 0;
  std::size_t requests = 0;
  {
    RequestRecorder recorder(context);
    auto result = ObservedRead(fixture, context, recorder);
    Require(result.asset.has_value(), "original-route read observation");
    recorder.CheckLive(*result.asset->NewRequestedBytes());
    peak = recorder.peak;
    requests = recorder.calls;
    Require(peak > 1 && requests > 1,
            "read seed plus real retained/parser requests");
    result.asset.reset();
    recorder.CheckEmpty();
  }
  {
    RequestRecorder recorder(context);
    auto result = ObservedRead(fixture, context, recorder, peak);
    Require(result.asset.has_value() && recorder.peak == peak,
            "actual observed original read peak is admitted");
    result.asset.reset();
    recorder.CheckEmpty();
  }
  {
    RequestRecorder recorder(context);
    auto result = ObservedRead(fixture, context, recorder, peak - 1);
    Require(
        !result.asset &&
            result.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
            result.diagnostic.code == mv::GltfDiagnosticCode::kLimitExceeded,
        "one byte below observed original read peak rejects");
    recorder.CheckEmpty();
  }
  for (std::size_t ordinal = 1; ordinal <= requests; ++ordinal) {
    RequestRecorder recorder(context);
    recorder.fail_ordinal = ordinal;
    bool caught = false;
    try {
      (void)ObservedRead(fixture, context, recorder);
    } catch (const std::bad_alloc&) {
      caught = true;
    }
    Require(caught && recorder.fault_entered,
            "actual read request ordinal fault propagates");
    recorder.CheckEmpty();
  }
  std::cout << "original read request peak " << peak << ", ordinals "
            << requests << '\n';
}
void OriginalExtractionRequests() {
  const auto fixture = LiteralFixture(kStripMode);
  auto context = Context();
  auto source = Read(fixture, context, true);
  Require(source.asset.has_value(),
          "source outside extraction new-request cap");
  const auto source_bytes = context.ActiveTrackedPayload();
  std::size_t peak = 0;
  std::size_t requests = 0;
  {
    RequestRecorder recorder(context);
    auto result = ObservedExtract(
        *source.asset, recorder, kGenerousRequestedBytes, {},
        detail::DefaultNativeWorkerBackend(), nullptr, kStripMode);
    RequireExtraction(result, "original extraction route observation");
    CheckLiteralExtraction(*result.extraction, kStripMode);
    recorder.CheckLive(result.extraction->NewRequestedBytes());
    peak = recorder.peak;
    requests = recorder.calls;
    Require(peak > 1 && requests > 1,
            "extraction seed and original rebound/value requests");
    result.extraction.reset();
    recorder.CheckEmpty();
  }
  for (const auto cap : {peak, peak - 1}) {
    RequestRecorder recorder(context);
    auto result = ObservedExtract(*source.asset, recorder, cap, {},
                                  detail::DefaultNativeWorkerBackend(), nullptr,
                                  kStripMode);
    if (cap == peak) {
      Require(result.extraction.has_value() && recorder.peak == peak,
              "actual observed original extraction peak admitted");
      result.extraction.reset();
    } else {
      Require(
          !result.extraction &&
              result.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
              result.diagnostic.code == mv::GltfDiagnosticCode::kLimitExceeded,
          "one byte below observed original extraction peak rejects");
    }
    recorder.CheckEmpty();
  }
  for (std::size_t ordinal = 1; ordinal <= requests; ++ordinal) {
    RequestRecorder recorder(context);
    recorder.fail_ordinal = ordinal;
    bool caught = false;
    try {
      (void)ObservedExtract(*source.asset, recorder, kGenerousRequestedBytes,
                            {}, detail::DefaultNativeWorkerBackend(), nullptr,
                            kStripMode);
    } catch (const std::bad_alloc&) {
      caught = true;
    }
    Require(caught && recorder.fault_entered,
            "actual extraction request fault propagates");
    recorder.CheckEmpty();
  }
  Require(context.ActiveTrackedPayload() == source_bytes,
          "all original extraction failures preserve source operation");
  source.asset.reset();
  Require(context.ActiveTrackedPayload() == 0,
          "original observed extraction lifetime cleanup");
  std::cout << "original extraction request peak " << peak << ", ordinals "
            << requests << '\n';
}

void OriginalEmptyReadRequests() {
  constexpr std::string_view kEmptyAssetJson = R"({"asset":{"version":"2.0"}})";
  auto context = Context();
  std::size_t peak = 0;
  std::size_t requests = 0;
  const auto attempt = [&](RequestRecorder& recorder, std::uint64_t cap) {
    return detail::TestReadGltfAssetCapped(
        JsonBytes(kEmptyAssetJson), {}, ReadLimits(cap), WorkPolicy(), context,
        {}, recorder.Backend(), detail::DefaultNativeWorkerBackend());
  };
  {
    RequestRecorder recorder(context);
    auto result = attempt(recorder, kGenerousRequestedBytes);
    Require(result.asset.has_value() && result.asset->PrimitiveCount() == 0 &&
                result.asset->Resources().empty(),
            "empty asset has real seed/control ownership");
    Require(std::ranges::equal(result.asset->OriginalJson(),
                               JsonBytes(kEmptyAssetJson)),
            "empty original JSON still retained");
    recorder.CheckLive(*result.asset->NewRequestedBytes());
    peak = recorder.peak;
    requests = recorder.calls;
    {
      auto copied = *result.asset;
      const auto owner_bytes = *copied.NewRequestedBytes();
      result.asset.reset();
      recorder.CheckLive(owner_bytes);
    }
    recorder.CheckEmpty();
  }
  Require(context.ActiveTrackedPayload() == 0,
          "empty copied owner final release");
  for (const auto cap : {peak, peak - 1}) {
    RequestRecorder recorder(context);
    auto result = attempt(recorder, cap);
    Require(result.asset.has_value() == (cap == peak),
            "empty read exact original peak boundary");
    if (cap != peak)
      Require(
          result.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
              result.diagnostic.code == mv::GltfDiagnosticCode::kLimitExceeded,
          "empty control/scratch cap refusal diagnostic");
    result.asset.reset();
    recorder.CheckEmpty();
  }
  for (std::size_t ordinal = 1; ordinal <= requests; ++ordinal) {
    RequestRecorder recorder(context);
    recorder.fail_ordinal = ordinal;
    bool caught = false;
    try {
      (void)attempt(recorder, kGenerousRequestedBytes);
    } catch (const std::bad_alloc&) {
      caught = true;
    }
    Require(caught && recorder.fault_entered,
            "empty read original allocation fault");
    recorder.CheckEmpty();
  }
}
void ReplayWorkerRequests(const Fixture& fixture, const mv::GltfAsset& source,
                          const geom::ExecutionContext& context, bool extract,
                          std::size_t peak, std::size_t requests) {
  for (const auto cap : {peak, peak - 1}) {
    RequestRecorder recorder(context);
    WorkerRecorder workers;
    const auto observer = workers.Observer();
    if (extract) {
      auto result =
          ObservedExtract(source, recorder, cap, {},
                          detail::DefaultNativeWorkerBackend(), &observer);
      Require(result.extraction.has_value() == (cap == peak),
              "worker extraction exact observed original peak boundary");
      if (cap == peak) {
        CheckParallelExtraction(*result.extraction);
      } else
        Require(
            result.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
                result.diagnostic.code ==
                    mv::GltfDiagnosticCode::kLimitExceeded,
            "worker extraction one-byte-less typed refusal");
      result.extraction.reset();
    } else {
      auto result =
          ObservedRead(fixture, context, recorder, cap, {},
                       detail::DefaultNativeWorkerBackend(), &observer);
      Require(result.asset.has_value() == (cap == peak),
              "worker read exact observed original peak boundary");
      if (cap != peak)
        Require(
            result.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
                result.diagnostic.code ==
                    mv::GltfDiagnosticCode::kLimitExceeded,
            "worker read one-byte-less typed refusal");
      result.asset.reset();
    }
    workers.CheckJoined(cap == peak);
    recorder.CheckEmpty();
  }
  for (std::size_t ordinal = 1; ordinal <= requests; ++ordinal) {
    RequestRecorder recorder(context);
    recorder.fail_ordinal = ordinal;
    WorkerRecorder workers;
    const auto observer = workers.Observer();
    bool caught = false;
    try {
      if (extract)
        (void)ObservedExtract(source, recorder, kGenerousRequestedBytes, {},
                              detail::DefaultNativeWorkerBackend(), &observer);
      else
        (void)ObservedRead(fixture, context, recorder, kGenerousRequestedBytes,
                           {}, detail::DefaultNativeWorkerBackend(), &observer);
    } catch (const std::bad_alloc&) {
      caught = true;
    }
    Require(caught && recorder.fault_entered,
            "every actual original worker-record scope request failure");
    workers.CheckJoined(false);
    recorder.CheckEmpty();
  }
}
void OriginalWorkerRequestsAndCleanup() {
  const auto fixture = ParallelFixture();
  auto context = Context(kParallelWorkerBudget);
  std::size_t read_peak = 0;
  std::size_t read_requests = 0;
  std::size_t extract_peak = 0;
  std::size_t extract_requests = 0;
  // The entire read and extraction backend scope is measured independently;
  // opaque native-thread/CRT storage is the declared contract exclusion.
  {
    RequestRecorder recorder(context);
    WorkerRecorder workers;
    const auto observer = workers.Observer();
    auto result =
        ObservedRead(fixture, context, recorder, kGenerousRequestedBytes, {},
                     detail::DefaultNativeWorkerBackend(), &observer);
    Require(result.asset.has_value(),
            "original request read with real native workers");
    workers.CheckJoined(true);
    recorder.CheckLive(*result.asset->NewRequestedBytes());
    read_peak = recorder.peak;
    read_requests = recorder.calls;
    result.asset.reset();
    recorder.CheckEmpty();
  }
  auto source = Read(fixture, context, true);
  Require(source.asset.has_value(), "real-worker extraction source");
  {
    RequestRecorder recorder(context);
    WorkerRecorder workers;
    const auto observer = workers.Observer();
    auto result =
        ObservedExtract(*source.asset, recorder, kGenerousRequestedBytes, {},
                        detail::DefaultNativeWorkerBackend(), &observer);
    RequireExtraction(result,
                      "original extraction request native worker scope");
    CheckParallelExtraction(*result.extraction);
    workers.CheckJoined(true);
    recorder.CheckLive(result.extraction->NewRequestedBytes());
    extract_peak = recorder.peak;
    extract_requests = recorder.calls;
    result.extraction.reset();
    recorder.CheckEmpty();
  }
  ReplayWorkerRequests(fixture, *source.asset, context, false, read_peak,
                       read_requests);
  ReplayWorkerRequests(fixture, *source.asset, context, true, extract_peak,
                       extract_requests);
  for (const bool extract : {false, true}) {
    {
      RequestRecorder recorder(context);
      WorkerRecorder workers;
      const auto observer = workers.Observer();
      FailSecondLaunch failure;
      bool caught = false;
      try {
        if (extract)
          (void)ObservedExtract(*source.asset, recorder,
                                kGenerousRequestedBytes, {}, failure.Backend(),
                                &observer);
        else
          (void)ObservedRead(fixture, context, recorder,
                             kGenerousRequestedBytes, {}, failure.Backend(),
                             &observer);
      } catch (const InjectedLaunchFailure&) {
        caught = true;
      }
      Require(caught && failure.calls == kFailSecondLaunchOrdinal &&
                  workers.launched.load() == 1 && workers.joined.load() == 1,
              "second-launch exception joins the first actual native thread");
      workers.CheckJoined(true);
      recorder.CheckEmpty();
    }
    {
      RequestRecorder recorder(context);
      WorkerRecorder workers;
      std::stop_source stop;
      workers.stop_on_entry = &stop;
      const auto observer = workers.Observer();
      if (extract) {
        auto result = ObservedExtract(
            *source.asset, recorder, kGenerousRequestedBytes, stop.get_token(),
            detail::DefaultNativeWorkerBackend(), &observer);
        Require(
            !result.extraction &&
                result.diagnostic.status == mv::GltfStatus::kCanceled,
            "actual entered extraction job cancellation publishes no owner");
      } else {
        auto result = ObservedRead(
            fixture, context, recorder, kGenerousRequestedBytes,
            stop.get_token(), detail::DefaultNativeWorkerBackend(), &observer);
        Require(!result.asset &&
                    result.diagnostic.status == mv::GltfStatus::kCanceled,
                "actual entered resource job cancellation publishes no owner");
      }
      workers.CheckJoined(true);
      recorder.CheckEmpty();
    }
  }
  source.asset.reset();
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "observed real-worker source cleanup");
}

}  // namespace
int main(int argc, char** argv) {
  try {
    constexpr int kLargeCaseArgumentCount = 4;
    if (argc != 1) {
      Require(argc == kLargeCaseArgumentCount && argv[1] == kLargeCaseOption &&
                  argv[2] == kResourceBudgetOption,
              "expected --large-source-index --resource-budget-mib POSITIVE");
      const std::string_view text(argv[3]);
      std::uint64_t budget_mib = 0;
      const auto parsed =
          std::from_chars(text.data(), text.data() + text.size(), budget_mib);
      Require(
          parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
          "whole unsigned large-case budget");
      LargeSourceIndex(budget_mib);
      std::cout << "large UInt32 public read/extract fixture passed\n";
      return 0;
    }
    LegacyAndCappedSemantics();
    RetainedOwnersAndStrongAssociation();
    FreshCaptureTokenAndBinaryFailure();
    AdmissionAndCancellation();
    SerialParallelSemantics();
    OriginalEmptyReadRequests();
    OriginalReadRequests();
    OriginalExtractionRequests();
    OriginalWorkerRequestsAndCleanup();
    std::cout << "capped read/extract public semantic fixtures passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
