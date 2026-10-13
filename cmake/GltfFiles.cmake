# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_GLTF_FILES
  "Build retained native glTF JSON file capture" OFF)
if(NOT MESHVALE_INTERCHANGE_GLTF_FILES)
  return()
endif()
if(NOT TARGET meshvale::gltf_workflow_support)
  message(FATAL_ERROR "glTF files require MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT=ON")
endif()

add_library(meshvale_interchange_gltf_files STATIC
  src/gltf_files.cpp src/gltf_file_paths.cpp src/gltf_file_native.cpp)
add_library(meshvale::gltf_files ALIAS meshvale_interchange_gltf_files)
set_target_properties(meshvale_interchange_gltf_files PROPERTIES
  EXPORT_NAME gltf_files POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_gltf_files PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_gltf_files PUBLIC
  meshvale::gltf_workflow_support PRIVATE Threads::Threads)
meshvale_warnings(meshvale_interchange_gltf_files)
install(TARGETS meshvale_interchange_gltf_files EXPORT MeshvaleInterchangeTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/gltf_files.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")

function(meshvale_add_gltf_file_tests)
  target_sources(meshvale_interchange_header_checks PRIVATE tests/headers/gltf_files_h.cpp)
  target_link_libraries(meshvale_interchange_header_checks PRIVATE meshvale::gltf_files)
  add_executable(meshvale_gltf_files_link examples/consumer/gltf_files_link.cpp)
  target_link_libraries(meshvale_gltf_files_link PRIVATE meshvale::gltf_files)
  meshvale_warnings(meshvale_gltf_files_link)
  add_test(NAME gltf_files_link COMMAND meshvale_gltf_files_link)
  add_executable(meshvale_gltf_files_tests tests/gltf_files.cpp)
  target_include_directories(meshvale_gltf_files_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_files_tests PRIVATE meshvale::gltf_files)
  meshvale_warnings(meshvale_gltf_files_tests)
  add_test(NAME gltf_files COMMAND meshvale_gltf_files_tests)
endfunction()
