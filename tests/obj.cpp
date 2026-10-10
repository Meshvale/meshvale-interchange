// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/obj.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace meshvale::geometry;
using namespace meshvale::interchange;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bool has(const std::vector<Diagnostic>& issues, const std::string& code) {
  return std::any_of(issues.begin(), issues.end(),
                     [&](const auto& d) { return d.code == code; });
}
const std::string mtl =
    "# Original material fixture\nnewmtl red\nKd 0.1 0.2 0.3\nmap_Kd "
    "texture.png\n"
    "newmtl blue\nKd 0 0 1\nnewmtl green\nKd 0 1 0\n";
const std::string obj = R"(mtllib sample.mtl
v 0 0 0
v 1 0 0
v 0 1 0
v 2 0 0
v 2 1 0
v 4 0 0
v 6 0 0
v 6 2 0
v 5 1 0
v 4 2 0
vt 0 0
vt 1 0
vt 0 1
vt 0.25 0
vt 1 0
vt 1 1
vt 0.25 1
vt 0 0
vt 1 0
vt 1 1
vt 0.5 0.5
vt 0 1
vn 0 0 1
o main object
g sheet seam
s 2
usemtl red
f 1/1/1 2/2/1 3/3/1
g quad
usemtl blue
f 2/4/1 4/5/1 5/6/1 3/7/1
o separate
g pentagon
s off
usemtl green
f -5/8/1 -4/9/1 -3/10/1 -2/11/1 -1/12/1
)";

void mixed_roundtrip() {
  const auto imported = read_obj(obj, mtl);
  require(imported.document.has_value(), "mixed OBJ import failed");
  const auto& document = *imported.document;
  require(inspect_storage(document.mesh).empty(),
          "mixed imported storage invalid");
  require(document.mesh.face_offsets == std::vector<index_t>({0, 3, 7, 12}),
          "parser triangulated polygons");
  require(document.mesh.corner_vertices ==
              std::vector<index_t>({0, 1, 2, 1, 3, 4, 2, 5, 6, 7, 8, 9}),
          "source/negative indices changed");
  const auto& uv =
      std::get<std::vector<double>>(document.mesh.attributes[0].values);
  require(uv[2] == 1 && uv[6] == 0.25, "UV seam at shared vertex lost");
  require(document.parts[0] == ObjPart{"main object", {"sheet", "seam"}} &&
              document.parts[2].object == "separate",
          "object/group state not retained");
  require(
      std::get<std::vector<std::int32_t>>(document.mesh.attributes[2].values) ==
          std::vector<std::int32_t>({0, 1, 2}),
      "material bindings changed");
  require(std::get<std::vector<std::uint32_t>>(
              document.mesh.attributes[4].values) ==
              std::vector<std::uint32_t>({2, 2, 0}),
          "smoothing groups changed");
  const auto exported = write_obj(document);
  if (!exported.text)
    for (const auto& d : exported.diagnostics)
      std::cerr << d.code << ": " << d.subject << '\n';
  require(exported.text.has_value(), "mixed OBJ roundtrip failed");
  require(exported.text->mtl == mtl, "opaque material bytes rewritten");
  const auto restored = read_obj(exported.text->obj, exported.text->mtl);
  require(restored.document && restored.document->mesh.corner_vertices ==
                                   document.mesh.corner_vertices,
          "reloaded loops differ");
  require(std::get<std::vector<double>>(
              restored.document->mesh.attributes[0].values) == uv,
          "dyadic corner UVs differ");
  require(write_obj(document).text->obj == exported.text->obj,
          "export not deterministic");
}
void missing_values_and_unbound_faces() {
  const std::string input =
      "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.125\nvn 0 0 1\n"
      "usemtl red\nf 1/1/1 2//1 3\nusemtl\nf 1 2 3\n";
  const auto loaded = read_obj(input, mtl);
  require(loaded.document.has_value(), "missing corner fixture import failed");
  const auto& mesh = loaded.document->mesh;
  require(*mesh.attributes[0].present ==
              std::vector<std::uint8_t>({1, 0, 0, 0, 0, 0}),
          "UV missingness changed");
  require(*mesh.attributes[1].present ==
              std::vector<std::uint8_t>({1, 1, 0, 0, 0, 0}),
          "normal missingness changed");
  require(std::get<std::vector<double>>(mesh.attributes[0].values)[1] == 0,
          "one-component UV default changed");
  require(std::get<std::vector<std::int32_t>>(mesh.attributes[2].values) ==
              std::vector<std::int32_t>({0, -1}),
          "unbound material became bound");
  require(write_obj(*loaded.document).text.has_value(),
          "missing/unbound roundtrip failed");
  ObjDocument empty;
  require(write_obj(empty).text.has_value(), "empty document export failed");
}
void malformed_and_unsupported_inputs() {
  const std::string base = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
  const auto raw = read_obj(base + "f 1 2 99\n");
  require(raw.document && has(raw.diagnostics, "mesh.vertex_range"),
          "malformed positive index hidden");
  require(!write_obj(*raw.document).text, "malformed mesh exported");
  for (const auto& face :
       {"f 1/x 2 3\n", "f 0 2 3\n", "f 1/99 2 3\n", "f -99 2 3\n", "f 1 2\n"})
    require(!read_obj(base + face).document, "invalid face accepted silently");
  for (const auto& directive :
       {"l 1 2\n", "p 1\n", "v 1 2 3 0.5 0.5 0.5\n", "vt 1 2 3\n", "s on\n",
        "curv 0 1 1 2 3\n", "v nan 0 0\n"})
    require(!read_obj(base + directive).document,
            "unsupported input silently dropped");
  require(!read_obj(base + "f 1 2 " + std::string(1, '\\') + "\n3\n").document,
          "line continuation silently ignored");
  require(!read_obj(base + "usemtl absent\nf 1 2 3\n", mtl).document,
          "missing material silently unbound");
  require(!read_obj(base + "f 1 2 3\n", "newmtl red\nnewmtl red\n").document,
          "duplicate materials ambiguous");
}
void export_limits() {
  const auto loaded = read_obj(obj, mtl);
  auto document = *loaded.document;
  auto extra = document.mesh.attributes[0];
  extra.name = "lightmap";
  extra.set_index = 1;
  document.mesh.attributes.push_back(extra);
  require(has(write_obj(document).diagnostics, "obj.unsupported_attribute"),
          "second UV set dropped");
  document = *loaded.document;
  document.parts[0].object = "safe\nf 1 2 3";
  require(has(write_obj(document).diagnostics, "obj.unsafe_part_name"),
          "name introduced an extra directive");
  document = *loaded.document;
  document.material_names[0] = "other";
  require(has(write_obj(document).diagnostics, "obj.material_library_mismatch"),
          "material names disconnected from library");
  document = *loaded.document;
  std::get<std::vector<double>>(document.mesh.attributes[0].values)[0] =
      std::numeric_limits<double>::infinity();
  require(has(write_obj(document).diagnostics, "obj.nonfinite_attribute"),
          "infinite UV exported");
}
struct DecimalComma : std::numpunct<char> {
  char do_decimal_point() const override { return ','; }
};
void numeric_roundtrip_and_locale() {
  ObjDocument document;
  document.mesh.positions = {{0.1, -0.3, 1.2345678901234567},
                             {1e200, 1e-200, -1e100},
                             {-7.123456789, 0.2, 3.14}};
  document.mesh.face_offsets = {0, 3};
  document.mesh.corner_vertices = {0, 1, 2};
  Attribute uv;
  uv.domain = AttributeDomain::corner;
  uv.name = "obj.uv0";
  uv.semantic = "texcoord";
  uv.set_index = 0;
  uv.components = 2;
  uv.values =
      std::vector<double>{0.1, 1e200, -0.3, 1e-200, 1.2345678901234567, 0.2};
  Attribute normal;
  normal.domain = AttributeDomain::corner;
  normal.name = "obj.normal";
  normal.semantic = "normal";
  normal.components = 3;
  normal.values =
      std::vector<double>{0.1, -0.3, 0.9, 0.2, 0.4, 0.8, 0.15, 0.25, 0.95};
  document.mesh.attributes = {uv, normal};
  const auto original = std::locale();
  std::locale::global(std::locale(original, new DecimalComma));
  const auto exported = write_obj(document);
  std::locale::global(original);
  require(exported.text.has_value(),
          "non-dyadic/extreme roundtrip outside declared bound");
  require(exported.text->obj.find(',') == std::string::npos,
          "locale-dependent numeric output");
  const auto decoded = read_obj(exported.text->obj);
  require(decoded.document && decoded.document->mesh.positions[0][0] != 0,
          "decimal scalar collapsed");
  auto within_bound = [](double source, double result) {
    return std::abs(source - result) <=
           std::max(1.0, std::abs(source)) *
               (8.0 * std::numeric_limits<double>::epsilon());
  };
  for (std::size_t v = 0; v < document.mesh.positions.size(); ++v)
    for (std::size_t k = 0; k < 3; ++k)
      require(within_bound(document.mesh.positions[v][k],
                           decoded.document->mesh.positions[v][k]),
              "position exceeds text roundtrip bound");
  for (std::size_t a = 0; a < 2; ++a) {
    const auto& before =
        std::get<std::vector<double>>(document.mesh.attributes[a].values);
    const auto& after = std::get<std::vector<double>>(
        decoded.document->mesh.attributes[a].values);
    for (std::size_t i = 0; i < before.size(); ++i)
      require(within_bound(before[i], after[i]),
              "corner scalar exceeds text roundtrip bound");
  }
}
int main() {
  try {
    mixed_roundtrip();
    missing_values_and_unbound_faces();
    malformed_and_unsupported_inputs();
    export_limits();
    numeric_roundtrip_and_locale();
    std::cout << "Five OBJ/MTL interchange suites passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
