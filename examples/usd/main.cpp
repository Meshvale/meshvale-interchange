// SPDX-License-Identifier: Apache-2.0
#include <iostream>

#include "meshvale/interchange/usd.h"

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  auto result = meshvale::interchange::ReadUsdMesh(argv[1], argv[2]);
  if (!result.document) {
    for (const auto& diagnostic : result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.subject << '\n';
    }
    return 1;
  }
  const auto& document = *result.document;
  std::cout << document.mesh_path << ": " << document.mesh.positions.size()
            << " positions, " << document.mesh.face_count()
            << " polygon faces, " << document.mesh.attributes.size()
            << " owned channels\n";
  return 0;
}
