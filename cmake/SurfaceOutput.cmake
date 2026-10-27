# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_SURFACE_OUTPUT
  "Build supplied surface output and independent byte inspection" OFF)
if(NOT MESHVALE_INTERCHANGE_SURFACE_OUTPUT)
  return()
endif()

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

if(NOT TARGET meshvale::charged_triangulation OR NOT TARGET meshvale::materialization)
  message(FATAL_ERROR
    "Surface output requires installed Geometry materialization and charged_triangulation targets from a matching producer")
endif()

function(meshvale_require_geometry_charged_triangulation)
  cmake_push_check_state(RESET)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  else()
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  endif()
  unset(MESHVALE_GEOMETRY_CHARGED_TRIANGULATION_LINK CACHE)
  set(CMAKE_REQUIRED_LIBRARIES meshvale::charged_triangulation meshvale::materialization)
  check_cxx_source_compiles("#include <meshvale/geometry/charged_triangulation.h>
  #include <meshvale/geometry/materialization.h>
  #include <meshvale/geometry/editable_mesh.h>
  #include <meshvale/geometry/execution.h>
  #include <stop_token>
  int main(int argc, char**) {
    using namespace meshvale::geometry;
    using Legacy = ChargedTriangulationResult (*)(const Mesh&, const TriangulationOptions&,
      const ChargedTriangulationLimits&, const MeshMaterializationWorkPolicy&,
      const ExecutionContext&, std::stop_token);
    using Retained = ChargedTriangulationResult (*)(const MaterializedMesh&,
      const TriangulationOptions&, const ChargedTriangulationLimits&,
      const MeshMaterializationWorkPolicy&, std::stop_token);
    Legacy legacy = static_cast<Legacy>(&TriangulateCharged);
    Retained retained = static_cast<Retained>(&TriangulateCharged);
    if (argc > 1) {
      Mesh source;
      ExecutionContext execution;
      auto first = legacy(source, {}, {}, {}, execution, {});
      if (first.triangulation) {
        const auto& value = *first.triangulation;
        auto second = retained(value.Mesh(), {}, {}, {}, {});
        auto context = value.ShareExecutionContext();
        const bool same = SharesExecutionBudget(context, execution);
        return static_cast<int>(value.Counts().output_faces + value.FaceSources().size() +
          value.CornerSources().size() + value.FaceOutputOffsets().size() +
          value.NewRequestedBytes() + value.Inspection().IsStorageValid() +
          second.triangulation.has_value() + same);
      }
    }
    return 0;
  }" MESHVALE_GEOMETRY_CHARGED_TRIANGULATION_LINK)
  cmake_pop_check_state()
  if(NOT MESHVALE_GEOMETRY_CHARGED_TRIANGULATION_LINK)
    message(FATAL_ERROR
      "Geometry must provide compiled TriangulateCharged overloads, full maps/owners and shared-budget getters; use a matching documented producer SDK")
  endif()
endfunction()
meshvale_require_geometry_charged_triangulation()

function(meshvale_surface_strict target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  meshvale_warnings(${target})
  if(MSVC)
    target_compile_options(${target} PRIVATE /fp:strict)
  else()
    target_compile_options(${target} PRIVATE -fno-fast-math -ffp-contract=off)
  endif()
endfunction()

add_library(meshvale_interchange_surface_output STATIC
  src/surface_output.cpp src/surface_numeric.cpp src/surface_capsule.cpp
  src/surface_inspect.cpp)
add_library(meshvale::surface_output ALIAS meshvale_interchange_surface_output)
set_target_properties(meshvale_interchange_surface_output PROPERTIES
  EXPORT_NAME surface_output POSITION_INDEPENDENT_CODE ON)
target_link_libraries(meshvale_interchange_surface_output PUBLIC
  meshvale::interchange meshvale::materialization meshvale::charged_triangulation
  meshvale::stl PRIVATE Threads::Threads)
meshvale_surface_strict(meshvale_interchange_surface_output)
install(TARGETS meshvale_interchange_surface_output EXPORT MeshvaleInterchangeTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/surface_write.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")

function(meshvale_add_surface_output_tests)
  foreach(name IN ITEMS output numeric capsule)
    add_executable(meshvale_surface_${name}_tests tests/surface_${name}.cpp)
    target_include_directories(meshvale_surface_${name}_tests PRIVATE src tests)
    target_link_libraries(meshvale_surface_${name}_tests PRIVATE meshvale::surface_output)
    meshvale_surface_strict(meshvale_surface_${name}_tests)
    add_test(NAME surface_${name} COMMAND meshvale_surface_${name}_tests)
  endforeach()
  # A generated, unformatted TU retains both includes independently of formatter
  # include deduplication. The separate authored TU checks self-containment.
  set(surface_twice "${CMAKE_CURRENT_BINARY_DIR}/surface_write_twice.cpp")
  file(GENERATE OUTPUT "${surface_twice}" CONTENT
    "#include <meshvale/interchange/surface_write.h>\n#include <meshvale/interchange/surface_write.h>\n")
  target_sources(meshvale_interchange_surface_output PRIVATE
    tests/headers/surface_write_h.cpp "${surface_twice}")
endfunction()
