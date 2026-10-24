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

# Development snapshots share 0.0.0; require the canonical scalar-buffer seam.
function(meshvale_require_geometry_attributes)
  cmake_push_check_state(RESET)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  else()
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  endif()
  unset(MESHVALE_GEOMETRY_OWNED_ATTRIBUTES CACHE)
  set(CMAKE_REQUIRED_LIBRARIES meshvale::geometry)
  check_cxx_source_compiles("#include <meshvale/geometry/attributes.h>
  #include <type_traits>
  #include <variant>
  int main() {
    using namespace meshvale::geometry;
    static_assert(std::is_same_v<std::variant_alternative_t<0, AttributeValues>, ScalarBuffer<float>>);
    ScalarBuffer<double> values;
    values.push_back(1.0);
    Attribute attribute;
    attribute.values = values;
    return std::get<ScalarBuffer<double>>(attribute.values).size() == values.size() ? 0 : 1;
  }" MESHVALE_GEOMETRY_OWNED_ATTRIBUTES)
  cmake_pop_check_state()
  if(NOT MESHVALE_GEOMETRY_OWNED_ATTRIBUTES)
    message(FATAL_ERROR "Geometry must provide compiled ScalarBuffer attribute owners; use the documented producer revision")
  endif()
endfunction()
meshvale_require_geometry_attributes()

# A package version alone cannot identify development API availability.
if(NOT TARGET meshvale::materialization)
  message(FATAL_ERROR "Geometry must provide the installed materialization target; use the documented producer revision")
endif()
function(meshvale_require_geometry_materialization)
  cmake_push_check_state(RESET)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  else()
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  endif()
  unset(MESHVALE_GEOMETRY_CHARGED_MATERIALIZATION CACHE)
  set(CMAKE_REQUIRED_LIBRARIES meshvale::materialization)
  check_cxx_source_compiles("#include <meshvale/geometry/materialization.h>
  #include <meshvale/geometry/editable_mesh.h>
  #include <cstdint>
  #include <span>
  int main() {
    using namespace meshvale::geometry;
    MeshMaterializationInput input;
    input.positions = std::span<const float>{};
    input.topology = TriangleTopologyInput{};
    AttributeMaterializationInput attribute;
    attribute.values = std::span<const std::int8_t>{};
    ExecutionContext context;
    auto result = MaterializeMesh(input, {}, {}, context);
    if (result.mesh) {
      auto inspection = InspectMaterializedMesh(*result.mesh, {}, {});
      auto selection = SelectMeshFaces(*result.mesh, {}, {}, {});
      return inspection.inspection && selection.selection ? 0 : 1;
    }
    return 0;
  }" MESHVALE_GEOMETRY_CHARGED_MATERIALIZATION)
  cmake_pop_check_state()
  if(NOT MESHVALE_GEOMETRY_CHARGED_MATERIALIZATION)
    message(FATAL_ERROR "Geometry must provide compiled typed materialization, inspection and face selection; use the documented producer revision")
  endif()
endfunction()
meshvale_require_geometry_materialization()

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
if(NOT TARGET meshvale::editing)
  message(FATAL_ERROR "Geometry must provide the installed execution target")
endif()
include(cmake/GltfDependencies.cmake)
find_package(tinyobjloader 2.0.0 CONFIG REQUIRED)
find_package(Threads REQUIRED)
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
add_library(meshvale_interchange STATIC src/obj_read.cpp src/obj_write.cpp src/obj_file_read.cpp src/obj_file_publish.cpp src/obj_file_detail.cpp src/file_publication_native.cpp
  src/gltf_write.cpp src/gltf_accessor.cpp src/gltf_extract.cpp src/gltf_admission.cpp src/gltf_read.cpp src/gltf_resources.cpp src/gltf_operation_workers.cpp src/gltf_graph.cpp src/gltf_graph_declarations.cpp src/gltf_asset.cpp src/gltf_metadata.cpp src/gltf_allocation.cpp src/gltf_parser.cpp src/gltf_json_snapshot.cpp src/cgltf_reader.cpp src/glb.cpp)
add_library(meshvale::interchange ALIAS meshvale_interchange)
if(MSVC)
  # The compiled cgltf C-linkage entrypoint propagates allocation callbacks'
  # C++ exceptions. Disable /EHc's no-throw assumption for this owning TU.
  set_source_files_properties(src/cgltf_reader.cpp PROPERTIES COMPILE_OPTIONS /EHsc-)
endif()
set_target_properties(meshvale_interchange PROPERTIES EXPORT_NAME interchange POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange PUBLIC meshvale::geometry PRIVATE tinyobjloader::tinyobjloader_double meshvale::editing Threads::Threads)
target_include_directories(meshvale_interchange SYSTEM PRIVATE
  "${meshvale_cgltf_SOURCE_DIR}" "${meshvale_jsoncons_SOURCE_DIR}/include")
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
add_library(meshvale_interchange_gltf_materialization STATIC src/gltf_materialization.cpp)
add_library(meshvale::gltf_materialization ALIAS meshvale_interchange_gltf_materialization)
set_target_properties(meshvale_interchange_gltf_materialization PROPERTIES
  EXPORT_NAME gltf_materialization POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_gltf_materialization PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_gltf_materialization PUBLIC
  meshvale::interchange meshvale::materialization)
meshvale_warnings(meshvale_interchange_gltf_materialization)
add_library(meshvale_interchange_stl STATIC src/stl.cpp)
add_library(meshvale::stl ALIAS meshvale_interchange_stl)
set_target_properties(meshvale_interchange_stl PROPERTIES
  EXPORT_NAME stl POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_stl PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_stl PUBLIC
  meshvale::interchange meshvale::materialization)
meshvale_warnings(meshvale_interchange_stl)
if(MSVC)
  target_compile_options(meshvale_interchange_stl PRIVATE /fp:strict)
else()
  target_compile_options(meshvale_interchange_stl PRIVATE -fno-fast-math -ffp-contract=off)
endif()
add_library(meshvale_interchange_off STATIC src/off.cpp)
add_library(meshvale::off ALIAS meshvale_interchange_off)
set_target_properties(meshvale_interchange_off PROPERTIES
  EXPORT_NAME off POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_off PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_off PUBLIC
  meshvale::interchange meshvale::materialization)
meshvale_warnings(meshvale_interchange_off)
if(MSVC)
  target_compile_options(meshvale_interchange_off PRIVATE /fp:strict)
else()
  target_compile_options(meshvale_interchange_off PRIVATE -fno-fast-math -ffp-contract=off)
endif()
add_library(meshvale_interchange_ply STATIC src/ply.cpp)
add_library(meshvale::ply ALIAS meshvale_interchange_ply)
set_target_properties(meshvale_interchange_ply PROPERTIES
  EXPORT_NAME ply POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_interchange_ply PUBLIC cxx_std_20)
target_link_libraries(meshvale_interchange_ply PUBLIC
  meshvale::interchange meshvale::materialization)
meshvale_warnings(meshvale_interchange_ply)
if(MSVC)
  target_compile_options(meshvale_interchange_ply PRIVATE /fp:strict)
else()
  target_compile_options(meshvale_interchange_ply PRIVATE -fno-fast-math -ffp-contract=off)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/GltfTopologyWrite.cmake)
include(cmake/GltfWorkflowSupport.cmake)
include(cmake/GltfFiles.cmake)
include(cmake/OffFiles.cmake)
install(TARGETS meshvale_interchange meshvale_interchange_gltf_materialization meshvale_interchange_stl meshvale_interchange_off meshvale_interchange_ply
  EXPORT MeshvaleInterchangeTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/obj.h include/meshvale/interchange/obj_files.h include/meshvale/interchange/gltf.h include/meshvale/interchange/gltf_write.h include/meshvale/interchange/glb.h include/meshvale/interchange/gltf_materialization.h include/meshvale/interchange/stl.h include/meshvale/interchange/off.h include/meshvale/interchange/ply.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")
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
file(GLOB meshvale_base_notice_files "${CMAKE_CURRENT_SOURCE_DIR}/licenses/*.txt")
install(FILES ${meshvale_base_notice_files} DESTINATION "${CMAKE_INSTALL_DATADIR}/MeshvaleInterchange/licenses")
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
include(cmake/MaterializationTestProfile.cmake)
if(BUILD_TESTING)
  add_library(meshvale_interchange_header_checks OBJECT
    tests/headers/gltf_write_h.cpp
    tests/headers/gltf_h.cpp
    tests/headers/obj_h.cpp
    tests/headers/obj_files_h.cpp tests/headers/glb_h.cpp
    tests/headers/gltf_materialization_h.cpp tests/header_stl.cpp tests/header_off.cpp tests/header_ply.cpp)
  target_link_libraries(meshvale_interchange_header_checks PRIVATE meshvale::gltf_materialization meshvale::stl meshvale::off meshvale::ply)
  meshvale_warnings(meshvale_interchange_header_checks)
  add_executable(meshvale_stl_tests tests/stl.cpp)
  add_executable(meshvale_stl_request_tests tests/stl_requests.cpp)
  target_include_directories(meshvale_stl_request_tests PRIVATE src)
  foreach(meshvale_stl_test IN ITEMS meshvale_stl_tests meshvale_stl_request_tests)
    target_link_libraries(${meshvale_stl_test} PRIVATE meshvale::stl)
    meshvale_warnings(${meshvale_stl_test})
    if(MSVC)
      target_compile_options(${meshvale_stl_test} PRIVATE /fp:strict)
    else()
      target_compile_options(${meshvale_stl_test} PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
  endforeach()
  if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
    add_test(NAME stl_interchange COMMAND meshvale_stl_tests)
    add_test(NAME stl_requests COMMAND meshvale_stl_request_tests)
  else()
    add_test(NAME stl_interchange COMMAND meshvale_stl_tests --unsupported-profile)
    add_test(NAME stl_requests COMMAND meshvale_stl_request_tests --unsupported-profile)
  endif()
  add_executable(meshvale_off_tests tests/off.cpp)
  add_executable(meshvale_off_request_tests tests/off_requests.cpp)
  target_include_directories(meshvale_off_request_tests PRIVATE src)
  foreach(meshvale_off_test IN ITEMS meshvale_off_tests meshvale_off_request_tests)
    target_link_libraries(${meshvale_off_test} PRIVATE meshvale::off)
    meshvale_warnings(${meshvale_off_test})
    if(MSVC)
      target_compile_options(${meshvale_off_test} PRIVATE /fp:strict)
    else()
      target_compile_options(${meshvale_off_test} PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
  endforeach()
  if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
    add_test(NAME off_interchange COMMAND meshvale_off_tests)
    add_test(NAME off_requests COMMAND meshvale_off_request_tests)
  else()
    add_test(NAME off_interchange COMMAND meshvale_off_tests --unsupported-profile)
    add_test(NAME off_requests COMMAND meshvale_off_request_tests --unsupported-profile)
  endif()
  add_executable(meshvale_ply_tests tests/ply.cpp)
  add_executable(meshvale_ply_request_tests tests/ply_requests.cpp)
  target_include_directories(meshvale_ply_request_tests PRIVATE src)
  foreach(meshvale_ply_test IN ITEMS meshvale_ply_tests meshvale_ply_request_tests)
    target_link_libraries(${meshvale_ply_test} PRIVATE meshvale::ply)
    meshvale_warnings(${meshvale_ply_test})
    if(MSVC)
      target_compile_options(${meshvale_ply_test} PRIVATE /fp:strict)
    else()
      target_compile_options(${meshvale_ply_test} PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
  endforeach()
  if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
    add_test(NAME ply_interchange COMMAND meshvale_ply_tests)
    add_test(NAME ply_requests COMMAND meshvale_ply_request_tests)
  else()
    add_test(NAME ply_interchange COMMAND meshvale_ply_tests --unsupported-profile)
    add_test(NAME ply_requests COMMAND meshvale_ply_request_tests --unsupported-profile)
  endif()
  add_executable(meshvale_glb_tests tests/glb.cpp)
  target_include_directories(meshvale_glb_tests PRIVATE src)
  target_link_libraries(meshvale_glb_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_glb_tests)
  add_test(NAME glb_container COMMAND meshvale_glb_tests)
  add_executable(meshvale_obj_tests tests/obj.cpp)
  target_link_libraries(meshvale_obj_tests PRIVATE meshvale::interchange)
  meshvale_warnings(meshvale_obj_tests)
  add_test(NAME obj_interchange COMMAND meshvale_obj_tests)
  add_executable(meshvale_obj_file_tests tests/obj_files.cpp)
  target_link_libraries(meshvale_obj_file_tests PRIVATE meshvale::interchange)
  meshvale_warnings(meshvale_obj_file_tests)
  add_test(NAME obj_files COMMAND meshvale_obj_file_tests)
  add_executable(meshvale_gltf_allocation_tests tests/gltf_allocation.cpp)
  target_include_directories(meshvale_gltf_allocation_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_allocation_tests PRIVATE meshvale::interchange meshvale::triangulation)
  meshvale_warnings(meshvale_gltf_allocation_tests)
  add_test(NAME gltf_allocation COMMAND meshvale_gltf_allocation_tests)
  add_executable(meshvale_gltf_read_tests tests/gltf_read.cpp)
  target_link_libraries(meshvale_gltf_read_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_read_tests)
  add_test(NAME gltf_read COMMAND meshvale_gltf_read_tests)
  add_executable(meshvale_gltf_execution_budget_tests tests/gltf_execution_budget.cpp)
  target_include_directories(meshvale_gltf_execution_budget_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_execution_budget_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_execution_budget_tests)
  add_test(NAME gltf_execution_budget COMMAND meshvale_gltf_execution_budget_tests)
  add_executable(meshvale_gltf_workflow_read_tests tests/gltf_workflow_read.cpp)
  target_include_directories(meshvale_gltf_workflow_read_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_workflow_read_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_workflow_read_tests)
  add_test(NAME gltf_workflow_read COMMAND meshvale_gltf_workflow_read_tests)
  add_test(NAME gltf_workflow_read_large_index COMMAND meshvale_gltf_workflow_read_tests
    --large-source-index --resource-budget-mib 1024)
  set_tests_properties(gltf_workflow_read_large_index PROPERTIES RUN_SERIAL TRUE)
  add_executable(meshvale_gltf_resource_tests tests/gltf_resources.cpp)
  target_include_directories(meshvale_gltf_resource_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_resource_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_resource_tests)
  add_test(NAME gltf_resources COMMAND meshvale_gltf_resource_tests)
  add_executable(meshvale_gltf_write_tests tests/gltf_write.cpp)
  target_include_directories(meshvale_gltf_write_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_write_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_write_tests)
  add_test(NAME gltf_write COMMAND meshvale_gltf_write_tests)
  add_executable(meshvale_gltf_write_oracle tests/gltf_write_oracle.cpp)
  target_link_libraries(meshvale_gltf_write_oracle PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_write_oracle)
  add_executable(meshvale_gltf_metadata_tests tests/gltf_metadata.cpp)
  target_include_directories(meshvale_gltf_metadata_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_metadata_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_metadata_tests)
  add_test(NAME gltf_metadata COMMAND meshvale_gltf_metadata_tests)
  add_executable(meshvale_gltf_accessor_tests tests/gltf_accessor.cpp)
  target_include_directories(meshvale_gltf_accessor_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_accessor_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_accessor_tests)
  add_test(NAME gltf_accessor COMMAND meshvale_gltf_accessor_tests)
  add_executable(meshvale_gltf_extract_tests tests/gltf_extract.cpp)
  target_include_directories(meshvale_gltf_extract_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_extract_tests PRIVATE meshvale::interchange meshvale::editing)
  meshvale_warnings(meshvale_gltf_extract_tests)
  add_test(NAME gltf_extract COMMAND meshvale_gltf_extract_tests)
  add_executable(meshvale_gltf_json_tests tests/gltf_json_snapshot.cpp)
  target_include_directories(meshvale_gltf_json_tests PRIVATE src)
  target_link_libraries(meshvale_gltf_json_tests PRIVATE meshvale::interchange)
  meshvale_warnings(meshvale_gltf_json_tests)
  add_test(NAME gltf_json_snapshot COMMAND meshvale_gltf_json_tests)
  if(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL "${_MESHVALE_INTERCHANGE_PROFILE_QUALIFIED}")
    add_executable(meshvale_gltf_materialization_tests tests/gltf_materialization.cpp)
    target_include_directories(meshvale_gltf_materialization_tests PRIVATE src)
    target_link_libraries(meshvale_gltf_materialization_tests PRIVATE meshvale::gltf_materialization)
    meshvale_warnings(meshvale_gltf_materialization_tests)
    add_test(NAME gltf_materialization COMMAND meshvale_gltf_materialization_tests)
    add_executable(meshvale_gltf_materialization_semantics_tests
      tests/gltf_materialization_semantics/main.cpp
      tests/gltf_materialization_semantics/fixture_support.cpp
      tests/gltf_materialization_semantics/fixture_data.cpp
      tests/gltf_materialization_semantics/raw_channels.cpp
      tests/gltf_materialization_semantics/topology_provenance.cpp
      tests/gltf_materialization_semantics/lifetimes.cpp)
    target_link_libraries(meshvale_gltf_materialization_semantics_tests PRIVATE meshvale::gltf_materialization)
    meshvale_warnings(meshvale_gltf_materialization_semantics_tests)
    add_test(NAME gltf_materialization_semantics COMMAND meshvale_gltf_materialization_semantics_tests)
  else()
    add_executable(meshvale_gltf_materialization_unsupported_tests examples/consumer/gltf_materialization_unsupported.cpp)
    target_link_libraries(meshvale_gltf_materialization_unsupported_tests PRIVATE meshvale::gltf_materialization)
    meshvale_warnings(meshvale_gltf_materialization_unsupported_tests)
    add_test(NAME unsupported_gltf_materialization_profile COMMAND meshvale_gltf_materialization_unsupported_tests)
  endif()
  if(MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE)
    meshvale_add_gltf_topology_write_tests()
  endif()
  if(MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT)
    meshvale_add_gltf_workflow_support_tests()
  endif()
  if(MESHVALE_INTERCHANGE_GLTF_FILES)
    meshvale_add_gltf_file_tests()
  endif()
  if(MESHVALE_INTERCHANGE_OFF_FILES)
    meshvale_add_off_file_tests()
  endif()
endif()

# Optional format modules own their SDK discovery, exports and installed notices.
include(cmake/usd.cmake)

option(MESHVALE_INTERCHANGE_FBX "Build optional native FBX polygon import" OFF)
if(MESHVALE_INTERCHANGE_FBX)
  include(cmake/fbx.cmake)
endif()
