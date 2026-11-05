// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_OPTIONS_H_
#define MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_OPTIONS_H_

#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/geometry/triangulation.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <nanobind/nanobind.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../src/gltf_allocation.h"
#include "surface_record_capture.h"

namespace meshvale::interchange::surface_python_detail {

inline constexpr std::size_t kSurfacePythonFormatCount = 4;

struct SurfaceIngressOptions {
  RecordIngressLimits record;
  std::uint64_t max_option_rows = 0;
  std::uint64_t max_option_bytes = 0;
  std::uint64_t max_catalog_resource_bindings = 0;
  std::uint64_t max_destination_native_code_units = 0;
  std::uint64_t max_bridge_new_requested_bytes = 0;
  std::uint64_t max_operation_requested_allowance = 0;
};
struct SurfaceFormatPreset {
  SurfaceEncodeLimits encode_limits;
  SurfaceWorkPolicy encode_work;
  SurfaceInspectLimits inspect_limits;
  SurfaceWorkPolicy inspect_work;
  SurfaceFilePublicationLimits publication_limits;
  SurfaceFilePublicationWorkPolicy publication_work;
};
// Value-only configuration: constructing/copying this projection allocates no
// native backing and creates no execution context, proof or delegate result.
struct SurfacePresetProjection {
  geometry::ExecutionOptions execution;
  SurfaceIngressOptions ingress_limits;
  RecordIngressWork ingress_work;
  geometry::MeshMaterializationLimits source_materialization;
  geometry::MeshInspectionLimits source_inspection;
  geometry::MeshMaterializationWorkPolicy mesh_work;
  geometry::TriangulationOptions triangulation_options;
  geometry::ChargedTriangulationLimits triangulation_limits;
  GltfReadOperationLimits catalog_read_limits;
  GltfWorkPolicy catalog_read_work;
  std::array<SurfaceFormatPreset, kSurfacePythonFormatCount> formats;
  const SurfaceFormatPreset& For(SurfaceEncoding) const;
  std::uint64_t OperationAllowance(SurfaceEncoding, bool catalog,
                                   bool publication) const;
};
// GIL required. Accepts the closed _values tree of built-in dictionaries or
// MappingProxyType branches directly, without thawing/rebuilding the tree.
// All branches, inactive zeros and containments validate before return. No
// stop token is inspected and no source data/native operation is touched.
SurfacePresetProjection ReadSurfacePreset(nanobind::handle values);

struct SurfaceOptionsPlan {
  SurfaceEncoding encoding = SurfaceEncoding::kInvalid;
  SurfaceRepresentation representation = SurfaceRepresentation::kInvalid;
  SurfaceNarrowing positions = SurfaceNarrowing::kInvalid;
  SurfaceCoordinates coordinates = SurfaceCoordinates::kInvalid;
  bool acknowledge_triangle_conversion = false;
  bool acknowledge_corner_expansion = false;
  bool acknowledge_unused_vertices = false;
  double max_position_linf_error = 0;
  std::optional<std::uint64_t> corner_normal_attribute;
  std::size_t channels = 0, uv = 0, materials = 0, catalog_uv = 0;
  std::size_t uv_error_bounds = 0;
  std::size_t declared_unit_bytes = 0, ascii_name_bytes = 0;
  std::size_t binary_header_bytes = 0, text_bytes = 0;
  std::uint64_t option_rows = 0, option_bytes = 0;
};
// Caller admits the exact frozen SurfaceOptions/row Python types before this
// internal parser. Revalidates every field's native representation and all
// option bounds before allocating any native storage. No mesh/catalog is read.
SurfaceOptionsPlan ValidateSurfaceOptions(nanobind::handle options,
                                          const SurfacePresetProjection&);

// Stable native options only. The enclosing operation charges this object's
// inline/control storage; every backing vector uses its same capped bridge.
// GIL required for construction. Keep source options unchanged/alive through
// construction; no Python source is retained afterwards. No stop checks occur
// during preparation. Allocation failures preserve their native provenance.
// Nonmovable: Request borrows these rows/text through all native joins.
class OwnedSurfaceOptions {
 public:
  OwnedSurfaceOptions(const SurfaceOptionsPlan&, nanobind::handle options,
                      const gltf_detail::AllocationContext& bridge);
  OwnedSurfaceOptions(const OwnedSurfaceOptions&) = delete;
  OwnedSurfaceOptions& operator=(const OwnedSurfaceOptions&) = delete;
  OwnedSurfaceOptions(OwnedSurfaceOptions&&) = delete;
  OwnedSurfaceOptions& operator=(OwnedSurfaceOptions&&) = delete;
  SurfaceEncoding Encoding() const noexcept;
  SurfaceEncodeRequest Request(const geometry::MeshInspection& original,
                               const geometry::ChargedTriangulation& triangles,
                               const GltfAsset* catalog = nullptr) const;

 private:
  SurfaceOptionsPlan plan_;
  std::vector<SurfaceChannelRule, gltf_detail::Allocator<SurfaceChannelRule>>
      channels_;
  std::vector<SurfaceUvBinding, gltf_detail::Allocator<SurfaceUvBinding>> uv_;
  std::vector<SurfaceMaterialBinding,
              gltf_detail::Allocator<SurfaceMaterialBinding>>
      materials_;
  std::vector<SurfaceCatalogUvBinding,
              gltf_detail::Allocator<SurfaceCatalogUvBinding>>
      catalog_uv_;
  std::vector<double, gltf_detail::Allocator<double>> uv_errors_;
  std::vector<char, gltf_detail::Allocator<char>> text_;
};

}  // namespace meshvale::interchange::surface_python_detail
#endif  // MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_OPTIONS_H_
