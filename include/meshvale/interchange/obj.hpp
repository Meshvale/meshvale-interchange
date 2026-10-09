// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <meshvale/geometry/mesh.hpp>

namespace meshvale::interchange {

struct ObjPart {
    std::string object;
    std::vector<std::string> groups;
    bool operator==(const ObjPart&) const = default;
};
struct ObjDocument {
    geometry::Mesh mesh;
    std::vector<ObjPart> parts;
    std::vector<std::string> material_names;
    // Retained verbatim; this interface does not resolve referenced resource files.
    std::string material_library;
};
struct ObjImportResult {
    std::optional<ObjDocument> document;
    std::vector<geometry::Diagnostic> diagnostics;
};
struct ObjText {
    std::string obj;
    std::string mtl;
};
struct ObjExportResult {
    std::optional<ObjText> text;
    std::vector<geometry::Diagnostic> diagnostics;
};

[[nodiscard]] ObjImportResult read_obj(const std::string& obj, const std::string& mtl = {});
[[nodiscard]] ObjExportResult write_obj(const ObjDocument& document);

}  // namespace meshvale::interchange
