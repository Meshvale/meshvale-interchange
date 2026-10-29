// SPDX-License-Identifier: Apache-2.0
#include "gltf_accessor.h"

#include <meshvale/geometry/execution.h>

#include <array>
#include <atomic>
#include <bit>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace mv = meshvale::interchange;
namespace detail = mv::gltf_detail;
void Require(bool condition) {
  if (!condition) throw std::runtime_error("accessor assertion failed");
}
mv::GltfExtractionLimits Limits() { return {100000, 100000, 10000000}; }
mv::GltfWorkPolicy Policy() { return {1, 1, 1, 1}; }
void LayoutAndSparse() {
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  detail::AllocationContext allocation(context, {});
  // MAT3 UBYTE columns padded to four bytes; final trailing padding omitted.
  const std::array<std::byte, 23> matrix{
      std::byte{1},  std::byte{2},  std::byte{3},  std::byte{255},
      std::byte{4},  std::byte{5},  std::byte{6},  std::byte{255},
      std::byte{7},  std::byte{8},  std::byte{9},  std::byte{255},
      std::byte{10}, std::byte{11}, std::byte{12}, std::byte{255},
      std::byte{13}, std::byte{14}, std::byte{15}, std::byte{255},
      std::byte{16}, std::byte{17}, std::byte{18}};
  detail::AccessorSource source;
  source.component_type = mv::GltfComponentType::kUint8;
  source.shape = mv::GltfAccessorShape::kMat3;
  source.count = 2;
  source.base = detail::AccessorByteView{0, 0, 0, matrix};
  detail::AccessorExecutionStats stats;
  auto result = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                       context, allocation, {}, &stats);
  Require(result.values.has_value());
  const auto& values =
      std::get<detail::ChargedValues<std::uint8_t>>(result.values->raw);
  for (std::size_t i = 0; i < values.size(); ++i) Require(values[i] == i + 1);
  Require(stats.launched == 2 && stats.joined == 2);
  source.base->bytes = std::span(matrix).first(22);
  Require(detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                 allocation, {})
              .diagnostic.status == mv::GltfStatus::kMalformed);
  const std::array<std::byte, 1> selector{std::byte{1}};
  source.base.reset();
  source.sparse =
      detail::AccessorSparseSource{1,
                                   {1, 0, 0, selector},
                                   0,
                                   mv::GltfComponentType::kUint8,
                                   {2, 0, 0, std::span(matrix).first(11)},
                                   0};
  result = detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                  allocation, {});
  Require(result.values.has_value());
  const auto& sparse =
      std::get<detail::ChargedValues<std::uint8_t>>(result.values->raw);
  for (std::size_t i = 0; i < 9; ++i) {
    Require(sparse[i] == 0);
    Require(sparse[i + 9] == i + 1);
  }
  Require(result.values->sparse_destinations[0] == 1);
}
void RawAndNormalized() {
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  detail::AllocationContext allocation(context, {});
  const std::array<std::byte, 4> bytes{std::byte{128}, std::byte{127},
                                       std::byte{255}, std::byte{0}};
  detail::AccessorSource source;
  source.component_type = mv::GltfComponentType::kInt8;
  source.count = 4;
  source.normalized = true;
  source.base = detail::AccessorByteView{0, 0, 0, bytes};
  auto result = detail::DecodeAccessor(source, true, Limits(), Policy(),
                                       context, allocation, {});
  Require(result.values.has_value());
  const auto& raw =
      std::get<detail::ChargedValues<std::int8_t>>(result.values->raw);
  Require(raw[0] == -128 && raw[1] == 127 && raw[2] == -1 && raw[3] == 0);
  Require(result.values->normalized[0] == -1 &&
          result.values->normalized[1] == 1);
  Require(result.values->normalized[2] == -1.0 / 127.0);
  auto occupied = meshvale::geometry::TryReserveWorkers(context, 4);
  detail::AccessorExecutionStats stats;
  auto serial = detail::DecodeAccessor(source, true, Limits(), Policy(),
                                       context, allocation, {}, &stats);
  Require(serial.values && stats.launched == 0 && stats.joined == 0);
  Require(serial.values->normalized == result.values->normalized);
  Require(serial.values->raw == result.values->raw);
  source.normalized = false;
  auto raw_only = detail::DecodeAccessor(source, true, Limits(), Policy(),
                                         context, allocation, {});
  Require(raw_only.values && raw_only.values->normalized.empty());
  // Exact float bit endpoints: -0 and smallest positive binary32 subnormal.
  const std::array<std::byte, 8> float_bytes{
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{128},
      std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
  source.component_type = mv::GltfComponentType::kFloat32;
  source.count = 2;
  source.base->bytes = float_bytes;
  auto floats = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                       context, allocation, {});
  Require(floats.values.has_value());
  const auto& raw_float =
      std::get<detail::ChargedValues<float>>(floats.values->raw);
  Require(std::bit_cast<std::uint32_t>(raw_float[0]) == 0x80000000U);
  Require(std::bit_cast<std::uint32_t>(raw_float[1]) == 1);
}
void DeformationRawWords() {
  constexpr std::size_t kRequestBytes = 100000;
  constexpr std::size_t kWorkers = 4;
  meshvale::geometry::ExecutionContext context({kWorkers, 1, kRequestBytes});
  {
    const detail::AllocationContext root(context, {});
    const auto parent = root.WithRequestLimit(kRequestBytes);
    const auto allocation = parent.WithAdditionalRequestLimit(kRequestBytes);
    // Independent wire inputs: -0, minimum subnormal, signaling NaN and an
    // arbitrary quiet-NaN payload. No float lvalue/arithmetic reads these
    // words.
    const std::array<std::byte, 16> bytes{
        std::byte{0},    std::byte{0},    std::byte{0},    std::byte{128},
        std::byte{1},    std::byte{0},    std::byte{0},    std::byte{0},
        std::byte{1},    std::byte{0},    std::byte{128},  std::byte{127},
        std::byte{0x45}, std::byte{0x23}, std::byte{0xc1}, std::byte{0x7f}};
    constexpr std::array<std::uint32_t, 4> kExpected{0x80000000U, 1,
                                                     0x7f800001U, 0x7fc12345U};
    detail::AccessorSource source;
    source.component_type = mv::GltfComponentType::kFloat32;
    source.count = kExpected.size();
    source.base = detail::AccessorByteView{0, 0, 0, bytes};
    const auto ordinary = detail::DecodeAccessor(
        source, false, Limits(), Policy(), context, allocation, {});
    Require(!ordinary.values &&
            ordinary.diagnostic.status == mv::GltfStatus::kMalformed &&
            ordinary.diagnostic.code == mv::GltfDiagnosticCode::kBinaryValue);
    source.retain_float_words = true;
    detail::AccessorExecutionStats stats;
    auto raw = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                      context, allocation, {}, &stats);
    Require(raw.values && raw.values->float_words &&
            raw.values->normalized.empty() && stats.launched == kWorkers &&
            stats.joined == kWorkers);
    const auto& words =
        std::get<detail::ChargedValues<std::uint32_t>>(raw.values->raw);
    Require(words.size() == kExpected.size());
    for (std::size_t i = 0; i < kExpected.size(); ++i)
      Require(words[i] == kExpected[i]);
    Require(detail::DecodeAccessor(source, true, Limits(), Policy(), context,
                                   allocation, {})
                .diagnostic.status == mv::GltfStatus::kInvalidRequest);
    const std::array<std::byte, 1> selector{std::byte{0}};
    const std::array<std::byte, 4> replacement{std::byte{0}, std::byte{0},
                                               std::byte{128}, std::byte{63}};
    source.sparse = detail::AccessorSparseSource{1,
                                                 {1, 0, 0, selector},
                                                 0,
                                                 mv::GltfComponentType::kUint8,
                                                 {2, 0, 0, replacement},
                                                 0};
    auto sparse = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                         context, allocation, {});
    Require(sparse.values && sparse.values->float_words &&
            sparse.values->sparse_destinations.size() == 1 &&
            sparse.values->sparse_destinations[0] == 0);
    const auto& sparse_words =
        std::get<detail::ChargedValues<std::uint32_t>>(sparse.values->raw);
    Require(sparse_words[0] == 0x3f800000U && sparse_words[2] == kExpected[2] &&
            sparse_words[3] == kExpected[3]);
    source.component_type = mv::GltfComponentType::kUint8;
    Require(detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                   allocation, {})
                .diagnostic.status == mv::GltfStatus::kInvalidRequest);
    source.retain_float_words = false;
    source.component_type = mv::GltfComponentType::kUint32;
    source.count = 2;
    source.sparse.reset();
    source.base->stride = 8;
    source.base->target = detail::ViewTarget::kVertex;
    source.role = detail::AccessorRole::kVertex;
    Require(!detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                    allocation, {})
                 .values);
    source.role = detail::AccessorRole::kGeneric;
    Require(!detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                    allocation, {})
                 .values);
    source.role = detail::AccessorRole::kDeformationVertex;
    const auto custom = detail::DecodeAccessor(
        source, false, Limits(), Policy(), context, allocation, {});
    Require(custom.values && !custom.values->float_words);
    const auto& custom_words =
        std::get<detail::ChargedValues<std::uint32_t>>(custom.values->raw);
    Require(custom_words.size() == 2 && custom_words[0] == kExpected[0] &&
            custom_words[1] == kExpected[2]);
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0);
  std::cout << "deformation raw FLOAT words/sparse/legacy refusal PASS; "
               "role admission remains separate; payload=0 workers=0\n";
}
void DeformationVertexAlignment() {
  meshvale::geometry::ExecutionContext context({4, 1, 100000});
  {
    const detail::AllocationContext allocation(context, {});
    const std::array<std::byte, 20> wire{};
    for (const auto type :
         {mv::GltfComponentType::kUint8, mv::GltfComponentType::kUint16}) {
      detail::AccessorSource source;
      source.component_type = type;
      source.shape = mv::GltfAccessorShape::kVec4;
      source.count = 2;
      source.base = detail::AccessorByteView{0, 0, 0, wire};
      source.role = detail::AccessorRole::kVertex;
      Require(detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                     allocation, {})
                  .values.has_value());
      source.role = detail::AccessorRole::kDeformationVertex;
      const auto absent_stride = detail::DecodeAccessor(
          source, false, Limits(), Policy(), context, allocation, {});
      Require(!absent_stride.values &&
              absent_stride.diagnostic.status == mv::GltfStatus::kMalformed &&
              absent_stride.diagnostic.code ==
                  mv::GltfDiagnosticCode::kBinaryLayout);
      source.base->stride = type == mv::GltfComponentType::kUint8 ? 4 : 8;
      Require(detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                     allocation, {})
                  .values.has_value());
      source.base_offset = 2;
      Require(!detail::DecodeAccessor(source, false, Limits(), Policy(),
                                      context, allocation, {})
                   .values);
    }
    // No base vertex stream exists here. Sparse replacement data remains
    // tightly packed; the new base-stride condition cannot invent a stride.
    const std::array<std::byte, 1> selector{std::byte{1}};
    const std::array<std::byte, 4> replacement{std::byte{1}, std::byte{2},
                                               std::byte{3}, std::byte{4}};
    detail::AccessorSource sparse;
    sparse.component_type = mv::GltfComponentType::kUint8;
    sparse.shape = mv::GltfAccessorShape::kVec4;
    sparse.count = 2;
    sparse.role = detail::AccessorRole::kDeformationVertex;
    sparse.sparse = detail::AccessorSparseSource{1,
                                                 {0, 0, 0, selector},
                                                 0,
                                                 mv::GltfComponentType::kUint8,
                                                 {1, 0, 0, replacement},
                                                 0};
    Require(detail::DecodeAccessor(sparse, false, Limits(), Policy(), context,
                                   allocation, {})
                .values.has_value());
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0);
}
void PreparedRawRows() {
  meshvale::geometry::ExecutionContext context({4, 1, 100000});
  {
    std::stop_source stop;
    const detail::AllocationContext allocation(context, stop.get_token());
    const std::array<std::byte, 16> bytes{
        std::byte{1},  std::byte{2},  std::byte{3},  std::byte{4},
        std::byte{5},  std::byte{6},  std::byte{7},  std::byte{8},
        std::byte{9},  std::byte{10}, std::byte{11}, std::byte{12},
        std::byte{13}, std::byte{14}, std::byte{15}, std::byte{16}};
    detail::AccessorSource source;
    source.component_type = mv::GltfComponentType::kUint16;
    source.shape = mv::GltfAccessorShape::kVec2;
    source.count = 4;
    source.role = detail::AccessorRole::kDeformationVertex;
    source.base = detail::AccessorByteView{0, 0, 0, bytes, 4};
    struct Visits {
      std::size_t scalars = 0, references = 0, bytes = 0;
      std::stop_source* cancel = nullptr;
    } visits;
    detail::AccessorCopyWork work{
        [](void* state) {
          auto& v = *static_cast<Visits*>(state);
          ++v.scalars;
          if (v.cancel && v.scalars == 2) v.cancel->request_stop();
        },
        [](void* state) { ++static_cast<Visits*>(state)->references; },
        [](std::size_t count, void* state) {
          static_cast<Visits*>(state)->bytes += count;
        },
        &visits};
    auto prepared = detail::PrepareAccessorDecode(source, false, Limits(),
                                                  allocation, &work);
    Require(prepared.values && visits.scalars == 0 && visits.bytes == 0);
    const auto& values =
        std::get<detail::ChargedValues<std::uint16_t>>(prepared.values->raw);
    for (const auto value : values) Require(value == 0);
    Require(detail::CopyPreparedAccessorRows(source, *prepared.values, 0, 2,
                                             allocation, stop.get_token(),
                                             &work)
                .status == mv::GltfStatus::kAccepted);
    Require(detail::CopyPreparedAccessorRows(source, *prepared.values, 2, 4,
                                             allocation, stop.get_token(),
                                             &work)
                .status == mv::GltfStatus::kAccepted);
    constexpr std::array<std::uint16_t, 8> kExpected{
        0x0201, 0x0403, 0x0605, 0x0807, 0x0a09, 0x0c0b, 0x0e0d, 0x100f};
    for (std::size_t i = 0; i < values.size(); ++i)
      Require(values[i] == kExpected[i]);
    Require(visits.scalars == 8 && visits.references == 4 &&
            visits.bytes == 16);
    Require(detail::CopyPreparedAccessorRows(source, *prepared.values, 0, 5,
                                             allocation, stop.get_token(),
                                             &work)
                .status == mv::GltfStatus::kInvalidRequest);
    auto partial =
        detail::PrepareAccessorDecode(source, false, Limits(), allocation);
    Require(partial.values.has_value());
    visits = {};
    visits.cancel = &stop;
    const auto canceled = detail::CopyPreparedAccessorRows(
        source, *partial.values, 0, 4, allocation, stop.get_token(), &work);
    Require(canceled.status == mv::GltfStatus::kCanceled &&
            visits.scalars == 2 && visits.bytes == 4);
    const auto& partial_values =
        std::get<detail::ChargedValues<std::uint16_t>>(partial.values->raw);
    Require(partial_values[0] == 0x0201 && partial_values[1] == 0x0403 &&
            partial_values[2] == 0);
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0);
}
void NormalizedRoundingEnvironment() {
  struct EnvironmentRestore {
    std::fenv_t saved;
    EnvironmentRestore() { Require(std::fegetenv(&saved) == 0); }
    ~EnvironmentRestore() { std::fesetenv(&saved); }
  } restore;
  struct Case {
    mv::GltfComponentType type;
    std::array<std::int64_t, 8> values;
    std::array<std::uint64_t, 8> expected;
  };
  // Expected bits independently rounded from Python Fraction(value, divisor).
  const std::array<Case, 4> cases{{
      {mv::GltfComponentType::kInt8,
       {-128, -127, -63, -1, 0, 1, 63, 127},
       {0xbff0000000000000ULL, 0xbff0000000000000ULL, 0xbfdfbf7efdfbf7f0ULL,
        0xbf80204081020408ULL, 0, 0x3f80204081020408ULL, 0x3fdfbf7efdfbf7f0ULL,
        0x3ff0000000000000ULL}},
      {mv::GltfComponentType::kUint8,
       {0, 1, 2, 63, 127, 128, 254, 255},
       {0, 0x3f70101010101010ULL, 0x3f80101010101010ULL, 0x3fcf9f9f9f9f9fa0ULL,
        0x3fdfdfdfdfdfdfe0ULL, 0x3fe0101010101010ULL, 0x3fefdfdfdfdfdfe0ULL,
        0x3ff0000000000000ULL}},
      {mv::GltfComponentType::kInt16,
       {-32768, -32767, -16383, -1, 0, 1, 16383, 32767},
       {0xbff0000000000000ULL, 0xbff0000000000000ULL, 0xbfdfffbfff7fff00ULL,
        0xbf00002000400080ULL, 0, 0x3f00002000400080ULL, 0x3fdfffbfff7fff00ULL,
        0x3ff0000000000000ULL}},
      {mv::GltfComponentType::kUint16,
       {0, 1, 2, 16383, 32767, 32768, 65534, 65535},
       {0, 0x3ef0001000100010ULL, 0x3f00001000100010ULL, 0x3fcfff9fff9fffa0ULL,
        0x3fdfffdfffdfffe0ULL, 0x3fe0001000100010ULL, 0x3fefffdfffdfffe0ULL,
        0x3ff0000000000000ULL}},
  }};
  for (const auto rounding :
       {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    Require(std::fesetround(rounding) == 0);
    for (const auto& test : cases) {
      meshvale::geometry::ExecutionContext context({4, 1, 10000000});
      {
        detail::AllocationContext allocation(context, {});
        detail::AccessorSource source;
        source.component_type = test.type;
        source.count = test.values.size();
        source.normalized = true;
        const auto width =
            detail::DescribeAccessorLayout(test.type, source.shape)
                .component_bytes;
        std::vector<std::byte> bytes(source.count * width);
        for (std::size_t row = 0; row < source.count; ++row)
          for (std::size_t byte = 0; byte < width; ++byte)
            bytes[row * width + byte] = static_cast<std::byte>(
                static_cast<std::uint64_t>(test.values[row]) >> (byte * 8));
        source.base = detail::AccessorByteView{0, 0, 0, bytes};
        detail::AccessorExecutionStats stats;
        auto parallel = detail::DecodeAccessor(source, true, Limits(), Policy(),
                                               context, allocation, {}, &stats);
        Require(parallel.values && stats.launched == 4 && stats.joined == 4);
        auto occupied = meshvale::geometry::TryReserveWorkers(context, 4);
        auto serial = detail::DecodeAccessor(source, true, Limits(), Policy(),
                                             context, allocation, {}, &stats);
        Require(serial.values && stats.launched == 0 && stats.joined == 0);
        Require(serial.values->raw == parallel.values->raw);
        for (std::size_t row = 0; row < source.count; ++row) {
          Require(std::bit_cast<std::uint64_t>(
                      parallel.values->normalized[row]) == test.expected[row]);
          Require(std::bit_cast<std::uint64_t>(
                      serial.values->normalized[row]) == test.expected[row]);
        }
        Require(std::fegetround() == rounding);
      }
      Require(context.ActiveWorkers() == 0 &&
              context.ActiveTrackedPayload() == 0);
    }
  }
}
void OtherMatrixLayouts() {
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  detail::AllocationContext allocation(context, {});
  const std::array<std::byte, 14> byte_matrix{
      std::byte{1}, std::byte{2}, std::byte{250}, std::byte{251},
      std::byte{3}, std::byte{4}, std::byte{250}, std::byte{251},
      std::byte{5}, std::byte{6}, std::byte{250}, std::byte{251},
      std::byte{7}, std::byte{8}};
  detail::AccessorSource source;
  source.component_type = mv::GltfComponentType::kUint8;
  source.shape = mv::GltfAccessorShape::kMat2;
  source.count = 2;
  source.base = detail::AccessorByteView{0, 0, 0, byte_matrix};
  auto result = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                       context, allocation, {});
  Require(result.values.has_value());
  const auto& values =
      std::get<detail::ChargedValues<std::uint8_t>>(result.values->raw);
  for (std::size_t i = 0; i < values.size(); ++i) Require(values[i] == i + 1);
  std::array<std::byte, 46> short_matrix{};
  const std::array<std::size_t, 18> addresses{
      0, 2, 4, 8, 10, 12, 16, 18, 20, 24, 26, 28, 32, 34, 36, 40, 42, 44};
  for (std::size_t i = 0; i < addresses.size(); ++i)
    short_matrix[addresses[i]] = static_cast<std::byte>(i + 1);
  source.component_type = mv::GltfComponentType::kUint16;
  source.shape = mv::GltfAccessorShape::kMat3;
  source.base->bytes = short_matrix;
  auto shorts = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                       context, allocation, {});
  Require(shorts.values.has_value());
  const auto& short_values =
      std::get<detail::ChargedValues<std::uint16_t>>(shorts.values->raw);
  for (std::size_t i = 0; i < short_values.size(); ++i)
    Require(short_values[i] == i + 1);
  source.base->bytes = std::span(short_matrix).first(45);
  Require(detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                 allocation, {})
              .diagnostic.status == mv::GltfStatus::kMalformed);
}
void FailureAndLifetime() {
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  detail::AllocationContext allocation(context, {});
  detail::AccessorSource source;
  source.component_type = mv::GltfComponentType::kUint32;
  source.role = detail::AccessorRole::kIndices;
  source.count = 3;
  // UINT32 remains integer at and beyond the binary32 precision boundary.
  const std::array<std::byte, 12> bytes{
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1},
      std::byte{1}, std::byte{0}, std::byte{0}, std::byte{1}};
  source.base = detail::AccessorByteView{0, 0, 0, bytes};
  {
    auto result = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                         context, allocation, {});
    Require(result.values.has_value());
    const auto& values =
        std::get<detail::ChargedValues<std::uint32_t>>(result.values->raw);
    Require(values[1] == 16777216U && values[2] == 16777217U);
    Require(context.ActiveTrackedPayload() > 0);
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0);
  const std::array<std::byte, 4> restart{std::byte{255}, std::byte{255},
                                         std::byte{255}, std::byte{255}};
  source.count = 1;
  source.base->bytes = restart;
  Require(detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                 allocation, {})
              .diagnostic.code == mv::GltfDiagnosticCode::kIndexRole);
  source.count = std::numeric_limits<std::size_t>::max();
  source.base.reset();
  auto limited = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                        context, allocation, {});
  Require(limited.diagnostic.status == mv::GltfStatus::kUnsupportedProfile);
  source.count = 3;
  std::stop_source canceled;
  canceled.request_stop();
  detail::AllocationContext canceled_allocation(context, canceled.get_token());
  Require(detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                 canceled_allocation, canceled.get_token())
              .diagnostic.status == mv::GltfStatus::kCanceled);
  auto invalid_limits = Limits();
  invalid_limits.max_components = 0;
  Require(detail::DecodeAccessor(source, false, invalid_limits, Policy(),
                                 context, allocation, {})
              .diagnostic.status == mv::GltfStatus::kInvalidLimits);
  meshvale::geometry::ExecutionContext small({2, 1, 1});
  detail::AllocationContext small_allocation(small, {});
  Require(detail::DecodeAccessor(source, false, Limits(), Policy(), small,
                                 small_allocation, {})
              .diagnostic.status == mv::GltfStatus::kBudgetExceeded);
  Require(small.ActiveTrackedPayload() == 0 && small.ActiveWorkers() == 0);
}
void WorkerFailuresAndCancellation() {
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  detail::AllocationContext allocation(context, {});
  detail::AccessorSource source;
  source.count = 4;
  detail::AccessorExecutionHooks hooks;
  hooks.before_launch = [](std::size_t worker, void*) {
    if (worker == 1)
      throw std::runtime_error("injected partial launch failure");
  };
  detail::AccessorExecutionStats stats;
  bool threw = false;
  try {
    (void)detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                 allocation, {}, &stats, &hooks);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  Require(threw && stats.launched == 1 && stats.joined == 1);
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0);
  const std::array<std::byte, 16> bytes{std::byte{0}, std::byte{0},
                                        std::byte{128}, std::byte{127}};
  source.base = detail::AccessorByteView{0, 0, 0, bytes};
  struct Schedule {
    std::stop_source stop;
    std::atomic<bool> first_done{false};
    bool runtime_failure = false;
  } schedule;
  hooks = {};
  hooks.state = &schedule;
  hooks.before_job = [](std::size_t job, void* opaque) {
    auto& state = *static_cast<Schedule*>(opaque);
    if (job != 0)
      while (!state.first_done.load(std::memory_order_acquire))
        std::this_thread::yield();
    if (job == 2 && state.runtime_failure)
      throw std::runtime_error("worker runtime failure");
  };
  hooks.after_job = [](std::size_t job, void* opaque) noexcept {
    auto& state = *static_cast<Schedule*>(opaque);
    if (job == 0) {
      state.stop.request_stop();
      state.first_done.store(true, std::memory_order_release);
    }
  };
  auto result = detail::DecodeAccessor(
      source, false, Limits(), Policy(), context, allocation,
      schedule.stop.get_token(), &stats, &hooks);
  Require(!result.values &&
          result.diagnostic.status == mv::GltfStatus::kMalformed);
  Require(stats.launched == 4 && stats.joined == 4);
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0);
  schedule.stop = std::stop_source{};
  schedule.first_done.store(false);
  schedule.runtime_failure = true;
  threw = false;
  try {
    (void)detail::DecodeAccessor(source, false, Limits(), Policy(), context,
                                 allocation, schedule.stop.get_token(), &stats,
                                 &hooks);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  Require(threw && stats.launched == 4 && stats.joined == 4);
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0);
}
void AllocationFaults() {
  struct FaultState {
    detail::AllocationBackend backend = detail::DefaultAllocationBackend();
    std::size_t remaining;
    std::size_t calls = 0;
  };
  detail::AllocationBackend backend{
      [](std::size_t bytes, std::size_t alignment, void* opaque) -> void* {
        auto& state = *static_cast<FaultState*>(opaque);
        ++state.calls;
        if (state.remaining == 0) throw std::bad_alloc();
        --state.remaining;
        return state.backend.allocate(bytes, alignment, state.backend.state);
      },
      [](void* block, std::size_t bytes, std::size_t alignment,
         void* opaque) noexcept {
        auto& state = *static_cast<FaultState*>(opaque);
        state.backend.deallocate(block, bytes, alignment, state.backend.state);
      },
      nullptr};
  std::size_t rejected = 0;
  bool accepted = false;
  for (std::size_t allowance = 0; allowance < 16; ++allowance) {
    meshvale::geometry::ExecutionContext context({4, 1, 10000000});
    FaultState state{detail::DefaultAllocationBackend(), allowance};
    backend.state = &state;
    detail::AccessorExecutionStats stats;
    try {
      auto allocation = detail::AllocationContext(context, {}, backend)
                            .WithRequestLimit(100000);
      detail::AccessorSource source;
      source.count = 64;
      auto result = detail::DecodeAccessor(source, false, Limits(), Policy(),
                                           context, allocation, {}, &stats);
      Require(result.values.has_value());
      Require(stats.launched == 4 && stats.joined == 4);
      accepted = true;
    } catch (const std::bad_alloc&) {
      ++rejected;
    }
    Require(context.ActiveWorkers() == 0 &&
            context.ActiveTrackedPayload() == 0);
    if (accepted) break;
  }
  Require(accepted && rejected >= 4);
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--deformation-raw-words") {
      DeformationRawWords();
      DeformationVertexAlignment();
      PreparedRawRows();
      return 0;
    }
    Require(argc == 1);
    LayoutAndSparse();
    RawAndNormalized();
    DeformationRawWords();
    DeformationVertexAlignment();
    PreparedRawRows();
    NormalizedRoundingEnvironment();
    OtherMatrixLayouts();
    FailureAndLifetime();
    WorkerFailuresAndCancellation();
    AllocationFaults();
    std::cout << "typed layout, sparse, bits, normalization, workers and "
                 "rollback passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
