// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_FBX_H_
#define MESHVALE_INTERCHANGE_FBX_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "meshvale/geometry/mesh.h"

namespace meshvale::interchange {

enum class FbxMapping {
  kByControlPoint,
  kByPolygonVertex,
  kByPolygon,
  kAllSame
};
enum class FbxReference { kDirect, kIndexToDirect };
struct FbxAttributeSource {
  std::string name;
  std::string semantic;
  FbxMapping mapping;
  FbxReference reference;
  // Source table width: four for FBX normals, two for UVs.
  std::uint32_t components;
  std::vector<double> direct_values;
  std::vector<std::int32_t> indices;
};
struct FbxPolygonMesh {
  std::vector<std::string> node_names;
  std::vector<std::uint32_t> child_indices;
  geometry::Mesh mesh;
  std::vector<FbxAttributeSource> attribute_sources;
};
struct FbxSourceSpace {
  // Axes use 1=x, 2=y, 3=z; front_parity uses 1=even, 2=odd.
  int up_axis;
  int up_sign;
  int front_parity;
  int front_sign;
  bool right_handed;
  double centimeters_per_unit;
};
struct FbxAsset {
  std::vector<std::byte> source_bytes;
  FbxSourceSpace source_space;
  std::vector<FbxPolygonMesh> meshes;
};
struct FbxImportOptions {
  std::size_t max_input_bytes = 64 * 1024 * 1024;
  std::uint32_t max_nodes = 10000;
  std::uint32_t max_node_depth = 64;
  std::uint32_t max_meshes = 1000;
  std::uint32_t max_control_points = 1000000;
  std::uint32_t max_corners = 4000000;
  std::uint32_t max_attribute_elements = 32;
  std::uint32_t max_attribute_table_rows = 4000000;
};
struct FbxImportResult {
  std::optional<FbxAsset> asset;
  std::vector<geometry::Diagnostic> diagnostics;
};

// See docs/fbx.md for the supported subset, source-space policy and limits.
// Returned values own their storage. Expected failures produce no partial
// asset; allocation failures propagate. Calls in this module serialize SDK
// access.
[[nodiscard]] FbxImportResult ReadFbxFile(const std::filesystem::path& input,
                                          const FbxImportOptions& options = {});

}  // namespace meshvale::interchange

#endif  // MESHVALE_INTERCHANGE_FBX_H_
