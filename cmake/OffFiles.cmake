# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_OFF_FILES
  "Build retained OFF file capture and verified publication" OFF)
if(NOT MESHVALE_INTERCHANGE_OFF_FILES)
  return()
endif()

add_library(meshvale_interchange_off_files STATIC
  src/off_files.cpp src/off_file_native.cpp src/off_file_paths.cpp
  src/off_file_publish_native.cpp)
add_library(meshvale::off_files ALIAS meshvale_interchange_off_files)
set_target_properties(meshvale_interchange_off_files PROPERTIES
  EXPORT_NAME off_files POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_off_files PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_off_files PUBLIC meshvale::off
  PRIVATE Threads::Threads)
meshvale_warnings(meshvale_interchange_off_files)
if(MSVC)
  target_compile_options(meshvale_interchange_off_files PRIVATE /fp:strict)
else()
  target_compile_options(meshvale_interchange_off_files PRIVATE
    -fno-fast-math -ffp-contract=off)
endif()
install(TARGETS meshvale_interchange_off_files EXPORT MeshvaleInterchangeTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/off_files.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")

function(meshvale_add_off_file_tests)
  target_sources(meshvale_interchange_header_checks PRIVATE
    tests/headers/off_files_h.cpp)
  target_link_libraries(meshvale_interchange_header_checks PRIVATE
    meshvale::off_files)
  add_executable(meshvale_off_files_tests tests/off_files.cpp)
  add_executable(meshvale_off_files_request_tests tests/off_files_requests.cpp)
  foreach(fixture IN ITEMS meshvale_off_files_tests meshvale_off_files_request_tests)
    target_include_directories(${fixture} PRIVATE src)
    target_link_libraries(${fixture} PRIVATE meshvale::off_files)
    meshvale_warnings(${fixture})
    if(MSVC)
      target_compile_options(${fixture} PRIVATE /fp:strict)
    else()
      target_compile_options(${fixture} PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
  endforeach()
  add_executable(meshvale_off_files_consumer_tests
    examples/off-files-consumer/main.cpp)
  target_link_libraries(meshvale_off_files_consumer_tests PRIVATE
    meshvale::off_files)
  meshvale_warnings(meshvale_off_files_consumer_tests)
  if(MSVC)
    target_compile_options(meshvale_off_files_consumer_tests PRIVATE /fp:strict)
  else()
    target_compile_options(meshvale_off_files_consumer_tests PRIVATE
      -fno-fast-math -ffp-contract=off)
  endif()
  if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL
      "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
    add_test(NAME off_files_semantics COMMAND meshvale_off_files_tests)
    add_test(NAME off_files_requests COMMAND meshvale_off_files_request_tests)
    add_test(NAME off_files_consumer COMMAND meshvale_off_files_consumer_tests)
  else()
    add_test(NAME off_files_semantics
      COMMAND meshvale_off_files_tests --unsupported-profile)
    add_test(NAME off_files_requests
      COMMAND meshvale_off_files_request_tests --unsupported-profile)
    add_test(NAME off_files_consumer
      COMMAND meshvale_off_files_consumer_tests --unsupported-profile)
  endif()
endfunction()
