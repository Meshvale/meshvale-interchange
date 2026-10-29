// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_READ_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_READ_H_

#include <string_view>

#include "gltf_asset_state.h"

namespace meshvale::interchange::gltf_detail {
inline constexpr std::string_view kDeformationQuaternionPolicy =
    "gltf-quaternion-source-squared-v1";
inline constexpr GltfNormativeRevision kDeformationRevision{
    "1c84b58ba3338be4731ea546fa4138dcbb93d42d",
    "aeaab65b1291d722ee610db247ae43c575ed63e1"};
void ValidateDeformationConfiguration(const GltfDeformationPolicy& policy);
// The already bounded reader owns state and projected words. This operation
// retains original source views and checks actual authored node rotations.
void AdmitDeformationRotations(AssetState& state);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_READ_H_
