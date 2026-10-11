// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_PARSER_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_PARSER_H_

#include <cstddef>
#include <string_view>

#include "gltf_allocation.h"

struct cgltf_data;

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
// Receives only an independently admitted core projection. The optional private
// observer copies converted fields before parser release for qualification.
using CoreProjectionObserver = void (*)(const cgltf_data&, void*);
[[nodiscard]] CoreParserSummary InspectCoreProjection(
    std::string_view input, const AllocationContext& allocation,
    CoreProjectionObserver observer = nullptr, void* observer_state = nullptr);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_PARSER_H_
