# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE
  "Build checked native glTF topology selection output" ON)
if(NOT MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE)
  return()
endif()

find_package(Threads REQUIRED)
function(meshvale_require_geometry_shared_materialization)
  cmake_push_check_state(RESET)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  else()
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  endif()
  unset(MESHVALE_GEOMETRY_SHARED_MATERIALIZATION CACHE)
  set(CMAKE_REQUIRED_LIBRARIES meshvale::materialization meshvale::editing)
  check_cxx_source_compiles("#include <meshvale/geometry/editable_mesh.h>
  #include <meshvale/geometry/execution.h>
  #include <meshvale/geometry/materialization.h>
  int main() {
    using namespace meshvale::geometry;
    MeshMaterializationInput input;
    ExecutionContext context;
    auto result = MaterializeMesh(input, {}, {}, context);
    if (result.mesh) {
      auto shared = result.mesh->ShareExecutionContext();
      return SharesExecutionBudget(shared, shared) ? 0 : 1;
    }
    return 0;
  }" MESHVALE_GEOMETRY_SHARED_MATERIALIZATION)
  cmake_pop_check_state()
  if(NOT MESHVALE_GEOMETRY_SHARED_MATERIALIZATION)
    message(FATAL_ERROR "Checked glTF topology output requires compiled MaterializedMesh::ShareExecutionContext and SharesExecutionBudget; use the documented Geometry producer or explicitly disable MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE")
  endif()
endfunction()
meshvale_require_geometry_shared_materialization()

add_library(meshvale_interchange_gltf_topology_write STATIC
  src/gltf_topology_write.cpp src/gltf_topology_write_lexical.cpp
  src/gltf_topology_write_workers.cpp)
add_library(meshvale::gltf_topology_write ALIAS meshvale_interchange_gltf_topology_write)
set_target_properties(meshvale_interchange_gltf_topology_write PROPERTIES
  EXPORT_NAME gltf_topology_write POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_gltf_topology_write PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_gltf_topology_write PUBLIC
  meshvale::gltf_materialization PRIVATE Threads::Threads)
meshvale_warnings(meshvale_interchange_gltf_topology_write)
install(TARGETS meshvale_interchange_gltf_topology_write
  EXPORT MeshvaleInterchangeTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/gltf_topology_write.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")

function(meshvale_add_gltf_topology_write_tests)
  if(NOT MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE)
    return()
  endif()
  target_sources(meshvale_interchange_header_checks PRIVATE
    tests/headers/gltf_topology_write_h.cpp)
  target_link_libraries(meshvale_interchange_header_checks PRIVATE meshvale::gltf_topology_write)
  add_executable(meshvale_gltf_topology_write_link examples/consumer/gltf_topology_write_link.cpp)
  target_link_libraries(meshvale_gltf_topology_write_link PRIVATE meshvale::gltf_topology_write)
  meshvale_warnings(meshvale_gltf_topology_write_link)
  add_test(NAME gltf_topology_write_link COMMAND meshvale_gltf_topology_write_link)
  if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
    add_executable(meshvale_gltf_topology_write_tests tests/gltf_topology_write.cpp)
    target_include_directories(meshvale_gltf_topology_write_tests PRIVATE src)
    target_link_libraries(meshvale_gltf_topology_write_tests PRIVATE meshvale::gltf_topology_write)
    meshvale_warnings(meshvale_gltf_topology_write_tests)
    add_test(NAME gltf_topology_write COMMAND meshvale_gltf_topology_write_tests)
    set(_MESHVALE_TOPOLOGY_WRITE_FOCUSED_TIMEOUT_SECONDS 120)
    set_tests_properties(gltf_topology_write PROPERTIES
      TIMEOUT "${_MESHVALE_TOPOLOGY_WRITE_FOCUSED_TIMEOUT_SECONDS}")
    add_executable(meshvale_gltf_topology_write_semantics_tests
      tests/gltf_topology_write_semantics/main.cpp
      tests/gltf_topology_write_semantics/fixture_support.cpp
      tests/gltf_topology_write_semantics/fixture_data.cpp
      tests/gltf_topology_write_semantics/raw_channels.cpp
      tests/gltf_topology_write_semantics/written_checks.cpp
      tests/gltf_topology_write_semantics/selection_lifetimes.cpp
      tests/gltf_topology_write_semantics/rejected_candidates.cpp
      tests/gltf_topology_write_semantics/lexical_preservation.cpp)
    target_link_libraries(meshvale_gltf_topology_write_semantics_tests PRIVATE meshvale::gltf_topology_write)
    meshvale_warnings(meshvale_gltf_topology_write_semantics_tests)
    add_test(NAME gltf_topology_write_semantics COMMAND meshvale_gltf_topology_write_semantics_tests)
    set(_MESHVALE_TOPOLOGY_WRITE_LARGE_CASE_MIB 2048)
    add_test(NAME gltf_topology_write_large_index COMMAND meshvale_gltf_topology_write_semantics_tests
      --large-source-index --resource-budget-mib "${_MESHVALE_TOPOLOGY_WRITE_LARGE_CASE_MIB}")
    set_tests_properties(gltf_topology_write_large_index PROPERTIES RUN_SERIAL TRUE LABELS large_resource)
  endif()
endfunction()
