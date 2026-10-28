# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_STL_FILES
  "Build retained STL file capture and verified codec-preserving publication" OFF)
if(NOT MESHVALE_INTERCHANGE_STL_FILES)
  return()
endif()
if(NOT TARGET meshvale::stl)
  message(FATAL_ERROR "STL files requires the native STL codec target")
endif()

function(meshvale_stl_files_strict_floating_point target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /fp:strict)
  else()
    target_compile_options(${target} PRIVATE -fno-fast-math -ffp-contract=off)
  endif()
endfunction()

add_library(meshvale_interchange_stl_files STATIC src/stl_files.cpp)
add_library(meshvale::stl_files ALIAS meshvale_interchange_stl_files)
set_target_properties(meshvale_interchange_stl_files PROPERTIES
  EXPORT_NAME stl_files POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_stl_files PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_stl_files PUBLIC meshvale::stl
  PRIVATE Threads::Threads)
target_include_directories(meshvale_interchange_stl_files PRIVATE src)
meshvale_warnings(meshvale_interchange_stl_files)
meshvale_stl_files_strict_floating_point(meshvale_interchange_stl_files)
install(TARGETS meshvale_interchange_stl_files EXPORT MeshvaleInterchangeTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/stl_files.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")

function(meshvale_add_stl_file_tests)
  target_sources(meshvale_interchange_header_checks PRIVATE
    tests/headers/stl_files_h.cpp)
  target_link_libraries(meshvale_interchange_header_checks PRIVATE meshvale::stl_files)
  foreach(kind IN ITEMS semantics requests consumer)
    set(fixture meshvale_stl_files_${kind}_tests)
    add_executable(${fixture} tests/stl_files_${kind}.cpp)
    target_include_directories(${fixture} PRIVATE tests)
    if(kind STREQUAL "requests")
      target_include_directories(${fixture} PRIVATE src)
    endif()
    target_link_libraries(${fixture} PRIVATE meshvale::stl_files)
    meshvale_warnings(${fixture})
    meshvale_stl_files_strict_floating_point(${fixture})
    if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL
        "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
      add_test(NAME stl_files_${kind} COMMAND ${fixture})
    else()
      add_test(NAME stl_files_${kind} COMMAND ${fixture} --unsupported-profile)
    endif()
  endforeach()
  set(_MESHVALE_INTERCHANGE_STL_FILES_REQUEST_TIMEOUT_SECONDS 600)
  set_tests_properties(stl_files_requests PROPERTIES
    TIMEOUT "${_MESHVALE_INTERCHANGE_STL_FILES_REQUEST_TIMEOUT_SECONDS}" RUN_SERIAL TRUE)
  set(probe "${CMAKE_CURRENT_BINARY_DIR}/stl_files_internal_twice.cpp")
  file(WRITE "${probe}"
    "#include \"stl_files_internal.h\"\n#include \"stl_files_internal.h\"\n")
  target_sources(meshvale_interchange_stl_files PRIVATE "${probe}")
  if(WIN32)
    # Explicit alias qualification is separate from the ordinary CTest inventory.
    add_executable(meshvale_stl_files_aliases_tests
      tests/stl_files_aliases.cpp tests/stl_files_aliases_internal.cpp)
    target_include_directories(meshvale_stl_files_aliases_tests PRIVATE tests)
    set_source_files_properties(tests/stl_files_aliases_internal.cpp PROPERTIES
      INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(meshvale_stl_files_aliases_tests PRIVATE
      MESHVALE_STL_ALIAS_INTERNAL_PROBE=1)
    target_link_libraries(meshvale_stl_files_aliases_tests PRIVATE meshvale::stl_files)
    meshvale_warnings(meshvale_stl_files_aliases_tests)
    meshvale_stl_files_strict_floating_point(meshvale_stl_files_aliases_tests)
  endif()
endfunction()
