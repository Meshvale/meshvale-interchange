// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_materialization_test_internal.h"

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = mv::gltf_materialization_detail;
constexpr std::size_t kWorkerBudget = 2;
constexpr std::size_t kPayloadBudget = 16777216;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kRequestLimit = 4194304;
constexpr std::uint64_t kDescriptorLimit = 1048576;
constexpr std::uint64_t kChannelLimit = 1024;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kPartitionBytes = 64;
constexpr std::uint64_t kCancellationBlockScalars = 1;
constexpr std::uint64_t kMaximumJobs = 32;
constexpr std::size_t kRequestRecordCapacity = 32;
constexpr std::string_view kFixtureOption = "--fixture";
constexpr std::string_view kPositionRecord = "positions";
constexpr std::string_view kOffsetsRecord = "offsets";
constexpr std::string_view kCornersRecord = "corners";
constexpr std::string_view kAttributeRecord = "attribute";
constexpr std::string_view kNormalizedRecord = "normalized";
constexpr std::string_view kMappingRecord = "mapping";
constexpr std::string_view kIdentityRecord = "identity";
constexpr std::string_view kIndicesRecord = "indices";
constexpr std::string_view kInt8Type = "i8";
constexpr std::string_view kInt16Type = "i16";
constexpr std::string_view kFloat32Type = "f32";
constexpr std::string_view kFloat64Type = "f64";
constexpr std::string_view kInt32Type = "i32";
constexpr std::string_view kUint8Type = "u8";
constexpr std::string_view kUint16Type = "u16";
constexpr std::string_view kUint32Type = "u32";
constexpr std::string_view kUint64Type = "u64";
constexpr std::string_view kDocument =
    R"({"asset":{"version":"2.0"},"extras":{"retained":9007199254740993},"accessors":[{"componentType":5126,"type":"VEC3","count":3,"min":[0,0,0],"max":[0,0,0]},{"componentType":5120,"type":"SCALAR","count":3,"normalized":true}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"_SIGNED":1}},{"attributes":{"POSITION":0,"_SIGNED":1}}]},{"primitives":[{"attributes":{"POSITION":0,"_SIGNED":1}},{"attributes":{"POSITION":0,"_SIGNED":1}}]}]})";
constexpr std::uint64_t kSelectedPrimitiveOrdinal = 3;
constexpr std::uint64_t kExpectedMesh = 1;
constexpr std::uint64_t kExpectedPrimitive = 1;

void Require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
geo::ExecutionContext Context() {
  return geo::ExecutionContext(
      {kWorkerBudget, kParallelMinimumBytes, kPayloadBudget});
}
geo::MeshMaterializationWorkPolicy Work() {
  return {kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
          kMaximumJobs};
}
mv::GltfReadLimits ReadLimits() {
  return {kRequestLimit, kRequestLimit, kRequestLimit, kCountLimit,
          kChannelLimit, kRequestLimit, kChannelLimit, kChannelLimit,
          kChannelLimit, kCountLimit,   kCountLimit,   kCountLimit,
          kCountLimit,   kRequestLimit};
}
mv::GltfMaterializationLimits Limits() {
  return {kChannelLimit,
          kRequestLimit,
          {kCountLimit, kCountLimit, kCountLimit, kCountLimit, kChannelLimit,
           kCountLimit, kCountLimit, kCountLimit, kCountLimit, kDescriptorLimit,
           kRequestLimit},
          {kCountLimit, kRequestLimit}};
}
mv::GltfPrimitiveExtraction Extract(std::string_view document,
                                    std::span<const std::byte> bytes,
                                    const geo::ExecutionContext& context,
                                    std::uint64_t ordinal,
                                    std::stop_token stop = {}) {
  const mv::SuppliedGltfResource resource{mv::GltfResourceKind::kBuffer, 0,
                                          bytes};
  auto read = mv::ReadGltfAsset(
      std::as_bytes(std::span(document.data(), document.size())),
      bytes.empty() ? std::span<const mv::SuppliedGltfResource>{}
                    : std::span(&resource, 1),
      ReadLimits(),
      {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumBytes,
       kCancellationBlockScalars},
      context, stop);
  Require(read.asset.has_value(), "actual reader rejected focused fixture");
  auto extracted =
      mv::ExtractGltfPrimitive(*read.asset, *read.asset->Primitive(ordinal),
                               {mv::GltfTriangleRequest::kConvertStripOrFan,
                                mv::GltfValueRequest::kRawAndNormalized},
                               {kCountLimit, kCountLimit, kRequestLimit}, stop);
  Require(extracted.extraction.has_value(),
          "actual extractor rejected focused fixture");
  return *extracted.extraction;
}
void RequireRollback(const geo::ExecutionContext& context,
                     std::size_t source_bytes) {
  Require(context.ActiveTrackedPayload() == source_bytes,
          "failed adapter retained new charges");
  Require(context.ActiveWorkers() == 0, "failed adapter retained worker slots");
}
void LimitsCancellationAndDelegation() {
  auto context = Context();
  std::stop_source original_stop;
  const auto extraction =
      Extract(kDocument, {}, context, kSelectedPrimitiveOrdinal,
              original_stop.get_token());
  original_stop.request_stop();
  const auto source_bytes = context.ActiveTrackedPayload();
  auto invalid = Limits();
  invalid.inspection.max_issues = 0;
  const auto invalid_result =
      mv::MaterializeGltfPrimitive(extraction, invalid, Work());
  Require(invalid_result.status ==
                  mv::GltfMaterializationStatus::kInvalidConfiguration &&
              !invalid_result.primitive,
          "nested zero limit must reject before admission");
  auto limited = Limits();
  limited.max_channels = 1;
  Require(mv::MaterializeGltfPrimitive(extraction, limited, Work())
                  .diagnostic.code ==
              mv::GltfMaterializationDiagnosticCode::kChannelLimit,
          "channel cap must count POSITION");
  limited = Limits();
  limited.max_bridge_new_requested_bytes = 1;
  const auto seed_limit =
      mv::MaterializeGltfPrimitive(extraction, limited, Work());
  Require(seed_limit.status == mv::GltfMaterializationStatus::kLimitExceeded &&
              seed_limit.diagnostic.code ==
                  mv::GltfMaterializationDiagnosticCode::kBridgeRequestLimit,
          "too-small bridge seed cap must be an operation limit");
  limited = Limits();
  limited.materialization.max_vertices = 1;
  const auto capture_limit =
      mv::MaterializeGltfPrimitive(extraction, limited, Work());
  Require(
      capture_limit.status == mv::GltfMaterializationStatus::kGeometryFailure &&
          capture_limit.diagnostic.stage ==
              mv::GltfMaterializationStage::kCapture &&
          capture_limit.diagnostic.geometry_status ==
              geo::MaterializationStatus::kLimitExceeded &&
          capture_limit.diagnostic.geometry_diagnostic->code ==
              geo::MaterializationDiagnosticCode::kCountLimit,
      "capture failure must retain actual Geometry outcome");
  limited = Limits();
  limited.inspection.max_new_requested_bytes = 1;
  const auto inspection_limit =
      mv::MaterializeGltfPrimitive(extraction, limited, Work());
  Require(inspection_limit.status ==
                  mv::GltfMaterializationStatus::kGeometryFailure &&
              inspection_limit.diagnostic.stage ==
                  mv::GltfMaterializationStage::kInspection &&
              inspection_limit.diagnostic.geometry_status ==
                  geo::MaterializationStatus::kLimitExceeded,
          "inspection failure must retain actual Geometry outcome");
  std::stop_source fresh_stop;
  fresh_stop.request_stop();
  Require(mv::MaterializeGltfPrimitive(extraction, Limits(), Work(),
                                       fresh_stop.get_token())
                  .status == mv::GltfMaterializationStatus::kCanceled,
          "fresh pre-cancel must reject");
  fresh_stop = std::stop_source{};
  detail::TestHooks hooks;
  hooks.descriptor_state = &fresh_stop;
  hooks.after_descriptor = [](std::uint64_t, void* state) noexcept {
    static_cast<std::stop_source*>(state)->request_stop();
  };
  const auto planned_stop = detail::TestMaterializeGltfPrimitive(
      extraction, Limits(), Work(), fresh_stop.get_token(), hooks);
  Require(planned_stop.status == mv::GltfMaterializationStatus::kCanceled &&
              planned_stop.diagnostic.stage ==
                  mv::GltfMaterializationStage::kBridge,
          "descriptor stop must rollback before capture");
  RequireRollback(context, source_bytes);
  auto occupied = geo::TryReservePayload(
      context, context.TrackedPayloadBudget() - source_bytes);
  Require(occupied.status == geo::ReservationStatus::kAccepted,
          "contention reservation failed");
  const auto contention =
      mv::MaterializeGltfPrimitive(extraction, Limits(), Work());
  Require(contention.status == mv::GltfMaterializationStatus::kBudgetExceeded &&
              contention.diagnostic.code ==
                  mv::GltfMaterializationDiagnosticCode::kRequestBudget,
          "shared ledger contention must differ from operation cap");
  occupied.lease.Reset();
  RequireRollback(context, source_bytes);
  auto success = mv::MaterializeGltfPrimitive(extraction, Limits(), Work());
  Require(success.primitive.has_value(),
          "original stopped token must not cancel later bridge");
  Require(success.primitive->SourcePrimitive().MeshIndex() == kExpectedMesh &&
              success.primitive->SourcePrimitive().PrimitiveIndex() ==
                  kExpectedPrimitive,
          "selected primitive identity must not come from shared accessors");
  const auto charges = success.primitive->NewRequestedBytes();
  Require(charges.bridge > 0 && charges.materialization > 0 &&
              charges.inspection > 0 &&
              context.ActiveTrackedPayload() == source_bytes + charges.bridge +
                                                    charges.materialization +
                                                    charges.inspection,
          "three live caps must correspond to original shared ledger");
  const auto before_copy = context.ActiveTrackedPayload();
  std::optional<mv::GltfMaterializedPrimitive> copy = *success.primitive;
  success.primitive.reset();
  Require(context.ActiveTrackedPayload() == before_copy &&
              copy->Inspection().IsStorageValid(),
          "owner copy must share storage and charge");
  copy.reset();
  RequireRollback(context, source_bytes);
}

struct RequestObserver {
  struct Record {
    void* pointer = nullptr;
    std::size_t bytes = 0;
  };
  const geo::ExecutionContext* context;
  std::size_t source_bytes = 0;
  std::array<Record, kRequestRecordCapacity> records{};
  std::size_t calls = 0;
  std::size_t fail_request = 0;
  std::size_t active_bytes = 0;
  std::size_t peak_bytes = 0;
  bool invalid = false;
  mv::gltf_detail::AllocationBackend original =
      mv::gltf_detail::DefaultAllocationBackend();
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<RequestObserver*>(state);
    ++self.calls;
    if (self.context->ActiveTrackedPayload() <
        self.source_bytes + self.active_bytes + bytes)
      self.invalid = true;
    if (self.calls == self.fail_request) throw std::bad_alloc();
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    bool saved = false;
    for (auto& record : self.records) {
      if (!record.pointer) {
        record = {pointer, bytes};
        saved = true;
        break;
      }
    }
    if (!saved) self.invalid = true;
    self.active_bytes += bytes;
    if (self.active_bytes > self.peak_bytes)
      self.peak_bytes = self.active_bytes;
    return pointer;
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<RequestObserver*>(state);
    if (self.context->ActiveTrackedPayload() <
        self.source_bytes + self.active_bytes)
      self.invalid = true;
    bool found = false;
    for (auto& record : self.records) {
      if (record.pointer == pointer) {
        found = record.bytes == bytes;
        record = {};
        break;
      }
    }
    if (!found) self.invalid = true;
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    self.active_bytes -= bytes;
  }
  detail::TestHooks Hooks() {
    detail::TestHooks result;
    result.allocation = {Allocate, Free, this};
    return result;
  }
};
void ActualBridgeRequestsAndFaults() {
  auto context = Context();
  auto extraction = Extract(kDocument, {}, context, kSelectedPrimitiveOrdinal);
  const auto source_bytes = context.ActiveTrackedPayload();
  RequestObserver observer{&context, source_bytes};
  auto result = detail::TestMaterializeGltfPrimitive(
      extraction, Limits(), Work(), {}, observer.Hooks());
  Require(result.primitive.has_value(), "observed adapter did not succeed");
  const auto request_count = observer.calls;
  {
    const auto copied = *result.primitive;
    Require(observer.calls == request_count &&
                copied.Mesh().View().positions.data() ==
                    result.primitive->Mesh().View().positions.data(),
            "copied adapter must not allocate or duplicate canonical storage");
  }
  const auto peak = observer.peak_bytes;
  Require(request_count > 0 && observer.active_bytes ==
                                   result.primitive->NewRequestedBytes().bridge,
          "actual bridge requests must equal live bridge report");
  result.primitive.reset();
  Require(!observer.invalid && observer.active_bytes == 0,
          "bridge requests freed before their charge was released");
  RequireRollback(context, source_bytes);
  for (std::size_t ordinal = 1; ordinal <= request_count; ++ordinal) {
    RequestObserver fault{&context, source_bytes};
    fault.fail_request = ordinal;
    bool threw = false;
    try {
      (void)detail::TestMaterializeGltfPrimitive(extraction, Limits(), Work(),
                                                 {}, fault.Hooks());
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    Require(threw && !fault.invalid && fault.active_bytes == 0,
            "every actual bridge backend failure must propagate and rollback");
    RequireRollback(context, source_bytes);
  }
  auto limits = Limits();
  limits.max_bridge_new_requested_bytes = peak;
  RequestObserver exact{&context, source_bytes};
  auto admitted = detail::TestMaterializeGltfPrimitive(
      extraction, limits, Work(), {}, exact.Hooks());
  Require(admitted.primitive.has_value(), "actual peak cap must admit");
  admitted.primitive.reset();
  Require(!exact.invalid && exact.active_bytes == 0,
          "exact-cap result must clean up");
  --limits.max_bridge_new_requested_bytes;
  const auto rejected =
      mv::MaterializeGltfPrimitive(extraction, limits, Work());
  Require(rejected.status == mv::GltfMaterializationStatus::kLimitExceeded,
          "one byte below actual bridge peak must reject");
  RequireRollback(context, source_bytes);
}

template <class T>
constexpr std::string_view TypeName() {
  if constexpr (std::is_same_v<T, float>)
    return kFloat32Type;
  else if constexpr (std::is_same_v<T, double>)
    return kFloat64Type;
  else if constexpr (std::is_same_v<T, std::int8_t>)
    return kInt8Type;
  else if constexpr (std::is_same_v<T, std::int16_t>)
    return kInt16Type;
  else if constexpr (std::is_same_v<T, std::int32_t>)
    return kInt32Type;
  else if constexpr (std::is_same_v<T, std::uint8_t>)
    return kUint8Type;
  else if constexpr (std::is_same_v<T, std::uint16_t>)
    return kUint16Type;
  else if constexpr (std::is_same_v<T, std::uint32_t>)
    return kUint32Type;
  else {
    static_assert(std::is_same_v<T, std::uint64_t>);
    return kUint64Type;
  }
}
template <class T>
void Values(std::span<const T> values) {
  for (const auto value : values) {
    std::cout << ' ';
    if constexpr (std::is_same_v<T, float>)
      std::cout << std::bit_cast<std::uint32_t>(value);
    else if constexpr (std::is_same_v<T, double>)
      std::cout << std::bit_cast<std::uint64_t>(value);
    else if constexpr (std::is_signed_v<T>)
      std::cout << static_cast<std::int64_t>(value);
    else
      std::cout << static_cast<std::uint64_t>(value);
  }
  std::cout << '\n';
}
void RunFixture(const char* json_path, const char* buffer_path) {
  std::ifstream json_file(json_path, std::ios::binary);
  std::ifstream buffer_file(buffer_path, std::ios::binary);
  Require(json_file.is_open() && buffer_file.is_open(), "fixture file missing");
  std::string json{std::istreambuf_iterator<char>(json_file),
                   std::istreambuf_iterator<char>()};
  const std::string buffer{std::istreambuf_iterator<char>(buffer_file),
                           std::istreambuf_iterator<char>()};
  auto context = Context();
  auto extraction = Extract(
      json, std::as_bytes(std::span(buffer.data(), buffer.size())), context, 0);
  auto result = mv::MaterializeGltfPrimitive(extraction, Limits(), Work());
  Require(result.primitive.has_value(), "fixture adapter rejected source");
  const auto& owner = *result.primitive;
  const auto view = owner.Mesh().View();
  std::cout << kPositionRecord;
  Values(view.positions);
  std::cout << kOffsetsRecord;
  Values(view.face_offsets);
  std::cout << kCornersRecord;
  Values(view.corner_vertices);
  for (const auto& attribute : view.attributes) {
    std::visit(
        [&](auto values) {
          std::cout << kAttributeRecord << ' ' << attribute.name << ' '
                    << TypeName<typename decltype(values)::value_type>();
          Values(values);
        },
        attribute.values);
  }
  if (const auto indices = owner.SourceExtraction().SourceIndices()) {
    std::visit(
        [&](auto values) {
          std::cout << kIndicesRecord << ' '
                    << TypeName<typename decltype(values)::value_type>();
          Values(values);
        },
        indices->raw);
  }
  for (const auto& source : owner.SourceExtraction().Channels()) {
    std::cout << kNormalizedRecord << ' ' << source.semantic;
    Values(source.normalized_values);
  }
  for (const auto& mapping : owner.ChannelMappings())
    std::cout << kMappingRecord << ' ' << mapping.source_channel << ' '
              << (mapping.attribute ? std::to_string(*mapping.attribute)
                                    : std::string(kPositionRecord))
              << '\n';
  std::cout << kIdentityRecord << ' ' << owner.SourcePrimitive().MeshIndex()
            << ' ' << owner.SourcePrimitive().PrimitiveIndex() << '\n';
}
}  // namespace
int main(int argc, char** argv) {
  try {
    constexpr int kFixtureArgumentCount = 4;
    constexpr int kNoArgumentCount = 1;
    if (argc == kFixtureArgumentCount && argv[1] == kFixtureOption) {
      RunFixture(argv[2], argv[3]);
    } else {
      Require(argc == kNoArgumentCount, "unknown materialization test option");
      LimitsCancellationAndDelegation();
      ActualBridgeRequestsAndFaults();
      std::cout << "charged glTF bridge limits, cancellation, requests and "
                   "faults passed\n";
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
