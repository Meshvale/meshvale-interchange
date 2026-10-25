# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_PLY_FILES
  "Build retained typed PLY file capture and verified publication" OFF)
if(NOT MESHVALE_INTERCHANGE_PLY_FILES)
  return()
endif()

add_library(meshvale_interchange_ply_files STATIC
  src/ply_files.cpp src/ply_file_native.cpp src/ply_file_paths.cpp
  src/ply_file_publish_native.cpp)
add_library(meshvale::ply_files ALIAS meshvale_interchange_ply_files)
set_target_properties(meshvale_interchange_ply_files PROPERTIES
  EXPORT_NAME ply_files POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_ply_files PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_ply_files PUBLIC meshvale::ply
  PRIVATE Threads::Threads)
meshvale_warnings(meshvale_interchange_ply_files)
if(MSVC)
  target_compile_options(meshvale_interchange_ply_files PRIVATE /fp:strict)
else()
  target_compile_options(meshvale_interchange_ply_files PRIVATE
    -fno-fast-math -ffp-contract=off)
endif()
install(TARGETS meshvale_interchange_ply_files EXPORT MeshvaleInterchangeTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/ply_files.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")

function(meshvale_add_ply_file_tests)
  target_sources(meshvale_interchange_header_checks PRIVATE
    tests/headers/ply_files_h.cpp)
  target_link_libraries(meshvale_interchange_header_checks PRIVATE meshvale::ply_files)
  add_executable(meshvale_ply_files_tests tests/ply_files.cpp)
  target_link_libraries(meshvale_ply_files_tests PRIVATE meshvale::ply_files)
  target_compile_definitions(meshvale_ply_files_tests PRIVATE
    MESHVALE_PLY_FILES_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ply-files")
  meshvale_warnings(meshvale_ply_files_tests)
  if(MSVC)
    target_compile_options(meshvale_ply_files_tests PRIVATE /fp:strict)
  else()
    target_compile_options(meshvale_ply_files_tests PRIVATE -fno-fast-math -ffp-contract=off)
  endif()
  add_executable(meshvale_ply_files_request_tests tests/ply_files_requests.cpp)
  target_include_directories(meshvale_ply_files_request_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(meshvale_ply_files_request_tests PRIVATE meshvale::ply_files)
  meshvale_warnings(meshvale_ply_files_request_tests)
  if(MSVC)
    target_compile_options(meshvale_ply_files_request_tests PRIVATE /fp:strict)
  else()
    target_compile_options(meshvale_ply_files_request_tests PRIVATE
      -fno-fast-math -ffp-contract=off)
  endif()
  add_executable(meshvale_ply_files_consumer_tests
    examples/ply-files-consumer/main.cpp)
  target_link_libraries(meshvale_ply_files_consumer_tests PRIVATE meshvale::ply_files)
  meshvale_warnings(meshvale_ply_files_consumer_tests)
  if(MSVC)
    target_compile_options(meshvale_ply_files_consumer_tests PRIVATE /fp:strict)
  else()
    target_compile_options(meshvale_ply_files_consumer_tests PRIVATE
      -fno-fast-math -ffp-contract=off)
  endif()
  if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL
      "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
    add_test(NAME ply_files_semantics COMMAND meshvale_ply_files_tests)
    add_test(NAME ply_files_requests COMMAND meshvale_ply_files_request_tests)
    add_test(NAME ply_files_consumer COMMAND meshvale_ply_files_consumer_tests)
  else()
    add_test(NAME ply_files_semantics COMMAND meshvale_ply_files_tests --unsupported-profile)
    add_test(NAME ply_files_requests COMMAND meshvale_ply_files_request_tests --unsupported-profile)
    add_test(NAME ply_files_consumer COMMAND meshvale_ply_files_consumer_tests --unsupported-profile)
  endif()
endfunction()
