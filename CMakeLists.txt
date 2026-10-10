# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(MeshvaleInterchange LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(MeshvaleGeometry 0.0.0 EXACT CONFIG REQUIRED)

# Development snapshots share 0.0.0; require the owned-position producer seam.
include(CMakePushCheckState)
include(CheckCXXSourceCompiles)
function(meshvale_require_geometry_positions)
  cmake_push_check_state(RESET)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  else()
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  endif()
  unset(MESHVALE_GEOMETRY_OWNED_POSITIONS CACHE)
  set(CMAKE_REQUIRED_LIBRARIES meshvale::geometry)
  check_cxx_source_compiles("#include <meshvale/geometry/position_buffer.h>
  int main() {
    meshvale::geometry::PositionBuffer positions;
    positions.Append({1.0, 2.0, 3.0});
    positions.Set(0, {3.0, 2.0, 1.0});
    return positions.Get(0)[0] == 3.0 ? 0 : 1;
  }" MESHVALE_GEOMETRY_OWNED_POSITIONS)
  cmake_pop_check_state()
  if(NOT MESHVALE_GEOMETRY_OWNED_POSITIONS)
    message(FATAL_ERROR "Geometry must provide the compiled PositionBuffer Append/Set/Get interface; use the documented producer revision")
  endif()
endfunction()
meshvale_require_geometry_positions()

# These notices accompany the Geometry code linked into native/Python outputs.
get_filename_component(meshvale_geometry_notice_root "${MeshvaleGeometry_RECORD_SOURCE}" DIRECTORY)
get_filename_component(meshvale_geometry_notice_root "${meshvale_geometry_notice_root}" DIRECTORY)
function(meshvale_require_geometry_notice installed retained)
  file(READ "${installed}" meshvale_installed_notice)
  file(READ "${retained}" meshvale_retained_notice)
  # Git checkouts and installed source archives may use different line endings.
  string(REPLACE "\r\n" "\n" meshvale_installed_notice "${meshvale_installed_notice}")
  string(REPLACE "\r\n" "\n" meshvale_retained_notice "${meshvale_retained_notice}")
  if(NOT meshvale_installed_notice STREQUAL meshvale_retained_notice)
    message(FATAL_ERROR "Installed Geometry notice differs from retained ${retained}")
  endif()
endfunction()
foreach(meshvale_notice IN ITEMS eigen-mpl2.txt eigen-apache.txt eigen-notices.txt)
  meshvale_require_geometry_notice("${meshvale_geometry_notice_root}/licenses/${meshvale_notice}"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/${meshvale_notice}")
endforeach()
meshvale_require_geometry_notice("${meshvale_geometry_notice_root}/NOTICE"
  "${CMAKE_CURRENT_SOURCE_DIR}/licenses/meshvale-geometry-notice.txt")
find_package(tinyobjloader 2.0.0 CONFIG REQUIRED)
if(NOT TARGET tinyobjloader::tinyobjloader_double)
  message(FATAL_ERROR "tinyobjloader must be built with double precision")
endif()
find_package(Git QUIET)
set(MESHVALE_INTERCHANGE_VERSION "0.0.0")
if(Git_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE meshvale_tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(meshvale_tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
    set(MESHVALE_INTERCHANGE_VERSION "${CMAKE_MATCH_1}")
  endif()
endif()
add_library(meshvale_interchange STATIC src/obj_read.cpp src/obj_write.cpp src/obj_file_read.cpp src/obj_file_publish.cpp src/obj_file_detail.cpp)
add_library(meshvale::interchange ALIAS meshvale_interchange)
set_target_properties(meshvale_interchange PROPERTIES EXPORT_NAME interchange POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange PUBLIC meshvale::geometry PRIVATE tinyobjloader::tinyobjloader_double)
target_include_directories(meshvale_interchange PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
function(meshvale_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()
meshvale_warnings(meshvale_interchange)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS meshvale_interchange EXPORT MeshvaleInterchangeTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT MeshvaleInterchangeTargets NAMESPACE meshvale::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchange")
configure_package_config_file(cmake/MeshvaleInterchangeConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchange")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeConfigVersion.cmake"
  VERSION "${MESHVALE_INTERCHANGE_VERSION}" COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchange")
install(FILES LICENSE NOTICE THIRD_PARTY.md DESTINATION "${CMAKE_INSTALL_DATADIR}/MeshvaleInterchange")
install(DIRECTORY licenses/ DESTINATION "${CMAKE_INSTALL_DATADIR}/MeshvaleInterchange/licenses")
option(MESHVALE_INTERCHANGE_PYTHON "Build the optional Python OBJ interface" OFF)
if(MESHVALE_INTERCHANGE_PYTHON)
  find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
  execute_process(COMMAND "${Python_EXECUTABLE}" -c
    "import sysconfig; print(int(bool(sysconfig.get_config_var('Py_GIL_DISABLED'))))"
    OUTPUT_VARIABLE meshvale_python_free_threaded OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(meshvale_python_free_threaded STREQUAL "1")
    message(FATAL_ERROR "Free-threaded Python is not supported by this binding")
  endif()
  execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_VARIABLE meshvale_nanobind_dir OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  find_package(nanobind 3.1 CONFIG REQUIRED HINTS "${meshvale_nanobind_dir}")
  nanobind_add_module(_interchange NB_STATIC NB_DOMAIN meshvale_interchange python/bindings.cpp)
  meshvale_geometry_add_python_record(meshvale_interchange_record DOMAIN meshvale_interchange)
  target_link_libraries(_interchange PRIVATE meshvale_interchange_record meshvale::interchange)
  meshvale_warnings(_interchange)
  install(TARGETS _interchange LIBRARY DESTINATION meshvale_interchange COMPONENT python)
endif()


include(CTest)
if(BUILD_TESTING)
  add_library(meshvale_interchange_header_checks OBJECT
    tests/headers/obj_h.cpp
    tests/headers/obj_files_h.cpp)
  target_link_libraries(meshvale_interchange_header_checks PRIVATE meshvale::interchange)
  meshvale_warnings(meshvale_interchange_header_checks)
  add_executable(meshvale_obj_tests tests/obj.cpp)
  target_link_libraries(meshvale_obj_tests PRIVATE meshvale::interchange)
  meshvale_warnings(meshvale_obj_tests)
  add_test(NAME obj_interchange COMMAND meshvale_obj_tests)
  add_executable(meshvale_obj_file_tests tests/obj_files.cpp)
  target_link_libraries(meshvale_obj_file_tests PRIVATE meshvale::interchange)
  meshvale_warnings(meshvale_obj_file_tests)
  add_test(NAME obj_files COMMAND meshvale_obj_file_tests)
endif()
