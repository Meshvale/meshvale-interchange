// SPDX-License-Identifier: Apache-2.0
#include "gltf_accessor.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include "gltf_operation_workers.h"
#include "meshvale/geometry/execution.h"

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr std::size_t kMatrixColumnAlignment = 4;
constexpr std::size_t kVertexAlignment = 4;
constexpr std::size_t kMaximumVertexStride = 252;
constexpr std::size_t kBitsPerByte = 8;
constexpr std::size_t kMinimumDecodeWorkers = 2;
constexpr std::uint32_t kAccessorRule = 300;
constexpr std::uint32_t kBinaryLayoutRule = 301;
constexpr std::uint32_t kSparseSelectorRule = 302;
constexpr std::uint32_t kBinaryValueRule = 303;
constexpr std::uint32_t kExtractionLimitRule = 304;
constexpr std::uint32_t kFloatExponentMask = 0x7f800000U;

GltfDiagnostic Diagnostic(GltfStatus status, GltfDiagnosticCode code,
                          std::uint32_t rule = kAccessorRule) {
  return {status, code, {}, {}, {}, {}, rule};
}
struct DecodeFailure {
  GltfDiagnostic diagnostic;
};
[[noreturn]] void Fail(GltfStatus status, GltfDiagnosticCode code,
                       std::uint32_t rule = kAccessorRule) {
  throw DecodeFailure{Diagnostic(status, code, rule)};
}
[[noreturn]] void LayoutFailure(const AccessorByteView& view,
                                std::size_t offset) {
  auto diagnostic =
      Diagnostic(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout,
                 kBinaryLayoutRule);
  diagnostic.resource_kind = GltfResourceKind::kBuffer;
  diagnostic.resource_index = view.resource_index;
  if (offset <=
      std::numeric_limits<std::uint64_t>::max() - view.resource_offset)
    diagnostic.resource_span = {view.resource_offset + offset, 0};
  throw DecodeFailure{diagnostic};
}
std::size_t Add(std::size_t first, std::size_t second) {
  if (second > std::numeric_limits<std::size_t>::max() - first)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow);
  return first + second;
}
std::size_t Product(std::size_t first, std::size_t second) {
  if (second != 0 && first > std::numeric_limits<std::size_t>::max() / second)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow);
  return first * second;
}
bool Unsigned(GltfComponentType type) {
  return type == GltfComponentType::kUint8 ||
         type == GltfComponentType::kUint16 ||
         type == GltfComponentType::kUint32;
}
std::size_t ComponentBytes(GltfComponentType type) {
  switch (type) {
    case GltfComponentType::kInt8:
    case GltfComponentType::kUint8:
      return sizeof(std::uint8_t);
    case GltfComponentType::kInt16:
    case GltfComponentType::kUint16:
      return sizeof(std::uint16_t);
    case GltfComponentType::kUint32:
    case GltfComponentType::kFloat32:
      return sizeof(std::uint32_t);
  }
  Fail(GltfStatus::kInvalidRequest, GltfDiagnosticCode::kInvalidRequest);
}
std::uint32_t ReadLittleEndian(const std::byte* source, std::size_t size) {
  std::uint32_t bits = 0;
  for (std::size_t i = 0; i < size; ++i)
    bits |= std::uint32_t{std::to_integer<std::uint8_t>(source[i])}
            << (i * kBitsPerByte);
  return bits;
}
void CheckExtent(const AccessorByteView& view, std::size_t offset,
                 std::size_t count, std::size_t step,
                 std::size_t final_extent) {
  // Rearranged comparisons prove malformed range without overflow.
  if (offset > view.bytes.size() || final_extent > view.bytes.size() - offset)
    LayoutFailure(view, offset);
  if (count > 1 &&
      (count - 1) > (view.bytes.size() - offset - final_extent) / step)
    LayoutFailure(view, offset);
}
void CheckAlignment(const AccessorByteView& view, std::size_t offset,
                    std::size_t size, bool matrix, bool vertex) {
  if (offset % size || (view.resource_offset % size + offset % size) % size ||
      (matrix && (view.resource_offset % kMatrixColumnAlignment +
                  offset % kMatrixColumnAlignment) %
                     kMatrixColumnAlignment) ||
      (vertex && offset % kVertexAlignment))
    LayoutFailure(view, offset);
}
void ValidateSource(const AccessorSource& source,
                    const AccessorLayout& layout) {
  if (!source.count)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
  if (source.normalized &&
      (source.component_type == GltfComponentType::kFloat32 ||
       source.component_type == GltfComponentType::kUint32))
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole);
  if (source.role == AccessorRole::kIndices &&
      (source.shape != GltfAccessorShape::kScalar ||
       !Unsigned(source.component_type) || source.normalized))
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole);
  if (source.role == AccessorRole::kVertex &&
      source.component_type == GltfComponentType::kUint32)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole);
  const bool vertex = source.role == AccessorRole::kVertex;
  if (source.base) {
    const auto& view = *source.base;
    const auto step = view.stride ? view.stride : layout.packed_step;
    if ((view.target == ViewTarget::kVertex && !vertex) ||
        (view.target == ViewTarget::kIndices &&
         source.role != AccessorRole::kIndices))
      LayoutFailure(view, source.base_offset);
    CheckAlignment(view, source.base_offset, layout.component_bytes,
                   layout.matrix, vertex);
    if (step < layout.packed_step || step % layout.component_bytes ||
        (view.stride && (!vertex || view.stride % kVertexAlignment ||
                         view.stride > kMaximumVertexStride)) ||
        (layout.matrix && step % kMatrixColumnAlignment) ||
        (vertex && step % kVertexAlignment))
      LayoutFailure(view, source.base_offset);
    CheckExtent(view, source.base_offset, source.count, step,
                layout.final_extent);
  } else if (source.base_offset) {
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout);
  }
  if (source.sparse) {
    const auto& sparse = *source.sparse;
    if (!sparse.count || sparse.count > source.count ||
        !Unsigned(sparse.index_type) || sparse.indices.stride ||
        sparse.values.stride || sparse.indices.target != ViewTarget::kAbsent ||
        sparse.values.target != ViewTarget::kAbsent)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout);
    const auto index_bytes = ComponentBytes(sparse.index_type);
    CheckAlignment(sparse.indices, sparse.indices_offset, index_bytes, false,
                   false);
    CheckAlignment(sparse.values, sparse.values_offset, layout.component_bytes,
                   layout.matrix, vertex);
    CheckExtent(sparse.indices, sparse.indices_offset, sparse.count,
                index_bytes, index_bytes);
    CheckExtent(sparse.values, sparse.values_offset, sparse.count,
                layout.packed_step, layout.final_extent);
  }
}
template <class T>
T ReadScalar(const std::byte* bytes) {
  const auto bits = ReadLittleEndian(bytes, sizeof(T));
  if constexpr (std::is_same_v<T, float>) {
    if ((bits & kFloatExponentMask) == kFloatExponentMask)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue,
           kBinaryValueRule);
    return std::bit_cast<float>(bits);
  } else if constexpr (std::is_signed_v<T>) {
    using UnsignedT = std::make_unsigned_t<T>;
    return std::bit_cast<T>(static_cast<UnsignedT>(bits));
  } else {
    return static_cast<T>(bits);
  }
}
template <class T>
double Normalize(T value) {
  if constexpr (std::is_floating_point_v<T>) {
    return value;
  } else {
    // Integer division constructs nearest-even binary64 without consulting the
    // caller's or worker's floating environment. Raw source values stay intact.
    static_assert(std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53 &&
                  sizeof(double) == sizeof(std::uint64_t));
    constexpr unsigned kFractionBits = 52;
    constexpr std::int64_t kExponentBias = 1023;
    constexpr auto kHiddenBit = std::uint64_t{1} << kFractionBits;
    constexpr auto kSignBit = std::uint64_t{1} << 63;
    bool negative = false;
    std::uint64_t numerator;
    if constexpr (std::is_signed_v<T>) {
      if (value == std::numeric_limits<T>::min()) return -1.0;
      const auto wide = static_cast<std::int64_t>(value);
      negative = wide < 0;
      numerator = static_cast<std::uint64_t>(negative ? -wide : wide);
    } else {
      numerator = value;
    }
    if (numerator == 0) return 0.0;
    const auto denominator =
        static_cast<std::uint64_t>(std::numeric_limits<T>::max());
    std::int64_t exponent = 0;
    while (numerator < denominator) {
      numerator <<= 1;
      --exponent;
    }
    auto remainder = numerator - denominator;
    auto significand = kHiddenBit;
    for (unsigned bit = kFractionBits; bit != 0;) {
      --bit;
      remainder <<= 1;
      if (remainder >= denominator) {
        remainder -= denominator;
        significand |= std::uint64_t{1} << bit;
      }
    }
    const auto twice_remainder = remainder << 1;
    if (twice_remainder > denominator ||
        (twice_remainder == denominator && (significand & 1)))
      ++significand;
    if (significand == (kHiddenBit << 1)) {
      significand >>= 1;
      ++exponent;
    }
    const auto bits = (negative ? kSignBit : 0) |
                      (static_cast<std::uint64_t>(exponent + kExponentBias)
                       << kFractionBits) |
                      (significand - kHiddenBit);
    return std::bit_cast<double>(bits);
  }
}
template <class T>
void DecodeRows(const AccessorSource& source, const AccessorLayout& layout,
                const std::span<const std::uint64_t> sparse_destinations,
                ChargedValues<T>& raw, ChargedValues<double>& normalized,
                std::size_t first, std::size_t last, std::stop_token stop) {
  auto sparse = std::lower_bound(sparse_destinations.begin(),
                                 sparse_destinations.end(), first);
  const auto components = layout.rows * layout.columns;
  for (auto row = first; row < last; ++row) {
    if (stop.stop_requested())
      Fail(GltfStatus::kCanceled, GltfDiagnosticCode::kCanceled);
    const std::byte* bytes = nullptr;
    if (source.base)
      bytes = source.base->bytes.data() + source.base_offset +
              row * (source.base->stride ? source.base->stride
                                         : layout.packed_step);
    if (sparse != sparse_destinations.end() && *sparse == row) {
      if constexpr (std::is_same_v<T, float>) {
        if (bytes)
          for (std::size_t column = 0; column < layout.columns; ++column)
            for (std::size_t component = 0; component < layout.rows;
                 ++component)
              (void)ReadScalar<T>(bytes + column * layout.column_step +
                                  component * sizeof(T));
      }
      const auto ordinal =
          static_cast<std::size_t>(sparse - sparse_destinations.begin());
      bytes = source.sparse->values.bytes.data() +
              source.sparse->values_offset + ordinal * layout.packed_step;
      ++sparse;
    }
    for (std::size_t column = 0; column < layout.columns; ++column) {
      for (std::size_t component = 0; component < layout.rows; ++component) {
        const auto slot = row * components + column * layout.rows + component;
        const T value =
            bytes ? ReadScalar<T>(bytes + column * layout.column_step +
                                  component * sizeof(T))
                  : T{};
        if (source.role == AccessorRole::kIndices &&
            value == std::numeric_limits<T>::max())
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kIndexRole,
               kBinaryValueRule);
        raw[slot] = value;
        if (!normalized.empty()) normalized[slot] = Normalize(value);
      }
    }
  }
}
struct JobResult {
  std::optional<GltfDiagnostic> diagnostic;
  std::exception_ptr exception;
};
void RunRows(const AccessorSource& source, const AccessorLayout& layout,
             DecodedAccessor& output, const GltfWorkPolicy& policy,
             const geometry::ExecutionContext& context,
             const AllocationContext& allocation, std::stop_token stop,
             AccessorExecutionStats* stats, const AccessorExecutionHooks* hooks,
             const NativeExecutionOptions* execution) {
  const auto partition =
      static_cast<std::size_t>(policy.channel_partition_elements);
  const auto job_count =
      source.count / partition + (source.count % partition != 0);
  if (stats) stats->jobs = job_count;
  auto execute = [&](std::size_t first, std::size_t last) {
    std::visit(
        [&](auto& values) {
          DecodeRows(source, layout, output.sparse_destinations, values,
                     output.normalized, first, last, stop);
        },
        output.raw);
  };
  const bool eligible = job_count >= kMinimumDecodeWorkers &&
                        Product(source.count, layout.rows * layout.columns) >=
                            policy.parallel_min_channel_components;
  auto workers = geometry::TryReserveWorkers(context, eligible ? job_count : 0);
  if (workers.Count() < kMinimumDecodeWorkers) {
    execute(0, source.count);
    return;
  }
  ChargedValues<JobResult> results(job_count, JobResult{},
                                   Allocator<JobResult>(allocation));
  std::exception_ptr launch_exception;
  if (execution) {
    struct DecodeJob {
      decltype(execute)& operation;
      std::size_t partition;
      std::size_t rows;
      JobResult* results;
      const AccessorExecutionHooks* hooks;
    } work{execute, partition, source.count, results.data(), hooks};
    auto invoke = [](void* argument, std::size_t ordinal) noexcept {
      auto& job = *static_cast<DecodeJob*>(argument);
      const auto first = ordinal * job.partition;
      const auto last = first + std::min(job.partition, job.rows - first);
      try {
        if (job.hooks && job.hooks->before_job)
          job.hooks->before_job(ordinal, job.hooks->state);
        job.operation(first, last);
      } catch (const DecodeFailure& failure) {
        job.results[ordinal].diagnostic = failure.diagnostic;
      } catch (...) {
        job.results[ordinal].exception = std::current_exception();
      }
      if (job.hooks && job.hooks->after_job)
        job.hooks->after_job(ordinal, job.hooks->state);
    };
    OperationWorkerStats actual;
    try {
      RunOperationJobs(allocation, workers, job_count, invoke, &work,
                       *execution, actual,
                       hooks ? hooks->before_launch : nullptr,
                       hooks ? hooks->state : nullptr);
    } catch (...) {
      launch_exception = std::current_exception();
    }
    if (stats) {
      stats->launched += actual.launched;
      stats->joined += actual.joined;
    }
  } else {
    ChargedValues<std::jthread> threads{Allocator<std::jthread>(allocation)};
    threads.reserve(workers.Count());
    // workers precedes threads in destruction order; explicit joins also cover
    // launch exceptions. Thread/exception runtime allocations remain
    // exclusions.
    try {
      for (std::size_t worker = 0; worker < workers.Count(); ++worker) {
        if (hooks && hooks->before_launch)
          hooks->before_launch(worker, hooks->state);
        threads.emplace_back([&, worker] {
          for (auto job = worker; job < job_count; job += workers.Count()) {
            const auto first = job * partition;
            const auto last = first + std::min(partition, source.count - first);
            try {
              if (hooks && hooks->before_job)
                hooks->before_job(job, hooks->state);
              execute(first, last);
            } catch (const DecodeFailure& failure) {
              results[job].diagnostic = failure.diagnostic;
            } catch (...) {
              results[job].exception = std::current_exception();
            }
            if (hooks && hooks->after_job) hooks->after_job(job, hooks->state);
          }
        });
        if (stats) ++stats->launched;
      }
    } catch (...) {
      launch_exception = std::current_exception();
    }
    for (auto& thread : threads) {
      thread.join();
      if (stats) ++stats->joined;
    }
  }
  if (launch_exception) std::rethrow_exception(launch_exception);
  for (const auto& result : results)
    if (result.exception) std::rethrow_exception(result.exception);
  for (const auto& result : results)
    if (result.diagnostic &&
        result.diagnostic->status == GltfStatus::kMalformed)
      throw DecodeFailure{*result.diagnostic};
  for (const auto& result : results)
    if (result.diagnostic) throw DecodeFailure{*result.diagnostic};
}
}  // namespace

DecodedAccessor::DecodedAccessor(const AllocationContext& allocation)
    : raw(ChargedValues<float>(Allocator<float>(allocation))),
      normalized(Allocator<double>(allocation)),
      sparse_destinations(Allocator<std::uint64_t>(allocation)) {}
AccessorLayout DescribeAccessorLayout(GltfComponentType component,
                                      GltfAccessorShape shape) {
  const auto bytes = ComponentBytes(component);
  std::size_t rows = 1, columns = 1;
  bool matrix = false;
  switch (shape) {
    case GltfAccessorShape::kScalar:
      break;
    case GltfAccessorShape::kVec2:
      rows = 2;
      break;
    case GltfAccessorShape::kVec3:
      rows = 3;
      break;
    case GltfAccessorShape::kVec4:
      rows = 4;
      break;
    case GltfAccessorShape::kMat2:
      rows = columns = 2;
      matrix = true;
      break;
    case GltfAccessorShape::kMat3:
      rows = columns = 3;
      matrix = true;
      break;
    case GltfAccessorShape::kMat4:
      rows = columns = 4;
      matrix = true;
      break;
    default:
      Fail(GltfStatus::kInvalidRequest, GltfDiagnosticCode::kInvalidRequest);
  }
  const auto column_bytes = rows * bytes;
  const auto column_step =
      matrix ? (column_bytes + kMatrixColumnAlignment - 1) /
                   kMatrixColumnAlignment * kMatrixColumnAlignment
             : column_bytes;
  return {bytes,
          rows,
          columns,
          column_step,
          columns * column_step,
          (columns - 1) * column_step + column_bytes,
          matrix};
}
GltfRawValues RawView(const OwnedRawValues& values) {
  return std::visit(
      [](const auto& raw) -> GltfRawValues {
        using Value = typename std::remove_cvref_t<decltype(raw)>::value_type;
        return std::span<const Value>(raw);
      },
      values);
}
OwnedRawValues MakeRawValues(GltfComponentType component,
                             const AllocationContext& allocation,
                             std::size_t count) {
  switch (component) {
    case GltfComponentType::kInt8:
      return ChargedValues<std::int8_t>(count, 0,
                                        Allocator<std::int8_t>(allocation));
    case GltfComponentType::kUint8:
      return ChargedValues<std::uint8_t>(count, 0,
                                         Allocator<std::uint8_t>(allocation));
    case GltfComponentType::kInt16:
      return ChargedValues<std::int16_t>(count, 0,
                                         Allocator<std::int16_t>(allocation));
    case GltfComponentType::kUint16:
      return ChargedValues<std::uint16_t>(count, 0,
                                          Allocator<std::uint16_t>(allocation));
    case GltfComponentType::kUint32:
      return ChargedValues<std::uint32_t>(count, 0,
                                          Allocator<std::uint32_t>(allocation));
    case GltfComponentType::kFloat32:
      return ChargedValues<float>(count, 0, Allocator<float>(allocation));
  }
  Fail(GltfStatus::kInvalidRequest, GltfDiagnosticCode::kInvalidRequest);
}
AccessorDecodeResult DecodeAccessor(
    const AccessorSource& source, bool normalized_view,
    const GltfExtractionLimits& limits, const GltfWorkPolicy& policy,
    const geometry::ExecutionContext& context,
    const AllocationContext& allocation, std::stop_token stop,
    AccessorExecutionStats* stats, const AccessorExecutionHooks* hooks,
    const NativeExecutionOptions* execution) {
  if (stats) *stats = {};
  try {
    constexpr auto kHostMaximum = std::numeric_limits<std::size_t>::max();
    for (const auto value : {limits.max_components, limits.max_triangle_corners,
                             limits.max_output_requested_bytes,
                             policy.parallel_min_channel_components,
                             policy.channel_partition_elements}) {
      if (!value)
        Fail(GltfStatus::kInvalidLimits, GltfDiagnosticCode::kZeroLimit);
      if (value > kHostMaximum)
        Fail(GltfStatus::kInvalidLimits,
             GltfDiagnosticCode::kLimitNotRepresentable);
    }
    allocation.Checkpoint();
    const auto layout =
        DescribeAccessorLayout(source.component_type, source.shape);
    ValidateSource(source, layout);
    const auto components = Product(source.count, layout.rows * layout.columns);
    if (components > limits.max_components)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded,
           kExtractionLimitRule);
    auto request_bytes = AllocationRequestBytes(
        Product(components, layout.component_bytes), layout.component_bytes);
    if (normalized_view && source.normalized)
      request_bytes =
          Add(request_bytes,
              AllocationRequestBytes(Product(components, sizeof(double)),
                                     alignof(double)));
    if (source.sparse)
      request_bytes = Add(request_bytes,
                          AllocationRequestBytes(Product(source.sparse->count,
                                                         sizeof(std::uint64_t)),
                                                 alignof(std::uint64_t)));
    if (request_bytes > limits.max_output_requested_bytes)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded,
           kExtractionLimitRule);
    DecodedAccessor output(allocation);
    output.base_stride = source.base && source.base->stride
                             ? source.base->stride
                             : layout.packed_step;
    output.packed_sparse_step = layout.packed_step;
    output.raw = MakeRawValues(source.component_type, allocation, components);
    if (normalized_view && source.normalized)
      output.normalized.resize(components);
    if (source.sparse) {
      const auto& sparse = *source.sparse;
      const auto index_bytes = ComponentBytes(sparse.index_type);
      output.sparse_destinations.resize(sparse.count);
      for (std::size_t i = 0; i < sparse.count; ++i) {
        allocation.Checkpoint();
        const auto destination =
            ReadLittleEndian(sparse.indices.bytes.data() +
                                 sparse.indices_offset + i * index_bytes,
                             index_bytes);
        if (destination >= source.count ||
            (i && destination <= output.sparse_destinations[i - 1]))
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout,
               kSparseSelectorRule);
        output.sparse_destinations[i] = destination;
      }
    }
    RunRows(source, layout, output, policy, context, allocation, stop, stats,
            hooks, execution);
    allocation.Checkpoint();
    return {std::move(output),
            Diagnostic(GltfStatus::kAccepted, GltfDiagnosticCode::kNone)};
  } catch (const DecodeFailure& failure) {
    return {{}, failure.diagnostic};
  } catch (const AdmissionFailure& failure) {
    switch (failure.Code()) {
      case AdmissionCode::kBudgetExceeded:
        return {{},
                Diagnostic(GltfStatus::kBudgetExceeded,
                           GltfDiagnosticCode::kPayloadBudget)};
      case AdmissionCode::kCanceled:
        return {
            {},
            Diagnostic(GltfStatus::kCanceled, GltfDiagnosticCode::kCanceled)};
      case AdmissionCode::kSizeLimit:
        return {{},
                Diagnostic(GltfStatus::kUnsupportedProfile,
                           GltfDiagnosticCode::kSizeOverflow)};
      case AdmissionCode::kInvalidObject:
        throw;
    }
    throw;
  }
}
}  // namespace meshvale::interchange::gltf_detail
