// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_OBJ_H_
#define MESHVALE_INTERCHANGE_OBJ_H_

#include <optional>
#include <string>
#include <vector>

#include "meshvale/geometry/mesh.h"

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
  // Retained verbatim; this interface does not resolve referenced resource
  // files.
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

[[nodiscard]] ObjImportResult read_obj(const std::string& obj,
                                       const std::string& mtl = {});
[[nodiscard]] ObjExportResult write_obj(const ObjDocument& document);

}  // namespace meshvale::interchange

#endif  // MESHVALE_INTERCHANGE_OBJ_H_
