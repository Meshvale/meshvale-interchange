// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_WRITE_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_WRITE_INTERNAL_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_topology_write.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <stop_token>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_json_snapshot.h"
#include "gltf_topology_write_workers.h"

namespace meshvale::interchange::gltf_topology_write_detail {
template <class T>
using Vector = std::vector<T, gltf_detail::Allocator<T>>;
class Failure : public std::exception {
 public:
  Failure(GltfTopologyWriteStatus status,
          GltfTopologyWriteDiagnostic diagnostic) noexcept;
  [[nodiscard]] GltfTopologyWriteStatus Status() const noexcept;
  [[nodiscard]] const GltfTopologyWriteDiagnostic& Diagnostic() const noexcept;
  [[nodiscard]] const char* what() const noexcept override;

 private:
  GltfTopologyWriteStatus status_;
  GltfTopologyWriteDiagnostic diagnostic_;
};
[[noreturn]] void Reject(
    GltfTopologyWriteStatus status, GltfTopologyWriteStage stage,
    GltfTopologyWriteDiagnosticCode code,
    std::optional<geometry::index_t> attribute = std::nullopt,
    std::optional<geometry::index_t> element = std::nullopt,
    std::optional<GltfSourceSpan> source = std::nullopt);
[[nodiscard]] std::size_t Add(std::size_t left, std::size_t right,
                              GltfTopologyWriteStage stage);
[[nodiscard]] std::size_t Multiply(std::size_t left, std::size_t right,
                                   GltfTopologyWriteStage stage);
struct LexicalSpan {
  std::size_t begin = 0;
  std::size_t end = 0;
  // Container's original closing delimiter; meaningful only for containers.
  std::size_t closing = 0;
};
// No DOM or source copy. Ordinals match the immutable retained flat document;
// stack and span index are fresh requests under the caller's complete cap.
[[nodiscard]] Vector<LexicalSpan> BuildLexicalIndex(
    const gltf_detail::JsonSnapshot& document,
    const gltf_detail::AllocationContext& allocation,
    const GltfTopologyWriteLimits& limits,
    const GltfTopologyWriteWorkPolicy& work);
[[nodiscard]] GltfTopologyWriteResult WriteWithBackend(
    const GltfTopologyWriteRequest& request,
    const GltfTopologyWriteLimits& limits,
    const GltfTopologyWriteWorkPolicy& work, std::stop_token stop,
    const gltf_detail::AllocationBackend& allocation_backend,
    const NativeWorkerBackend& worker_backend);
}  // namespace meshvale::interchange::gltf_topology_write_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_WRITE_INTERNAL_H_
