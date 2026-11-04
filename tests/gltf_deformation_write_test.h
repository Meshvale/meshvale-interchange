// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_GLTF_DEFORMATION_WRITE_TEST_H_
#define MESHVALE_INTERCHANGE_TESTS_GLTF_DEFORMATION_WRITE_TEST_H_

#include <meshvale/geometry/execution.h>

#include <filesystem>

void ExerciseDeformationWriterBudgets(
    const std::filesystem::path& directory,
    meshvale::geometry::ExecutionContext& context);

#endif  // MESHVALE_INTERCHANGE_TESTS_GLTF_DEFORMATION_WRITE_TEST_H_
