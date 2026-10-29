// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_NUMERICAL_PROFILE_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_NUMERICAL_PROFILE_H_

#include <meshvale/interchange/gltf.h>

#include <cstdint>
#include <optional>

namespace meshvale::interchange::gltf_detail {
// Query the executing thread, never a cached/caller-provided success. The
// observation is one operation/worker's initially default-constructed record.
// No floating controls or sticky flags are changed. Counter overflow propagates
// AdmissionFailure; expected numerical exclusions return false with real facts.
[[nodiscard]] bool EstablishGltfNumericalProfile(
    GltfDeformationNumericObservation& observation,
    std::optional<std::uint64_t> partition, bool caller);
void RecordGltfExactBlock(GltfDeformationNumericObservation& observation);
}  // namespace meshvale::interchange::gltf_detail

#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_NUMERICAL_PROFILE_H_
