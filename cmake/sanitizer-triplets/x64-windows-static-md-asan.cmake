# SPDX-License-Identifier: Apache-2.0
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_C_FLAGS "/fsanitize=address /Zi")
set(VCPKG_CXX_FLAGS "/fsanitize=address /Zi")
set(VCPKG_LINKER_FLAGS "/INCREMENTAL:NO")
set(VCPKG_ENV_PASSTHROUGH ASAN_OPTIONS)
