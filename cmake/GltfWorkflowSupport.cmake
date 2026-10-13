# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT
  "Build independent native glTF topology output inspection"
  "${MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE}")
if(NOT MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT)
  return()
endif()
if(NOT TARGET meshvale::gltf_topology_write)
  message(FATAL_ERROR "glTF workflow support requires MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE=ON")
endif()

add_library(meshvale_interchange_gltf_workflow_support STATIC
  src/gltf_workflow_support.cpp
  src/gltf_workflow_support_geometry.cpp
  src/gltf_workflow_support_lexical.cpp
  src/gltf_workflow_support_reload.cpp)
add_library(meshvale::gltf_workflow_support ALIAS meshvale_interchange_gltf_workflow_support)
set_target_properties(meshvale_interchange_gltf_workflow_support PROPERTIES
  EXPORT_NAME gltf_workflow_support POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_gltf_workflow_support PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_gltf_workflow_support PUBLIC
  meshvale::gltf_topology_write PRIVATE Threads::Threads)
meshvale_warnings(meshvale_interchange_gltf_workflow_support)
install(TARGETS meshvale_interchange_gltf_workflow_support
  EXPORT MeshvaleInterchangeTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/gltf_workflow_support.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")

function(meshvale_add_gltf_workflow_support_tests)
  if(NOT MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT)
    return()
  endif()
  target_compile_definitions(meshvale_interchange_gltf_topology_write PRIVATE MESHVALE_INTERCHANGE_TESTING=1)
  target_sources(meshvale_interchange_header_checks PRIVATE
    tests/headers/gltf_workflow_support_h.cpp)
  target_link_libraries(meshvale_interchange_header_checks PRIVATE meshvale::gltf_workflow_support)
  add_executable(meshvale_gltf_workflow_support_link examples/consumer/gltf_workflow_support_link.cpp)
  target_link_libraries(meshvale_gltf_workflow_support_link PRIVATE meshvale::gltf_workflow_support)
  meshvale_warnings(meshvale_gltf_workflow_support_link)
  add_test(NAME gltf_workflow_support_link COMMAND meshvale_gltf_workflow_support_link)
  if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
    add_executable(meshvale_gltf_workflow_inspection_tests
      tests/gltf_workflow_inspection.cpp
      tests/gltf_workflow_inspection_support.cpp
      tests/gltf_workflow_inspection_requests.cpp
      tests/gltf_topology_write_semantics/fixture_data.cpp)
    target_include_directories(meshvale_gltf_workflow_inspection_tests PRIVATE src)
    target_link_libraries(meshvale_gltf_workflow_inspection_tests PRIVATE meshvale::gltf_workflow_support)
    meshvale_warnings(meshvale_gltf_workflow_inspection_tests)
    add_test(NAME gltf_workflow_inspection COMMAND meshvale_gltf_workflow_inspection_tests)
    set(_MESHVALE_WORKFLOW_INSPECTION_LARGE_CASE_MIB 2048)
    add_test(NAME gltf_workflow_inspection_large_index COMMAND meshvale_gltf_workflow_inspection_tests
      --large-source-index --resource-budget-mib "${_MESHVALE_WORKFLOW_INSPECTION_LARGE_CASE_MIB}")
    set_tests_properties(gltf_workflow_inspection_large_index PROPERTIES
      RUN_SERIAL TRUE LABELS large_resource)
  endif()
endfunction()
