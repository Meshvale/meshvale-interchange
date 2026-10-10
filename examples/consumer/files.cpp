// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "meshvale/interchange/obj_files.h"

namespace fs = std::filesystem;
struct ExampleFiles {
  fs::path root;
  ExampleFiles() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
      auto path =
          fs::current_path() / (".meshvale-example-" + std::to_string(stamp) +
                                "-" + std::to_string(attempt));
      if (fs::create_directory(path)) {
        root = std::move(path);
        return;
      }
    }
    throw std::runtime_error("cannot create example directory");
  }
  ~ExampleFiles() {
    std::error_code error;
    fs::remove_all(root, error);
  }
};
void put(const fs::path& path, const std::string& bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  if (stream.fail()) throw std::runtime_error("example file write failed");
}
int main() {
  using namespace meshvale::interchange;
  try {
    ExampleFiles files;
    fs::create_directory(files.root / "input");
    put(files.root / "input/model.obj",
        "mtllib sample.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nusemtl blue\nf "
        "1 2 3 4\n");
    put(files.root / "input/sample.mtl",
        "newmtl blue\nKd 0 0 1\nmap_Kd texture.bin\n");
    put(files.root / "input/texture.bin", std::string("a\0b", 3));
    const auto imported = read_obj_file(files.root / "input/model.obj");
    if (!imported.asset) return 1;
    ObjBundleOptions options;
    options.supplemental_files.push_back(
        {"example.txt", "Verified polygon/resource round trip\n"});
    const auto published =
        publish_obj_bundle(*imported.asset, files.root / "bundle", options);
    if (published.outcome != ObjBundleOutcome::published || !published.entry)
      return 2;
    const auto reloaded =
        read_obj_file(files.root / "bundle" / *published.entry);
    if (!reloaded.asset ||
        reloaded.asset->resources != imported.asset->resources ||
        reloaded.asset->document.mesh.corner_vertices !=
            imported.asset->document.mesh.corner_vertices)
      return 3;
    std::cout << "Installed OBJ file import, resource snapshot and verified "
                 "bundle publication passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 4;
  }
}
