// SPDX-License-Identifier: Apache-2.0
#include "obj_file_detail.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace meshvale::interchange::detail {
[[noreturn]] void fail(std::string code, std::string subject) {
  throw FileFailure{{std::move(code), std::move(subject), std::nullopt}};
}
void cancelled(std::stop_token stop) {
  if (stop.stop_requested()) fail("obj.cancelled", "files");
}
std::string key(const fs::path& path) {
  const auto value = path.generic_u8string();
  return {value.begin(), value.end()};
}
std::string lower(std::string text) {
  for (auto& c : text)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  return text;
}
void portable(const fs::path& path) {
  const auto name = key(path);
  if (name.empty() || path.is_absolute() || path.has_root_name() ||
      path.has_root_directory())
    fail("obj.resource_path", "resources");
  for (unsigned char c : name)
    if (c < 32 || c == 127 ||
        std::string(":<>\"|?*\\#").find(static_cast<char>(c)) !=
            std::string::npos)
      fail("obj.resource_path", "resources");
  for (const auto& part : path) {
    const auto component = key(part);
    if (component.empty() || component == "." || component == ".." ||
        component.back() == '.' || component.back() == ' ')
      fail("obj.resource_path", "resources");
    const auto stem = lower(component.substr(0, component.find('.')));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        (stem.size() == 4 &&
         (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
         stem[3] >= '1' && stem[3] <= '9'))
      fail("obj.resource_path", "resources");
  }
}
fs::path reference(const fs::path& base, std::string name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  const fs::path path{std::u8string(name.begin(), name.end())};
  if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
    fail("obj.resource_path", "resources");
  const auto relative = (base / path).lexically_normal();
  portable(relative);
  return relative;
}
bool within(const fs::path& root, const fs::path& path) {
  const auto relative = path.lexically_relative(root);
  return !relative.empty() && !relative.is_absolute() &&
         std::none_of(relative.begin(), relative.end(),
                      [](const auto& p) { return p == ".."; });
}
std::string read_bytes(const fs::path& root, const fs::path& relative,
                       std::stop_token stop) {
  cancelled(stop);
  portable(relative);
  const auto file = root / relative;
  std::error_code error;
  const auto target = fs::canonical(file, error);
  if (error) fail("obj.resource_missing", key(relative));
  if (!within(root, target)) fail("obj.resource_outside_root", key(relative));
  if (!fs::is_regular_file(target))
    fail("obj.resource_not_file", key(relative));
  std::ifstream stream(file, std::ios::binary);
  if (!stream) fail("obj.resource_read", key(relative));
  std::string bytes{std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>()};
  if (stream.bad()) fail("obj.resource_read", key(relative));
  cancelled(stop);
  return bytes;
}
void insert_path(std::map<std::string, fs::path>& paths, const fs::path& path) {
  portable(path);
  const auto folded = lower(key(path));
  for (const auto& [existing, unused] : paths) {
    (void)unused;
    if (existing == folded || existing.starts_with(folded + "/") ||
        folded.starts_with(existing + "/"))
      fail("obj.resource_collision", key(path));
  }
  paths.emplace(folded, path);
}
std::string combined(const std::vector<fs::path>& libraries,
                     const std::vector<ObjResource>& resources) {
  std::string text;
  std::map<std::string, fs::path> paths;
  for (const auto& library : libraries) {
    insert_path(paths, library);
    const auto found =
        std::find_if(resources.begin(), resources.end(),
                     [&](const auto& r) { return r.path == library; });
    if (found == resources.end()) fail("obj.resource_missing", key(library));
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += found->bytes;
  }
  return text;
}
std::string clean_line(std::string line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
  if (line.find('\0') != std::string::npos) fail("obj.resource_syntax", "text");
  const auto comment = line.find('#');
  if (comment != std::string::npos) line.erase(comment);
  const auto last = line.find_last_not_of(" \t");
  if (last != std::string::npos && line[last] == '\\')
    fail("obj.resource_continuation", "text");
  if (last == std::string::npos)
    line.clear();
  else
    line.resize(last + 1);
  return line;
}
}  // namespace meshvale::interchange::detail
