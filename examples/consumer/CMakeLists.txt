# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(MeshvaleInterchangeInstalledConsumer LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(MeshvaleInterchange CONFIG REQUIRED)
add_executable(meshvale_interchange_consumer main.cpp)
target_link_libraries(meshvale_interchange_consumer PRIVATE meshvale::interchange)
add_executable(meshvale_obj_bundle_consumer files.cpp)
target_link_libraries(meshvale_obj_bundle_consumer PRIVATE meshvale::interchange)
enable_testing()
add_test(NAME installed_interchange_consumer COMMAND meshvale_interchange_consumer)
add_test(NAME installed_obj_bundle_consumer COMMAND meshvale_obj_bundle_consumer)

# Compile each installed public header first, including legacy/canonical mixtures.
add_library(meshvale_interchange_installed_header_checks OBJECT
  ../../tests/headers/obj_h.cpp ../../tests/headers/obj_hpp.cpp
  ../../tests/headers/obj_files_h.cpp ../../tests/headers/obj_files_hpp.cpp)
target_link_libraries(meshvale_interchange_installed_header_checks PRIVATE meshvale::interchange)
if(MSVC)
  target_compile_options(meshvale_interchange_installed_header_checks PRIVATE /W4 /WX)
else()
  target_compile_options(meshvale_interchange_installed_header_checks PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
