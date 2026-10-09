// SPDX-License-Identifier: Apache-2.0
#include <tiny_obj_loader.h>

#include <algorithm>
#include <filesystem>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "obj_file_detail.h"

namespace meshvale::interchange {
namespace {
using namespace detail;
std::vector<fs::path> libraries(const std::string& obj, const fs::path& base) {
  std::vector<fs::path> paths;
  std::istringstream lines(obj);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream record(clean_line(line));
    std::string directive, name;
    record >> directive;
    if (directive != "mtllib") continue;
    bool any = false;
    while (record >> name) {
      any = true;
      const auto path = reference(base, name);
      if (std::find(paths.begin(), paths.end(), path) == paths.end())
        paths.push_back(path);
    }
    if (!any) fail("obj.resource_syntax", "mtllib");
  }
  return paths;
}
std::vector<fs::path> textures(const std::string& mtl, const fs::path& base) {
  static const std::set<std::string> scalars{
      "newmtl", "Ka", "Kd",  "Ks",    "Ke",        "Tf", "Ns",
      "Ni",     "d",  "Tr",  "illum", "sharpness", "Pr", "Pm",
      "Ps",     "Pc", "Pcr", "aniso", "anisor"};
  static const std::set<std::string> maps{
      "map_Ka",   "map_Kd", "map_Ks",   "map_Ns",   "map_d", "map_bump",
      "map_Bump", "bump",   "map_disp", "map_Disp", "disp",  "refl",
      "map_Pr",   "map_Pm", "map_Ps",   "map_Ke",   "norm"};
  std::vector<fs::path> paths;
  std::istringstream lines(mtl);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream record(clean_line(line));
    std::string directive, rest;
    record >> directive;
    if (directive.empty() || scalars.contains(directive)) continue;
    if (!maps.contains(directive))
      fail("obj.unsupported_mtl_directive", directive);
    std::getline(record >> std::ws, rest);
    if (rest.find('"') != std::string::npos ||
        rest.find('\'') != std::string::npos)
      fail("obj.resource_syntax", directive);
    std::string name;
    tinyobj::texture_option_t options{};
    if (!tinyobj::ParseTextureNameAndOption(&name, &options, rest.c_str()) ||
        name.empty())
      fail("obj.resource_syntax", directive);
    paths.push_back(reference(base, name));
  }
  return paths;
}
}  // namespace

ObjFileResult read_obj_file(const std::filesystem::path& input,
                            const ObjFileOptions& options) {
  ObjFileResult result;
  try {
    cancelled(options.stop);
    const auto absolute = fs::absolute(input).lexically_normal();
    const auto root =
        fs::canonical(options.resource_root.empty() ? absolute.parent_path()
                                                    : options.resource_root);
    if (!fs::is_directory(root)) fail("obj.resource_root", "root");
    // Canonicalize the parent, retaining the entry filename and its authored
    // reference base.
    const auto logical =
        fs::canonical(absolute.parent_path()) / absolute.filename();
    if (!within(root, logical)) fail("obj.resource_outside_root", "input");
    ObjFileAsset asset;
    asset.obj_path = logical.lexically_relative(root);
    const auto text = read_bytes(root, asset.obj_path, options.stop);
    asset.material_libraries = libraries(text, asset.obj_path.parent_path());
    std::map<std::string, fs::path> names;
    insert_path(names, asset.obj_path);
    auto load = [&](const fs::path& path) {
      const auto found =
          std::find_if(asset.resources.begin(), asset.resources.end(),
                       [&](const auto& r) { return r.path == path; });
      if (found != asset.resources.end()) return found->bytes;
      insert_path(names, path);
      auto bytes = read_bytes(root, path, options.stop);
      asset.resources.push_back({path, bytes});
      return bytes;
    };
    // Libraries are retained separately even when textures refer to another
    // library's bytes.
    for (const auto& library : asset.material_libraries) load(library);
    for (const auto& library : asset.material_libraries) {
      const auto mtl = load(library);
      for (const auto& texture : textures(mtl, library.parent_path()))
        load(texture);
    }
    auto parsed =
        read_obj(text, combined(asset.material_libraries, asset.resources));
    result.diagnostics = std::move(parsed.diagnostics);
    if (!parsed.document) return result;
    asset.document = std::move(*parsed.document);
    std::sort(
        asset.resources.begin(), asset.resources.end(),
        [](const auto& a, const auto& b) { return key(a.path) < key(b.path); });
    cancelled(options.stop);
    result.asset = std::move(asset);
  } catch (const FileFailure& error) {
    result.diagnostics.push_back(error.diagnostic);
  } catch (const fs::filesystem_error&) {
    result.diagnostics.push_back(
        {"obj.filesystem_error", "import", std::nullopt});
  }
  return result;
}
}  // namespace meshvale::interchange
