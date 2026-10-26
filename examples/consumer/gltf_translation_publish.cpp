// SPDX-License-Identifier: Apache-2.0
#include <exception>
#include <iostream>

#include "../../tests/gltf_file_publish_translation_fixture.h"

int main() {
  try {
    meshvale::interchange::test_translation_publication::Positive(false);
    meshvale::interchange::test_translation_publication::Positive(true);
    std::cout << "installed direct translation JSON/GLB publication passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
