# SPDX-License-Identifier: Apache-2.0
# Loaded only when the root's MESHVALE_INTERCHANGE_FBX option is enabled.
function(meshvale_find_fbx_sdk)
  if(TARGET MeshvaleFbxSdk::sdk)
    return()
  endif()
  if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
      NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "FBX requires the evaluated Windows x64 MSVC configuration")
  endif()
  if(CMAKE_CONFIGURATION_TYPES)
    if(NOT CMAKE_CONFIGURATION_TYPES STREQUAL "Release")
      message(FATAL_ERROR "FBX requires CMAKE_CONFIGURATION_TYPES=Release")
    endif()
  elseif(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "FBX requires CMAKE_BUILD_TYPE=Release")
  endif()
  if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND
      NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL")
    message(FATAL_ERROR "FBX requires the Release dynamic MSVC CRT (/MD)")
  endif()
  set(FBXSDK_ROOT "" CACHE PATH "Local licensed Autodesk FBX SDK 2020.3.11 VS2022")
  if(NOT FBXSDK_ROOT)
    message(FATAL_ERROR "Set FBXSDK_ROOT to a local licensed SDK; no automatic download is performed")
  endif()
  foreach(fbx_file IN ITEMS include/fbxsdk.h include/fbxsdk/fbxsdk_version.h
      lib/x64/release/libfbxsdk.lib lib/x64/release/libfbxsdk.dll)
    if(NOT EXISTS "${FBXSDK_ROOT}/${fbx_file}")
      message(FATAL_ERROR "Missing FBX SDK payload: ${fbx_file}")
    endif()
  endforeach()
  file(READ "${FBXSDK_ROOT}/include/fbxsdk/fbxsdk_version.h" meshvale_fbx_version)
  foreach(version_part IN ITEMS "MAJOR;2020" "MINOR;3" "POINT;11")
    list(GET version_part 0 part)
    list(GET version_part 1 value)
    if(NOT meshvale_fbx_version MATCHES "#define[ \t]+FBXSDK_VERSION_${part}[ \t]+${value}([ \t\r\n]|$)")
      message(FATAL_ERROR "FBX SDK must be exactly 2020.3.11")
    endif()
  endforeach()
  add_library(MeshvaleFbxSdk::sdk SHARED IMPORTED GLOBAL)
  set_target_properties(MeshvaleFbxSdk::sdk PROPERTIES
    IMPORTED_IMPLIB "${FBXSDK_ROOT}/lib/x64/release/libfbxsdk.lib"
    IMPORTED_LOCATION "${FBXSDK_ROOT}/lib/x64/release/libfbxsdk.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${FBXSDK_ROOT}/include"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${FBXSDK_ROOT}/include"
    INTERFACE_COMPILE_DEFINITIONS FBXSDK_SHARED)
endfunction()

# A generated installed copy contains only the finder above. Producer code
# below is never evaluated by the installed package.
if(MESHVALE_FBX_FIND_ONLY)
  return()
endif()
option(MESHVALE_INTERCHANGE_FBX "Build the optional native FBX polygon importer" OFF)
if(NOT MESHVALE_INTERCHANGE_FBX)
  return()
endif()
meshvale_find_fbx_sdk()
add_library(meshvale_interchange_fbx STATIC src/fbx_read.cpp)
add_library(meshvale::interchange_fbx ALIAS meshvale_interchange_fbx)
set_target_properties(meshvale_interchange_fbx PROPERTIES EXPORT_NAME interchange_fbx
  POSITION_INDEPENDENT_CODE ON MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_features(meshvale_interchange_fbx PUBLIC cxx_std_20)
target_include_directories(meshvale_interchange_fbx PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(meshvale_interchange_fbx PUBLIC meshvale::geometry PRIVATE MeshvaleFbxSdk::sdk)
meshvale_warnings(meshvale_interchange_fbx)
install(TARGETS meshvale_interchange_fbx EXPORT MeshvaleInterchangeFbxTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/interchange/fbx.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")
install(EXPORT MeshvaleInterchangeFbxTargets NAMESPACE meshvale::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchangeFbx")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fbx.cmake" meshvale_fbx_module)
string(FIND "${meshvale_fbx_module}" "# A generated installed copy" meshvale_fbx_split)
string(SUBSTRING "${meshvale_fbx_module}" 0 ${meshvale_fbx_split} meshvale_fbx_finder)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleFbxSdk.cmake" "${meshvale_fbx_finder}")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/fbx-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeFbxConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchangeFbx")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeFbxConfigVersion.cmake"
  VERSION "${MESHVALE_INTERCHANGE_VERSION}" COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeFbxConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeFbxConfigVersion.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleFbxSdk.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchangeFbx")
install(FILES licenses/optional/fbx-acknowledgement.txt LICENSE NOTICE THIRD_PARTY.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/MeshvaleInterchangeFbx")
install(FILES licenses/eigen-mpl2.txt licenses/eigen-apache.txt
  licenses/eigen-notices.txt licenses/meshvale-geometry-notice.txt
  DESTINATION "${CMAKE_INSTALL_DATADIR}/MeshvaleInterchangeFbx/licenses")
if(BUILD_TESTING)
  add_executable(meshvale_fbx_tests tests/fbx.cpp)
  target_link_libraries(meshvale_fbx_tests PRIVATE meshvale::interchange_fbx MeshvaleFbxSdk::sdk)
  set_target_properties(meshvale_fbx_tests PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  meshvale_warnings(meshvale_fbx_tests)
  add_test(NAME fbx_polygons COMMAND meshvale_fbx_tests "${CMAKE_CURRENT_BINARY_DIR}/fbx-fixtures")
  set_tests_properties(fbx_polygons PROPERTIES ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${FBXSDK_ROOT}/lib/x64/release")
endif()
