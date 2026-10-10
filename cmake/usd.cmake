# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_INTERCHANGE_USD "Build optional native USD mesh extraction" OFF)
if(NOT MESHVALE_INTERCHANGE_USD)
  return()
endif()
find_package(pxr 0.26.8 EXACT CONFIG REQUIRED)
find_package(MeshvaleGeometry 0.0.0 EXACT CONFIG REQUIRED)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(meshvale_usd_source_root "${CMAKE_CURRENT_LIST_DIR}/..")
set(MESHVALE_USD_DEPENDENCY_NOTICE_ROOT "${PXR_CMAKE_DIR}/.." CACHE PATH
  "Installed dependency share root containing usd/tbb/hwloc/zlib copyright")
foreach(meshvale_usd_dependency IN ITEMS usd tbb hwloc zlib)
  set(meshvale_usd_retained_notice "${meshvale_usd_source_root}/licenses/optional/${meshvale_usd_dependency}.txt")
  set(meshvale_usd_installed_notice "${MESHVALE_USD_DEPENDENCY_NOTICE_ROOT}/${meshvale_usd_dependency}/copyright")
  if(NOT EXISTS "${meshvale_usd_retained_notice}" OR NOT EXISTS "${meshvale_usd_installed_notice}")
    message(FATAL_ERROR "USD dependency ${meshvale_usd_dependency} requires retained and installed copyright texts")
  endif()
  file(READ "${meshvale_usd_retained_notice}" meshvale_usd_retained_text)
  file(READ "${meshvale_usd_installed_notice}" meshvale_usd_installed_text)
  string(REPLACE "\r\n" "\n" meshvale_usd_retained_text "${meshvale_usd_retained_text}")
  string(REPLACE "\r\n" "\n" meshvale_usd_installed_text "${meshvale_usd_installed_text}")
  if(NOT meshvale_usd_retained_text STREQUAL meshvale_usd_installed_text)
    message(FATAL_ERROR "USD dependency ${meshvale_usd_dependency} copyright differs from retained text")
  endif()
  install(FILES "${meshvale_usd_retained_notice}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/MeshvaleInterchangeUsd/licenses")
endforeach()
install(FILES "${meshvale_usd_source_root}/LICENSE" "${meshvale_usd_source_root}/NOTICE"
  "${meshvale_usd_source_root}/THIRD_PARTY.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/MeshvaleInterchangeUsd")
add_library(meshvale_interchange_usd STATIC "${meshvale_usd_source_root}/src/usd_read.cpp")
add_library(meshvale::interchange_usd ALIAS meshvale_interchange_usd)
set_target_properties(meshvale_interchange_usd PROPERTIES
  EXPORT_NAME interchange_usd POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
target_compile_features(meshvale_interchange_usd PUBLIC cxx_std_20)
target_include_directories(meshvale_interchange_usd PUBLIC
  $<BUILD_INTERFACE:${meshvale_usd_source_root}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(meshvale_interchange_usd PUBLIC meshvale::geometry
  PRIVATE usdGeom usd sdf vt gf tf)
meshvale_warnings(meshvale_interchange_usd)
install(TARGETS meshvale_interchange_usd EXPORT MeshvaleInterchangeUsdTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${meshvale_usd_source_root}/include/meshvale/interchange/usd.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/interchange")
install(EXPORT MeshvaleInterchangeUsdTargets NAMESPACE meshvale::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchangeUsd")
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/usd-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeUsdConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchangeUsd")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeUsdConfigVersion.cmake"
  VERSION "${MESHVALE_INTERCHANGE_VERSION}" COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeUsdConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleInterchangeUsdConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleInterchangeUsd")
if(BUILD_TESTING)
  add_library(meshvale_usd_header_check OBJECT "${meshvale_usd_source_root}/tests/usd_header.cpp")
  target_link_libraries(meshvale_usd_header_check PRIVATE meshvale::interchange_usd)
  meshvale_warnings(meshvale_usd_header_check)
  add_executable(meshvale_usd_tests "${meshvale_usd_source_root}/tests/usd.cpp")
  target_link_libraries(meshvale_usd_tests PRIVATE meshvale::interchange_usd usdGeom usd sdf vt gf tf)
  meshvale_warnings(meshvale_usd_tests)
  add_test(NAME usd_mesh COMMAND meshvale_usd_tests "${CMAKE_CURRENT_BINARY_DIR}/usd-fixtures")
endif()
