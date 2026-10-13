// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_BUNDLE_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_BUNDLE_TEST_INTERNAL_H_

#include <meshvale/interchange/gltf_topology_write.h>

#include <cstddef>
#include <optional>
#include <span>

namespace meshvale::interchange::gltf_topology_write_detail {
// Private negative-fixture seam, compiled only when support tests are enabled.
// Missing overrides preserve originals. Replacement resources own their text
// and full byte views; unmodified resources retain the original bundle owner.
struct BundleTestOverrides {
  std::optional<std::span<const std::byte>> json;
  std::optional<std::span<const GltfResourceView>> resources;
  std::optional<std::span<const geometry::index_t>> faces;
  std::optional<std::span<const geometry::index_t>> corners;
  std::optional<std::span<const GltfSourceCorner>> original_corners;
  std::optional<std::optional<GltfTopologyBindingChange>> changed_binding;
};
[[nodiscard]] GltfTopologyWriteBundle CloneBundleForTest(
    const GltfTopologyWriteBundle& original,
    const BundleTestOverrides& overrides);
}  // namespace meshvale::interchange::gltf_topology_write_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_TOPOLOGY_BUNDLE_TEST_INTERNAL_H_
