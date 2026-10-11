# SPDX-License-Identifier: Apache-2.0
# Select the expected outcome; actual tests must prove the producer's behavior.
set(meshvale_materialization_default unsupported)
if((MSVC AND WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8) OR
    (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
     CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "18.1.3" AND
     CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  set(meshvale_materialization_default qualified)
endif()
set(MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE "${meshvale_materialization_default}"
  CACHE STRING "Expected Geometry materialization runtime outcome: qualified or unsupported")
set_property(CACHE MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE PROPERTY STRINGS qualified unsupported)
if(NOT MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL "qualified" AND
    NOT MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE STREQUAL "unsupported")
  message(FATAL_ERROR "Materialization test profile must be qualified or unsupported")
endif()
