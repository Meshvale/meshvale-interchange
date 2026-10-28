# SPDX-License-Identifier: Apache-2.0
"""Focused STL development candidate; this is not release/platform qualification.

Populate .local/stl-candidate/{g,gp,v} with the workflow's exact public sources.
Native Geometry and the unchanged Python owned-record producer stay separate.
The native rows require MSVC qualified 26 or genuine GNU13 unsupported 25;
Windows also checks default-OFF 23 and its negative installed footprint. Python
rows build source/sdist wheels and test fresh ordinary CPython3.10/3.13 runtimes.
Each installed runtime also runs the explicit four-case boundary/shutdown gate.
No capability, version or dependency-pin override is accepted.
"""
import argparse
from collections import Counter
from email.parser import BytesParser
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
from runpy import run_path
import shutil
import struct
import subprocess
import sys
import sysconfig
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile


GEOMETRY_NATIVE_REVISION = "5dd210b19d05bf651b853b8fdac398c7feceeba9"
GEOMETRY_PYTHON_REVISION = "48298b24be7c132816fb5580d22d58bde61558d0"
GEOMETRY_PYTHON_VERSION = "0.0.1.dev134+g48298b24b"
VCPKG_REVISION = "cb5a41b03cde4086d554b3f3e2eac39206b16eda"
CONFIGURATION = "Release"
WORKERS = 2
PYTHON_TEST_COUNTS = {"test_obj": 8, "test_stl_files_admission": 10,
                      "test_stl_files_api": 9, "test_stl_files_commands": 7}
STL_TESTS = {"stl_files_semantics", "stl_files_requests", "stl_files_consumer"}
INSTALLED_TESTS = {"installed_stl_files_consumer"}
OPTIONAL_OFF = ("GLTF_TOPOLOGY_WRITE", "GLTF_WORKFLOW_SUPPORT", "GLTF_FILES",
                "OFF_FILES", "PLY_FILES", "SURFACE_OUTPUT", "SURFACE_FILES", "USD", "FBX")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def one(directory, pattern):
    paths = list(directory.glob(pattern))
    if len(paths) != 1:
        raise RuntimeError(f"Need one fresh {pattern} in {directory}: {paths}")
    return paths[0]


def ordinary_python(expected):
    if (sys.implementation.name != "cpython" or
            ".".join(map(str, sys.version_info[:2])) != expected or
            struct.calcsize("P") != 8 or sysconfig.get_config_var("Py_GIL_DISABLED") or
            (hasattr(sys, "_is_gil_enabled") and not sys._is_gil_enabled())):
        raise RuntimeError("Need the selected ordinary x64 CPython: " + expected)


def native_options(profile):
    options = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + CONFIGURATION,
               "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_CXX_STANDARD=20",
               "-DCMAKE_CXX_STANDARD_REQUIRED=ON", "-DCMAKE_CXX_EXTENSIONS=OFF",
               "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
               "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"]
    if profile == "qualified":
        options += ["-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL", "-DCMAKE_CXX_FLAGS=/EHsc"]
    else:
        options += ["-DCMAKE_CXX_COMPILER=" + str(Path(shutil.which("g++-13")).resolve())]
    return options


def selections(profile):
    return ["-DMESHVALE_INTERCHANGE_" + name + "=OFF" for name in OPTIONAL_OFF] + [
        "-DMESHVALE_INTERCHANGE_PYTHON=OFF",
        "-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE=" + profile]


def configure(runner, source, build, options):
    runner.run("cmake", "-S", source, "-B", build, *options)


def build_install(runner, build, prefix=None):
    runner.run("cmake", "--build", build, "--config", CONFIGURATION,
               "--parallel", WORKERS, "--verbose")
    if prefix is not None:
        runner.run("cmake", "--install", build, "--config", CONFIGURATION, "--prefix", prefix)


def tests(runner, build, expected, label, unsupported=False):
    listing = runner.run("ctest", "--test-dir", build, "-C", CONFIGURATION,
                         "--show-only=json-v1")
    (runner.evidence / (label + "-inventory.json")).write_text(listing, encoding="utf-8")
    entries = json.loads(listing)["tests"]
    names = [entry["name"] for entry in entries]
    if len(names) != len(expected) or set(names) != expected:
        raise RuntimeError(f"{label}: actual inventory differs: {names}")
    if unsupported:
        selected = expected & (STL_TESTS | INSTALLED_TESTS)
        if any(entry["command"][-1] != "--unsupported-profile"
               for entry in entries if entry["name"] in selected):
            raise RuntimeError("STL cases must invoke their genuine unsupported paths")
    junit = runner.evidence / (label + ".xml")
    log = build / "Testing/Temporary/LastTest.log"
    try:
        runner.run("ctest", "--test-dir", build, "-C", CONFIGURATION, "--verbose",
                   "--output-on-failure", "--output-junit", junit)
    finally:
        if log.is_file():
            shutil.copyfile(log, runner.evidence / (label + "-actual-LastTest.log"))
    cases = list(ET.parse(junit).getroot().iter("testcase"))
    if (not log.is_file() or len(cases) != len(expected) or
            {case.get("name") for case in cases} != expected or
            any(case.get("status", "run") != "run" for case in cases) or
            any(case.find(kind) is not None for case in cases
                for kind in ("failure", "error", "skipped"))):
        raise RuntimeError(label + ": executed inventory failed, skipped or differs")


def audit_compilation(checker, build, source, compiler, version, stl_files=None):
    checker["validate_compiler"](build, compiler, version)
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    if not entries:
        raise RuntimeError("No actual compilation records: " + str(build))
    owned = [source / name for name in ("src", "python", "tests", "examples")]
    first_party = [entry for entry in entries if any(
        directory in Path(entry["file"]).resolve().parents for directory in owned)]
    if not first_party:
        raise RuntimeError("No actual first-party compilation records: " + str(build))
    # Nanobind/Eigen and other dependency sources retain their own warning policy.
    for entry in first_party:
        text = entry.get("command") or " ".join(entry["arguments"])
        flag = "/WX" if compiler == "MSVC" else "-Werror"
        if not re.search(r"(?<!\S)" + re.escape(flag) + r"(?=\s|$)", text):
            raise RuntimeError("Missing first-party warnings-as-errors: " + entry["file"])
    if stl_files is not None:
        actual = any(Path(entry["file"]).name == "stl_files.cpp" for entry in entries)
        if actual != stl_files:
            raise RuntimeError("Compiled STL files selection differs from the candidate")


def sdk_manifest(prefix):
    return [{"path": p.relative_to(prefix).as_posix(), "bytes": p.stat().st_size,
             "sha256": sha256(p)} for p in sorted(prefix.rglob("*")) if p.is_file()]


def consumer(runner, root, build, sdk, common, dependencies, checker, compiler, version,
             label, unsupported):
    configure(runner, root / "examples/stl-files-consumer", build, common + dependencies + [
        "-DMeshvaleInterchange_DIR=" + str(sdk / "lib/cmake/MeshvaleInterchange"),
        "-DBUILD_TESTING=ON", "-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE=" +
        ("unsupported" if unsupported else "qualified")])
    build_install(runner, build)
    audit_compilation(checker, build, root, compiler, version)
    commands = (build / "compile_commands.json").read_text(encoding="utf-8").replace("\\\\", "/").replace("\\", "/")
    if str(sdk / "include").replace("\\", "/") not in commands:
        raise RuntimeError("Installed consumer lacks actual SDK header provenance")
    for forbidden in (root / "include", root / "src"):
        if str(forbidden).replace("\\", "/") in commands:
            raise RuntimeError("Installed consumer reached a source-private include")
    tests(runner, build, INSTALLED_TESTS, label, unsupported)


def dependencies(runner, root, work, output, profile):
    vcpkg = work / "v"
    if profile == "qualified":
        runner.run("cmd", "/d", "/c", vcpkg / "bootstrap-vcpkg.bat", "-disableMetrics")
        triplet, overlay = "x64-windows-static-md", []
    else:
        runner.run("bash", vcpkg / "bootstrap-vcpkg.sh", "-disableMetrics")
        triplet = "x64-linux"
        directory = runner.evidence / "triplets"
        directory.mkdir()
        (directory / "x64-linux.cmake").write_text(
            f'include("{(vcpkg / "triplets/x64-linux.cmake").as_posix()}")\n'
            'list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n'
            f'  "-DCMAKE_CXX_COMPILER={Path(shutil.which("g++-13")).resolve().as_posix()}"\n'
            f'  "-DCMAKE_C_COMPILER={Path(shutil.which("gcc-13")).resolve().as_posix()}")\n'
            'list(APPEND VCPKG_ENV_PASSTHROUGH CC CXX)\n', encoding="utf-8")
        overlay = ["--overlay-triplets=" + str(directory)]
    installed = output / "dependencies"
    tool = vcpkg / ("vcpkg.exe" if sys.platform == "win32" else "vcpkg")
    runner.run(tool, "install", "--triplet=" + triplet, "--x-manifest-root=" + str(root),
               "--x-install-root=" + str(installed), *overlay, "--disable-metrics")
    parser = installed / triplet / "share/tinyobjloader"
    if (parser / "copyright").read_text(encoding="utf-8").strip() != (
            root / "licenses/tinyobjloader.txt").read_text(encoding="utf-8").strip():
        raise RuntimeError("Selected parser notice differs from the public notice")
    return parser


def native(runner, root, output, profile, common, dependency, checker, compiler, version):
    build, sdk = output / "native-build", output / "sdk"
    options = common + dependency + selections(profile) + [
        "-DBUILD_TESTING=ON", "-DMESHVALE_INTERCHANGE_STL_FILES=ON"]
    configure(runner, root, build, options)
    build_install(runner, build, sdk)
    audit_compilation(checker, build, root, compiler, version, True)
    runner.run(sys.executable, root / "scripts/check-native-tests.py", "--build-dir", build,
               "--configuration", CONFIGURATION, "--materialization-profile", profile,
               "--compiler-id", compiler, "--topology-write", "OFF", "--stl-files", "ON")
    expected = checker["expected_inventory"](profile, topology_write=False, stl_files=True)
    if len(expected) != (26 if profile == "qualified" else 25):
        raise RuntimeError("Candidate native contract inventory changed")
    tests(runner, build, expected, "native", profile == "unsupported")
    before = sdk_manifest(sdk)
    write_json(runner.evidence / "sdk-before-move.json", before)
    consumer(runner, root, output / "installed-build", sdk, common, dependency,
             checker, compiler, version, "installed", profile == "unsupported")
    moved = output / "moved-sdk"
    sdk.rename(moved)
    if sdk.exists() or sdk_manifest(moved) != before:
        raise RuntimeError("Physical SDK move changed bytes or left the original prefix")
    consumer(runner, root, output / "moved-build", moved, common, dependency,
             checker, compiler, version, "moved", profile == "unsupported")
    if profile != "qualified":
        return
    off, off_sdk = output / "default-off-build", output / "default-off-sdk"
    # Deliberately omit STL_FILES: this is the real default, not an OFF override.
    configure(runner, root, off, common + dependency + selections(profile) + ["-DBUILD_TESTING=ON"])
    build_install(runner, off, off_sdk)
    audit_compilation(checker, off, root, compiler, version, False)
    runner.run(sys.executable, root / "scripts/check-native-tests.py", "--build-dir", off,
               "--configuration", CONFIGURATION, "--materialization-profile", profile,
               "--compiler-id", compiler, "--topology-write", "OFF", "--stl-files", "OFF")
    expected = checker["expected_inventory"](profile, topology_write=False)
    if len(expected) != 23:
        raise RuntimeError("Default-OFF native inventory changed")
    tests(runner, off, expected, "default-off")
    manifest = sdk_manifest(off_sdk)
    write_json(runner.evidence / "default-off-sdk.json", manifest)
    if len(manifest) != 30 or any("stl_files" in item["path"] for item in manifest):
        raise RuntimeError("Default-OFF SDK must retain 30 files and no STL-files artifact")
    for path in off_sdk.rglob("*.cmake"):
        if "meshvale::stl_files" in path.read_text(encoding="utf-8"):
            raise RuntimeError("Default-OFF export unexpectedly contains the optional target")
    try:
        configure(runner, root / "examples/stl-files-consumer", output / "negative-build",
                  common + dependency + ["-DMeshvaleInterchange_DIR=" + str(
                      off_sdk / "lib/cmake/MeshvaleInterchange")])
    except subprocess.CalledProcessError as error:
        diagnostic = (error.stdout or "") + (error.stderr or "")
        if (error.returncode != 1 or "lacks optional" not in diagnostic or
                "meshvale::stl_files" not in diagnostic):
            raise
        write_json(runner.evidence / "default-off-negative-consumer.json",
                   {"expected_refusal": True, "returncode": error.returncode,
                    "command_number": runner.command_number,
                    "reason": "The installed package lacks optional meshvale::stl_files"})
    else:
        raise RuntimeError("Default-OFF optional consumer unexpectedly configured")


def package_metadata(path):
    with zipfile.ZipFile(path) as archive:
        return BytesParser().parsebytes(archive.read(next(
            name for name in archive.namelist() if name.endswith(".dist-info/METADATA"))))


def extract_source(archive_path):
    # Outside every checkout: setuptools-scm must use the actual sdist identity.
    parent = Path(tempfile.mkdtemp(prefix="meshvale-stl-sdist-"))
    with tarfile.open(archive_path) as archive:
        for entry in archive:
            parts = PurePosixPath(entry.name)
            if parts.is_absolute() or ".." in parts.parts or entry.issym() or entry.islnk():
                raise RuntimeError("Unsafe source archive member: " + entry.name)
            target = parent.joinpath(*parts.parts)
            if entry.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif entry.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(entry) as source, target.open("wb") as destination:
                    shutil.copyfileobj(source, destination)
            else:
                raise RuntimeError("Unexpected source archive member: " + entry.name)
    return one(parent, "meshvale_interchange-*")


def boundary_gate(runner, python, source, product_build, selected_python, wheel):
    """Build the CPython-only fixture for this exact installed candidate once."""
    evidence = runner.evidence
    record = {"status": "RUNNING", "selected_python": selected_python,
              "source": str(source), "product_build": str(product_build),
              "wheel": str(wheel), "wheel_sha256": sha256(wheel)}
    output = evidence / "boundary-runtime"
    try:
        cpp = source / "tests/python/stl_boundary_fixture.cpp"
        probe = source / "tests/python/stl_boundary_probe.py"
        if not cpp.is_file() or not probe.is_file():
            raise RuntimeError("Selected source lacks the reviewed public boundary fixtures")
        # Query the *fresh runtime*, never borrow the build host's Python paths.
        query = (
            "import json,pathlib,struct,sys,sysconfig; "
            "base=pathlib.Path(sys.base_prefix); "
            "print(json.dumps({'runtime':sys.executable,'base_prefix':str(base),"
            "'include':sysconfig.get_path('include'),"
            "'library':str(base/'libs'/('python'+str(sys.version_info.major)"
            "+str(sys.version_info.minor)+'.lib')),"
            "'suffix':sysconfig.get_config_var('EXT_SUFFIX'),'version':sys.version,"
            "'major_minor':list(sys.version_info[:2]),'implementation':sys.implementation.name,"
            "'pointer_bytes':struct.calcsize('P'),"
            "'gil_disabled':sysconfig.get_config_var('Py_GIL_DISABLED'),"
            "'gil_enabled':sys._is_gil_enabled() if hasattr(sys,'_is_gil_enabled') else True}))")
        selected = json.loads(runner.run(python, "-I", "-B", "-W", "error", "-c", query))
        write_json(evidence / "boundary-python.json", selected)
        expected_version = list(map(int, selected_python.split(".")))
        if (selected["implementation"] != "cpython" or
                selected["major_minor"] != expected_version or selected["pointer_bytes"] != 8 or
                selected["gil_disabled"] or not selected["gil_enabled"] or
                Path(selected["runtime"]).resolve() != python.resolve()):
            raise RuntimeError("Boundary fixture requires this row's ordinary x64 runtime")
        include = Path(selected["include"])
        library = Path(selected["library"])
        suffix = selected["suffix"]
        if (not isinstance(suffix, str) or not suffix.startswith(".") or
                not suffix.endswith(".pyd") or "/" in suffix or "\\" in suffix):
            raise RuntimeError("Selected runtime lacks a Windows extension suffix")
        compiler_record = one(product_build / "CMakeFiles", "*/CMakeCXXCompiler.cmake")
        text = compiler_record.read_text(encoding="utf-8")

        def compiler_field(name):
            match = re.search(r'set\(' + name + r' "([^"\n]+)"\)', text)
            if match is None:
                raise RuntimeError("Missing actual compiler record field: " + name)
            return match.group(1)

        compiler = Path(compiler_field("CMAKE_CXX_COMPILER")).resolve(strict=True)
        if (compiler_field("CMAKE_CXX_COMPILER_ID") != "MSVC" or
                compiler_field("CMAKE_CXX_SIZEOF_DATA_PTR") != "8" or
                compiler.name.lower() != "cl.exe"):
            raise RuntimeError("Boundary fixture requires the product's actual x64 MSVC compiler")
        database = product_build / "compile_commands.json"
        entries = json.loads(database.read_text(encoding="utf-8"))
        binding = (source / "python/stl_files_bindings.cpp").resolve()
        matches = [entry for entry in entries if Path(entry["file"]).resolve() == binding]
        if len(matches) != 1:
            raise RuntimeError("Need one selected-source binding compilation record")
        entry = matches[0]
        if "arguments" in entry:
            executable, options = entry["arguments"][0], " ".join(entry["arguments"][1:])
        else:
            first = re.match(r'^(?:"([^"]+)"|(\S+))\s+(.*)$', entry["command"].strip())
            if first is None:
                raise RuntimeError("Cannot read the actual product compiler command")
            executable, options = first.group(1) or first.group(2), first.group(3)
        actual = shutil.which(executable, path=runner.environment.get("PATH"))
        if actual is None or Path(actual).resolve() != compiler:
            raise RuntimeError("Product compiler command differs from its compiled identity")
        for flag in ("MD", "std:c++20", "W4", "WX"):
            if not re.search(r"(?<!\S)[-/]" + re.escape(flag) + r"(?=\s|$)", options):
                raise RuntimeError("Missing actual product compiler option: " + flag)
        inputs = {"runtime": python, "compiler": compiler, "compiler_record": compiler_record,
                  "compile_commands": database, "binding_source": binding,
                  "Python.h": include / "Python.h", "python_library": library,
                  "fixture_source": cpp, "probe_source": probe}
        record["inputs"] = {name: {"path": str(path), "bytes": path.stat().st_size,
                                   "sha256": sha256(path)} for name, path in inputs.items()}
        record["compiler_version"] = compiler_field("CMAKE_CXX_COMPILER_VERSION")
        if any(runner.environment.get(name) for name in ("CL", "_CL_")):
            raise RuntimeError("Hidden MSVC command-option environment overrides are not admitted")
        # Only compiler environment paths are retained; do not copy credentials.
        environment = {name: runner.environment.get(name, "") for name in
                       ("PATH", "INCLUDE", "LIB", "LIBPATH", "WINDOWSSDKDIR", "WINDOWSSDKVERSION",
                        "VCINSTALLDIR", "VCTOOLSINSTALLDIR", "UNIVERSALCRTSDKDIR", "UCRTVERSION")}
        record["compiler_environment"] = environment
        record["compiler_environment_sha256"] = hashlib.sha256(
            json.dumps(environment, sort_keys=True).encode("utf-8")).hexdigest()
        build = evidence / "boundary-build"
        build.mkdir(exist_ok=False)
        artifact = build / ("stl_boundary_fixture" + suffix)
        record["artifact_path"] = str(artifact)
        write_json(evidence / "boundary-gate.json", record)
        build_runner = type(runner)(build, build, runner.environment)
        build_runner.run(compiler, "/nologo", "/LD", "/MD", "/O2", "/EHsc", "/std:c++20",
                         "/permissive-", "/W4", "/WX", "/I" + str(include), cpp,
                         "/Fo" + str(build / "stl_boundary_fixture.obj"), "/link",
                         "/LIBPATH:" + str(library.parent), library.name, "/OUT:" + str(artifact))
        record["artifact"] = {"bytes": artifact.stat().st_size, "sha256": sha256(artifact)}
        write_json(evidence / "boundary-gate.json", record)
        runner.run(python, "-I", "-B", "-W", "error", probe,
                   "--fixture", artifact, "--output", output)
        outer_stderr = evidence / f"command-{runner.command_number:03}.stderr"
        parent = json.loads((output / "result.json").read_text(encoding="utf-8"))
        child = json.loads((output / "child-result.json").read_text(encoding="utf-8"))
        shutdown = output / "child.stderr.bin"
        if (outer_stderr.read_bytes() or shutdown.read_bytes() or
                parent["status"] != "PASS" or parent["exit_code"] != 0 or
                child["status"] != "PASS" or parent["child"] != child or
                parent["fixture_sha256"] != record["artifact"]["sha256"] or
                Path(parent["argv"][0]).resolve() != python.resolve() or
                child["python"] != selected["version"]):
            raise RuntimeError("Actual boundary parent/child/shutdown identity gate failed")
        for name in ("stdout", "stderr"):
            if parent[name + "_sha256"] != sha256(output / ("child." + name + ".bin")):
                raise RuntimeError("Boundary raw child log identity differs")
        cases = child["cases"]
        if set(cases) != {"normal", "provider", "single", "batch"} or any(
                case["status"] != "PASS" or case["post_scope"] != {"workers": 0, "payload": 0}
                for case in cases.values()):
            raise RuntimeError("Boundary gate needs all four cases and zero post-GC charge/workers")
        legacy = tuple(expected_version) < (3, 12)
        if child["legacy_raw_triple_required"] != legacy:
            raise RuntimeError("Boundary legacy raw-triple route differs from the actual interpreter")
        provider = cases["provider"]["injection"]
        if (provider["provider_calls"] != 1 or provider["provider_error_active"] != 1 or
                provider["provider_arm_result"] != 0):
            raise RuntimeError("Boundary provider did not attest the competing same-call errors")
        for name, paths in (("single", ("exit_code", "export_json")),
                            ("batch", ("exit_code", "export_json", "outcome"))):
            rows = cases[name]["replays"]
            expected = {(path, index) for path in paths for index in range(3)}
            witness = cases[name]["injection"]
            if (len(rows) != len(expected) or {(row["path"], row["index"]) for row in rows} != expected or
                    not witness["armed"] or witness["witness_workers"] <= 0 or
                    witness["witness_payload"] <= cases[name]["post_prepare_baseline"] or
                    witness["signal_calls"] != 1 or not witness["signal_value_initially_without_traceback"] or
                    not witness["signal_traceback_distinct"] or
                    (legacy and witness["legacy_raw_triple_attested"] != 1)):
                raise RuntimeError("Boundary replay/native witness/legacy attestation failed")
        record["status"] = "PASS"
    except BaseException as error:
        record["status"] = "FAIL"
        record["exception_type"] = type(error).__name__
        record["exception"] = str(error)
        raise
    finally:
        record["runtime_records"] = [{"path": str(path), "bytes": path.stat().st_size,
                                      "sha256": sha256(path)} for path in sorted(output.glob("*"))
                                     if path.is_file()]
        write_json(evidence / "boundary-gate.json", record)


def python_candidate(runner, root, work, output, parser, selected_python, checker):
    ordinary_python(selected_python)
    if not re.search(r'^requires-python\s*=\s*">=3\.10"\s*$',
                     (root / "pyproject.toml").read_text(encoding="utf-8"), re.M):
        raise RuntimeError("Declared runtime minimum changed; reselect the floor gate")
    geometry_wheels = output / "geometry-wheels"
    runner.run(sys.executable, "-m", "pip", "wheel", work / "gp", "--no-deps", "--verbose",
               "--wheel-dir", geometry_wheels,
               "--config-settings=build-dir=" + str(output / "geometry-wheel-build"),
               "--config-settings=cmake.define.CMAKE_EXPORT_COMPILE_COMMANDS=ON")
    audit_compilation(checker, output / "geometry-wheel-build", work / "gp", "MSVC", "")
    geometry = one(geometry_wheels, "*.whl")
    if package_metadata(geometry)["Version"] != GEOMETRY_PYTHON_VERSION:
        raise RuntimeError("Python producer is not the unchanged G134 source build")
    runner.run(sys.executable, work / "gp/scripts/check-package.py", geometry)
    config = ["--config-setting=cmake.define.CMAKE_PREFIX_PATH=" +
              ";".join([str(output / "geometry-prefix"), str(parser)]),
              "--config-setting=cmake.define.CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
              "--config-setting=cmake.define.CMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    dist = output / "dist"
    runner.run(sys.executable, "-m", "build", "--sdist", "--outdir", dist, root)
    sdist = one(dist, "*.tar.gz")
    runner.run(sys.executable, root / "scripts/check-package.py", sdist)
    runner.run(sys.executable, "-m", "build", "--wheel", "--outdir", output / "source-wheels",
               "--config-setting=build-dir=" + str(output / "source-wheel-build"), *config, root)
    audit_compilation(checker, output / "source-wheel-build", root, "MSVC", "", True)
    extracted = extract_source(sdist)
    if any((directory / ".git").exists() for directory in (extracted, *extracted.parents)):
        raise RuntimeError("Extracted source must be outside every Git checkout")
    write_json(runner.evidence / "extracted-source.json", {"path": str(extracted),
               "sdist_sha256": sha256(sdist), "git_metadata_present": (extracted / ".git").exists()})
    runner.run(sys.executable, "-m", "build", "--wheel", "--outdir", output / "sdist-wheels",
               "--config-setting=build-dir=" + str(output / "sdist-wheel-build"), *config, extracted)
    audit_compilation(checker, output / "sdist-wheel-build", extracted, "MSVC", "", True)
    wheels = [one(output / name, "*.whl") for name in ("source-wheels", "sdist-wheels")]
    with tarfile.open(sdist) as archive:
        info = next(entry for entry in archive if entry.name.endswith("/PKG-INFO"))
        sdist_version = BytesParser().parsebytes(archive.extractfile(info).read())["Version"]
    if {package_metadata(wheel)["Version"] for wheel in wheels} != {sdist_version}:
        raise RuntimeError("Source, sdist and extracted-source wheel identities differ")
    for wheel in wheels:
        runner.run(sys.executable, root / "scripts/check-package.py", wheel)
    write_json(runner.evidence / "artifacts.json", [
        {"path": str(path), "bytes": path.stat().st_size, "sha256": sha256(path)}
        for path in (sdist, geometry, *wheels)])
    for label, wheel, source in zip(("source", "sdist"), wheels, (root, extracted)):
        environment = output / (label + "-runtime")
        runner.run(sys.executable, "-m", "venv", environment)
        python = environment / "Scripts/python.exe"
        runner.run(python, "-m", "pip", "install", "--no-index", "--no-deps", geometry, wheel)
        outside = Path(tempfile.mkdtemp(prefix="meshvale-stl-" + label + "-runtime-"))
        evidence = runner.evidence / (label + "-runtime")
        evidence.mkdir()
        runtime_runner = type(runner)(outside, evidence, runner.environment)
        write_json(evidence / "selection.json", {"cwd": str(outside), "python": str(python),
                   "wheel": str(wheel), "wheel_sha256": sha256(wheel),
                   "geometry_wheel": str(geometry), "geometry_sha256": sha256(geometry),
                   "test_source": str(source)})
        for order in ("import meshvale_geometry; import meshvale_interchange.stl_files",
                      "import meshvale_interchange.stl_files; import meshvale_geometry"):
            observed = runtime_runner.run(python, "-I", "-B", "-W", "error", "-c", order)
            if observed or (evidence / f"command-{runtime_runner.command_number:03}.stderr").read_text(encoding="utf-8"):
                raise RuntimeError("Installed import emitted a warning or diagnostic")
        runtime_runner.run(python, "-I", "-B", "-W", "error", root / "scripts/stl-files-candidate.py",
                           "runtime-tests", "--python", selected_python, "--source", source,
                           "--wheel", wheel, "--evidence", evidence)
        runtime_runner.run(python, "-m", "pip", "check")
        product_build = output / ("source-wheel-build" if label == "source" else "sdist-wheel-build")
        boundary_gate(runtime_runner, python, source, product_build, selected_python, wheel)
        write_json(evidence / "runtime-gates.json", {"status": "PASS", "ordinary_tests": 34,
                   "unittest_sha256": sha256(evidence / "unittest.json"),
                   "boundary_sha256": sha256(evidence / "boundary-gate.json"),
                   "boundary_cases": 4, "boundary_terminal_replays": 15})


def flatten(suite):
    for entry in suite:
        if isinstance(entry, unittest.TestSuite):
            yield from flatten(entry)
        else:
            yield entry


def runtime_tests(args):
    ordinary_python(args.python)
    from importlib.metadata import distribution, version
    if version("meshvale-geometry") != GEOMETRY_PYTHON_VERSION:
        raise RuntimeError("Wrong installed record producer")
    installed = distribution("meshvale-interchange")
    entries = [entry for entry in installed.entry_points if entry.group == "console_scripts"]
    if [(entry.name, entry.value) for entry in entries] != [
            ("meshvale-stl", "meshvale_interchange.stl_files:main")]:
        raise RuntimeError("Installed console entrypoint differs")
    namespace = {}
    with zipfile.ZipFile(args.wheel) as archive:
        names = [name for name in archive.namelist()
                 if name.startswith("meshvale_interchange/") and not name.endswith("/")]
        if len(names) != 5:
            raise RuntimeError("Expected exactly five installed namespace members")
        for name in names:
            path = Path(installed.locate_file(name))
            data = archive.read(name)
            if path.read_bytes() != data:
                raise RuntimeError("Installed namespace bytes differ from selected wheel: " + name)
            namespace[name] = hashlib.sha256(data).hexdigest()
        if archive.read("meshvale_interchange/_stl_file_presets.json") != (
                args.source / "docs/stl-file-presets.json").read_bytes():
            raise RuntimeError("Installed packaged preset differs from selected source")
    loader = unittest.TestLoader()
    suite = loader.discover(str(args.source / "tests/python"), pattern="test_*.py")
    if loader.errors:
        write_json(args.evidence / "unittest-load-errors.json", loader.errors)
        raise RuntimeError("Actual test-module import failed: " + "\n".join(loader.errors))
    names = [case.id() for case in flatten(suite)]
    counts = dict(Counter(name.split(".", 1)[0] for name in names))
    write_json(args.evidence / "unittest-inventory.json", {"tests": names, "counts": counts})
    if counts != PYTHON_TEST_COUNTS or len(set(names)) != 34:
        raise RuntimeError("Need all eight legacy OBJ and26 STL methods: " + repr(counts))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    record = {"tests": names, "tests_run": result.testsRun, "namespace_sha256": namespace,
              "errors": len(result.errors), "failures": len(result.failures),
              "skipped": len(result.skipped), "expected_failures": len(result.expectedFailures),
              "unexpected_successes": len(result.unexpectedSuccesses),
              "python": sys.version, "interchange_version": installed.version,
              "geometry_version": version("meshvale-geometry")}
    write_json(args.evidence / "unittest.json", record)
    if (result.testsRun != 34 or not result.wasSuccessful() or result.skipped or
            result.expectedFailures or result.unexpectedSuccesses):
        raise RuntimeError("Actual full installed suite failed, skipped or was incomplete")


def qualify(args):
    root = Path(__file__).resolve().parents[1]
    work, output = root / ".local/stl-candidate", root / ".local/stl-candidate/out"
    output.mkdir(parents=True, exist_ok=False)
    evidence = output / "evidence"
    evidence.mkdir()
    environment = os.environ.copy()
    environment.update({"CMAKE_BUILD_PARALLEL_LEVEL": str(WORKERS),
                        "VCPKG_MAX_CONCURRENCY": str(WORKERS),
                        "VCPKG_BINARY_SOURCES": "clear", "VCPKG_DISABLE_METRICS": "1"})
    if args.profile == "unsupported":
        environment.update({"CC": "gcc-13", "CXX": "g++-13"})
    runner_type = run_path(str(root / "scripts/run-native-sanitizers.py"))["Runner"]
    runner = runner_type(root, evidence, environment)
    checker = run_path(str(root / "scripts/check-native-tests.py"))
    result = {"candidate_only": True, "passed": False, "mode": args.mode,
              "materialization_profile": args.profile, "python": args.python,
              "native_expected": (26 if args.profile == "qualified" else 25)
                                 if args.mode == "native" else None,
              "runtime_tests_per_wheel": 34 if args.mode == "python" else None,
              "boundary_cases_per_runtime": 4 if args.mode == "python" else None,
              "boundary_replays_per_runtime": 15 if args.mode == "python" else None,
              "default_off_expected": 23 if args.mode == "native" and
                                      args.profile == "qualified" else None,
              "native_geometry": GEOMETRY_NATIVE_REVISION,
              "python_geometry": GEOMETRY_PYTHON_REVISION,
              "release_qualified": False, "whole_platform_qualified": False}
    try:
        if any(name.startswith("SETUPTOOLS_SCM_PRETEND") for name in environment):
            raise RuntimeError("Version overrides are forbidden in this candidate")
        windows = platform.system() == "Windows"
        if platform.machine().lower() not in ("amd64", "x86_64") or windows != (args.profile == "qualified"):
            raise RuntimeError("Need Windows x64 MSVC or Linux x64 genuine GNU13")
        if not windows and platform.system() != "Linux":
            raise RuntimeError("Only the selected Windows/Linux candidate rows are supported")
        sources = [("interchange", root, None), ("native_geometry", work / "g", GEOMETRY_NATIVE_REVISION),
                   ("vcpkg", work / "v", VCPKG_REVISION)]
        if args.mode == "python":
            sources.append(("python_geometry", work / "gp", GEOMETRY_PYTHON_REVISION))
        identities = {}
        result["source_identities"] = identities
        for label, source, expected in sources:
            actual = runner.run("git", "-C", source, "rev-parse", "HEAD").strip()
            if (expected and actual != expected) or runner.run("git", "-C", source, "status", "--porcelain").strip():
                raise RuntimeError(label + ": exact clean committed source required")
            identities[label] = actual
        compiler, version = ("MSVC", "") if windows else ("GNU", runner.run("g++-13", "-dumpfullversion").strip())
        if not windows and version.split(".")[0] != "13":
            raise RuntimeError("Need actual GNU13, not a capability selector")
        runner.run("cmake", "--version")
        runner.run("ninja", "--version")
        runner.run(sys.executable, "-m", "pip", "freeze")
        write_json(evidence / "host.json", {"platform": platform.platform(), "python": sys.version,
                   "runner_image": environment.get("ImageOS"), "runner_version": environment.get("ImageVersion"),
                   "compiler": compiler, "gnu_version": version})
        common = native_options(args.profile)
        geometry_build, geometry_prefix = output / "geometry-build", output / "geometry-prefix"
        configure(runner, work / "g", geometry_build, common + ["-DBUILD_TESTING=OFF",
                  "-DCMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON"])
        build_install(runner, geometry_build, geometry_prefix)
        audit_compilation(checker, geometry_build, work / "g", compiler, version)
        parser = dependencies(runner, root, work, output, args.profile)
        dependency = ["-DMeshvaleGeometry_DIR=" + str(geometry_prefix / "lib/cmake/MeshvaleGeometry"),
                      "-Dtinyobjloader_DIR=" + str(parser)]
        if args.mode == "native":
            native(runner, root, output, args.profile, common, dependency, checker, compiler, version)
        else:
            if not windows:
                raise RuntimeError("The selected Python candidate is Windows ordinary CPython only")
            python_candidate(runner, root, work, output, parser, args.python, checker)
        result["passed"] = True
    except BaseException as error:
        result["exception_type"] = type(error).__name__
        result["exception"] = str(error)
        raise
    finally:
        try:
            for name in ("CMakeCache.txt", "compile_commands.json", "CMakeCXXCompiler.cmake", "LastTest.log"):
                for path in output.rglob(name):
                    if evidence not in path.parents:
                        target = evidence / "build-records" / path.relative_to(output)
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(path, target)
        except BaseException as error:
            result["passed"] = False
            result["evidence_exception"] = str(error)
            raise
        finally:
            write_json(evidence / "result.json", result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("native", "python", "runtime-tests"))
    parser.add_argument("--profile", choices=("qualified", "unsupported"), default="qualified")
    parser.add_argument("--python", choices=("3.10", "3.13"), default="3.13")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--wheel", type=Path)
    parser.add_argument("--evidence", type=Path)
    arguments = parser.parse_args()
    if arguments.mode == "runtime-tests":
        runtime_tests(arguments)
    else:
        qualify(arguments)
