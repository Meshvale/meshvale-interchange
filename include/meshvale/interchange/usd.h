// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_USD_H_
#define MESHVALE_INTERCHANGE_USD_H_

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "meshvale/geometry/mesh.h"

namespace meshvale::interchange {

// Admission limits bound source size and retained cardinalities, not
// allocations performed by the OpenUSD parser, plugins, allocator or worker
// pool.
struct UsdMeshLimits {
  std::uint64_t source_bytes = 64 * 1024 * 1024;
  std::uint64_t positions = 10000000;
  std::uint64_t faces = 10000000;
  std::uint64_t corners = 50000000;
  std::uint64_t primvars = 64;
  std::uint64_t attribute_scalars = 200000000;
};

// The original table and optional indices retain sharing, unused authored
// values and face-varying seam topology alongside the expanded Mesh channel.
struct UsdPrimvarSource {
  std::string name;
  std::string type;
  std::string interpolation;
  std::uint32_t components = 0;
  geometry::AttributeValues values;
  std::optional<std::vector<std::int32_t>> indices;
  std::int32_t unauthored_values_index = -1;
};

struct UsdMeshDocument {
  geometry::Mesh mesh;
  std::string mesh_path;
  std::string up_axis;
  double meters_per_unit = 0;
  std::string orientation;
  std::string subdivision_scheme;
  bool double_sided = false;
  // Row-major USD matrix convention. Positions stay in mesh-local space;
  // this evaluated default-time matrix is informational and is not baked.
  std::array<double, 16> local_to_world{};
  std::vector<UsdPrimvarSource> primvars;
};

struct UsdMeshImportResult {
  std::optional<UsdMeshDocument> document;
  std::vector<geometry::Diagnostic> diagnostics;
};

// Read one absolute prim path from a stable, caller-owned local .usda/.usdc
// regular file. Reject composition arcs, time samples, instances, holes,
// skinning, material bindings and unsupported authored mesh data. Extract a
// polygon cage at default time; never tessellate, weld, normalize or repair.
// All returned storage is owned independently of the destroyed SDK stage.
// Expected failures return no document; allocation exceptions propagate.
// Calls own separate SDK stages; SDK plugins/resolver configuration is global
// and must remain stable. This is not a sandbox or a total-memory budget.
[[nodiscard]] UsdMeshImportResult ReadUsdMesh(const std::filesystem::path& path,
                                              const std::string& mesh_path,
                                              const UsdMeshLimits& limits = {});

}  // namespace meshvale::interchange

#endif  // MESHVALE_INTERCHANGE_USD_H_
