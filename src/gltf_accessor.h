// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_ACCESSOR_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_ACCESSOR_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/interchange/gltf.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <variant>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_detail {

template <class T>
using ChargedValues = std::vector<T, Allocator<T>>;
using OwnedRawValues =
    std::variant<ChargedValues<std::int8_t>, ChargedValues<std::uint8_t>,
                 ChargedValues<std::int16_t>, ChargedValues<std::uint16_t>,
                 ChargedValues<std::uint32_t>, ChargedValues<float>>;

enum class AccessorRole { kGeneric, kVertex, kIndices, kDeformationVertex };
enum class ViewTarget { kAbsent, kVertex, kIndices };
struct AccessorByteView {
  std::uint64_t index = 0;
  std::uint64_t resource_index = 0;
  std::uint64_t resource_offset = 0;
  std::span<const std::byte> bytes;
  std::size_t stride = 0;
  ViewTarget target = ViewTarget::kAbsent;
};
struct AccessorSparseSource {
  std::size_t count = 0;
  AccessorByteView indices;
  std::size_t indices_offset = 0;
  GltfComponentType index_type = GltfComponentType::kUint8;
  AccessorByteView values;
  std::size_t values_offset = 0;
};
struct AccessorSource {
  std::uint64_t index = 0;
  GltfComponentType component_type = GltfComponentType::kFloat32;
  GltfAccessorShape shape = GltfAccessorShape::kScalar;
  AccessorRole role = AccessorRole::kGeneric;
  bool normalized = false;
  std::size_t count = 0;
  std::optional<AccessorByteView> base;
  std::size_t base_offset = 0;
  std::optional<AccessorSparseSource> sparse;
  // Deformation catalog only: decode FLOAT wire words through integer storage,
  // without interpreting unconstrained extra IBM rows. Actual role validators
  // must separately admit every used row before arithmetic/derived views.
  // Legacy sources keep false and retain their existing finite-value rule.
  bool retain_float_words = false;
};
struct AccessorLayout {
  std::size_t component_bytes;
  std::size_t rows;
  std::size_t columns;
  std::size_t column_step;
  std::size_t packed_step;
  std::size_t final_extent;
  bool matrix;
};
struct AccessorExecutionStats {
  std::size_t jobs = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
};
// Private deterministic fault/scheduling injection for worker qualification.
// State remains alive until synchronous DecodeAccessor returns. Production
// passes null; callbacks never run under the execution ledger mutex.
struct AccessorExecutionHooks {
  void (*before_launch)(std::size_t worker, void* state) = nullptr;
  void (*before_job)(std::size_t job, void* state) = nullptr;
  void (*after_job)(std::size_t job, void* state) noexcept = nullptr;
  void* state = nullptr;
};
struct DecodedAccessor {
  explicit DecodedAccessor(const AllocationContext& allocation);
  OwnedRawValues raw;
  ChargedValues<double> normalized;
  ChargedValues<std::uint64_t> sparse_destinations;
  std::size_t base_stride = 0;
  std::size_t packed_sparse_step = 0;
  bool float_words = false;
};
struct AccessorDecodeResult {
  std::optional<DecodedAccessor> values;
  GltfDiagnostic diagnostic;
};
// Borrowed, synchronous observer for the deformation collection's own plan.
// Calls represent actual component visits, relationship visits and source-byte
// reads. Its state/allocation stop outlive each copy and every joined worker;
// no source read token, scheduler, ledger or successful proof is supplied here.
struct AccessorCopyWork {
  void (*scalar)(void* state);
  void (*reference)(void* state);
  void (*bytes)(std::size_t count, void* state);
  void* state;
};

// Resources borrow their asset owner through this synchronous operation. All
// destinations and immutable sparse selectors are prepared before row workers.
// Runtime exceptions propagate only after every started worker has joined.
[[nodiscard]] AccessorLayout DescribeAccessorLayout(GltfComponentType component,
                                                    GltfAccessorShape shape);
[[nodiscard]] GltfRawValues RawView(const OwnedRawValues& values);
[[nodiscard]] OwnedRawValues MakeRawValues(GltfComponentType component,
                                           const AllocationContext& allocation,
                                           std::size_t count);
// Prepare validates actual source layout, allocates owned destination storage
// and reads/validates sparse selectors. Raw row destinations are initialized,
// not admitted values or a successful source proof. The caller retains source
// backing and schedules the dependency phase before any role/derived work.
[[nodiscard]] AccessorDecodeResult PrepareAccessorDecode(
    const AccessorSource& source, bool normalized_view,
    const GltfExtractionLimits& limits, const AllocationContext& allocation,
    const AccessorCopyWork* work = nullptr);
// Copies only a caller-planned disjoint half-open row range into matching
// prepared raw storage. Derived/normalized storage must be empty; that work
// belongs to the new profile's separately guarded role phase. Expected decoder
// and admission failures return their actual typed diagnostic. Runtime failures
// propagate to the collection's join owner.
[[nodiscard]] GltfDiagnostic CopyPreparedAccessorRows(
    const AccessorSource& source, DecodedAccessor& output, std::size_t first,
    std::size_t last, const AllocationContext& allocation, std::stop_token stop,
    const AccessorCopyWork* work = nullptr);
[[nodiscard]] AccessorDecodeResult DecodeAccessor(
    const AccessorSource& source, bool normalized_view,
    const GltfExtractionLimits& limits, const GltfWorkPolicy& policy,
    const geometry::ExecutionContext& context,
    const AllocationContext& allocation, std::stop_token stop,
    AccessorExecutionStats* stats = nullptr,
    const AccessorExecutionHooks* hooks = nullptr,
    const NativeExecutionOptions* execution = nullptr);

}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_ACCESSOR_H_
