# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_SURFACE_FILES
  "Build verified supplied-surface directory publication" OFF)
if(NOT MESHVALE_INTERCHANGE_SURFACE_FILES)
  return()
endif()
if(NOT TARGET meshvale::surface_output)
  message(FATAL_ERROR
    "Surface files requires MESHVALE_INTERCHANGE_SURFACE_OUTPUT=ON")
endif()

add_library(meshvale_interchange_surface_files STATIC
  src/surface_file_publish.cpp
  src/surface_file_native.cpp
  src/surface_file_publish_native.cpp
  src/surface_file_receipt.cpp)
add_library(meshvale::surface_files ALIAS meshvale_interchange_surface_files)
set_target_properties(meshvale_interchange_surface_files PROPERTIES
  EXPORT_NAME surface_files POSITION_INDEPENDENT_CODE ON)
target_link_libraries(meshvale_interchange_surface_files PUBLIC
  meshvale::surface_output PRIVATE Threads::Threads)
meshvale_surface_strict(meshvale_interchange_surface_files)
install(TARGETS meshvale_interchange_surface_files EXPORT MeshvaleInterchangeTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/surface_files.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")

function(meshvale_add_surface_file_tests)
  foreach(name IN ITEMS semantics requests consumer)
    add_executable(meshvale_surface_files_${name}_tests
      tests/surface_files_${name}.cpp)
    target_include_directories(meshvale_surface_files_${name}_tests PRIVATE src tests)
    target_link_libraries(meshvale_surface_files_${name}_tests PRIVATE
      meshvale::surface_files)
    if(NOT name STREQUAL "consumer")
      target_include_directories(meshvale_surface_files_${name}_tests SYSTEM PRIVATE
        "${meshvale_jsoncons_SOURCE_DIR}/include")
    endif()
    meshvale_surface_strict(meshvale_surface_files_${name}_tests)
    add_test(NAME surface_files_${name} COMMAND meshvale_surface_files_${name}_tests)
  endforeach()
  set(surface_files_twice "${CMAKE_CURRENT_BINARY_DIR}/surface_files_twice.cpp")
  file(GENERATE OUTPUT "${surface_files_twice}" CONTENT
    "#include <meshvale/interchange/surface_files.h>\n#include <meshvale/interchange/surface_files.h>\n")
  target_sources(meshvale_interchange_surface_files PRIVATE
    tests/headers/surface_files_h.cpp "${surface_files_twice}")
endfunction()
