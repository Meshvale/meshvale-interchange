# SPDX-License-Identifier: Apache-2.0
"""Verify configured native and installed materialization profile inventories."""
import argparse
import json
from pathlib import Path
import re
import subprocess

QUALIFIED_PROFILE = "qualified"
UNSUPPORTED_PROFILE = "unsupported"
MATERIALIZATION_PROFILES = (QUALIFIED_PROFILE, UNSUPPORTED_PROFILE)
COMPILER_IDS = ("MSVC", "Clang", "GNU", "AppleClang")
PROFILE_CACHE_KEY = "MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE"
TESTING_CACHE_KEY = "BUILD_TESTING"
CONFIGURATIONS_CACHE_KEY = "CMAKE_CONFIGURATION_TYPES"
BUILD_TYPE_CACHE_KEY = "CMAKE_BUILD_TYPE"
STRING_CACHE_TYPE = "STRING"
BOOL_CACHE_TYPE = "BOOL"
TRUE_CACHE_VALUES = frozenset({"ON", "TRUE", "YES", "1"})
COMMON_TESTS = frozenset({
    "glb_container", "obj_interchange", "obj_files", "gltf_allocation",
    "gltf_read", "gltf_resources", "gltf_write", "gltf_metadata",
    "gltf_accessor", "gltf_extract", "gltf_json_snapshot",
})
PROFILE_TESTS = {
    QUALIFIED_PROFILE: frozenset({"gltf_materialization", "gltf_materialization_semantics"}),
    UNSUPPORTED_PROFILE: frozenset({"unsupported_gltf_materialization_profile"}),
}
COMMON_INSTALLED_TESTS = frozenset({
    "installed_interchange_consumer", "installed_obj_bundle_consumer",
    "installed_execution_dependency_consumer", "installed_gltf_consumer",
    "installed_glb_consumer", "installed_gltf_write_consumer",
})
PROFILE_INSTALLED_TESTS = {
    QUALIFIED_PROFILE: frozenset({"installed_gltf_materialization_consumer"}),
    UNSUPPORTED_PROFILE: frozenset({"installed_unsupported_gltf_materialization_profile"}),
}


def cache_entries(path):
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")):
            continue
        name_and_type, separator, value = line.partition("=")
        name, typed, cache_type = name_and_type.partition(":")
        if separator and typed:
            if name in result:
                raise ValueError("duplicate CMake cache entry: " + name)
            result[name] = (cache_type, value)
    return result


def cache_value(cache, name, expected_type):
    if name not in cache or cache[name][0] != expected_type:
        raise ValueError("missing or incorrectly typed CMake cache entry: " + name)
    return cache[name][1]


def validate_cache(cache, profile, configuration):
    if cache_value(cache, PROFILE_CACHE_KEY, STRING_CACHE_TYPE) != profile:
        raise ValueError("materialization CLI profile differs from configured CMake profile")
    if cache_value(cache, TESTING_CACHE_KEY, BOOL_CACHE_TYPE).upper() not in TRUE_CACHE_VALUES:
        raise ValueError("native inventory requires BUILD_TESTING=ON")
    if CONFIGURATIONS_CACHE_KEY in cache:
        configurations = cache_value(cache, CONFIGURATIONS_CACHE_KEY, STRING_CACHE_TYPE).split(";")
        if configuration not in configurations:
            raise ValueError("requested configuration is absent from multi-config cache")
    elif cache_value(cache, BUILD_TYPE_CACHE_KEY, STRING_CACHE_TYPE) != configuration:
        raise ValueError("requested configuration differs from single-config CMake build type")


def validate_compiler(build_dir, expected_id, expected_version):
    records = list((build_dir / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if len(records) != 1:
        raise ValueError("need one fresh CMake compiled-compiler identity record")
    text = records[0].read_text(encoding="utf-8")
    actual_id = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"\n]+)"\)', text)
    actual_version = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"\n]+)"\)', text)
    if actual_id is None or actual_id.group(1) != expected_id:
        raise ValueError("configured C++ compiler identity differs from declared CI cohort")
    if expected_version and (actual_version is None or actual_version.group(1) != expected_version):
        raise ValueError("configured C++ compiler version differs from declared admitted cohort")


def expected_inventory(profile, installed=False):
    common = COMMON_INSTALLED_TESTS if installed else COMMON_TESTS
    specific = PROFILE_INSTALLED_TESTS if installed else PROFILE_TESTS
    return common | specific[profile]


def validate_inventory(build_dir, configuration, expected):
    tests = json.loads(subprocess.check_output([
        "ctest", "--test-dir", str(build_dir), "-C", configuration,
        "--show-only=json-v1"], text=True))["tests"]
    names = [test["name"] for test in tests]
    if len(names) != len(expected) or set(names) != expected:
        raise ValueError(f"expected {sorted(expected)}, found {names}")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--installed-build-dir", type=Path)
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--compiler-id", choices=COMPILER_IDS, required=True)
    parser.add_argument("--compiler-version")
    parser.add_argument("--materialization-profile", choices=MATERIALIZATION_PROFILES,
                        required=True)
    args = parser.parse_args()
    try:
        checked = []
        for build_dir, installed in ((args.build_dir, False),
                                     (args.installed_build_dir, True)):
            if build_dir is None:
                continue
            validate_cache(cache_entries(build_dir / "CMakeCache.txt"),
                           args.materialization_profile, args.configuration)
            validate_compiler(build_dir, args.compiler_id, args.compiler_version)
            checked.append(("installed" if installed else "native",
                            validate_inventory(build_dir, args.configuration,
                                               expected_inventory(args.materialization_profile,
                                                                  installed))))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    for label, names in checked:
        print(f"{label}: verified {len(names)} test entries; materialization "
              f"{args.materialization_profile}; {args.configuration}; {names}")


if __name__ == "__main__":
    main()
