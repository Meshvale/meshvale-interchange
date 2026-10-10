// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_OBJ_FILE_DETAIL_H_
#define MESHVALE_INTERCHANGE_SRC_OBJ_FILE_DETAIL_H_
#include <filesystem>
#include <map>
#include <stop_token>
#include <string>
#include <vector>

#include "meshvale/interchange/obj_files.h"

namespace meshvale::interchange::detail {
namespace fs = std::filesystem;
struct FileFailure {
  geometry::Diagnostic diagnostic;
};
[[noreturn]] void fail(std::string code, std::string subject);
void cancelled(std::stop_token stop);
std::string key(const fs::path& path);
std::string lower(std::string text);
void portable(const fs::path& path);
fs::path reference(const fs::path& base, std::string name);
bool within(const fs::path& root, const fs::path& path);
std::string read_bytes(const fs::path& root, const fs::path& relative,
                       std::stop_token stop);
void insert_path(std::map<std::string, fs::path>& paths, const fs::path& path);
std::string combined(const std::vector<fs::path>& libraries,
                     const std::vector<ObjResource>& resources);
std::string clean_line(std::string line);
}  // namespace meshvale::interchange::detail

#endif  // MESHVALE_INTERCHANGE_SRC_OBJ_FILE_DETAIL_H_
