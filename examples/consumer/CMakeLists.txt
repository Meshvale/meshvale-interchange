# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(MeshvaleInterchangeInstalledConsumer LANGUAGES CXX)
find_package(MeshvaleInterchange CONFIG REQUIRED)
add_executable(meshvale_interchange_consumer main.cpp)
target_link_libraries(meshvale_interchange_consumer PRIVATE meshvale::interchange)
enable_testing()
add_test(NAME installed_interchange_consumer COMMAND meshvale_interchange_consumer)
