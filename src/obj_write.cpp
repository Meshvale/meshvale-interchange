// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "meshvale/interchange/obj.h"

namespace meshvale::interchange {
namespace {
using namespace geometry;
bool name_safe(const std::string& value, bool token = false) {
  if (value.find_first_of("\r\n#") != std::string::npos ||
      value.find('\0') != std::string::npos)
    return false;
  if (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                         value.back() == ' ' || value.back() == '\t'))
    return false;
  return !token ||
         (!value.empty() && value.find_first_of(" \t") == std::string::npos);
}
bool authored(const Attribute* a, index_t row) {
  return a && (!a->present || a->present->at(row) == 1);
}
bool close(double a, double b) {
  return std::isfinite(a) && std::isfinite(b) &&
         std::abs(a - b) <= 8 * std::numeric_limits<double>::epsilon() *
                                std::max(1.0, std::abs(a));
}
}  // namespace

ObjExportResult write_obj(const ObjDocument& source) {
  ObjExportResult result;
  result.diagnostics = inspect_storage(source.mesh);
  if (!result.diagnostics.empty()) return result;
  auto issue = [&](const char* code, std::string subject,
                   std::optional<index_t> row = std::nullopt) {
    result.diagnostics.push_back({code, std::move(subject), row});
  };
  const Attribute *uv = nullptr, *normal = nullptr, *material = nullptr,
                  *part = nullptr, *smoothing = nullptr;
  for (const auto& a : source.mesh.attributes) {
    const Attribute** slot = nullptr;
    bool valid = false;
    if (a.name == "obj.uv0") {
      slot = &uv;
      valid = a.domain == AttributeDomain::corner && a.semantic == "texcoord" &&
              a.set_index == 0 && a.components == 2 &&
              std::holds_alternative<std::vector<double>>(a.values);
    } else if (a.name == "obj.normal") {
      slot = &normal;
      valid = a.domain == AttributeDomain::corner && a.semantic == "normal" &&
              !a.set_index && a.components == 3 &&
              std::holds_alternative<std::vector<double>>(a.values);
    } else if (a.name == "obj.material") {
      slot = &material;
      valid = a.domain == AttributeDomain::face &&
              a.semantic == "material_index" && !a.set_index &&
              a.components == 1 &&
              std::holds_alternative<std::vector<std::int32_t>>(a.values) &&
              !a.present;
    } else if (a.name == "obj.part" || a.name == "obj.smoothing_group") {
      const bool is_part = a.name == "obj.part";
      slot = is_part ? &part : &smoothing;
      valid = a.domain == AttributeDomain::face && a.semantic == "label" &&
              !a.set_index && a.components == 1 &&
              std::holds_alternative<std::vector<std::uint32_t>>(a.values) &&
              !a.present;
    }
    if (!slot || !valid || a.offsets || !a.metadata.empty() || (slot && *slot))
      issue("obj.unsupported_attribute", a.name);
    else
      *slot = &a;
  }
  if (!result.diagnostics.empty()) return result;
  for (const auto& p : source.parts) {
    if (!name_safe(p.object) ||
        !std::all_of(p.groups.begin(), p.groups.end(),
                     [](const auto& group) { return name_safe(group, true); }))
      issue("obj.unsafe_part_name", "parts");
  }
  for (const auto& name : source.material_names)
    if (name.empty() || !name_safe(name))
      issue("obj.unsafe_material_name", "materials");
  const auto material_check = read_obj("", source.material_library);
  if (!material_check.document ||
      material_check.document->material_names != source.material_names)
    issue("obj.material_library_mismatch", "materials");
  auto material_at = [&](index_t face) -> std::int32_t {
    return material
               ? std::get<std::vector<std::int32_t>>(material->values)[face]
               : -1;
  };
  auto part_at = [&](index_t face) -> ObjPart {
    if (!part) return {};
    const auto id = std::get<std::vector<std::uint32_t>>(part->values)[face];
    return id < source.parts.size() ? source.parts[id] : ObjPart{};
  };
  auto smoothing_at = [&](index_t face) -> std::uint32_t {
    return smoothing
               ? std::get<std::vector<std::uint32_t>>(smoothing->values)[face]
               : 0;
  };
  for (index_t face = 0; face < source.mesh.face_count(); ++face) {
    const auto id = material_at(face);
    if (id < -1 || (id >= 0 && static_cast<std::size_t>(id) >=
                                   source.material_names.size()))
      issue("obj.material_range", "obj.material", face);
    if (part && std::get<std::vector<std::uint32_t>>(part->values)[face] >=
                    source.parts.size())
      issue("obj.part_range", "obj.part", face);
  }
  for (const auto* a : {uv, normal}) {
    if (!a) continue;
    const auto& values = std::get<std::vector<double>>(a->values);
    for (index_t c = 0; c < source.mesh.corner_vertices.size(); ++c)
      if (authored(a, c))
        for (std::uint32_t k = 0; k < a->components; ++k)
          if (!std::isfinite(values[c * a->components + k]))
            issue("obj.nonfinite_attribute", a->name, c);
  }
  if (!result.diagnostics.empty()) return result;
  std::ostringstream obj;
  obj.imbue(std::locale::classic());
  obj << std::setprecision(std::numeric_limits<double>::max_digits10);
  obj << "# Meshvale polygon export\n";
  if (!source.material_library.empty()) obj << "mtllib materials.mtl\n";
  for (std::size_t row = 0; row < source.mesh.positions.size(); ++row) {
    const auto position = source.mesh.positions.Get(row);
    obj << "v " << position[0] << ' ' << position[1] << ' ' << position[2]
        << '\n';
  }
  std::vector<index_t> uv_indices(source.mesh.corner_vertices.size(), 0),
      normal_indices(uv_indices.size(), 0);
  for (const auto* a : {uv, normal}) {
    if (!a) continue;
    auto& indices = a == uv ? uv_indices : normal_indices;
    const auto& values = std::get<std::vector<double>>(a->values);
    index_t next = 1;
    for (index_t c = 0; c < indices.size(); ++c) {
      if (!authored(a, c)) continue;
      indices[c] = next++;
      obj << (a == uv ? "vt" : "vn");
      for (std::uint32_t k = 0; k < a->components; ++k)
        obj << ' ' << values[c * a->components + k];
      obj << '\n';
    }
  }
  ObjPart previous_part;
  std::int32_t previous_material = -1;
  std::uint32_t previous_smoothing = 0;
  for (index_t face = 0; face < source.mesh.face_count(); ++face) {
    const auto member = part_at(face);
    if (face == 0 || member != previous_part) {
      obj << "o " << member.object << "\ng";
      for (const auto& group : member.groups) obj << ' ' << group;
      obj << '\n';
      previous_part = member;
    }
    const auto assigned = material_at(face);
    if (assigned != previous_material) {
      obj << "usemtl ";
      if (assigned >= 0)
        obj << source.material_names[static_cast<std::size_t>(assigned)];
      obj << '\n';
      previous_material = assigned;
    }
    const auto smooth = smoothing_at(face);
    if (face == 0 || smooth != previous_smoothing) {
      obj << "s " << smooth << '\n';
      previous_smoothing = smooth;
    }
    obj << 'f';
    for (index_t c = source.mesh.face_offsets[face];
         c < source.mesh.face_offsets[face + 1]; ++c) {
      obj << ' ' << source.mesh.corner_vertices[c] + 1;
      if (uv_indices[c] || normal_indices[c]) {
        obj << '/';
        if (uv_indices[c]) obj << uv_indices[c];
        if (normal_indices[c]) obj << '/' << normal_indices[c];
      }
    }
    obj << '\n';
  }
  ObjText text{obj.str(), source.material_library};
  const auto reloaded = read_obj(text.obj, text.mtl);
  if (!reloaded.document || !inspect_storage(reloaded.document->mesh).empty()) {
    issue("obj.reload_failed", "output");
    return result;
  }
  const auto& restored = *reloaded.document;
  bool verified =
      restored.mesh.face_offsets == source.mesh.face_offsets &&
      restored.mesh.corner_vertices == source.mesh.corner_vertices &&
      restored.mesh.positions.size() == source.mesh.positions.size() &&
      restored.material_library == source.material_library &&
      restored.material_names == source.material_names;
  if (verified) {
    for (index_t v = 0; v < source.mesh.positions.size(); ++v) {
      const auto source_position = source.mesh.positions.Get(v);
      const auto restored_position = restored.mesh.positions.Get(v);
      for (std::size_t k = 0; k < 3; ++k)
        verified &= close(source_position[k], restored_position[k]);
    }
    for (index_t face = 0; face < source.mesh.face_count(); ++face) {
      verified &=
          material_at(face) == std::get<std::vector<std::int32_t>>(
                                   restored.mesh.attributes[2].values)[face];
      verified &=
          part_at(face) == restored.parts[std::get<std::vector<std::uint32_t>>(
                               restored.mesh.attributes[3].values)[face]];
      verified &=
          smoothing_at(face) == std::get<std::vector<std::uint32_t>>(
                                    restored.mesh.attributes[4].values)[face];
    }
    for (std::size_t ordinal = 0; ordinal < 2; ++ordinal) {
      const auto* before = ordinal == 0 ? uv : normal;
      const auto& after = restored.mesh.attributes[ordinal];
      for (index_t c = 0; c < source.mesh.corner_vertices.size(); ++c) {
        verified &= authored(before, c) == authored(&after, c);
        if (!authored(before, c)) continue;
        const auto& a = std::get<std::vector<double>>(before->values);
        const auto& b = std::get<std::vector<double>>(after.values);
        for (std::uint32_t k = 0; k < before->components; ++k)
          verified &= close(a[c * before->components + k],
                            b[c * before->components + k]);
      }
    }
  }
  if (!verified) {
    issue("obj.roundtrip_mismatch", "output");
    return result;
  }
  result.text = std::move(text);
  result.diagnostics.push_back(
      {"obj.corner_pools_expanded", "UV/normal file indices", std::nullopt});
  return result;
}
}  // namespace meshvale::interchange
