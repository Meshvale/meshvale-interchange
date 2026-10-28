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
TOPOLOGY_WRITE_CACHE_KEY = "MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE"
WORKFLOW_SUPPORT_CACHE_KEY = "MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT"
GLTF_FILES_CACHE_KEY = "MESHVALE_INTERCHANGE_GLTF_FILES"
OFF_FILES_CACHE_KEY = "MESHVALE_INTERCHANGE_OFF_FILES"
PLY_FILES_CACHE_KEY = "MESHVALE_INTERCHANGE_PLY_FILES"
SURFACE_OUTPUT_CACHE_KEY = "MESHVALE_INTERCHANGE_SURFACE_OUTPUT"
SURFACE_FILES_CACHE_KEY = "MESHVALE_INTERCHANGE_SURFACE_FILES"
STL_FILES_CACHE_KEY = "MESHVALE_INTERCHANGE_STL_FILES"
TOPOLOGY_WRITE_ON = "ON"
TOPOLOGY_WRITE_OFF = "OFF"
TOPOLOGY_WRITE_STATES = (TOPOLOGY_WRITE_ON, TOPOLOGY_WRITE_OFF)
TESTING_CACHE_KEY = "BUILD_TESTING"
CONFIGURATIONS_CACHE_KEY = "CMAKE_CONFIGURATION_TYPES"
BUILD_TYPE_CACHE_KEY = "CMAKE_BUILD_TYPE"
STRING_CACHE_TYPE = "STRING"
BOOL_CACHE_TYPE = "BOOL"
TRUE_CACHE_VALUES = frozenset({"ON", "TRUE", "YES", "1"})
COMMON_TESTS = frozenset({
    "ply_interchange", "ply_requests",
    "stl_interchange", "stl_requests",
    "off_interchange", "off_requests",
    "glb_container", "obj_interchange", "obj_files", "gltf_allocation",
    "gltf_read", "gltf_execution_budget", "gltf_resources", "gltf_write", "gltf_metadata",
    "gltf_accessor", "gltf_extract", "gltf_direct_translation", "gltf_json_snapshot",
    "gltf_workflow_read", "gltf_workflow_read_large_index",
})
PROFILE_TESTS = {
    QUALIFIED_PROFILE: frozenset({"gltf_materialization", "gltf_materialization_semantics"}),
    UNSUPPORTED_PROFILE: frozenset({"unsupported_gltf_materialization_profile"}),
}
COMMON_INSTALLED_TESTS = frozenset({
    "installed_ply_consumer",
    "installed_stl_consumer",
    "installed_off_consumer",
    "installed_interchange_consumer", "installed_obj_bundle_consumer",
    "installed_execution_dependency_consumer", "installed_gltf_consumer",
    "installed_gltf_direct_translation_consumer",
    "installed_glb_consumer", "installed_gltf_write_consumer",
    "installed_gltf_workflow_read_consumer",
})
PROFILE_INSTALLED_TESTS = {
    QUALIFIED_PROFILE: frozenset({"installed_gltf_materialization_consumer"}),
    UNSUPPORTED_PROFILE: frozenset({"installed_unsupported_gltf_materialization_profile"}),
}
SURFACE_OUTPUT_TESTS = frozenset({"surface_output", "surface_numeric", "surface_capsule"})
SURFACE_OUTPUT_INSTALLED_TESTS = frozenset({"installed_surface_output_consumer"})
SURFACE_FILE_TESTS = frozenset({
    "surface_files_semantics", "surface_files_requests", "surface_files_consumer",
})
SURFACE_FILE_INSTALLED_TESTS = frozenset({"installed_surface_files_consumer"})
STL_FILE_TESTS = frozenset({"stl_files_semantics", "stl_files_requests", "stl_files_consumer"})
STL_FILE_INSTALLED_TESTS = frozenset({"installed_stl_files_consumer"})
TOPOLOGY_WRITE_TESTS = frozenset({"gltf_topology_write_link"})
TOPOLOGY_WRITE_QUALIFIED_TESTS = frozenset({
    "gltf_topology_write", "gltf_topology_write_semantics",
    "gltf_topology_write_large_index",
})
TOPOLOGY_WRITE_INSTALLED_TESTS = frozenset({"installed_gltf_topology_write_link"})
TOPOLOGY_WRITE_QUALIFIED_INSTALLED_TESTS = frozenset({"installed_gltf_topology_write_consumer"})
WORKFLOW_SUPPORT_TESTS = frozenset({"gltf_workflow_support_link"})
WORKFLOW_SUPPORT_QUALIFIED_TESTS = frozenset({"gltf_workflow_inspection", "gltf_workflow_inspection_large_index"})
WORKFLOW_SUPPORT_INSTALLED_TESTS = frozenset({"installed_gltf_workflow_support_link"})
WORKFLOW_SUPPORT_QUALIFIED_INSTALLED_TESTS = frozenset({
    "installed_gltf_workflow_support_consumer", "installed_gltf_translation_output_consumer",
})
GLTF_FILE_TESTS = frozenset({
    "gltf_files_link", "gltf_files", "gltf_files_glb", "gltf_files_glb_requests",
})
GLTF_FILE_INSTALLED_TESTS = frozenset({"installed_gltf_files_link", "installed_gltf_files_consumer"})
GLTF_FILE_QUALIFIED_TESTS = frozenset({
    "gltf_file_publish_requests", "gltf_file_publish_semantics",
    "gltf_file_verified_provider_semantics", "gltf_file_verified_provider_requests",
    "gltf_file_publish_glb_semantics", "gltf_file_publish_glb_requests",
})
GLTF_FILE_UNSUPPORTED_TESTS = frozenset({
    "unsupported_gltf_file_publication_dependency",
    "unsupported_gltf_file_verified_provider_dependency",
    "unsupported_gltf_glb_publication_dependency",
})
OFF_FILE_TESTS = frozenset({"off_files_consumer", "off_files_semantics", "off_files_requests"})
OFF_FILE_INSTALLED_TESTS = frozenset({"installed_off_files_consumer"})
PLY_FILE_TESTS = frozenset({"ply_files_consumer", "ply_files_semantics", "ply_files_requests"})
PLY_FILE_INSTALLED_TESTS = frozenset({"installed_ply_files_consumer"})
GLTF_FILE_QUALIFIED_INSTALLED_TESTS = frozenset({
    "installed_gltf_file_publish_consumer", "installed_gltf_file_publish_glb_consumer",
    "installed_gltf_translation_publish_consumer",
})


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


def validate_cache(cache, profile, configuration, topology_write=True,
                   workflow_support=None, gltf_files=None, off_files=None, ply_files=None,
                   surface_output=None, surface_files=None, stl_files=None):
    if cache_value(cache, PROFILE_CACHE_KEY, STRING_CACHE_TYPE) != profile:
        raise ValueError("materialization CLI profile differs from configured CMake profile")
    if cache_value(cache, TESTING_CACHE_KEY, BOOL_CACHE_TYPE).upper() not in TRUE_CACHE_VALUES:
        raise ValueError("native inventory requires BUILD_TESTING=ON")
    actual_topology_write = cache_value(cache, TOPOLOGY_WRITE_CACHE_KEY, BOOL_CACHE_TYPE)
    if (actual_topology_write.upper() in TRUE_CACHE_VALUES) != topology_write:
        raise ValueError("topology writer CLI selection differs from configured CMake option")
    if workflow_support is not None:
        actual_support = cache_value(cache, WORKFLOW_SUPPORT_CACHE_KEY, BOOL_CACHE_TYPE)
        if (actual_support.upper() in TRUE_CACHE_VALUES) != workflow_support:
            raise ValueError("workflow support selection differs from configured CMake option")
    if gltf_files is not None:
        actual_files = cache_value(cache, GLTF_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
        if (actual_files.upper() in TRUE_CACHE_VALUES) != gltf_files:
            raise ValueError("glTF files selection differs from configured CMake option")
    if off_files is not None:
        actual_off_files = cache_value(cache, OFF_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
        if (actual_off_files.upper() in TRUE_CACHE_VALUES) != off_files:
            raise ValueError("OFF files selection differs from configured CMake option")
    if ply_files is not None:
        actual_ply_files = cache_value(cache, PLY_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
        if (actual_ply_files.upper() in TRUE_CACHE_VALUES) != ply_files:
            raise ValueError("PLY files selection differs from configured CMake option")
    if surface_output is not None:
        actual_surface = cache_value(cache, SURFACE_OUTPUT_CACHE_KEY, BOOL_CACHE_TYPE)
        if (actual_surface.upper() in TRUE_CACHE_VALUES) != surface_output:
            raise ValueError("surface output selection differs from configured CMake option")
    if surface_files is not None:
        actual_surface_files = cache_value(cache, SURFACE_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
        if (actual_surface_files.upper() in TRUE_CACHE_VALUES) != surface_files:
            raise ValueError("surface files selection differs from configured CMake option")
    if stl_files is not None:
        actual_stl_files = (cache_value(cache, STL_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
                            if STL_FILES_CACHE_KEY in cache else "OFF")
        if (actual_stl_files.upper() in TRUE_CACHE_VALUES) != stl_files:
            raise ValueError("STL files selection differs from configured CMake option")
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


def expected_inventory(profile, installed=False, topology_write=True,
                       workflow_support=False, gltf_files=False, off_files=False, ply_files=False,
                       surface_output=False, surface_files=False, stl_files=False):
    common = COMMON_INSTALLED_TESTS if installed else COMMON_TESTS
    specific = PROFILE_INSTALLED_TESTS if installed else PROFILE_TESTS
    expected = common | specific[profile]
    if topology_write:
        expected |= TOPOLOGY_WRITE_INSTALLED_TESTS if installed else TOPOLOGY_WRITE_TESTS
        if profile == QUALIFIED_PROFILE:
            expected |= (TOPOLOGY_WRITE_QUALIFIED_INSTALLED_TESTS if installed else
                         TOPOLOGY_WRITE_QUALIFIED_TESTS)
    if workflow_support:
        expected |= WORKFLOW_SUPPORT_INSTALLED_TESTS if installed else WORKFLOW_SUPPORT_TESTS
        if profile == QUALIFIED_PROFILE:
            expected |= (WORKFLOW_SUPPORT_QUALIFIED_INSTALLED_TESTS if installed else
                         WORKFLOW_SUPPORT_QUALIFIED_TESTS)
    if gltf_files:
        expected |= GLTF_FILE_INSTALLED_TESTS if installed else GLTF_FILE_TESTS
        if profile == QUALIFIED_PROFILE:
            expected |= (GLTF_FILE_QUALIFIED_INSTALLED_TESTS if installed else
                         GLTF_FILE_QUALIFIED_TESTS)
        elif not installed:
            expected |= GLTF_FILE_UNSUPPORTED_TESTS
    if off_files:
        expected |= OFF_FILE_INSTALLED_TESTS if installed else OFF_FILE_TESTS
    if ply_files:
        expected |= PLY_FILE_INSTALLED_TESTS if installed else PLY_FILE_TESTS
    if surface_output:
        expected |= SURFACE_OUTPUT_INSTALLED_TESTS if installed else SURFACE_OUTPUT_TESTS
    if surface_files:
        expected |= SURFACE_FILE_INSTALLED_TESTS if installed else SURFACE_FILE_TESTS
    if stl_files:
        expected |= STL_FILE_INSTALLED_TESTS if installed else STL_FILE_TESTS
    return expected


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
    parser.add_argument("--topology-write", choices=TOPOLOGY_WRITE_STATES,
                        default=TOPOLOGY_WRITE_ON)
    parser.add_argument("--workflow-support", choices=TOPOLOGY_WRITE_STATES,
                        help="Defaults to the native configured support option")
    parser.add_argument("--gltf-files", choices=TOPOLOGY_WRITE_STATES,
                        help="Defaults to the native configured files option")
    parser.add_argument("--off-files", choices=TOPOLOGY_WRITE_STATES,
                        help="Defaults to the native configured OFF files option")
    parser.add_argument("--ply-files", choices=TOPOLOGY_WRITE_STATES,
                        help="Defaults to the native configured PLY files option")
    parser.add_argument("--surface-output", choices=TOPOLOGY_WRITE_STATES,
                        help="Defaults to the native configured surface output option")
    parser.add_argument("--surface-files", choices=TOPOLOGY_WRITE_STATES,
                        help="Defaults to the native configured surface files option")
    parser.add_argument("--stl-files", choices=TOPOLOGY_WRITE_STATES,
                        help="Defaults to the native configured STL files option")
    args = parser.parse_args()
    try:
        checked = []
        native_cache = cache_entries(args.build_dir / "CMakeCache.txt")
        support = (cache_value(native_cache, WORKFLOW_SUPPORT_CACHE_KEY, BOOL_CACHE_TYPE)
                   if args.workflow_support is None else args.workflow_support)
        workflow_support = support.upper() in TRUE_CACHE_VALUES
        files = (cache_value(native_cache, GLTF_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
                 if args.gltf_files is None else args.gltf_files)
        gltf_files = files.upper() in TRUE_CACHE_VALUES
        off_selection = (cache_value(native_cache, OFF_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
                         if args.off_files is None else args.off_files)
        off_files = off_selection.upper() in TRUE_CACHE_VALUES
        ply_selection = (cache_value(native_cache, PLY_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
                         if args.ply_files is None else args.ply_files)
        ply_files = ply_selection.upper() in TRUE_CACHE_VALUES
        surface_selection = (cache_value(native_cache, SURFACE_OUTPUT_CACHE_KEY, BOOL_CACHE_TYPE)
                             if args.surface_output is None else args.surface_output)
        surface_output = surface_selection.upper() in TRUE_CACHE_VALUES
        surface_files_selection = (
            cache_value(native_cache, SURFACE_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
            if args.surface_files is None else args.surface_files)
        surface_files = surface_files_selection.upper() in TRUE_CACHE_VALUES
        stl_files_selection = (
            cache_value(native_cache, STL_FILES_CACHE_KEY, BOOL_CACHE_TYPE)
            if args.stl_files is None and STL_FILES_CACHE_KEY in native_cache else
            (args.stl_files or "OFF"))
        stl_files = stl_files_selection.upper() in TRUE_CACHE_VALUES
        for build_dir, installed in ((args.build_dir, False),
                                     (args.installed_build_dir, True)):
            if build_dir is None:
                continue
            validate_cache(cache_entries(build_dir / "CMakeCache.txt"),
                           args.materialization_profile, args.configuration,
                           args.topology_write == TOPOLOGY_WRITE_ON, workflow_support,
                           gltf_files, off_files, ply_files, surface_output, surface_files, stl_files)
            validate_compiler(build_dir, args.compiler_id, args.compiler_version)
            checked.append(("installed" if installed else "native",
                            validate_inventory(build_dir, args.configuration,
                                               expected_inventory(args.materialization_profile,
                                                                  installed,
                                                                  args.topology_write == TOPOLOGY_WRITE_ON,
                                                                  workflow_support,
                                                                  gltf_files,
                                                                  off_files,
                                                                  ply_files,
                                                                  surface_output,
                                                                  surface_files,
                                                                  stl_files))))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    for label, names in checked:
        print(f"{label}: verified {len(names)} test entries; materialization "
              f"{args.materialization_profile}; topology writer {args.topology_write}; "
              f"workflow support {support}; glTF files {files}; OFF files {off_selection}; "
              f"PLY files {ply_selection}; surface output {surface_selection}; "
              f"surface files {surface_files_selection}; "
              f"STL files {stl_files_selection}; "
              f"{args.configuration}; {names}")


if __name__ == "__main__":
    main()
