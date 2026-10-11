// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_METADATA_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_METADATA_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "gltf_asset_state.h"

namespace meshvale::interchange::gltf_detail {
// Exact bounded decimal operations over already syntax-admitted original text.
// No allocation, locale or floating point environment dependency.
[[nodiscard]] std::uint64_t ExactUnsigned(std::string_view lexeme);
[[nodiscard]] std::int64_t ExactSigned(std::string_view lexeme);
[[nodiscard]] int CompareDecimal(std::string_view left, std::string_view right);
void AdmitMetadata(AssetState& state);
void CheckMetadataRelationships(AssetState& state);
[[nodiscard]] OwnedVector<char> BuildCoreProjection(const AssetState& state);
[[nodiscard]] std::string_view GuardPath(std::uint32_t rule);
void AdmitResources(AssetState& state,
                    std::span<const SuppliedGltfResource> supplied);
void BuildGraph(AssetState& state);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_METADATA_H_
