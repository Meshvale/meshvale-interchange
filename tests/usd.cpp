// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/usd.h"

#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {
namespace usd = PXR_NS;
namespace interchange = meshvale::interchange;
std::filesystem::path fixture_root;
int checks = 0;

void Check(bool condition, const std::string& subject) {
  ++checks;
  if (!condition) throw std::runtime_error(subject);
}

std::string Base(const std::string& properties = {},
                 const std::string& metadata = {}) {
  return "#usda 1.0\n(upAxis = \"Z\"\n metersPerUnit = 0.1)\n"
         "def Mesh \"Mesh\" " +
         metadata +
         " {\n"
         "point3f[] points = [(0,0,0),(1,0,0),(1,1,0),(0,1,0),(2,0,0)]\n"
         "int[] faceVertexCounts = [4,3]\n"
         "int[] faceVertexIndices = [0,1,2,3,1,4,2]\n"
         "uniform token subdivisionScheme = \"none\"\n" +
         properties + "\n}\n";
}

std::filesystem::path Fixture(const std::string& name, const std::string& text,
                              const std::string& extension = ".usda") {
  auto layer = usd::SdfLayer::CreateAnonymous("fixture.usda");
  Check(layer && layer->ImportFromString(text), "fixture parses: " + name);
  const auto path = fixture_root / (name + extension);
  Check(layer->Export(path.string()), "fixture exports: " + name);
  layer.Reset();
  return path;
}

void Reject(const std::string& name, const std::string& text,
            const std::string& code) {
  auto result = interchange::ReadUsdMesh(Fixture(name, text), "/Mesh");
  Check(!result.document && result.diagnostics.size() == 1 &&
            result.diagnostics[0].code == code,
        name + " rejected as " + code);
}

void RoundTrips() {
  const std::string attributes =
      "normal3f[] normals = [(0,0,1),(0,0,-1)] (interpolation = \"uniform\")\n"
      "texCoord2f[] primvars:st = [(0,0),(1,0),(1,1),(0,1),(0.5,0.5)] "
      "(interpolation = \"faceVarying\"\n unauthoredValuesIndex = 4)\n"
      "int[] primvars:st:indices = [0,1,2,3,4,0,0]\n"
      "texCoord2d[] primvars:detail = "
      "[(0.125,0.25),(0.5,0.75),(1,1),(2,2),(3,3)] "
      "(interpolation = \"vertex\")\n"
      "uniform token orientation = \"leftHanded\"\n"
      "double3 xformOp:translate = (10,20,30)\n"
      "uniform token[] xformOpOrder = [\"xformOp:translate\"]";
  for (const std::string extension : {".usda", ".usdc"}) {
    const auto path = Fixture("mixed" + extension, Base(attributes), extension);
    auto result = interchange::ReadUsdMesh(path, "/Mesh");
    Check(result.document.has_value(), "mixed import " + extension);
    Check(result.diagnostics.size() == 2, "coverage notices " + extension);
    const auto& document = *result.document;
    Check(document.mesh.positions.size() == 5 &&
              document.mesh.positions.Get(4)[0] == 2,
          "owned positions " + extension);
    Check(document.mesh.face_offsets == std::vector<std::uint64_t>{0, 4, 7} &&
              document.mesh.corner_vertices ==
                  std::vector<std::uint64_t>{0, 1, 2, 3, 1, 4, 2},
          "mixed polygons " + extension);
    Check(document.up_axis == "Z" && document.meters_per_unit == 0.1 &&
              document.orientation == "leftHanded" &&
              document.subdivision_scheme == "none" &&
              document.local_to_world[12] == 10 &&
              document.local_to_world[13] == 20 &&
              document.local_to_world[14] == 30 &&
              document.mesh.positions.Get(0)[0] == 0,
          "units/orientation/unbaked transform " + extension);
    Check(document.mesh.attributes.size() == 3 && document.primvars.size() == 3,
          "three channels " + extension);
    bool seam = false;
    bool precise = false;
    for (std::size_t i = 0; i < document.primvars.size(); ++i) {
      const auto& source = document.primvars[i];
      const auto& channel = document.mesh.attributes[i];
      if (source.name == "st") {
        seam =
            source.indices == std::vector<std::int32_t>{0, 1, 2, 3, 4, 0, 0} &&
            source.unauthored_values_index == 4 &&
            std::get<std::vector<float>>(source.values).size() == 10 &&
            channel.domain == meshvale::geometry::AttributeDomain::corner &&
            channel.present == std::vector<std::uint8_t>{1, 1, 1, 1, 0, 1, 1} &&
            std::get<std::vector<float>>(channel.values).size() == 14 &&
            std::get<std::vector<float>>(channel.values)[4] == 1 &&
            std::get<std::vector<float>>(channel.values)[12] == 0;
      }
      if (source.name == "detail") {
        precise = std::get<std::vector<double>>(channel.values)[0] == 0.125 &&
                  !source.indices &&
                  channel.domain == meshvale::geometry::AttributeDomain::vertex;
      }
    }
    Check(seam && precise,
          "retained indexing/missingness/scalar type " + extension);
    Check(meshvale::geometry::inspect_storage(document.mesh).empty(),
          "valid owned storage " + extension);
    std::filesystem::remove(path);
    auto copied = document;
    result.document.reset();
    Check(copied.mesh.positions.Get(4)[0] == 2 && copied.primvars.size() == 3,
          "owned output after file/SDK/result destruction " + extension);
  }
}

void Interpolations() {
  const std::vector<std::string> interpolations = {
      "constant", "uniform", "vertex", "varying", "faceVarying"};
  const std::vector<int> rows = {1, 2, 5, 5, 7};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    std::string values;
    for (int row = 0; row < rows[i]; ++row) {
      if (row) values += ',';
      values += "(" + std::to_string(row) + ",0)";
    }
    auto result = interchange::ReadUsdMesh(
        Fixture(interpolations[i],
                Base("float2[] primvars:st = [" + values +
                     "] (interpolation = \"" + interpolations[i] + "\")")),
        "/Mesh");
    Check(result.document &&
              result.document->primvars[0].interpolation == interpolations[i],
          "interpolation " + interpolations[i]);
    Check(!result.document->mesh.attributes[0].present,
          "authored presence " + interpolations[i]);
  }
  auto normals = interchange::ReadUsdMesh(
      Fixture("indexed-normal",
              Base("normal3d[] primvars:n = [(0,0,1),(0,1,0)] "
                   "(interpolation = \"uniform\")\n"
                   "int[] primvars:n:indices = [1,0]")),
      "/Mesh");
  Check(normals.document &&
            std::get<std::vector<double>>(
                normals.document->mesh.attributes[0].values)[1] == 1,
        "indexed float64 normal");
}

void Failures() {
  Reject("unsupported", Base("float[] primvars:weight = [1]"),
         "usd.unsupported_primvar");
  Reject("half", Base("texCoord2h[] primvars:st = [(0,0)]"),
         "usd.unsupported_primvar");
  Reject("missing-primvar", Base("texCoord2f[] primvars:st"),
         "usd.missing_value");
  Reject("blocked-primvar", Base("texCoord2f[] primvars:st = None"),
         "usd.missing_value");
  Reject("missing-normal", Base("normal3f[] normals"), "usd.missing_value");
  Reject("custom", Base("custom int customData = 4"),
         "usd.unsupported_attribute");
  Reject("holes", Base("int[] holeIndices = [0]"), "usd.unsupported_attribute");
  Reject("crease", Base("int[] creaseIndices = [0,1]"),
         "usd.unsupported_attribute");
  Reject("skin", Base("int[] primvars:skel:jointIndices = [0]"),
         "usd.unsupported_primvar");
  Reject("binding", Base("rel skel:skeleton = </Skeleton>"),
         "usd.unsupported_binding");
  Reject("material", Base("rel material:binding = </Material>"),
         "usd.unsupported_binding");
  Reject("reference", Base({}, "(prepend references = @missing.usda@)"),
         "usd.unsupported_composition");
  Reject("internal-reference", Base({}, "(prepend references = </Other>)"),
         "usd.unsupported_composition");
  Reject("payload", Base({}, "(prepend payload = @missing.usdc@)"),
         "usd.unsupported_composition");
  Reject("instance", Base({}, "(instanceable = true)"),
         "usd.unsupported_instance");
  Reject("samples", Base("point3f[] points.timeSamples = {1: [(0,0,0)]}"),
         "usd.unsupported_animation");
  Reject(
      "negative-index",
      Base("texCoord2f[] primvars:st = [(0,0)] (interpolation = \"uniform\")\n"
           "int[] primvars:st:indices = [-1,0]"),
      "usd.invalid_primvar_index");
  Reject(
      "overflow-index",
      Base("texCoord2f[] primvars:st = [(0,0)] (interpolation = \"uniform\")\n"
           "int[] primvars:st:indices = [2147483647,0]"),
      "usd.invalid_primvar_index");
  Reject(
      "missing-index",
      Base("texCoord2f[] primvars:st = [(0,0)] (interpolation = \"uniform\")\n"
           "int[] primvars:st:indices = [0]"),
      "usd.primvar_cardinality");
  Reject("element-size",
         Base("texCoord2f[] primvars:st = [(0,0)] (elementSize = 2)"),
         "usd.unsupported_primvar");
  auto invalid = Base();
  invalid.replace(invalid.find("[0,1,2,3,1,4,2]"), 15, "[-1,1,2,3,1,4,2]");
  Reject("negative-vertex", invalid, "usd.invalid_vertex_index");
  invalid = Base();
  invalid.replace(invalid.find("[0,1,2,3,1,4,2]"), 15,
                  "[2147483647,1,2,3,1,4,2]");
  Reject("overflow-vertex", invalid, "usd.invalid_vertex_index");
  invalid = Base();
  invalid.replace(invalid.find("[4,3]"), 5, "[4,-3]");
  Reject("negative-count", invalid, "usd.invalid_face_count");
  invalid = Base();
  invalid.replace(invalid.find("[4,3]"), 5, "[2147483647,3]");
  Reject("overflow-count", invalid, "usd.invalid_face_count");
  invalid = Base();
  invalid.replace(invalid.find("[4,3]"), 5, "[3,3]");
  Reject("count-mismatch", invalid, "usd.topology_cardinality");
  invalid = Base();
  invalid.replace(invalid.find("(0,0,0)"), 7, "(inf,0,0)");
  Reject("nonfinite-position", invalid, "usd.nonfinite_position");
  Reject("sublayer", "#usda 1.0\n(subLayers = [@missing.usda@])\n",
         "usd.unsupported_composition");
  Reject("variant", Base({}, "(variants = { string shape = \"a\" })"),
         "usd.unsupported_composition");
}

void FreshRoot() {
  for (const std::string extension : {".usda", ".usdc"}) {
    const auto path = Fixture("fresh-root", Base(), extension);
    auto cached_layer = usd::SdfLayer::FindOrOpen(path.string());
    auto cached_stage = usd::UsdStage::Open(cached_layer);
    Check(cached_layer && cached_stage, "persistent SDK cache exists");
    auto original = interchange::ReadUsdMesh(path, "/Mesh");
    auto changed = Base();
    changed.replace(changed.find("(0,0,0)"), 7, "(-2,0,0)");
    Fixture("fresh-root", changed, extension);
    auto current = interchange::ReadUsdMesh(path, "/Mesh");
    Check(original.document && current.document &&
              original.document->mesh.positions.Get(0)[0] == 0 &&
              current.document->mesh.positions.Get(0)[0] == -2,
          "anonymous root sees repeated-path replacement " + extension);
    cached_stage.Reset();
    cached_layer.Reset();
  }
}

void LimitsAndPaths() {
  const auto path = Fixture("limits", Base());
  for (int field = 0; field < 4; ++field) {
    interchange::UsdMeshLimits limits;
    if (field == 0) limits.source_bytes = std::filesystem::file_size(path) - 1;
    if (field == 1) limits.positions = 4;
    if (field == 2) limits.faces = 1;
    if (field == 3) limits.corners = 6;
    auto result = interchange::ReadUsdMesh(path, "/Mesh", limits);
    Check(!result.document && result.diagnostics[0].code == "usd.limit",
          "admission cap " + std::to_string(field));
  }
  auto limits = interchange::UsdMeshLimits{};
  limits.source_bytes = std::filesystem::file_size(path);
  limits.positions = 5;
  limits.faces = 2;
  limits.corners = 7;
  Check(interchange::ReadUsdMesh(path, "/Mesh", limits).document.has_value(),
        "exact cardinality limits");
  limits.primvars = 0;
  auto zero = interchange::ReadUsdMesh(path, "/Mesh", limits);
  Check(!zero.document && zero.diagnostics[0].code == "usd.invalid_limits",
        "zero limit");
  for (const auto& selected : {"Mesh", "/Mesh.points", "", "/Other"}) {
    Check(!interchange::ReadUsdMesh(path, selected).document,
          "invalid selection");
  }
  Check(!interchange::ReadUsdMesh(fixture_root / "missing.usda", "/Mesh")
             .document,
        "missing file");
  Check(!interchange::ReadUsdMesh(fixture_root / "archive.usdz", "/Mesh")
             .document,
        "USDZ rejected");
  const auto attributes =
      Fixture("scalar-limits", Base("texCoord2f[] primvars:st = [(0,0),(1,1)] "
                                    "(interpolation = \"uniform\")"));
  limits = interchange::UsdMeshLimits{};
  limits.attribute_scalars = 8;
  Check(interchange::ReadUsdMesh(attributes, "/Mesh", limits)
            .document.has_value(),
        "exact source+expanded scalar admission");
  limits.attribute_scalars = 7;
  auto short_limit = interchange::ReadUsdMesh(attributes, "/Mesh", limits);
  Check(!short_limit.document && short_limit.diagnostics[0].code == "usd.limit",
        "one less scalar admission");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) return 2;
    fixture_root = argv[1];
    std::filesystem::create_directories(fixture_root);
    RoundTrips();
    Interpolations();
    Failures();
    LimitsAndPaths();
    FreshRoot();
    std::cout << "USD public adapter checks: " << checks << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
