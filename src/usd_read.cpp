// SPDX-License-Identifier: Apache-2.0
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/stageCacheContext.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "meshvale/interchange/usd.h"

namespace meshvale::interchange {
namespace {
namespace usd = PXR_NS;

struct Failure {
  std::string code;
  std::string subject;
};

[[noreturn]] void Reject(const std::string& code, const std::string& subject) {
  throw Failure{code, subject};
}

void Admit(std::uint64_t count, std::uint64_t limit,
           const std::string& subject) {
  if (count > limit || count > std::numeric_limits<std::size_t>::max()) {
    Reject("usd.limit", subject);
  }
}

class ScalarAdmission {
 public:
  explicit ScalarAdmission(std::uint64_t limit) : remaining_(limit) {}
  void Take(std::uint64_t rows, std::uint32_t components,
            const std::string& subject) {
    if (components == 0 || rows > remaining_ / components ||
        rows > std::vector<double>{}.max_size() / components) {
      Reject("usd.limit", subject);
    }
    remaining_ -= rows * components;
  }

 private:
  std::uint64_t remaining_;
};

void CheckLayer(const usd::SdfLayerRefPtr& layer) {
  if (!layer->GetSubLayerPaths().empty() ||
      !layer->GetCompositionAssetDependencies().empty()) {
    Reject("usd.unsupported_composition", "root layer");
  }
  layer->Traverse(usd::SdfPath::AbsoluteRootPath(),
                  [&](const usd::SdfPath& path) {
                    if (path.ContainsPrimVariantSelection()) {
                      Reject("usd.unsupported_composition", path.GetString());
                    }
                    for (const auto& field : layer->ListFields(path)) {
                      const auto& name = field.GetString();
                      if (name == "references" || name == "payload" ||
                          name == "inheritPaths" || name == "specializes" ||
                          name == "variantSelection" ||
                          name == "variantSetNames" || name == "clips") {
                        Reject("usd.unsupported_composition", path.GetString());
                      }
                      if (name == "timeSamples") {
                        Reject("usd.unsupported_animation", path.GetString());
                      }
                    }
                  });
}

template <typename Vector, typename Scalar>
geometry::AttributeValues ReadTable(const usd::VtValue& value,
                                    std::uint32_t components,
                                    ScalarAdmission& admission,
                                    const std::string& subject) {
  if (!value.IsHolding<usd::VtArray<Vector>>()) {
    Reject("usd.unsupported_type", subject);
  }
  const auto& table = value.UncheckedGet<usd::VtArray<Vector>>();
  admission.Take(table.size(), components, subject);
  std::vector<Scalar> output;
  output.reserve(table.size() * components);
  for (const auto& row : table) {
    for (std::uint32_t component = 0; component < components; ++component) {
      output.push_back(row[static_cast<int>(component)]);
    }
  }
  return output;
}

geometry::AttributeValues ReadValues(const usd::UsdAttribute& attribute,
                                     std::uint32_t components,
                                     ScalarAdmission& admission) {
  usd::VtValue value;
  const auto subject = attribute.GetPath().GetString();
  if (!attribute.Get(&value, usd::UsdTimeCode::Default())) {
    Reject("usd.missing_value", subject);
  }
  if (components == 2 && value.IsHolding<usd::VtArray<usd::GfVec2f>>()) {
    return ReadTable<usd::GfVec2f, float>(value, components, admission,
                                          subject);
  }
  if (components == 2 && value.IsHolding<usd::VtArray<usd::GfVec2d>>()) {
    return ReadTable<usd::GfVec2d, double>(value, components, admission,
                                           subject);
  }
  if (components == 3 && value.IsHolding<usd::VtArray<usd::GfVec3f>>()) {
    return ReadTable<usd::GfVec3f, float>(value, components, admission,
                                          subject);
  }
  if (components == 3 && value.IsHolding<usd::VtArray<usd::GfVec3d>>()) {
    return ReadTable<usd::GfVec3d, double>(value, components, admission,
                                           subject);
  }
  Reject("usd.unsupported_type", subject);
}

void AddChannel(UsdMeshDocument& document, UsdPrimvarSource source,
                ScalarAdmission& admission) {
  geometry::Attribute attribute;
  attribute.name = source.name;
  attribute.semantic = source.components == 2 ? "texcoord" : "normal";
  attribute.components = source.components;
  const bool constant = source.interpolation == "constant";
  if (constant || source.interpolation == "uniform") {
    attribute.domain = geometry::AttributeDomain::face;
  } else if (source.interpolation == "vertex" ||
             source.interpolation == "varying") {
    attribute.domain = geometry::AttributeDomain::vertex;
  } else if (source.interpolation == "faceVarying") {
    attribute.domain = geometry::AttributeDomain::corner;
  } else {
    Reject("usd.unsupported_interpolation", source.name);
  }
  const auto output_rows = document.mesh.row_count(attribute.domain);
  const auto input_rows = constant ? 1 : output_rows;
  const auto table_rows = std::visit(
      [&](const auto& table) { return table.size() / source.components; },
      source.values);
  if (source.unauthored_values_index < -1 ||
      (source.unauthored_values_index >= 0 &&
       static_cast<std::uint64_t>(source.unauthored_values_index) >=
           table_rows)) {
    Reject("usd.invalid_primvar_index", source.name);
  }
  if (source.indices) {
    if (source.indices->size() != input_rows) {
      Reject("usd.primvar_cardinality", source.name);
    }
    for (const auto index : *source.indices) {
      if (index < 0 || static_cast<std::uint64_t>(index) >= table_rows) {
        Reject("usd.invalid_primvar_index", source.name);
      }
    }
  } else if (table_rows != input_rows || source.unauthored_values_index != -1) {
    Reject("usd.primvar_cardinality", source.name);
  }
  admission.Take(output_rows, source.components, source.name);
  if (source.unauthored_values_index >= 0) {
    attribute.present =
        std::vector<std::uint8_t>(static_cast<std::size_t>(output_rows), 1);
  }
  attribute.values = std::visit(
      [&](const auto& table) -> geometry::AttributeValues {
        using Table = std::decay_t<decltype(table)>;
        Table output;
        output.reserve(static_cast<std::size_t>(output_rows) *
                       source.components);
        for (std::uint64_t row = 0; row < output_rows; ++row) {
          const auto input_row = constant ? 0 : static_cast<std::size_t>(row);
          const auto index =
              source.indices
                  ? static_cast<std::size_t>((*source.indices)[input_row])
                  : input_row;
          if (attribute.present &&
              index ==
                  static_cast<std::size_t>(source.unauthored_values_index)) {
            (*attribute.present)[static_cast<std::size_t>(row)] = 0;
          }
          for (std::uint32_t component = 0; component < source.components;
               ++component) {
            output.push_back(table[index * source.components + component]);
          }
        }
        return output;
      },
      source.values);
  attribute.metadata["usd:interpolation"] = source.interpolation;
  attribute.metadata["usd:type"] = source.type;
  document.mesh.attributes.push_back(std::move(attribute));
  document.primvars.push_back(std::move(source));
}

void CheckMeshFeatures(const usd::UsdPrim& prim) {
  for (auto ancestor = prim; ancestor && !ancestor.IsPseudoRoot();
       ancestor = ancestor.GetParent()) {
    if (ancestor.IsInstance() || ancestor.IsInstanceProxy() ||
        ancestor.HasAuthoredInstanceable()) {
      Reject("usd.unsupported_instance", ancestor.GetPath().GetString());
    }
    if (ancestor != prim && !usd::UsdGeomPrimvarsAPI(ancestor)
                                 .GetPrimvarsWithAuthoredValues()
                                 .empty()) {
      Reject("usd.unsupported_inherited_primvar",
             ancestor.GetPath().GetString());
    }
    for (const auto& property : ancestor.GetAuthoredProperties()) {
      const auto name = property.GetName().GetString();
      if (name.starts_with("skel:") || name.starts_with("material:binding")) {
        Reject("usd.unsupported_binding", property.GetPath().GetString());
      }
    }
  }
  if (!prim.GetAuthoredRelationships().empty()) {
    Reject("usd.unsupported_relationship", prim.GetPath().GetString());
  }
  for (const auto& attribute : prim.GetAuthoredAttributes()) {
    const auto& name = attribute.GetName().GetString();
    if (name == "points" || name == "faceVertexCounts" ||
        name == "faceVertexIndices" || name == "normals" ||
        name == "orientation" || name == "subdivisionScheme" ||
        name == "doubleSided" || name == "extent" || name == "visibility" ||
        name == "purpose" || name == "xformOpOrder" ||
        name.starts_with("xformOp:") || name.starts_with("primvars:")) {
      continue;
    }
    Reject("usd.unsupported_attribute", attribute.GetPath().GetString());
  }
}

UsdMeshDocument Extract(const usd::UsdStageRefPtr& stage,
                        const usd::SdfPath& path, const UsdMeshLimits& limits) {
  const usd::UsdGeomMesh mesh(stage->GetPrimAtPath(path));
  if (!mesh || !mesh.GetPrim().IsActive() || !mesh.GetPrim().IsDefined()) {
    Reject("usd.mesh_not_found", path.GetString());
  }
  CheckMeshFeatures(mesh.GetPrim());
  usd::VtArray<usd::GfVec3f> points;
  usd::VtArray<int> counts;
  usd::VtArray<int> indices;
  if (!mesh.GetPointsAttr().Get(&points) ||
      !mesh.GetFaceVertexCountsAttr().Get(&counts) ||
      !mesh.GetFaceVertexIndicesAttr().Get(&indices)) {
    Reject("usd.missing_topology", path.GetString());
  }
  Admit(points.size(), limits.positions, "points");
  Admit(points.size(),
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
            (3 * sizeof(double)),
        "position address extent");
  Admit(counts.size(), limits.faces, "faces");
  Admit(counts.size(), std::vector<geometry::index_t>{}.max_size() - 1,
        "face offset address extent");
  Admit(indices.size(), limits.corners, "corners");
  Admit(indices.size(), std::vector<geometry::index_t>{}.max_size(),
        "corner address extent");
  if (counts.size() == std::numeric_limits<std::size_t>::max()) {
    Reject("usd.limit", "face offsets");
  }
  UsdMeshDocument document;
  document.mesh_path = path.GetString();
  document.up_axis = usd::UsdGeomGetStageUpAxis(stage).GetString();
  document.meters_per_unit = usd::UsdGeomGetStageMetersPerUnit(stage);
  usd::TfToken orientation;
  usd::TfToken subdivision;
  if (!mesh.GetOrientationAttr().Get(&orientation) ||
      !mesh.GetSubdivisionSchemeAttr().Get(&subdivision) ||
      !mesh.GetDoubleSidedAttr().Get(&document.double_sided)) {
    Reject("usd.invalid_mesh_metadata", path.GetString());
  }
  document.orientation = orientation.GetString();
  document.subdivision_scheme = subdivision.GetString();
  if ((document.orientation != "rightHanded" &&
       document.orientation != "leftHanded") ||
      (document.subdivision_scheme != "none" &&
       document.subdivision_scheme != "catmullClark" &&
       document.subdivision_scheme != "loop" &&
       document.subdivision_scheme != "bilinear")) {
    Reject("usd.invalid_mesh_metadata", path.GetString());
  }
  const auto transform =
      mesh.ComputeLocalToWorldTransform(usd::UsdTimeCode::Default());
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      document.local_to_world[static_cast<std::size_t>(row * 4 + column)] =
          transform[row][column];
    }
  }
  document.mesh.positions.reserve(points.size());
  for (const auto& point : points) {
    if (!std::isfinite(point[0]) || !std::isfinite(point[1]) ||
        !std::isfinite(point[2])) {
      Reject("usd.nonfinite_position", path.GetString());
    }
    document.mesh.positions.Append({point[0], point[1], point[2]});
  }
  document.mesh.face_offsets.reserve(counts.size() + 1);
  std::uint64_t total = 0;
  for (const auto count : counts) {
    if (count < 3 ||
        static_cast<std::uint64_t>(count) > indices.size() - total) {
      Reject("usd.invalid_face_count", path.GetString());
    }
    total += static_cast<std::uint64_t>(count);
    document.mesh.face_offsets.push_back(total);
  }
  if (total != indices.size()) {
    Reject("usd.topology_cardinality", path.GetString());
  }
  document.mesh.corner_vertices.reserve(indices.size());
  for (const auto index : indices) {
    if (index < 0 || static_cast<std::uint64_t>(index) >= points.size()) {
      Reject("usd.invalid_vertex_index", path.GetString());
    }
    document.mesh.corner_vertices.push_back(static_cast<std::uint64_t>(index));
  }
  ScalarAdmission admission(limits.attribute_scalars);
  const auto primvars =
      usd::UsdGeomPrimvarsAPI(mesh).GetPrimvarsWithAuthoredValues();
  const auto normals = mesh.GetNormalsAttr();
  const bool has_normals = normals.HasAuthoredValue();
  if (normals.IsAuthored() && !has_normals) {
    Reject("usd.missing_value", "normals");
  }
  for (const auto& primvar : usd::UsdGeomPrimvarsAPI(mesh).GetPrimvars()) {
    if (primvar.GetAttr().IsAuthored() && !primvar.HasAuthoredValue()) {
      Reject("usd.missing_value", primvar.GetName().GetString());
    }
  }
  Admit(primvars.size() + static_cast<std::size_t>(has_normals),
        limits.primvars, "primvars");
  if (has_normals) {
    UsdPrimvarSource source;
    source.name = "normals";
    source.type = normals.GetTypeName().GetAsToken().GetString();
    source.interpolation = mesh.GetNormalsInterpolation().GetString();
    source.components = 3;
    source.values = ReadValues(normals, 3, admission);
    AddChannel(document, std::move(source), admission);
  }
  for (const auto& primvar : primvars) {
    UsdPrimvarSource source;
    source.name = primvar.GetPrimvarName().GetString();
    source.type = primvar.GetTypeName().GetAsToken().GetString();
    source.interpolation = primvar.GetInterpolation().GetString();
    const auto type = primvar.GetTypeName();
    if (type == usd::SdfValueTypeNames->Normal3fArray ||
        type == usd::SdfValueTypeNames->Normal3dArray) {
      source.components = 3;
    } else if (type == usd::SdfValueTypeNames->TexCoord2fArray ||
               type == usd::SdfValueTypeNames->TexCoord2dArray ||
               ((source.name == "st" || source.name.starts_with("st:")) &&
                (type == usd::SdfValueTypeNames->Float2Array ||
                 type == usd::SdfValueTypeNames->Double2Array))) {
      source.components = 2;
    } else {
      Reject("usd.unsupported_primvar", source.name);
    }
    if (primvar.GetElementSize() != 1 ||
        (has_normals && source.name == "normals")) {
      Reject("usd.unsupported_primvar", source.name);
    }
    source.values = ReadValues(primvar.GetAttr(), source.components, admission);
    usd::VtIntArray primvar_indices;
    if (primvar.GetIndices(&primvar_indices)) {
      admission.Take(primvar_indices.size(), 1, source.name);
      source.indices = std::vector<std::int32_t>(primvar_indices.begin(),
                                                 primvar_indices.end());
    } else if (primvar.GetIndicesAttr().HasAuthoredValue()) {
      Reject("usd.invalid_primvar_index", source.name);
    }
    source.unauthored_values_index = primvar.GetUnauthoredValuesIndex();
    AddChannel(document, std::move(source), admission);
  }
  return document;
}
}  // namespace

UsdMeshImportResult ReadUsdMesh(const std::filesystem::path& path,
                                const std::string& mesh_path,
                                const UsdMeshLimits& limits) {
  usd::TfErrorMark errors;
  UsdMeshImportResult result;
  try {
    if (limits.source_bytes == 0 || limits.positions == 0 ||
        limits.faces == 0 || limits.corners == 0 || limits.primvars == 0 ||
        limits.attribute_scalars == 0) {
      Reject("usd.invalid_limits", "limits");
    }
    const auto extension = path.extension().string();
    if (extension != ".usda" && extension != ".usdc") {
      Reject("usd.unsupported_format", "source");
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(path, filesystem_error)) {
      Reject("usd.source_unavailable", "source");
    }
    const auto bytes = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
      Reject("usd.source_unavailable", "source");
    }
    Admit(bytes, limits.source_bytes, "source bytes");
    std::string path_error;
    if (!usd::SdfPath::IsValidPathString(mesh_path, &path_error)) {
      Reject("usd.invalid_mesh_path", mesh_path);
    }
    const usd::SdfPath selected(mesh_path);
    if (!selected.IsAbsolutePath() || !selected.IsPrimPath() ||
        selected.ContainsPrimVariantSelection()) {
      Reject("usd.invalid_mesh_path", mesh_path);
    }
    const auto utf8 = path.u8string();
    const std::string source_path(utf8.begin(), utf8.end());
    auto layer = usd::SdfLayer::OpenAsAnonymous(source_path);
    if (!layer || !errors.IsClean()) {
      Reject("usd.parse_failed", "source");
    }
    const auto format = layer->GetFileFormat()->GetFormatId().GetString();
    if (format != "usda" && format != "usdc") {
      Reject("usd.unsupported_format", "source");
    }
    CheckLayer(layer);
    const usd::UsdStageCacheContext cache_block(usd::UsdBlockStageCaches);
    auto stage = usd::UsdStage::Open(layer, usd::UsdStage::LoadNone);
    if (!stage || !errors.IsClean()) {
      Reject("usd.parse_failed", "stage");
    }
    auto document = Extract(stage, selected, limits);
    if (!errors.IsClean()) {
      Reject("usd.extraction_failed", mesh_path);
    }
    // No SDK handle or array escapes. The result contains ordinary owned
    // values.
    stage.Reset();
    layer.Reset();
    result.document = std::move(document);
    result.diagnostics.push_back({"usd.local_space_cage", mesh_path, {}});
    result.diagnostics.push_back({"usd.selected_mesh_only", mesh_path, {}});
  } catch (const Failure& failure) {
    result.diagnostics.push_back({failure.code, failure.subject, {}});
  }
  errors.Clear();
  return result;
}
}  // namespace meshvale::interchange
