// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/obj.h"
#include <iostream>

int main() {
    const auto input = meshvale::interchange::read_obj(
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n");
    if (!input.document || input.document->mesh.face_count() != 1 ||
        input.document->mesh.corner_vertices.size() != 4) return 1;
    const auto output = meshvale::interchange::write_obj(*input.document);
    if (!output.text) return 1;
    const auto reloaded = meshvale::interchange::read_obj(output.text->obj, output.text->mtl);
    if (!reloaded.document || reloaded.document->mesh.corner_vertices != input.document->mesh.corner_vertices) return 1;
    std::cout << "Imported and verified a polygon-preserving OBJ text round trip\n";
}
