# SPDX-License-Identifier: Apache-2.0
include(FetchContent)

# Archive overrides remain content-addressed; arbitrary source overrides would
# bypass these checks, so acquire into our own private build directory.
set(MESHVALE_CGLTF_ARCHIVE "" CACHE FILEPATH "Verified offline cgltf archive")
set(MESHVALE_JSONCONS_ARCHIVE "" CACHE FILEPATH "Verified offline jsoncons archive")
function(meshvale_gltf_archive name override url digest)
  if(override)
    file(SHA256 "${override}" actual)
    if(NOT actual STREQUAL digest)
      message(FATAL_ERROR "${name}: offline archive SHA256 mismatch")
    endif()
    set(input "${override}")
  else()
    set(input "${url}")
  endif()
  # SOURCE_DIR overrides cannot replace the selected archive input.
  string(TOUPPER "${name}" upper_name)
  if(DEFINED FETCHCONTENT_SOURCE_DIR_${upper_name} AND
     NOT "${FETCHCONTENT_SOURCE_DIR_${upper_name}}" STREQUAL "")
    message(FATAL_ERROR "${name}: use the verified archive override")
  endif()
  # A deliberately absent subdirectory acquires headers without configuring
  # upstream examples/targets/install. MakeAvailable supports CMake 3.24+.
  FetchContent_Declare(${name} URL "${input}" URL_HASH "SHA256=${digest}"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE SOURCE_SUBDIR meshvale-header-input-only)
  FetchContent_MakeAvailable(${name})
  set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
meshvale_gltf_archive(meshvale_cgltf "${MESHVALE_CGLTF_ARCHIVE}"
  "https://codeload.github.com/jkuhlmann/cgltf/tar.gz/360db1a95480fe102ae9c69b27c5d101167ff5ba"
  "445d135cf793232ae6a585ca1404e4ff28d4f4dbca070689034fe780370ac84a")
meshvale_gltf_archive(meshvale_jsoncons "${MESHVALE_JSONCONS_ARCHIVE}"
  "https://codeload.github.com/danielaparker/jsoncons/tar.gz/9e57e421285422d7aeeb5aa7dc839f6a19121ee6"
  "7984a2a3f9fa630162a6d3a64f79391aee2357a814ebafb5379fbeac0b731aeb")
