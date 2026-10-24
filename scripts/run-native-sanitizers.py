# SPDX-License-Identifier: Apache-2.0
"""Run matching Linux sanitizer builds and actual native interchange/Repair."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
from runpy import run_path
import shlex
import shutil
import subprocess

INVENTORY_CHECKER = Path(__file__).with_name("check-native-tests.py")
expected_inventory = run_path(str(INVENTORY_CHECKER))["expected_inventory"]

BUILD_CONFIGURATION = "Debug"
CLANG_CXX = "clang++-18"
CLANG_C = "clang-18"
CLANG_VERSION = "18.1.3"
GEOMETRY_REVISION = "48298b24be7c132816fb5580d22d58bde61558d0"
REPAIR_REVISION = "99c2fb742dd16a7d147d7994a8db80088289505d"
VCPKG_REVISION = "cb5a41b03cde4086d554b3f3e2eac39206b16eda"
MATERIALIZATION_PROFILE = "qualified"
BUILD_WORKERS = 2
OFF_REPAIR_TEST = "installed_off_repair_consumer"
DEPENDENCY_CONFIGURATIONS = {"Debug": "dbg", "Release": "rel"}
PROFILES = {
    "asan-ubsan": {
        "flags": "-fsanitize=address,undefined -fno-omit-frame-pointer",
        "link_flags": "-fsanitize=address,undefined",
        "allocation_probe": "enabled",
        "runtime": {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"},
    },
    "tsan": {
        "flags": "-fsanitize=thread -O1 -g -fno-omit-frame-pointer",
        "link_flags": "-fsanitize=thread",
        "allocation_probe": "disabled",
        "runtime": {"TSAN_OPTIONS": "halt_on_error=1"},
    },
}


class Runner:
    def __init__(self, root, evidence, environment):
        self.root, self.evidence, self.environment = root, evidence, environment
        self.command_number = 0

    def run(self, *command):
        command = list(map(str, command))
        print(shlex.join(command), flush=True)
        result = subprocess.run(command, cwd=self.root, env=self.environment,
                                capture_output=True, text=True)
        self.command_number += 1
        stem = self.evidence / f"command-{self.command_number:03}"
        stem.with_suffix(".stdout").write_text(result.stdout, encoding="utf-8")
        stem.with_suffix(".stderr").write_text(result.stderr, encoding="utf-8")
        with (self.evidence / "commands.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"command": command,
                                     "returncode": result.returncode}) + "\n")
        print(result.stdout, end="", flush=True)
        print(result.stderr, end="", flush=True)
        result.check_returncode()
        return result.stdout

    def inventory(self, build, expected, name):
        text = self.run("ctest", "--test-dir", build, "-C", BUILD_CONFIGURATION,
                        "--show-only=json-v1")
        (self.evidence / f"{name}-inventory.json").write_text(text, encoding="utf-8")
        actual = [test["name"] for test in json.loads(text)["tests"]]
        if len(actual) != len(expected) or set(actual) != expected:
            raise RuntimeError(f"{name}: expected {sorted(expected)}, found {actual}")
        print(f"{name}: verified {len(actual)} actual test entries", flush=True)

    def test(self, build, name):
        actual_log = Path(build) / "Testing" / "Temporary" / "LastTest.log"
        try:
            self.run("ctest", "--test-dir", build, "-C", BUILD_CONFIGURATION,
                     "--verbose", "--output-on-failure")
        finally:
            if actual_log.is_file():
                shutil.copyfile(actual_log, self.evidence / (name + "-actual-LastTest.log"))
        if not actual_log.is_file():
            raise RuntimeError("Executed CTest did not retain its actual test log")

    def instrumented(self, database, compile_flags, warnings_as_errors):
        # Preserve the complete input even when verification rejects a unit.
        target = self.evidence / (database.parent.name + "-compile-commands.json")
        shutil.copyfile(database, target)
        entries = json.loads(database.read_text(encoding="utf-8"))
        if not entries:
            raise RuntimeError(f"No compilation commands in {database}")
        compiler = Path(shutil.which(CLANG_CXX)).resolve()
        for entry in entries:
            arguments = entry.get("arguments") or shlex.split(entry["command"])
            actual_compiler = shutil.which(arguments[0])
            if not actual_compiler or Path(actual_compiler).resolve() != compiler:
                raise RuntimeError(f"Compiler mismatch in {database}: {arguments[0]}")
            required = shlex.split(compile_flags)
            if warnings_as_errors:
                required.append("-Werror")
            if not all(flag in arguments for flag in required):
                raise RuntimeError(f"Missing instrumentation/Werror in {entry['file']}")
        print(f"Verified {len(entries)} instrumented compile commands: {database}",
              flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--geometry-source", type=Path, required=True)
    parser.add_argument("--repair-source", type=Path, required=True)
    parser.add_argument("--vcpkg-root", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "AMD64"}:
        parser.error("This runner evaluates Linux x64 native sanitizer execution")
    root = Path(__file__).resolve().parents[1]
    geometry = (root / args.geometry_source).resolve()
    repair = (root / args.repair_source).resolve()
    vcpkg = (root / args.vcpkg_root).resolve()
    profile = PROFILES[args.profile]
    triplet = f"x64-linux-{args.profile}"
    work = root / ".local" / "native-sanitizers" / args.profile
    evidence = work / "evidence"
    work.mkdir(parents=True, exist_ok=False)
    evidence.mkdir()
    environment = os.environ.copy()
    environment.update({"CC": CLANG_C, "CXX": CLANG_CXX,
                        "CFLAGS": "", "CXXFLAGS": "", "LDFLAGS": "", "VCPKG_ROOT": str(vcpkg),
                        "VCPKG_BINARY_SOURCES": "clear", "VCPKG_DISABLE_METRICS": "1",
                        "VCPKG_MAX_CONCURRENCY": str(BUILD_WORKERS),
                        "CMAKE_BUILD_PARALLEL_LEVEL": str(BUILD_WORKERS),
                        "MESHVALE_SANITIZER_PROFILE": args.profile,
                        "MESHVALE_SANITIZER_COMPILE_FLAGS": profile["flags"],
                        "MESHVALE_SANITIZER_LINK_FLAGS": profile["link_flags"]})
    for variable in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS"):
        environment.pop(variable, None)
    environment.update(profile["runtime"])
    runner = Runner(root, evidence, environment)
    identity = {"profile": args.profile, "runtime": profile["runtime"],
                "compile_flags": profile["flags"], "link_flags": profile["link_flags"],
                "configuration": BUILD_CONFIGURATION,
                "interchange": runner.run("git", "rev-parse", "HEAD").strip(),
                "geometry": runner.run("git", "-C", geometry, "rev-parse", "HEAD").strip(),
                "repair": runner.run("git", "-C", repair, "rev-parse", "HEAD").strip(),
                "vcpkg": runner.run("git", "-C", vcpkg, "rev-parse", "HEAD").strip()}
    (evidence / "inputs.json").write_text(json.dumps(identity, indent=2) + "\n",
                                         encoding="utf-8")
    if (identity["geometry"] != GEOMETRY_REVISION or
            identity["repair"] != REPAIR_REVISION or identity["vcpkg"] != VCPKG_REVISION):
        raise RuntimeError("Dependency source revision differs from the selected cohort")
    for source in (geometry, repair, vcpkg):
        if runner.run("git", "-C", source, "status", "--porcelain").strip():
            raise RuntimeError("Dependency source checkout must be clean: " + str(source))
    compiler_identity = runner.run(CLANG_CXX, "--version")
    compiler_version = re.search(r"clang version ([0-9.]+)", compiler_identity)
    if compiler_version is None or compiler_version.group(1) != CLANG_VERSION:
        raise RuntimeError("Clang version differs from the admitted cohort")
    runner.run("cmake", "--version")
    runner.run("bash", vcpkg / "bootstrap-vcpkg.sh", "-disableMetrics")
    common = [f"-DCMAKE_BUILD_TYPE={BUILD_CONFIGURATION}",
              f"-DCMAKE_CXX_COMPILER={CLANG_CXX}", f"-DCMAKE_C_COMPILER={CLANG_C}",
              f"-DCMAKE_CXX_FLAGS={profile['flags']}",
              f"-DCMAKE_C_FLAGS={profile['flags']}",
              f"-DCMAKE_EXE_LINKER_FLAGS={profile['link_flags']}",
              f"-DCMAKE_SHARED_LINKER_FLAGS={profile['link_flags']}",
              "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    geometry_build, prefix = work / "geometry-build", work / "prefix"
    allocation_probe = "OFF" if profile["allocation_probe"] == "disabled" else "ON"
    runner.run("cmake", "-S", geometry, "-B", geometry_build, *common,
               f"-DMESHVALE_GEOMETRY_ALLOCATION_PROBE={allocation_probe}",
               "-DCMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON",
               f"-DMESHVALE_GEOMETRY_MATERIALIZATION_TEST_PROFILE={MATERIALIZATION_PROFILE}")
    runner.run("cmake", "--build", geometry_build, "--parallel", BUILD_WORKERS)
    runner.instrumented(geometry_build / "compile_commands.json", profile["flags"], True)
    geometry_inventory = runner.run("ctest", "--test-dir", geometry_build, "-C",
                                    BUILD_CONFIGURATION, "--show-only=json-v1")
    (evidence / "geometry-inventory.json").write_text(geometry_inventory, encoding="utf-8")
    runner.run("python", geometry / "scripts/check-native-tests.py", "--build-dir",
               geometry_build, "--configuration", BUILD_CONFIGURATION,
               "--allocation-probe", profile["allocation_probe"],
               "--materialization-profile", MATERIALIZATION_PROFILE,
               "--compiler-id", "Clang", "--compiler-version", CLANG_VERSION)
    runner.test(geometry_build, "geometry")
    runner.run("cmake", "--install", geometry_build, "--config", BUILD_CONFIGURATION,
               "--prefix", prefix)
    repair_build = work / "repair-build"
    runner.run("cmake", "-S", repair, "-B", repair_build, *common,
               f"-DCMAKE_PREFIX_PATH={prefix}", "-DBUILD_TESTING=OFF",
               "-DMESHVALE_REPAIR_PYTHON=OFF",
               "-DMESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON",
               "-DMESHVALE_REPAIR_GLTF_WORKFLOW=OFF",
               "-DMESHVALE_REPAIR_GLTF_FILE_WORKFLOW=OFF")
    runner.run("cmake", "--build", repair_build, "--parallel", BUILD_WORKERS)
    runner.instrumented(repair_build / "compile_commands.json", profile["flags"], True)
    shutil.copyfile(repair_build / "CMakeCache.txt", evidence / "repair-build-CMakeCache.txt")
    runner.run("cmake", "--install", repair_build, "--config", BUILD_CONFIGURATION,
               "--prefix", prefix)
    build, dependencies = work / "interchange-build", work / "dependencies"
    toolchain = [f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
                 f"-DVCPKG_TARGET_TRIPLET={triplet}",
                 f"-DVCPKG_OVERLAY_TRIPLETS={root}/cmake/sanitizer-triplets",
                 f"-DVCPKG_INSTALLED_DIR={dependencies}",
                 f"-DCMAKE_PREFIX_PATH={prefix}"]
    runner.run("cmake", "-S", root, "-B", build, *common, *toolchain,
               "-DMESHVALE_INTERCHANGE_USD=OFF", "-DMESHVALE_INTERCHANGE_FBX=OFF",
               "-DMESHVALE_INTERCHANGE_PYTHON=OFF",
               "-DMESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE=ON",
               "-DMESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT=ON",
               "-DMESHVALE_INTERCHANGE_GLTF_FILES=ON",
               f"-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE={MATERIALIZATION_PROFILE}")
    for suffix in DEPENDENCY_CONFIGURATIONS.values():
        parser_database = (vcpkg / "buildtrees/tinyobjloader" /
                           f"{triplet}-{suffix}/compile_commands.json")
        runner.instrumented(parser_database, profile["flags"], False)
    runner.run("cmake", "--build", build, "--parallel", BUILD_WORKERS)
    runner.instrumented(build / "compile_commands.json", profile["flags"], True)
    runner.inventory(build, expected_inventory(MATERIALIZATION_PROFILE, workflow_support=True,
                                               gltf_files=True), "interchange")
    runner.run("python", "scripts/check-native-tests.py", "--build-dir", build,
               "--configuration", BUILD_CONFIGURATION,
               "--materialization-profile", MATERIALIZATION_PROFILE,
               "--compiler-id", "Clang", "--compiler-version", CLANG_VERSION)
    runner.test(build, "interchange")
    runner.run("python", "tests/gltf_json_oracle.py", build / "meshvale_gltf_json_tests",
               work / "json-oracle")
    for name, mode in (("accessor-oracle", []), ("read-extract-oracle", ["read"])):
        runner.run("python", "tests/gltf_accessor_oracle.py",
                   build / "meshvale_gltf_extract_tests", work / name, *mode)
    runner.run("python", "tests/gltf_write_oracle.py", build / "meshvale_gltf_write_oracle",
               work / "write-reload-oracle")
    runner.run("python", "tests/gltf_materialization_oracle.py",
               "--exe", build / "meshvale_gltf_materialization_tests",
               "--output", work / "materialization-oracle")
    runner.run("cmake", "--install", build, "--config", BUILD_CONFIGURATION,
               "--prefix", prefix)
    consumer = work / "installed-build"
    runner.run("cmake", "-S", "examples/consumer", "-B", consumer, *common,
               *toolchain, "-DVCPKG_MANIFEST_MODE=OFF",
               "-DMESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE=ON",
               "-DMESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT=ON",
               "-DMESHVALE_INTERCHANGE_GLTF_FILES=ON",
               f"-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE={MATERIALIZATION_PROFILE}")
    runner.run("cmake", "--build", consumer, "--parallel", BUILD_WORKERS)
    runner.instrumented(consumer / "compile_commands.json", profile["flags"], True)
    runner.inventory(consumer, expected_inventory(MATERIALIZATION_PROFILE, installed=True, workflow_support=True,
                                                  gltf_files=True),
                     "installed")
    runner.run("python", "scripts/check-native-tests.py", "--build-dir", build,
               "--installed-build-dir", consumer, "--configuration", BUILD_CONFIGURATION,
               "--materialization-profile", MATERIALIZATION_PROFILE,
               "--compiler-id", "Clang", "--compiler-version", CLANG_VERSION)
    runner.test(consumer, "installed")
    off_repair = work / "off-repair-build"
    runner.run("cmake", "-S", "examples/off-repair-consumer", "-B", off_repair,
               *common, *toolchain, "-DVCPKG_MANIFEST_MODE=OFF",
               f"-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE={MATERIALIZATION_PROFILE}")
    runner.run("cmake", "--build", off_repair, "--parallel", BUILD_WORKERS)
    runner.instrumented(off_repair / "compile_commands.json", profile["flags"], True)
    shutil.copyfile(off_repair / "CMakeCache.txt", evidence / "off-repair-build-CMakeCache.txt")
    runner.inventory(off_repair, {OFF_REPAIR_TEST}, "off-repair")
    runner.test(off_repair, "off-repair")
    (evidence / "completed.json").write_text(json.dumps(identity, indent=2) + "\n",
                                             encoding="utf-8")


if __name__ == "__main__":
    main()
