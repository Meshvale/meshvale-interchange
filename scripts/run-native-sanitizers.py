# SPDX-License-Identifier: Apache-2.0
"""Run matching Linux sanitizer builds and the actual native glTF interfaces."""
import argparse
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess

BUILD_CONFIGURATION = "Debug"
BUILD_WORKERS = 2
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
NATIVE_TESTS = {
    "glb_container", "obj_interchange", "obj_files", "gltf_allocation",
    "gltf_read", "gltf_resources", "gltf_write", "gltf_metadata",
    "gltf_accessor", "gltf_extract", "gltf_json_snapshot",
}
INSTALLED_TESTS = {
    "installed_interchange_consumer", "installed_obj_bundle_consumer",
    "installed_execution_dependency_consumer", "installed_gltf_consumer",
    "installed_glb_consumer", "installed_gltf_write_consumer",
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

    def instrumented(self, database, compile_flags, warnings_as_errors):
        entries = json.loads(database.read_text(encoding="utf-8"))
        if not entries:
            raise RuntimeError(f"No compilation commands in {database}")
        compiler = Path(shutil.which("clang++")).resolve()
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
        target = self.evidence / (database.parent.name + "-compile-commands.json")
        target.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
        print(f"Verified {len(entries)} instrumented compile commands: {database}",
              flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--geometry-source", type=Path, required=True)
    parser.add_argument("--vcpkg-root", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "AMD64"}:
        parser.error("This runner evaluates Linux x64 native sanitizer execution")
    root = Path(__file__).resolve().parents[1]
    geometry = (root / args.geometry_source).resolve()
    vcpkg = (root / args.vcpkg_root).resolve()
    profile = PROFILES[args.profile]
    triplet = f"x64-linux-{args.profile}"
    work = root / ".local" / "native-sanitizers" / args.profile
    evidence = work / "evidence"
    work.mkdir(parents=True, exist_ok=False)
    evidence.mkdir()
    environment = os.environ.copy()
    environment.update({"CC": "clang", "CXX": "clang++", "VCPKG_ROOT": str(vcpkg),
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
                "vcpkg": runner.run("git", "-C", vcpkg, "rev-parse", "HEAD").strip()}
    (evidence / "inputs.json").write_text(json.dumps(identity, indent=2) + "\n",
                                         encoding="utf-8")
    runner.run("clang++", "--version")
    runner.run("cmake", "--version")
    runner.run("bash", vcpkg / "bootstrap-vcpkg.sh", "-disableMetrics")
    common = [f"-DCMAKE_BUILD_TYPE={BUILD_CONFIGURATION}",
              "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_C_COMPILER=clang",
              f"-DCMAKE_CXX_FLAGS={profile['flags']}",
              f"-DCMAKE_C_FLAGS={profile['flags']}",
              f"-DCMAKE_EXE_LINKER_FLAGS={profile['link_flags']}",
              f"-DCMAKE_SHARED_LINKER_FLAGS={profile['link_flags']}",
              "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    geometry_build, prefix = work / "geometry-build", work / "prefix"
    allocation_probe = "OFF" if profile["allocation_probe"] == "disabled" else "ON"
    runner.run("cmake", "-S", geometry, "-B", geometry_build, *common,
               f"-DMESHVALE_GEOMETRY_ALLOCATION_PROBE={allocation_probe}")
    runner.run("cmake", "--build", geometry_build, "--parallel", BUILD_WORKERS)
    runner.instrumented(geometry_build / "compile_commands.json", profile["flags"], True)
    geometry_inventory = runner.run("ctest", "--test-dir", geometry_build, "-C",
                                    BUILD_CONFIGURATION, "--show-only=json-v1")
    (evidence / "geometry-inventory.json").write_text(geometry_inventory, encoding="utf-8")
    runner.run("python", geometry / "scripts/check-native-tests.py", "--build-dir",
               geometry_build, "--configuration", BUILD_CONFIGURATION,
               "--allocation-probe", profile["allocation_probe"])
    runner.run("ctest", "--test-dir", geometry_build, "-C", BUILD_CONFIGURATION,
               "--output-on-failure")
    runner.run("cmake", "--install", geometry_build, "--config", BUILD_CONFIGURATION,
               "--prefix", prefix)
    build, dependencies = work / "interchange-build", work / "dependencies"
    toolchain = [f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
                 f"-DVCPKG_TARGET_TRIPLET={triplet}",
                 f"-DVCPKG_OVERLAY_TRIPLETS={root}/cmake/sanitizer-triplets",
                 f"-DVCPKG_INSTALLED_DIR={dependencies}",
                 f"-DCMAKE_PREFIX_PATH={prefix}"]
    runner.run("cmake", "-S", root, "-B", build, *common, *toolchain,
               "-DMESHVALE_INTERCHANGE_USD=OFF", "-DMESHVALE_INTERCHANGE_FBX=OFF",
               "-DMESHVALE_INTERCHANGE_PYTHON=OFF")
    for suffix in DEPENDENCY_CONFIGURATIONS.values():
        parser_database = (vcpkg / "buildtrees/tinyobjloader" /
                           f"{triplet}-{suffix}/compile_commands.json")
        runner.instrumented(parser_database, profile["flags"], False)
    runner.run("cmake", "--build", build, "--parallel", BUILD_WORKERS)
    runner.instrumented(build / "compile_commands.json", profile["flags"], True)
    runner.inventory(build, NATIVE_TESTS, "interchange")
    runner.run("ctest", "--test-dir", build, "-C", BUILD_CONFIGURATION,
               "--output-on-failure")
    runner.run("python", "tests/gltf_json_oracle.py", build / "meshvale_gltf_json_tests",
               work / "json-oracle")
    for name, mode in (("accessor-oracle", []), ("read-extract-oracle", ["read"])):
        runner.run("python", "tests/gltf_accessor_oracle.py",
                   build / "meshvale_gltf_extract_tests", work / name, *mode)
    runner.run("python", "tests/gltf_write_oracle.py", build / "meshvale_gltf_write_oracle",
               work / "write-reload-oracle")
    runner.run("cmake", "--install", build, "--config", BUILD_CONFIGURATION,
               "--prefix", prefix)
    consumer = work / "installed-build"
    runner.run("cmake", "-S", "examples/consumer", "-B", consumer, *common,
               *toolchain, "-DVCPKG_MANIFEST_MODE=OFF")
    runner.run("cmake", "--build", consumer, "--parallel", BUILD_WORKERS)
    runner.instrumented(consumer / "compile_commands.json", profile["flags"], True)
    runner.inventory(consumer, INSTALLED_TESTS, "installed")
    runner.run("ctest", "--test-dir", consumer, "-C", BUILD_CONFIGURATION,
               "--output-on-failure")
    (evidence / "completed.json").write_text(json.dumps(identity, indent=2) + "\n",
                                             encoding="utf-8")


if __name__ == "__main__":
    main()
