// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_files.h>

int main() {
  auto* volatile capture = &meshvale::interchange::ReadGltfFileAsset;
  auto* volatile publish = &meshvale::interchange::PublishGltfTopologyBundle;
  meshvale::interchange::GltfFilePublicationResult result;
  return capture != nullptr && publish != nullptr && !result.bundle ? 0 : 1;
}
