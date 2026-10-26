// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "meshvale/geometry/execution.h"

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "gltf_workflow_test_internal.h"

namespace {
namespace mv = meshvale::interchange;
namespace detail = mv::gltf_detail;
namespace geo = meshvale::geometry;
constexpr std::size_t kMiB = 1024 * 1024;
constexpr std::size_t kContextBytes = 512 * kMiB;
constexpr std::size_t kSourceOperationBytes = 16 * kMiB;
constexpr std::size_t kPositionsBytes = 9 * sizeof(float);
constexpr std::size_t kFloatBytes = sizeof(float);
constexpr std::size_t kVectorComponents = 3;
constexpr std::size_t kLargeKeys = 32768;
constexpr std::size_t kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xff;
constexpr std::uint32_t kNegativeZero = 0x80000000U;
constexpr std::uint32_t kInfinity = 0x7f800000U;
constexpr float kTimeStep = 0.5F;
constexpr std::size_t kWorkers = 4;
void Require(bool value, std::string_view what) {
  if (!value) throw std::runtime_error(std::string(what));
}
void Put(std::vector<std::byte>& bytes, std::size_t offset,
         std::uint32_t value) {
  for (std::size_t byte = 0; byte < kFloatBytes; ++byte)
    bytes[offset + byte] = std::byte((value >> (byte * kByteBits)) & kByteMask);
}
mv::GltfReadLimits ReadLimits() {
  return {kMiB, 8 * kMiB, 16 * kMiB, 65536, 64,    kMiB,   256,
          256,  32,       65536,     65536, 65536, 262144, 8 * kMiB};
}
mv::GltfWorkPolicy Work(bool serial = false) {
  return {
      kMiB,
      kMiB,
      kMiB,
      16384,
      {serial ? std::uint64_t(std::numeric_limits<std::int64_t>::max()) : 16384,
       16384, 256, serial ? 1U : 64U}};
}
mv::GltfExtractionLimits Limits() {
  return {kMiB,
          kMiB,
          40 * kMiB,
          {65536, 65536, 4096, 4096, 65536, 262144, 1048576, 2 * kMiB, 4 * kMiB,
           8 * kMiB}};
}
mv::GltfExtractionRequest Request(bool translated = true) {
  return {mv::GltfTriangleRequest::kRequireTriangles,
          mv::GltfValueRequest::kRaw,
          translated ? mv::GltfProcessingProfile::kDirectTranslationTrianglesV1
                     : mv::GltfProcessingProfile::kStaticLocalTrianglesV1};
}
struct Fixture {
  explicit Fixture(std::size_t key_count = 3) : keys(key_count) {
    bytes.resize(kPositionsBytes +
                 keys * kFloatBytes * (1 + kVectorComponents));
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    for (std::size_t component = 0; component < positions.size(); ++component)
      Put(bytes, component * kFloatBytes,
          std::bit_cast<std::uint32_t>(positions[component]));
    for (std::size_t key = 0; key < keys; ++key) {
      Put(bytes, TimeOffset() + key * kFloatBytes,
          std::bit_cast<std::uint32_t>(static_cast<float>(key) * kTimeStep));
      Put(bytes, OutputOffset() + key * kVectorComponents * kFloatBytes,
          std::bit_cast<std::uint32_t>(static_cast<float>(key)));
    }
  }
  std::size_t TimeOffset() const { return kPositionsBytes; }
  std::size_t OutputOffset() const { return TimeOffset() + keys * kFloatBytes; }
  std::string Source() const {
    const auto input_storage = zero_input ? "" : ",\"bufferView\":1";
    const auto output_storage = zero_output ? "" : ",\"bufferView\":2";
    const auto time_maximum =
        !time_maximum_override.empty() ? time_maximum_override
        : bad_bounds ? "7"
                     : std::to_string(static_cast<float>(keys - 1) * kTimeStep);
    const auto sampler =
        std::string(R"({"input":1,"output":2,"interpolation":")") +
        interpolation + R"("})";
    const auto target_node =
        omit_target_node
            ? std::string{}
            : std::string(R"("node":)") + std::to_string(target) + ",";
    const auto channel = std::string(R"({"sampler":0,"target":{)") +
                         target_node + R"("path":")" + path + R"("}})";
    const auto second =
        shared ? R"(,{"sampler":0,"target":{"node":2,"path":"translation"}})"
               : "";
    const auto animation = std::string(R"({"samplers":[)") + sampler +
                           R"(],"channels":[)" + channel + second + "]}";
    const auto scene =
        disconnected_users
            ? R"(,"nodes":[{"children":[1]},{"mesh":0},{"mesh":0},{"children":[4]},{"mesh":0}],"scenes":[{"nodes":[0]},{"nodes":[2]}],"scene":0)"
            : R"(,"nodes":[{"children":[1,2]},{"mesh":0},{"mesh":0},{}],"scenes":[{"nodes":[0,3]}])";
    return std::string(
               R"({"asset":{"version":"2.0"},"buffers":[{"uri":"samples.bin","byteLength":)") +
           std::to_string(bytes.size()) +
           R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},{"buffer":0,"byteOffset":36,"byteLength":)" +
           std::to_string(keys * kFloatBytes) +
           (stride ? R"(,"byteStride":4)" : "") +
           R"(},{"buffer":0,"byteOffset":)" + std::to_string(OutputOffset()) +
           R"(,"byteLength":)" +
           std::to_string(keys * kVectorComponents * kFloatBytes) +
           R"(}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"componentType":5126,"count":)" +
           std::to_string(keys) + R"(,"type":"SCALAR","min":[)" +
           (time_minimum_override.empty() ? "0" : time_minimum_override) +
           R"(],"max":[)" + time_maximum + "]" + input_storage +
           R"(},{"componentType":5126,"count":)" + std::to_string(keys) +
           R"(,"type":"VEC3")" + output_bounds + output_storage +
           R"(}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}])" +
           scene + R"(,"animations":[)" + animation +
           (two_clips ? "," + animation : "") + "]}";
  }
  mv::GltfReadResult Read(const geo::ExecutionContext& context,
                          mv::GltfWorkPolicy work = Work()) const {
    const auto text = Source();
    const std::array<mv::SuppliedGltfResource, 1> resources{
        {{mv::GltfResourceKind::kBuffer, 0, bytes}}};
    return mv::ReadGltfAssetCapped(
        std::as_bytes(std::span(text.data(), text.size())), resources,
        {ReadLimits(), kSourceOperationBytes}, work, context);
  }
  std::size_t keys;
  std::vector<std::byte> bytes;
  std::uint64_t target = 1;
  std::string interpolation = "LINEAR";
  std::string path = "translation";
  bool shared = true;
  bool two_clips = false;
  bool zero_input = false;
  bool zero_output = false;
  bool stride = false;
  bool bad_bounds = false;
  bool omit_target_node = false;
  bool disconnected_users = false;
  std::string time_minimum_override;
  std::string time_maximum_override;
  std::string output_bounds;
};
mv::GltfAsset Capture(const Fixture& fixture,
                      const geo::ExecutionContext& context,
                      mv::GltfWorkPolicy work = Work()) {
  auto read = fixture.Read(context, work);
  Require(read.asset.has_value(),
          "genuine reader must admit fixture metadata/resources");
  Require(read.asset->SharesExecutionBudget(context), "source ledger identity");
  return *read.asset;
}
void PositiveAndOwnership() {
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  std::optional<mv::GltfPrimitiveExtraction> retained;
  {
    Fixture fixture;
    auto source = Capture(fixture, context);
    Require(source.DirectTranslationWork().partition_keys == 16384,
            "captured work getter");
    const auto old = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(false), Limits());
    Require(!old.extraction && !old.direct_translation &&
                old.diagnostic.status == mv::GltfStatus::kUnsupportedProfile,
            "default static animation guard unchanged");
    auto result = mv::ExtractGltfPrimitiveCapped(source, *source.Primitive(0),
                                                 Request(), Limits());
    Require(
        result.extraction.has_value() && result.direct_translation.has_value(),
        "translation extraction");
    Require(result.direct_translation->complete &&
                result.direct_translation->status == mv::GltfStatus::kAccepted,
            "complete genuine observation");
    Require(result.direct_translation->mesh_users == 2 &&
                result.direct_translation->affected_channels == 2 &&
                result.direct_translation->distinct_samplers == 1 &&
                result.direct_translation->keys_checked == 3 &&
                result.direct_translation->decoded_bytes_checked == 48,
            "distinct shared sampler actual counts");
    const auto proof = result.extraction->DirectTranslation();
    Require(proof &&
                std::ranges::equal(proof->mesh_users,
                                   std::array<std::uint64_t, 2>{1, 2}) &&
                std::ranges::equal(proof->node_membership,
                                   std::array<std::uint8_t, 4>{2, 1, 1, 0}),
            "all-user/ancestor membership");
    Require(proof->tracks.size() == 2 && proof->tracks[1].node_index == 2 &&
                proof->tracks[1].input_accessor == 1 &&
                proof->tracks[1].output_accessor == 2 &&
                proof->tracks[1].key_count == 3,
            "literal channel descriptors");
    Require(result.extraction->ProcessingProfile() ==
                    mv::GltfProcessingProfile::kDirectTranslationTrianglesV1 &&
                result.extraction->DirectTranslationFacts()->keys_checked ==
                    result.direct_translation->keys_checked,
            "owner/result facts agree");
    Require(result.extraction->NewRequestedBytes() <=
                Limits().max_output_requested_bytes,
            "proof and ordinary allocations fit one owning allowance");
    Require(std::ranges::equal(result.extraction->TriangleIndices(),
                               std::array<std::uint32_t, 3>{0, 1, 2}),
            "literal local triangle geometry");
    retained = result.extraction;
  }
  Require(retained->DirectTranslation()->tracks[0].key_count == 3 &&
              context.ActiveTrackedPayload() > 0,
          "strong source/proof retention after caller source release");
  retained.reset();
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "last owner releases all charges/workers");
}
void RefusalAndActualPartialWork() {
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  for (const bool output : {false, true}) {
    Fixture fixture(1);
    fixture.zero_input = !output;
    fixture.zero_output = output;
    auto source = Capture(fixture, context);
    const auto result = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits());
    Require(
        !result.extraction && result.direct_translation &&
            result.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
            result.diagnostic.code ==
                mv::GltfDiagnosticCode::kUnsupportedEnum &&
            result.diagnostic.rule_id == 316 &&
            result.direct_translation->keys_checked == 0 &&
            result.direct_translation->decoded_bytes_checked == 0,
        "legal zero storage is dense-profile refusal without invented reads");
  }
  for (const auto target : {std::uint64_t{0}, std::uint64_t{3}}) {
    Fixture fixture;
    fixture.target = target;
    fixture.shared = false;
    auto source = Capture(fixture, context);
    const auto result = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits());
    if (!target) {
      Require(
          !result.extraction && result.direct_translation &&
              result.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
              result.direct_translation->affected_channels == 1 &&
              result.direct_translation->keys_checked == 0,
          "strict ancestor track refuses before sample certification");
    } else {
      Require(result.extraction && result.direct_translation->complete &&
                  result.direct_translation->affected_channels == 0 &&
                  result.direct_translation->keys_checked == 0,
              "unrelated track preserves static selected mesh with complete "
              "inventory");
    }
  }
  for (const bool output : {false, true}) {
    Fixture fixture;
    const auto offset =
        output ? fixture.OutputOffset() + kVectorComponents * kFloatBytes
               : fixture.TimeOffset() + kFloatBytes;
    Put(fixture.bytes, offset, kInfinity);
    auto source = Capture(fixture, context);
    const auto result = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits());
    Require(!result.extraction && result.direct_translation &&
                !result.direct_translation->complete &&
                result.diagnostic.status == mv::GltfStatus::kMalformed &&
                result.diagnostic.resource_span &&
                result.diagnostic.resource_span->offset == offset &&
                result.direct_translation->keys_checked == fixture.keys,
            "actual failing scalar byte and examined-key count");
  }
  Fixture wrong_bounds;
  wrong_bounds.bad_bounds = true;
  auto source = Capture(wrong_bounds, context);
  auto result = mv::ExtractGltfPrimitiveCapped(source, *source.Primitive(0),
                                               Request(), Limits());
  Require(!result.extraction && result.direct_translation->keys_checked == 3 &&
              result.diagnostic.status == mv::GltfStatus::kMalformed,
          "real key-bound comparison");
  auto limits = Limits();
  limits.direct_translation.max_total_keys = 2;
  result = mv::ExtractGltfPrimitiveCapped(source, *source.Primitive(0),
                                          Request(), limits);
  Require(!result.extraction && result.direct_translation &&
              result.direct_translation->keys_checked == 0 &&
              result.diagnostic.code == mv::GltfDiagnosticCode::kLimitExceeded,
          "logical limit distinct from decoded work");
  limits = Limits();
  limits.max_output_requested_bytes = 1;
  result = mv::ExtractGltfPrimitiveCapped(source, *source.Primitive(0),
                                          Request(), limits);
  Require(!result.extraction && !result.direct_translation &&
              result.diagnostic.code == mv::GltfDiagnosticCode::kLimitExceeded,
          "owner allowance applied before validator seed allocation");
  limits = Limits();
  auto invalid_work = Work();
  invalid_work.direct_translation.max_jobs = 0;
  auto invalid_source = Capture(wrong_bounds, context, invalid_work);
  result = mv::ExtractGltfPrimitiveCapped(
      invalid_source, *invalid_source.Primitive(0), Request(), limits);
  Require(!result.extraction && !result.direct_translation &&
              result.diagnostic.status == mv::GltfStatus::kInvalidLimits,
          "old source capture does not invent new work defaults");
}
void WorkAndTimeBoundaries() {
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  Fixture large(kLargeKeys);
  auto source = Capture(large, context);
  const auto parallel = mv::ExtractGltfPrimitiveCapped(
      source, *source.Primitive(0), Request(), Limits());
  Require(parallel.extraction &&
              parallel.direct_translation->execution.launched_workers >= 2 &&
              parallel.direct_translation->execution.launched_workers ==
                  parallel.direct_translation->execution.joined_workers &&
              parallel.direct_translation->keys_checked == kLargeKeys &&
              parallel.direct_translation->decoded_bytes_checked ==
                  kLargeKeys * 16 + kFloatBytes,
          "real partition boundary reread/worker joins");
  auto serial_source = Capture(large, context, Work(true));
  const auto serial = mv::ExtractGltfPrimitiveCapped(
      serial_source, *serial_source.Primitive(0), Request(), Limits());
  Require(
      serial.extraction &&
          serial.direct_translation->execution.launched_workers == 0 &&
          serial.direct_translation->keys_checked ==
              parallel.direct_translation->keys_checked &&
          std::ranges::equal(serial.extraction->TriangleIndices(),
                             parallel.extraction->TriangleIndices()),
      "serial policy preserves literal result with actual zero native workers");
  Fixture repeated;
  repeated.two_clips = true;
  auto repeated_source = Capture(repeated, context);
  const auto twice = mv::ExtractGltfPrimitiveCapped(
      repeated_source, *repeated_source.Primitive(0), Request(), Limits());
  Require(twice.extraction &&
              twice.direct_translation->distinct_samplers == 2 &&
              twice.direct_translation->keys_checked == 6,
          "different clip/sampler identities sharing bytes count separately");
  Fixture negative_zero(1);
  Put(negative_zero.bytes, negative_zero.TimeOffset(), kNegativeZero);
  auto zero_source = Capture(negative_zero, context);
  Require(mv::ExtractGltfPrimitiveCapped(zero_source, *zero_source.Primitive(0),
                                         Request(), Limits())
              .extraction.has_value(),
          "first negative zero equals authored numeric zero bounds");
  Fixture equal_zero;
  Put(equal_zero.bytes, equal_zero.TimeOffset(), kNegativeZero);
  Put(equal_zero.bytes, equal_zero.TimeOffset() + kFloatBytes, 0);
  auto equal_source = Capture(equal_zero, context);
  Require(mv::ExtractGltfPrimitiveCapped(
              equal_source, *equal_source.Primitive(0), Request(), Limits())
                  .diagnostic.status == mv::GltfStatus::kMalformed,
          "opposite zero signs are not strictly increasing");
  std::stop_source stop;
  stop.request_stop();
  const auto stopped = mv::ExtractGltfPrimitiveCapped(
      source, *source.Primitive(0), Request(), Limits(), stop.get_token());
  Require(!stopped.extraction && !stopped.direct_translation &&
              stopped.diagnostic.status == mv::GltfStatus::kCanceled,
          "pre-entry stop has no invented observation");
}
struct AllocationWitness {
  std::size_t allocations = 0;
  std::size_t frees = 0;
  bool fail = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& state = *static_cast<AllocationWitness*>(opaque);
    if (state.fail) throw std::bad_alloc();
    const auto backend = detail::DefaultAllocationBackend();
    auto* block = backend.allocate(bytes, alignment, backend.state);
    ++state.allocations;
    return block;
  }
  static void Free(void* block, std::size_t bytes, std::size_t alignment,
                   void* opaque) noexcept {
    auto& state = *static_cast<AllocationWitness*>(opaque);
    const auto backend = detail::DefaultAllocationBackend();
    backend.deallocate(block, bytes, alignment, backend.state);
    ++state.frees;
  }
  detail::AllocationBackend Backend() { return {Allocate, Free, this}; }
};
void RealStopAndBackendLifetime() {
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  Fixture fixture(kLargeKeys);
  auto source = Capture(fixture, context);
  const auto baseline = context.ActiveTrackedPayload();
  AllocationWitness backend;
  {
    auto result = detail::TestExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits(), {},
        backend.Backend(), detail::DefaultNativeWorkerBackend());
    Require(
        result.extraction.has_value() && backend.allocations > backend.frees,
        "real forwarding backend remains live with retained proof/ordinary "
        "owner");
  }
  Require(backend.allocations == backend.frees &&
              context.ActiveTrackedPayload() == baseline,
          "backend state outlives final owned free");
  backend.fail = true;
  bool escaped = false;
  try {
    (void)detail::TestExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits(), {},
        backend.Backend(), detail::DefaultNativeWorkerBackend());
  } catch (const std::bad_alloc&) {
    escaped = true;
  }
  Require(escaped && context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "actual backend exception escapes without fabricated result and "
          "releases charges");
  struct StopState {
    std::stop_source stop;
    std::atomic<std::size_t> entered = 0;
  } stop;
  const detail::OperationWorkerObserver observer{
      nullptr, nullptr,
      [](std::size_t, void* opaque) noexcept {
        auto& state = *static_cast<StopState*>(opaque);
        if (state.entered.fetch_add(1) == 1) state.stop.request_stop();
      },
      &stop};
  const auto canceled = detail::TestExtractGltfPrimitiveCapped(
      source, *source.Primitive(0), Request(), Limits(), stop.stop.get_token(),
      detail::DefaultAllocationBackend(), detail::DefaultNativeWorkerBackend(),
      &observer);
  Require(!canceled.extraction && canceled.direct_translation &&
              canceled.diagnostic.status == mv::GltfStatus::kCanceled &&
              canceled.direct_translation->execution.launched_workers ==
                  canceled.direct_translation->execution.joined_workers &&
              canceled.direct_translation->keys_checked <= kLargeKeys &&
              stop.entered >= 2,
          "real worker-entry stop retains actual nonprefix work and all joins");
  Require(context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "canceled validator releases all operation ownership");
}

class CallerEnvironment {
 public:
  CallerEnvironment() {
    Require(std::fegetenv(&saved_) == 0,
            "capture caller floating-point environment");
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    saved_mxcsr_ = _mm_getcsr();
#endif
  }
  ~CallerEnvironment() {
    (void)std::fesetenv(&saved_);
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    _mm_setcsr(saved_mxcsr_);
#endif
  }

 private:
  std::fenv_t saved_{};
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  unsigned int saved_mxcsr_ = 0;
#endif
};
void SubnormalSamplesAndCallerEnvironment() {
  constexpr std::uint32_t kPositiveSubnormal = 1;
  constexpr std::uint32_t kNegativeSubnormal = 0x80000001U;
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  constexpr unsigned int kDenormalsAreZero = 0x0040;
  constexpr unsigned int kFlushToZero = 0x8000;
  constexpr unsigned int kMxcsrControlBits = 0xffc0;
  const auto original_mxcsr = _mm_getcsr();
#endif
  const auto original_rounding = std::fegetround();
  {
    CallerEnvironment restore;
    Require(std::fesetround(FE_UPWARD) == 0, "set caller directed rounding");
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    _mm_setcsr(_mm_getcsr() | kDenormalsAreZero | kFlushToZero);
    const auto hostile_mxcsr = _mm_getcsr();
#endif
    const auto hostile_rounding = std::fegetround();
    geo::ExecutionContext context({kWorkers, 1, kContextBytes});
    Fixture fixture(2);
    fixture.time_maximum_override = "1e-45";
    fixture.output_bounds = R"(,"min":[-1e-45,-0,0],"max":[1e-45,0,0])";
    Put(fixture.bytes, fixture.TimeOffset(), kNegativeZero);
    Put(fixture.bytes, fixture.TimeOffset() + kFloatBytes, kPositiveSubnormal);
    Put(fixture.bytes, fixture.OutputOffset(), kNegativeSubnormal);
    Put(fixture.bytes, fixture.OutputOffset() + kFloatBytes, kNegativeZero);
    Put(fixture.bytes, fixture.OutputOffset() + kVectorComponents * kFloatBytes,
        kPositiveSubnormal);
    auto source = Capture(fixture, context);
    const auto result = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits());
    Require(result.extraction && result.direct_translation->complete &&
                result.direct_translation->keys_checked == 2 &&
                result.direct_translation->decoded_bytes_checked == 32,
            "subnormal time/order/translation/extrema remain finite "
            "represented values under caller DAZ/FTZ");
    Require(std::ranges::equal(source.Resources()[0].bytes, fixture.bytes),
            "raw signed-zero/subnormal source bits remain unchanged");
    Require(std::fegetround() == hostile_rounding,
            "native calls preserve caller directed rounding");
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    Require((_mm_getcsr() & kMxcsrControlBits) ==
                (hostile_mxcsr & kMxcsrControlBits),
            "native calls preserve caller MXCSR control modes");
#endif
  }
  Require(std::fegetround() == original_rounding,
          "test guard restores original rounding");
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  Require(_mm_getcsr() == original_mxcsr, "test guard restores original MXCSR");
#endif
}

void AllChannelMetadataAndBoundLocations() {
  constexpr std::uint64_t kCanonicalMetadataBytes = 4 + 16 * 2 + 64;
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  for (const bool absent : {false, true}) {
    Fixture fixture;
    fixture.shared = false;
    fixture.target = 3;
    fixture.omit_target_node = absent;
    auto source = Capture(fixture, context);
    for (const auto cap :
         {kCanonicalMetadataBytes - 1, kCanonicalMetadataBytes}) {
      auto limits = Limits();
      limits.direct_translation.max_metadata_bytes = cap;
      const auto result = mv::ExtractGltfPrimitiveCapped(
          source, *source.Primitive(0), Request(), limits);
      Require(result.direct_translation &&
                  result.direct_translation->scanned_channels == 1 &&
                  result.direct_translation->affected_channels == 0 &&
                  result.direct_translation->keys_checked == 0,
              "all scanned unrelated/absent-node channels consume metadata "
              "allowance");
      if (cap == kCanonicalMetadataBytes)
        Require(result.extraction && result.direct_translation->complete,
                "inclusive canonical all-channel metadata cap");
      else
        Require(!result.extraction &&
                    result.diagnostic.status ==
                        mv::GltfStatus::kUnsupportedProfile &&
                    result.diagnostic.code ==
                        mv::GltfDiagnosticCode::kLimitExceeded &&
                    result.diagnostic.rule_id == 317,
                "one-under canonical all-channel metadata cap");
    }
  }
  for (const bool output : {false, true})
    for (const bool minimum : {false, true}) {
      Fixture fixture;
      if (output)
        fixture.output_bounds =
            minimum ? R"(,"min":[-7,0,0])" : R"(,"max":[7,0,0])";
      else if (minimum)
        fixture.time_minimum_override = "-7";
      else
        fixture.bad_bounds = true;
      const auto text = fixture.Source();
      const std::string_view prefix = minimum ? R"("min":[)" : R"("max":[)";
      const auto token = std::string(prefix) + (minimum ? "-7" : "7");
      const auto start = text.find(token);
      Require(start != std::string::npos,
              "literal authored metadata token exists");
      auto source = Capture(fixture, context);
      const auto result = mv::ExtractGltfPrimitiveCapped(
          source, *source.Primitive(0), Request(), Limits());
      Require(
          !result.extraction && result.direct_translation &&
              result.direct_translation->keys_checked == 3 &&
              result.diagnostic.status == mv::GltfStatus::kMalformed &&
              result.diagnostic.code == mv::GltfDiagnosticCode::kBinaryValue &&
              result.diagnostic.json_span &&
              result.diagnostic.json_span->offset == start + prefix.size() &&
              result.diagnostic.json_span->length == (minimum ? 2U : 1U) &&
              !result.diagnostic.resource_span,
          "input/output bound mismatch names exact metadata number, not "
          "invented binary key");
    }
  Fixture fixture;
  auto work = Work();
  work.direct_translation.parallel_min_keys = 1;
  work.direct_translation.partition_keys = 1;
  work.direct_translation.max_jobs = 4;
  auto source = Capture(fixture, context, work);
  const auto result = mv::ExtractGltfPrimitiveCapped(
      source, *source.Primitive(0), Request(), Limits());
  Require(result.extraction &&
              result.direct_translation->execution.planned_jobs == 3 &&
              result.direct_translation->keys_checked == 3 &&
              result.direct_translation->decoded_bytes_checked == 56 &&
              result.direct_translation->execution.launched_workers ==
                  result.direct_translation->execution.joined_workers,
          "direct segment dispatch preserves actual partition-boundary rereads "
          "and joins");
}

void InactiveAndUnreachableUsers() {
  geo::ExecutionContext context({kWorkers, 1, kContextBytes});
  Fixture fixture;
  fixture.disconnected_users = true;
  fixture.target = 4;
  auto source = Capture(fixture, context);
  Require(
      source.Nodes().size() == 5 && source.Scenes().size() == 2 &&
          source.Scenes()[0].roots.size() == 1 &&
          source.Scenes()[0].roots[0] == 0 &&
          source.Scenes()[1].roots.size() == 1 &&
          source.Scenes()[1].roots[0] == 2 && source.Nodes()[4].parent == 3,
      "actual admitted default/inactive scene and unreachable ancestor layout");
  const auto accepted = mv::ExtractGltfPrimitiveCapped(
      source, *source.Primitive(0), Request(), Limits());
  Require(
      accepted.extraction && accepted.direct_translation->complete,
      "direct translation on unreachable user and inactive user is admitted");
  const auto proof = accepted.extraction->DirectTranslation();
  Require(proof &&
              std::ranges::equal(proof->mesh_users,
                                 std::array<std::uint64_t, 3>{1, 2, 4}) &&
              std::ranges::equal(proof->node_membership,
                                 std::array<std::uint8_t, 5>{2, 1, 1, 2, 1}) &&
              proof->tracks.size() == 2 && proof->tracks[0].node_index == 4 &&
              proof->tracks[1].node_index == 2,
          "literal membership includes every authored user and unreachable "
          "strict ancestor");
  for (const auto target :
       {std::uint64_t{2}, std::uint64_t{4}, std::uint64_t{3}}) {
    Fixture unsupported;
    unsupported.disconnected_users = true;
    unsupported.shared = false;
    unsupported.target = target;
    unsupported.path = target == 3 ? "translation" : "scale";
    auto admitted = Capture(unsupported, context);
    const auto result = mv::ExtractGltfPrimitiveCapped(
        admitted, *admitted.Primitive(0), Request(), Limits());
    Require(
        !result.extraction && result.direct_translation &&
            result.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
            result.diagnostic.code ==
                mv::GltfDiagnosticCode::kUnsupportedEnum &&
            result.direct_translation->mesh_users == 3 &&
            result.direct_translation->affected_channels == 1 &&
            result.direct_translation->keys_checked == 0,
        "core-compatible inactive/unreachable scale or unreachable ancestor "
        "track refuses without hidden decoding");
  }
}
using ChargedByteOwner = std::vector<std::byte, detail::Allocator<std::byte>>;
ChargedByteOwner OccupyWithOwnedBytes(const geo::ExecutionContext& context) {
  constexpr std::size_t kRemainingBytes = 1;
  constexpr std::byte kPayloadValue{0x5a};
  const auto before = context.ActiveTrackedPayload();
  const auto available = context.TrackedPayloadBudget() - before;
  const auto prefix = detail::AllocationRequestBytes(1, alignof(std::byte)) - 1;
  Require(available > prefix + kRemainingBytes,
          "owned pressure construction has checked headroom");
  const auto count = available - prefix - kRemainingBytes;
  const auto charge = detail::AllocationRequestBytes(count, alignof(std::byte));
  Require(charge == available - kRemainingBytes,
          "exact aligned native allocation request");
  const detail::AllocationContext allocation(context, {});
  ChargedByteOwner owner(count, kPayloadValue,
                         detail::Allocator<std::byte>(allocation));
  Require(owner.size() == count && owner.capacity() == count &&
              owner.front() == kPayloadValue && owner.back() == kPayloadValue &&
              context.ActiveTrackedPayload() - before == charge &&
              context.TrackedPayloadBudget() - context.ActiveTrackedPayload() ==
                  kRemainingBytes &&
              owner.get_allocator().Context().SharesExecutionBudget(allocation),
          "real owned payload and allocator/ledger identity verified before "
          "workflow expectation");
  return owner;
}
void OccupiedSharedLedgerAndReleaseRetry() {
  constexpr std::size_t kLedgerBytes = 2 * kMiB;
  geo::ExecutionContext context({kWorkers, 1, kLedgerBytes});
  Fixture fixture;
  auto source = Capture(fixture, context);
  const auto baseline = context.ActiveTrackedPayload();
  Require(baseline > 0 && source.SharesExecutionBudget(context),
          "actual source retention uses selected ledger");
  {
    const auto result = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits());
    Require(result.extraction && result.direct_translation->complete,
            "roomy operation caps work before actual pressure");
  }
  Require(context.ActiveTrackedPayload() == baseline,
          "baseline extraction releases its proof and ordinary owners");
  auto capped = Limits();
  capped.max_output_requested_bytes = 1;
  const auto operation_limit = mv::ExtractGltfPrimitiveCapped(
      source, *source.Primitive(0), Request(), capped);
  Require(!operation_limit.extraction && !operation_limit.direct_translation &&
              operation_limit.diagnostic.status ==
                  mv::GltfStatus::kUnsupportedProfile &&
              operation_limit.diagnostic.code ==
                  mv::GltfDiagnosticCode::kLimitExceeded,
          "owning operation cap refusal is distinct from root shared-ledger "
          "refusal");
  {
    geo::ExecutionContext foreign({kWorkers, 1, kLedgerBytes});
    const auto owner = OccupyWithOwnedBytes(foreign);
    Require(!source.SharesExecutionBudget(foreign) &&
                context.ActiveTrackedPayload() == baseline,
            "foreign owned payload cannot occupy source ledger");
    const auto result = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits());
    Require(
        result.extraction && result.direct_translation->complete &&
            !owner.empty(),
        "matched foreign-ledger pressure leaves source extraction admitted");
  }
  Require(context.ActiveTrackedPayload() == baseline,
          "foreign control releases source extraction ownership");
  {
    const auto owner = OccupyWithOwnedBytes(context);
    const auto occupied = context.ActiveTrackedPayload();
    const auto result = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits());
    Require(
        !result.extraction && !result.direct_translation &&
            result.diagnostic.status == mv::GltfStatus::kBudgetExceeded &&
            result.diagnostic.code == mv::GltfDiagnosticCode::kPayloadBudget &&
            context.ActiveTrackedPayload() == occupied &&
            context.ActiveWorkers() == 0 && !owner.empty(),
        "actual same-ledger owned storage refuses seed before helper without "
        "invented counters");
    Require(std::ranges::equal(source.Resources()[0].bytes, fixture.bytes),
            "failed pressure extraction preserves genuine source bytes");
  }
  Require(context.ActiveTrackedPayload() == baseline,
          "last pressure owner releases exact real charge");
  {
    const auto retry = mv::ExtractGltfPrimitiveCapped(
        source, *source.Primitive(0), Request(), Limits());
    Require(retry.extraction && retry.direct_translation->complete &&
                retry.direct_translation->keys_checked == 3 &&
                retry.direct_translation->decoded_bytes_checked == 48 &&
                std::ranges::equal(retry.extraction->TriangleIndices(),
                                   std::array<std::uint32_t, 3>{0, 1, 2}),
            "unchanged operation caps and literal geometry succeed after "
            "actual pressure release");
  }
  Require(context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "retry releases all validator/proof/ordinary ownership and worker "
          "leases");
}

}  // namespace
int main() {
  try {
    PositiveAndOwnership();
    RefusalAndActualPartialWork();
    WorkAndTimeBoundaries();
    RealStopAndBackendLifetime();
    SubnormalSamplesAndCallerEnvironment();
    AllChannelMetadataAndBoundLocations();
    InactiveAndUnreachableUsers();
    OccupiedSharedLedgerAndReleaseRetry();
    std::cout << "direct translation extraction foundation passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
