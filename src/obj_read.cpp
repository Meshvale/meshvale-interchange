// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/obj.h"
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>
#include <tiny_obj_loader.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <type_traits>

namespace meshvale::interchange {
namespace {
using namespace geometry;
struct FaceContext {
    ObjPart part;
    std::string material;
    std::uint32_t smoothing = 0;
    std::vector<std::pair<bool, bool>> corner_attributes;
};
bool number(std::string token) {
    if (!token.empty() && token.front() == '+') token.erase(0, 1);
    double value = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() && std::isfinite(value);
}
bool index_token(const std::string& token) {
    auto text = token;
    if (!text.empty() && text.front() == '+') text.erase(0, 1);
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value != 0;
}
std::string remainder(std::istringstream& line) {
    std::string value;
    std::getline(line >> std::ws, value);
    const auto end = value.find_last_not_of(" \t\r");
    return end == std::string::npos ? std::string{} : value.substr(0, end + 1);
}
bool preflight(const std::string& input, std::string& normalized,
               std::vector<FaceContext>& faces, std::vector<Diagnostic>& issues) {
    if (input.find('\0') != std::string::npos) {
        issues.push_back({"obj.nul_byte", "input", std::nullopt}); return false;
    }
    auto text = input;
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    std::istringstream source(text);
    std::string raw;
    FaceContext active;
    index_t ordinal = 0;
    while (std::getline(source, raw)) {
        ++ordinal;
        if (const auto comment = raw.find('#'); comment != std::string::npos) raw.erase(comment);
        std::istringstream line(raw);
        std::string directive;
        if (!(line >> directive)) continue;
        auto issue = [&](const char* code) { issues.push_back({code, directive, ordinal}); };
        if (raw.find_last_not_of(" \t\r") != std::string::npos &&
            raw[raw.find_last_not_of(" \t\r")] == '\\') {
            issue("obj.unsupported_continuation"); return false;
        }
        if (directive == "v" || directive == "vt" || directive == "vn") {
            std::vector<std::string> values;
            std::string token;
            while (line >> token) values.push_back(token);
            const bool shape = directive == "vt" ? (values.size() == 1 || values.size() == 2) : values.size() == 3;
            if (!shape) { issue("obj.unsupported_numeric_record"); return false; }
            if (!std::all_of(values.begin(), values.end(), number)) {
                issue("obj.invalid_numeric_record"); return false;
            }
        } else if (directive == "o") {
            active.part.object = remainder(line);
        } else if (directive == "g") {
            active.part.groups.clear();
            std::string group;
            while (line >> group) active.part.groups.push_back(group);
        } else if (directive == "usemtl") {
            active.material = remainder(line);
        } else if (directive == "s") {
            const auto value = remainder(line);
            if (value == "off") active.smoothing = 0;
            else {
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), active.smoothing);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    active.smoothing > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                    issue("obj.invalid_smoothing_group"); return false;
                }
            }
        } else if (directive == "f") {
            auto face = active;
            face.corner_attributes.clear();
            std::string token;
            while (line >> token) {
                const auto slash = token.find('/');
                if (!index_token(token.substr(0, slash))) { issue("obj.invalid_face_reference"); return false; }
                bool uv = false, normal = false;
                if (slash != std::string::npos) {
                    const auto next = token.find('/', slash + 1);
                    const auto texcoord = token.substr(slash + 1, next == std::string::npos ? next : next - slash - 1);
                    uv = !texcoord.empty();
                    if (uv && !index_token(texcoord)) { issue("obj.invalid_face_reference"); return false; }
                    if (next != std::string::npos) {
                        normal = true;
                        if (!index_token(token.substr(next + 1))) { issue("obj.invalid_face_reference"); return false; }
                    } else if (!uv) { issue("obj.invalid_face_reference"); return false; }
                }
                face.corner_attributes.emplace_back(uv, normal);
            }
            if (face.corner_attributes.size() < 3) { issue("obj.short_face"); return false; }
            faces.push_back(std::move(face));
        } else if (directive == "mtllib") {
            // One supplied material stream is loaded once, independently of source filenames.
            continue;
        } else {
            issue("obj.unsupported_directive"); return false;
        }
        normalized += raw + '\n';
    }
    return true;
}
Attribute channel(AttributeDomain domain, std::string name, std::string semantic,
                  std::uint32_t width = 1) {
    Attribute a;
    a.domain = domain; a.name = std::move(name); a.semantic = std::move(semantic); a.components = width;
    return a;
}
}  // namespace

ObjImportResult read_obj(const std::string& obj, const std::string& mtl) {
    static_assert(std::is_same_v<tinyobj::real_t, double>, "tinyobjloader double feature is required");
    ObjImportResult result;
    std::string normalized;
    std::vector<FaceContext> contexts;
    if (!preflight(obj, normalized, contexts, result.diagnostics)) return result;
    if (mtl.find('\0') != std::string::npos) {
        result.diagnostics.push_back({"obj.nul_byte", "material_library", std::nullopt}); return result;
    }
    if (!mtl.empty()) normalized = "mtllib __meshvale_supplied__.mtl\n" + normalized;
    tinyobj::ObjReaderConfig config;
    config.triangulate = false; config.vertex_color = false;
    tinyobj::ObjReader reader;
    const auto parsed = reader.ParseFromString(normalized, mtl, config);
    if (!reader.Warning().empty()) result.diagnostics.push_back({"obj.parser_warning", reader.Warning(), std::nullopt});
    if (!reader.Error().empty()) result.diagnostics.push_back({"obj.parser_error", reader.Error(), std::nullopt});
    if (!parsed) return result;
    ObjDocument document;
    document.material_library = mtl;
    for (const auto& material : reader.GetMaterials()) {
        if (material.name.empty() || std::find(document.material_names.begin(), document.material_names.end(),
                                             material.name) != document.material_names.end()) {
            result.diagnostics.push_back({"obj.ambiguous_material_name", material.name, std::nullopt}); return result;
        }
        document.material_names.push_back(material.name);
    }
    const auto& data = reader.GetAttrib();
    for (std::size_t i = 0; i < data.vertices.size(); i += 3)
        document.mesh.positions.push_back({data.vertices[i],data.vertices[i + 1],data.vertices[i + 2]});
    auto uv = channel(AttributeDomain::corner, "obj.uv0", "texcoord", 2); uv.set_index = 0;
    auto normal = channel(AttributeDomain::corner, "obj.normal", "normal", 3);
    uv.values = std::vector<double>{}; normal.values = std::vector<double>{};
    uv.present = std::vector<std::uint8_t>{}; normal.present = std::vector<std::uint8_t>{};
    auto material = channel(AttributeDomain::face, "obj.material", "material_index");
    auto part = channel(AttributeDomain::face, "obj.part", "label");
    auto smoothing = channel(AttributeDomain::face, "obj.smoothing_group", "label");
    material.values = std::vector<std::int32_t>{};
    part.values = std::vector<std::uint32_t>{}; smoothing.values = std::vector<std::uint32_t>{};
    index_t face = 0;
    for (const auto& shape : reader.GetShapes()) {
        std::size_t cursor = 0;
        for (std::size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f, ++face) {
            const auto size = shape.mesh.num_face_vertices[f];
            if (face >= contexts.size() || size != contexts[face].corner_attributes.size() ||
                cursor + size > shape.mesh.indices.size()) {
                result.diagnostics.push_back({"obj.parser_face_mismatch", "faces", face}); return result;
            }
            const auto& context = contexts[face];
            const auto found = std::find(document.material_names.begin(), document.material_names.end(), context.material);
            const auto assigned = context.material.empty() ? -1 :
                (found == document.material_names.end() ? -2 : static_cast<int>(found - document.material_names.begin()));
            if (assigned == -2 || f >= shape.mesh.material_ids.size() || shape.mesh.material_ids[f] != assigned) {
                result.diagnostics.push_back({"obj.unresolved_material", context.material, face}); return result;
            }
            auto member = std::find(document.parts.begin(), document.parts.end(), context.part);
            if (member == document.parts.end()) { document.parts.push_back(context.part); member = document.parts.end() - 1; }
            std::get<std::vector<std::int32_t>>(material.values).push_back(assigned);
            std::get<std::vector<std::uint32_t>>(part.values).push_back(static_cast<std::uint32_t>(member - document.parts.begin()));
            std::get<std::vector<std::uint32_t>>(smoothing.values).push_back(context.smoothing);
            for (std::size_t c = 0; c < size; ++c) {
                const auto& index = shape.mesh.indices[cursor++];
                const auto [has_uv, has_normal] = context.corner_attributes[c];
                if (index.vertex_index < 0 || (has_uv && (index.texcoord_index < 0 ||
                        static_cast<std::size_t>(index.texcoord_index) >= data.texcoords.size() / 2)) ||
                    (has_normal && (index.normal_index < 0 ||
                        static_cast<std::size_t>(index.normal_index) >= data.normals.size() / 3))) {
                    result.diagnostics.push_back({"obj.invalid_attribute_reference", "corners", document.mesh.corner_vertices.size()});
                    return result;
                }
                document.mesh.corner_vertices.push_back(static_cast<index_t>(index.vertex_index));
                uv.present->push_back(static_cast<std::uint8_t>(has_uv));
                normal.present->push_back(static_cast<std::uint8_t>(has_normal));
                auto& uv_values = std::get<std::vector<double>>(uv.values);
                auto& normal_values = std::get<std::vector<double>>(normal.values);
                for (std::size_t i = 0; i < 2; ++i)
                    uv_values.push_back(has_uv ? data.texcoords[static_cast<std::size_t>(index.texcoord_index) * 2 + i] : 0);
                for (std::size_t i = 0; i < 3; ++i)
                    normal_values.push_back(has_normal ? data.normals[static_cast<std::size_t>(index.normal_index) * 3 + i] : 0);
            }
            document.mesh.face_offsets.push_back(document.mesh.corner_vertices.size());
        }
        if (cursor != shape.mesh.indices.size()) {
            result.diagnostics.push_back({"obj.parser_face_mismatch", "corners", face}); return result;
        }
    }
    if (face != contexts.size()) {
        result.diagnostics.push_back({"obj.parser_face_mismatch", "faces", face}); return result;
    }
    document.mesh.attributes = {std::move(uv),std::move(normal),std::move(material),std::move(part),std::move(smoothing)};
    auto issues = inspect_storage(document.mesh);
    result.diagnostics.insert(result.diagnostics.end(), issues.begin(), issues.end());
    result.document = std::move(document);
    return result;
}
}  // namespace meshvale::interchange
