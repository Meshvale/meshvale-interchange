// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_PARSER_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_PARSER_H_

#include <cstddef>
#include <string_view>

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_detail {
struct JsonSyntaxSummary {
  bool accepted = false;
  std::size_t events = 0;
};
// Private syntax/configuration seam only: no metadata/schema/Unicode/duplicate
// admission, retained document or full glTF reader capability is implied.
[[nodiscard]] JsonSyntaxSummary InspectJsonSyntax(
    std::string_view input, int max_depth, const AllocationContext& allocation);
struct CoreParserSummary {
  bool accepted = false;
  std::size_t meshes = 0;
  std::size_t nodes = 0;
};
// Must ultimately receive only an independently admitted core projection.
// Stage-one tests supply original small syntax fixtures, not untrusted assets.
[[nodiscard]] CoreParserSummary InspectCoreProjection(
    std::string_view input, const AllocationContext& allocation);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_PARSER_H_
