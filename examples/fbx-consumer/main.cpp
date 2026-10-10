// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/fbx.h>

#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) return 1;
  const auto result = meshvale::interchange::ReadFbxFile(argv[1]);
  if (!result.asset) {
    for (const auto& diagnostic : result.diagnostics)
      std::cerr << diagnostic.code << ": " << diagnostic.subject << '\n';
    return 2;
  }
  std::cout << result.asset->meshes.size()
            << " polygon meshes in source space\n";
  for (const auto& mesh : result.asset->meshes) {
    if (!meshvale::geometry::inspect_storage(mesh.mesh).empty()) return 3;
    std::cout << mesh.mesh.positions.size() << " points, "
              << mesh.mesh.face_count() << " polygons\n";
  }
}
