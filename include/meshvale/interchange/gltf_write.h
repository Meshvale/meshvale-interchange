// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_GLTF_WRITE_H_
#define MESHVALE_INTERCHANGE_GLTF_WRITE_H_

#include <meshvale/interchange/gltf.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

namespace meshvale::interchange {
namespace gltf_write_detail {
struct BundleState;
struct Access;
}  // namespace gltf_write_detail

// Explicit caller policy; zero and non-host-representable fields are invalid.
// Resource totals count full bytes of each exposed resource, including aliases.
struct GltfWriteLimits {
  std::uint64_t max_json_bytes = 0;
  std::uint64_t max_resources = 0;
  std::uint64_t max_resource_bytes = 0;
  std::uint64_t max_total_resource_bytes = 0;
  std::uint64_t max_output_requested_bytes = 0;
};
struct GltfWriteWorkPolicy {
  std::uint64_t parallel_min_json_bytes = 0;
  std::uint64_t json_partition_bytes = 0;
};

// Immutable supplied-memory output. JSON is a newly charged exact copy;
// resources share the retained immutable source owner. Views require a bundle
// copy to remain alive. Independent copies and const reads can run
// concurrently; mutation/destruction of the same wrapper requires external
// synchronization.
class GltfWriteBundle {
 public:
  GltfWriteBundle(const GltfWriteBundle&);
  GltfWriteBundle& operator=(const GltfWriteBundle&);
  ~GltfWriteBundle();
  [[nodiscard]] std::span<const std::byte> Json() const noexcept;
  [[nodiscard]] std::span<const GltfResourceView> Resources() const noexcept;
  // Current retained new-operation requests, including accounting/control
  // blocks and allocator headers/padding. Shared source charges are excluded.
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct gltf_write_detail::Access;
  explicit GltfWriteBundle(
      std::shared_ptr<const gltf_write_detail::BundleState> owner);
  std::shared_ptr<const gltf_write_detail::BundleState> owner_;
};
struct GltfWriteResult {
  std::optional<GltfWriteBundle> bundle;
  GltfDiagnostic diagnostic;
};

// Emits unchanged JSON bytes and full captured resource identities/bytes from
// the retained asset, preserving opaque data and unsupported processing
// records. Uses the asset's shared execution ledger with a fresh operation stop
// token. Workers join before return; failure returns no partial output.
// Expected limit/budget/cancel failures use diagnostics; actual runtime
// exceptions propagate after joins. This operation performs no new semantic
// admission.
[[nodiscard]] GltfWriteResult WriteRetainedGltfAsset(
    const GltfAsset& asset, const GltfWriteLimits& limits,
    const GltfWriteWorkPolicy& work_policy, std::stop_token stop);

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_GLTF_WRITE_H_
