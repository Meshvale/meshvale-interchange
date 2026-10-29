// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_ACCESSOR_SOURCE_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_ACCESSOR_SOURCE_H_

#include <cstddef>
#include <cstdint>

#include "gltf_accessor.h"
#include "gltf_asset_state.h"

namespace meshvale::interchange::gltf_detail {
// Borrowed synchronous work observer. Callbacks are actual visited references
// and compared source bytes; they may propagate the caller's admission failure.
// Null preserves legacy resolution. No ledger, role or success is supplied
// here.
struct AccessorResolveWork {
  void (*reference)(void* state);
  void (*byte_visit)(void* state);
  void* state;
};
// One retained-source resolver for legacy extraction and deformation roles.
// Sources borrow the actual asset resources; callers retain its strong owner.
[[nodiscard]] AccessorSource ResolveAccessorSource(
    const AssetState& asset, std::uint64_t index, AccessorRole role,
    const AccessorResolveWork* work = nullptr);
[[nodiscard]] OwnedRawValues ReadAccessorBound(
    const JsonSnapshot& document, std::size_t bound,
    const AccessorSource& source, const AllocationContext& allocation,
    GltfDeformationNumericObservation* observation = nullptr);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_ACCESSOR_SOURCE_H_
