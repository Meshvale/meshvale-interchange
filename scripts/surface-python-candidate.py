# SPDX-License-Identifier: Apache-2.0
"""Focused Windows Surface Python development candidates, never a release gate.

The source/sdist ON and genuinely default-OFF wheels use separate build/runtime
directories. Geometry's native producer and Python record producer are distinct.
Only the eight unchanged Surface methods belong to this runner. The existing
STL runner owns its unchanged 34 methods and additional boundary/alias gates.
"""
import argparse
import ast
import base64
import configparser
import csv
from email.parser import BytesParser
import hashlib
import importlib.metadata
import io
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
from runpy import run_path
import shutil
import subprocess
import sys
import sysconfig
import tarfile
import tempfile
import time
from types import SimpleNamespace
import unittest
import zipfile


NATIVE_GEOMETRY = "5dd210b19d05bf651b853b8fdac398c7feceeba9"
PYTHON_GEOMETRY = "48298b24be7c132816fb5580d22d58bde61558d0"
VCPKG = "cb5a41b03cde4086d554b3f3e2eac39206b16eda"
PRODUCTS = {"meshvale-geometry", "meshvale-interchange"}
BOOTSTRAP_TOOLS = {"pip", "setuptools", "wheel"}
SURFACE_METHODS = {
    "test_catalog_association_and_resource_bytes_survive_source_release",
    "test_eight_routes_decode_actual_bytes_and_maps",
    "test_readonly_views_retain_native_owner_after_result_deletion",
    "test_publication_physical_receipt_and_no_overwrite",
    "test_pathlike_resolves_once_and_relative_path_uses_publication_cwd",
    "test_malformed_record_precedes_prestop_and_attempt_is_consumed",
    "test_zero_root_preparation_refusal_has_reachable_result",
    "test_absent_authored_normal_is_actual_native_refusal",
}
SURFACE_TUS = {
    "surface_record_capture.cpp", "surface_python_options.cpp",
    "surface_python_delivery.cpp", "surface_python_pipeline.cpp",
    "surface_python_publication.cpp", "surface_python_views.cpp",
    "surface_files_bindings.cpp",
}
OFF_FEATURES = ("GLTF_TOPOLOGY_WRITE", "GLTF_WORKFLOW_SUPPORT", "GLTF_FILES",
                "OFF_FILES", "PLY_FILES", "FBX", "USD")


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def reference(path):
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def retain(runner, path, phase):
    """Retain a selected actual input, with its original-to-artifact byte binding."""
    original = reference(path)
    target = runner.evidence / "retained-inputs" / original["sha256"] / path.name
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
    copied = reference(target)
    if (copied["bytes"], copied["sha256"]) != (original["bytes"], original["sha256"]):
        raise RuntimeError("Retained actual input bytes changed during capture")
    row = {"phase": phase, "original": original, "retained": copied}
    with (runner.evidence / "retained-inputs.jsonl").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(row) + "\n")
    return row


def scope_manifest(root, exclude_local=False):
    rows = []
    for directory, dirs, files in os.walk(root):
        directory = Path(directory)
        if directory == root and exclude_local and ".local" in dirs:
            dirs.remove(".local")
        if any((directory / name).is_symlink() for name in (*dirs, *files)):
            raise RuntimeError("Unselected symbolic source/SDK input")
        for name in files:
            path = directory / name
            rows.append({"relative": path.relative_to(root).as_posix(), **reference(path)})
    rows.sort(key=lambda row: row["relative"])
    return {"root": str(root), "excluded_top_level": [".local"] if exclude_local else [], "files": rows}


def hold(runner, root, label, exclude_local=False):
    value = scope_manifest(root, exclude_local)
    write(runner.evidence / "holds" / (label + "-before.json"), value)
    return value


def check_hold(runner, before, label, version_file=None, expected_version=None):
    after = scope_manifest(Path(before["root"]), bool(before["excluded_top_level"]))
    old, new = ({row["relative"]: row for row in value["files"]} for value in (before, after))
    excluded = {version_file} if version_file else set()
    if set(old) - excluded != set(new) - excluded or any(old[name] != new[name] for name in set(old) - excluded):
        raise RuntimeError("Complete source/SDK filename or byte hold changed: " + label)
    generated = None
    if version_file:
        path = Path(before["root"]) / version_file
        if not path.is_file() or expected_version is None:
            raise RuntimeError("Missing real backend-generated version metadata")
        tree = ast.parse(path.read_text(encoding="utf-8-sig"))
        values = [node.value.value for node in tree.body if isinstance(node, ast.Assign)
                  and isinstance(node.value, ast.Constant) and isinstance(node.value.value, str)
                  and any(isinstance(target, ast.Name) and target.id in {"__version__", "version"}
                          for target in node.targets)]
        if expected_version not in values:
            raise RuntimeError("Generated metadata differs from actual artifact SCM identity")
        generated = {"path": version_file, "before": old.get(version_file), "after": new.get(version_file),
                     "actual_version": expected_version, "exception": "Only this backend-owned generated version file; all other files including Git metadata remain unchanged"}
        retain(runner, path, label + "-generated-version")
    write(runner.evidence / "holds" / (label + "-after.json"), {
        "status": "PASS", "before": before, "after": after, "generated_version": generated})


def digest(data):
    return "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()


def canonical(name):
    return name.lower().replace("_", "-").replace(".", "-")


class InvocationJob:
    """One non-breakaway Windows job, assigned before any payload is launched.

    The self-member handle is intentionally retained through process teardown;
    closing a kill-on-close job in finally would kill the evidence controller.
    """
    def __init__(self):
        if sys.platform != "win32":
            raise RuntimeError("This candidate owns Windows x64 only")
        import ctypes as c
        from ctypes import wintypes as w

        class BasicLimits(c.Structure):
            _fields_ = [("process_time", c.c_int64), ("job_time", c.c_int64),
                        ("flags", w.DWORD), ("minimum", c.c_size_t),
                        ("maximum", c.c_size_t), ("active_limit", w.DWORD),
                        ("affinity", c.c_size_t), ("priority", w.DWORD),
                        ("scheduling", w.DWORD)]

        class IoCounters(c.Structure):
            _fields_ = [(name, c.c_uint64) for name in
                        ("read_ops", "write_ops", "other_ops", "read_bytes",
                         "write_bytes", "other_bytes")]

        class ExtendedLimits(c.Structure):
            _fields_ = [("basic", BasicLimits), ("io", IoCounters),
                        ("process_memory", c.c_size_t), ("job_memory", c.c_size_t),
                        ("peak_process", c.c_size_t), ("peak_job", c.c_size_t)]

        class Accounting(c.Structure):
            _fields_ = [("user", c.c_int64), ("kernel", c.c_int64),
                        ("period_user", c.c_int64), ("period_kernel", c.c_int64),
                        ("faults", w.DWORD), ("total", w.DWORD),
                        ("active", w.DWORD), ("terminated", w.DWORD)]

        kernel = c.WinDLL("kernel32", use_last_error=True)
        kernel.CreateJobObjectW.argtypes = [c.c_void_p, w.LPCWSTR]
        kernel.CreateJobObjectW.restype = w.HANDLE
        kernel.GetCurrentProcess.restype = w.HANDLE
        kernel.SetInformationJobObject.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
        kernel.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
        kernel.QueryInformationJobObject.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.c_void_p]
        self.c, self.kernel, self.Accounting = c, kernel, Accounting
        self.handle = kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise c.WinError(c.get_last_error())
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; no breakaway.
        if not kernel.SetInformationJobObject(self.handle, 9, c.byref(limits), c.sizeof(limits)):
            raise c.WinError(c.get_last_error())
        if not kernel.AssignProcessToJobObject(self.handle, kernel.GetCurrentProcess()):
            raise c.WinError(c.get_last_error())
        if self.counts()["active"] != 1:
            raise RuntimeError("Invocation job was not isolated before payload")

    def counts(self):
        value = self.Accounting()
        if not self.kernel.QueryInformationJobObject(self.handle, 1, self.c.byref(value),
                                                     self.c.sizeof(value), None):
            raise self.c.WinError(self.c.get_last_error())
        return {name: getattr(value, name) for name in ("active", "total", "terminated")}

    def drain(self, evidence, phase):
        while True:
            counts = self.counts()
            write(evidence / "owned-live.json", {"phase": phase, "controller_pid": os.getpid(),
                                               "counts": counts})
            if counts["active"] == 1:
                return counts
            print("Waiting for invocation-owned descendants: " + str(counts), flush=True)
            time.sleep(2)


class Runner:
    def __init__(self, cwd, evidence, environment, job):
        self.root, self.evidence, self.environment, self.job = cwd, evidence, environment, job
        self.command_number = 0

    def run(self, *command, cwd=None, timeout=3600):
        self.command_number += 1
        stem = self.evidence / f"command-{self.command_number:03}"
        argv = list(map(str, command))
        record = {"argv": argv, "cwd": str(cwd or self.root), "state": "starting",
                  "timeout_seconds": timeout, "pid": None, "returncode": None}
        error, process = None, None
        write(stem.with_suffix(".process.json"), record)
        try:
            with stem.with_suffix(".stdout").open("wb") as out, stem.with_suffix(".stderr").open("wb") as err:
                process = subprocess.Popen(argv, cwd=cwd or self.root, env=self.environment,
                                           stdout=out, stderr=err)
                record.update(pid=process.pid, state="running")
                write(stem.with_suffix(".process.json"), record)
                print(f"Started owned phase {self.command_number}: PID {process.pid}", flush=True)
                process.wait(timeout=timeout)
        except BaseException as caught:
            error = caught
            record.update(state="failed-pending", exception=type(caught).__name__)
            write(stem.with_suffix(".process.json"), record)
            # A spawn failure has no PID or exit code. A started payload must
            # naturally drain; launcher-only termination never proves joins.
            self.job.drain(self.evidence, stem.name + "-failed-pending")
            if process is not None:
                process.wait()
        record.update(returncode=process.returncode if process is not None else None,
                      owned_job=self.job.drain(self.evidence, stem.name),
                      state="failed" if error else "exited")
        write(stem.with_suffix(".process.json"), record)
        if error:
            raise error
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, argv)
        return stem.with_suffix(".stdout").read_text(encoding="utf-8", errors="replace")


def wheel_payload(wheel):
    """Validate the complete original RECORD, rejecting unselected data schemes."""
    with zipfile.ZipFile(wheel) as archive:
        members = [row for row in archive.infolist() if not row.is_dir()]
        names = [row.filename for row in members]
        if len(names) != len(set(names)):
            raise RuntimeError("Duplicate wheel payload member")
        for row in members:
            parts = PurePosixPath(row.filename)
            if (parts.is_absolute() or ".." in parts.parts or "\\" in row.filename
                    or any(part.endswith(".data") for part in parts.parts)
                    or (row.external_attr >> 16) & 0o170000 == 0o120000):
                raise RuntimeError("Unselected wheel member/layout: " + row.filename)
        records = [name for name in names if name.endswith(".dist-info/RECORD")]
        if len(records) != 1:
            raise RuntimeError("Need one wheel RECORD")
        record = records[0]
        rows = list(csv.reader(io.StringIO(archive.read(record).decode("utf-8"))))
        if any(len(row) != 3 for row in rows) or len({row[0] for row in rows}) != len(rows):
            raise RuntimeError("Malformed/duplicate wheel RECORD rows")
        table = {row[0]: row[1:] for row in rows}
        if set(table) != set(names):
            raise RuntimeError("Original wheel RECORD coverage differs from payload")
        payload = {name: archive.read(name) for name in names}
        for name, data in payload.items():
            expected = ["", ""] if name == record else [digest(data), str(len(data))]
            if table[name] != expected:
                raise RuntimeError("Original wheel RECORD digest differs: " + name)
        return payload, record


def installed_payload(wheel, locate, runtime):
    """Expected coverage is derived from the wheel, never surviving installed rows."""
    payload, record = wheel_payload(wheel)
    record_file = locate(record)
    rows = list(csv.reader(io.StringIO(record_file.read_text(encoding="utf-8"))))
    if any(len(row) != 3 for row in rows) or len({row[0] for row in rows}) != len(rows):
        raise RuntimeError("Malformed/duplicate installed RECORD")
    table = {row[0]: row[1:] for row in rows}
    if not set(payload) <= set(table):
        raise RuntimeError("Missing original wheel payload RECORD row")
    for name, expected in payload.items():
        path = locate(name).resolve()
        if runtime not in path.parents or not path.is_file():
            raise RuntimeError("Missing/outside-runtime original payload: " + name)
        if name == record:
            if table[name] != ["", ""]:
                raise RuntimeError("Installed RECORD self row differs")
        elif path.read_bytes() != expected or table[name] != [digest(expected), str(len(expected))]:
            raise RuntimeError("Installed original payload differs: " + name)
    dist_info = str(PurePosixPath(record).parent)
    entrypoints = payload.get(dist_info + "/entry_points.txt", b"").decode("utf-8")
    config = configparser.ConfigParser()
    config.read_string(entrypoints)
    scripts = dict(config["console_scripts"]) if config.has_section("console_scripts") else {}
    script_paths = {runtime / "Scripts" / (name + ".exe") for name in scripts}
    generated = []
    for name, fields in table.items():
        if name in payload:
            continue
        path = locate(name).resolve()
        metadata = name in {dist_info + "/INSTALLER", dist_info + "/REQUESTED",
                            dist_info + "/direct_url.json"}
        if (not metadata and path not in script_paths) or runtime not in path.parents:
            raise RuntimeError("Unselected generated installed member: " + name)
        data = path.read_bytes()
        if fields != [digest(data), str(len(data))]:
            raise RuntimeError("Generated installed RECORD digest differs: " + name)
        generated.append(reference(path))
    if any(not path.is_file() for path in script_paths):
        raise RuntimeError("Missing declared generated console script")
    return {"wheel": reference(wheel), "original_members": len(payload),
            "installed_record": reference(record_file), "console_scripts": scripts,
            "generated_members": generated, "original_payload_coverage": True}


def marker_environment():
    return {"implementation_name": sys.implementation.name,
              "implementation_version": platform.python_version(), "os_name": os.name,
              "platform_machine": platform.machine(), "platform_release": platform.release(),
              "platform_system": platform.system(), "platform_version": platform.version(),
              "python_full_version": platform.python_version(),
              "platform_python_implementation": platform.python_implementation(),
              "python_version": ".".join(map(str, sys.version_info[:2])), "sys_platform": sys.platform}


def runtime_export(args):
    """Stdlib-only target audit; no packaging/test dependency in the product venv."""
    root = Path(sys.prefix).resolve()
    packages = []
    for wheel in args.wheels:
        payload, record = wheel_payload(wheel)
        metadata = BytesParser().parsebytes(payload[record.rsplit("/", 1)[0] + "/METADATA"])
        distribution = importlib.metadata.distribution(metadata["Name"])
        if distribution.version != metadata["Version"]:
            raise RuntimeError("Installed version differs from exact original wheel")
        audit = installed_payload(wheel, lambda name: Path(distribution.locate_file(name)), root)
        audit.update(name=canonical(metadata["Name"]), version=metadata["Version"],
                     requires=metadata.get_all("Requires-Dist", []))
        packages.append(audit)
    installed = {canonical(dist.metadata["Name"]) for dist in importlib.metadata.distributions()}
    if installed - PRODUCTS - BOOTSTRAP_TOOLS or not PRODUCTS <= installed:
        raise RuntimeError("Unexpected installed product/tooling closure: " + repr(installed))
    write(args.output, {"packages": packages, "marker_environment": marker_environment(),
                        "installed_names": sorted(installed), "runtime": str(root)})


def graph(path):
    # build requires packaging; record this audit tool separately from products.
    from packaging.requirements import Requirement
    from packaging.utils import canonicalize_name
    document = json.loads(path.read_text(encoding="utf-8"))
    packages = {row["name"]: row for row in document["packages"]}
    if set(packages) != PRODUCTS:
        raise RuntimeError("Need exactly the selected product pair")
    extras = {name: set() for name in packages}
    visited, active = set(), []
    while True:
        pending = [(name, extra) for name, values in extras.items() for extra in {"", *values}
                   if (name, extra) not in visited]
        if not pending:
            break
        for name, extra in pending:
            visited.add((name, extra))
            for text in packages[name]["requires"]:
                requirement = Requirement(text)
                marker = {**document["marker_environment"], "extra": extra}
                if requirement.marker and not requirement.marker.evaluate(marker):
                    continue
                target = canonicalize_name(requirement.name)
                if (requirement.url or target not in packages
                        or not requirement.specifier.contains(packages[target]["version"], prereleases=True)):
                    raise RuntimeError("Active dependency not satisfied by selected wheels: " + text)
                extras[target].update(requirement.extras)
                active.append({"from": name, "extra": extra, "requirement": text, "to": target})
    return {"active_requirements": active, "active_extras": {k: sorted(v) for k, v in extras.items()},
            "audit_packaging_version": importlib.metadata.version("packaging")}


def copied_tests(args):
    helper = args.source / "surface_files_fixture.py"
    module = args.source / "test_surface_files_api.py"
    if not helper.is_file() or not module.is_file():
        raise RuntimeError("Missing copied original Surface test pair")
    loader = unittest.TestLoader()
    suite = loader.discover(str(args.source), pattern="test_surface_files_api.py")
    def flatten(value):
        for entry in value:
            if isinstance(entry, unittest.TestSuite):
                yield from flatten(entry)
            else:
                yield entry
    names = [test.id() for test in flatten(suite)]
    expected = {"test_surface_files_api.SurfaceFileApiTests." + name for name in SURFACE_METHODS}
    if loader.errors or len(names) != 8 or set(names) != expected:
        raise RuntimeError("Actual copied Surface eight-method inventory differs")
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    write(args.output, {"tests": names, "tests_run": result.testsRun,
                        "failures": len(result.failures), "errors": len(result.errors),
                        "skipped": len(result.skipped), "expected_failures": len(result.expectedFailures),
                        "unexpected_successes": len(result.unexpectedSuccesses),
                        "sources": [reference(module), reference(helper)]})
    if (result.testsRun != 8 or not result.wasSuccessful() or result.skipped
            or result.expectedFailures or result.unexpectedSuccesses):
        raise RuntimeError("Actual copied Surface methods failed/skipped/incomplete")


def audit_report(report, wheels):
    observed = json.loads(report.read_text(encoding="utf-8"))["install"]
    expected = {reference(wheel)["sha256"] for wheel in wheels}
    hashes = {row["download_info"]["archive_info"]["hashes"]["sha256"] for row in observed}
    names = {canonical(row["metadata"]["name"]) for row in observed}
    if len(observed) != 2 or hashes != expected or names != PRODUCTS:
        raise RuntimeError("Normal offline resolver did not select exactly the two audited wheels")


def compile_selection(helper, checker, build, source, enabled):
    helper["audit_compilation"](checker, build, source, "MSVC", "", True)
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    actual = {Path(row["file"]).name for row in entries} & SURFACE_TUS
    if actual != (SURFACE_TUS if enabled else set()):
        raise RuntimeError("Actual compiled Surface binding selection differs: " + repr(actual))
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    for flag in ("SURFACE_OUTPUT", "SURFACE_FILES"):
        expected = "ON" if enabled else "OFF"
        if "MESHVALE_INTERCHANGE_" + flag + ":BOOL=" + expected not in cache:
            raise RuntimeError("Missing actual typed Surface cache selection: " + flag)
    for row in entries:
        if Path(row["file"]).name in SURFACE_TUS and Path(row["file"]).resolve().parent != source / "python":
            raise RuntimeError("Surface TU shadowed by a foreign source")


def compilation_objects(build):
    objects = {}
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    for entry in entries:
        text = entry.get("command") or " ".join(entry["arguments"])
        match = re.search(r'(?:^|\s)/Fo(?:(?:"([^"]+)")|([^\s]+))', text)
        if not match:
            raise RuntimeError("Missing actual MSVC compile object selector")
        path = (Path(entry["directory"]) / (match.group(1) or match.group(2))).resolve()
        if path in objects:
            raise RuntimeError("Duplicate producing object identity")
        objects[path] = entry
    return objects


def producing_archives(runner, build, prefix, label, selected=None):
    """Bind LIST/EXTRACT members to actual producing compile objects and log rows."""
    objects = compilation_objects(build)
    log = build / ".ninja_log"
    records = {line.split("\t")[3] for line in log.read_text(encoding="utf-8").splitlines()
               if not line.startswith("#") and len(line.split("\t")) == 5}
    logged = {(build / name).resolve() for name in records}
    output = runner.evidence / (label + "-archive-members")
    output.mkdir()
    audits = []
    for ordinal, archive in enumerate(sorted(selected if selected is not None else prefix.rglob("*.lib"))):
        matches = [path for path in build.rglob("*.lib") if path.read_bytes() == archive.read_bytes()]
        if len(matches) != 1:
            raise RuntimeError("Installed archive has no unique actual producing archive")
        producer = matches[0].resolve()
        if producer not in logged:
            raise RuntimeError("Producing archive is absent from actual Ninja execution log")
        listing = runner.run("lib.exe", "/NOLOGO", "/LIST", producer)
        members = [line.strip() for line in listing.splitlines() if line.strip().lower().endswith(".obj")]
        if not members or len(members) != len(set(members)):
            raise RuntimeError("Missing/duplicate actual archive members")
        bound = []
        observed = set()
        for index, token in enumerate(members):
            candidate = (build / token).resolve()
            if candidate not in objects or candidate not in logged or not candidate.is_file():
                raise RuntimeError("Archive member lacks its actual compiled object/log: " + token)
            extracted = output / f"a{ordinal:03}-m{index:03}.obj"
            runner.run("lib.exe", "/NOLOGO", "/EXTRACT:" + token,
                       "/OUT:" + str(extracted), producer)
            if extracted.read_bytes() != candidate.read_bytes():
                raise RuntimeError("Archive member differs from producing compile object")
            observed.add(candidate)
            bound.append({"raw_member": token, "object": reference(candidate),
                          "retained_object": retain(runner, candidate, label),
                          "extracted": reference(extracted), "compile": objects[candidate]})
        target = producer.stem + ".dir"
        expected = {path for path in objects if target in path.parts}
        if not expected or observed != expected:
            raise RuntimeError("Archive omitted/added selected producing target objects")
        audits.append({"installed": reference(archive), "producer": reference(producer),
                       "retained_selected": retain(runner, archive, label),
                       "retained_producer": retain(runner, producer, label), "members": bound})
    if not audits:
        raise RuntimeError("No actual selected native archives")
    write(runner.evidence / (label + "-archives.json"), {
        "compile_database": reference(build / "compile_commands.json"),
        "actual_ninja_log": reference(log), "archives": audits})


def dependencies(runner, root, inputs, work):
    # Keep the selected triplet; add producing-command evidence, not new ABI flags.
    vcpkg = inputs / "v"
    runner.run("cmd", "/d", "/c", vcpkg / "bootstrap-vcpkg.bat", "-disableMetrics")
    overlay = runner.evidence / "triplets"
    overlay.mkdir()
    (overlay / "x64-windows-static-md.cmake").write_text(
        f'include("{(vcpkg / "triplets/x64-windows-static-md.cmake").as_posix()}")\n'
        'list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)\n', encoding="utf-8")
    prefix = work / "dependencies"
    runner.run(vcpkg / "vcpkg.exe", "install", "--triplet=x64-windows-static-md",
               "--x-manifest-root=" + str(root), "--x-install-root=" + str(prefix),
               "--overlay-triplets=" + str(overlay), "--disable-metrics")
    selected = prefix / "x64-windows-static-md"
    config = selected / "share/tinyobjloader"
    if (config / "copyright").read_text(encoding="utf-8").strip() != (root / "licenses/tinyobjloader.txt").read_text(encoding="utf-8").strip():
        raise RuntimeError("Selected Tinyobj notice differs")
    builds = [path.parent for path in (vcpkg / "buildtrees/tinyobjloader").rglob("compile_commands.json")
              if path.parent.name.endswith("-rel")]
    if len(builds) != 1:
        raise RuntimeError("Need actual selected Release Tinyobj producing database")
    entries = json.loads((builds[0] / "compile_commands.json").read_text(encoding="utf-8"))
    own = [entry for entry in entries if Path(entry["file"]).name == "tiny_obj_loader.cc"]
    if len(own) != 1:
        raise RuntimeError("Need one actual Tinyobj translation unit")
    text = own[0].get("command") or " ".join(own[0]["arguments"])
    if "TINYOBJLOADER_USE_DOUBLE" not in text or not re.search(r'(?:^|\s)/MD(?:\s|$)', text):
        raise RuntimeError("Actual Tinyobj producer is not double/Release-MD")
    producing_archives(runner, builds[0], selected / "lib", "tinyobj")
    write(runner.evidence / "tinyobj-selected.json", {
        "config": str(config), "prefix": str(selected), "compiler_record": own[0],
        "headers": [reference(path) for path in sorted((selected / "include").rglob("*")) if path.is_file()],
        "exports": [reference(path) for path in sorted(config.rglob("*")) if path.is_file()]})
    for directory in (selected / "include", config):
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                retain(runner, path, "tinyobj-sdk")
    return config


def native_link_evidence(runner, build, source, enabled, label, transcript, geometry_prefix, tiny_prefix):
    commands = runner.run("ninja", "-C", build, "-t", "commands", "_interchange")
    dependencies = runner.run("ninja", "-C", build, "-t", "deps")
    header_bindings = []
    if enabled:
        if any(name not in commands for name in SURFACE_TUS):
            raise RuntimeError("Native target graph lacks enabled Surface helpers")
        if "surface_file_publish.cpp" not in commands or "surface_output.cpp" not in commands:
            raise RuntimeError("Current native Surface-producing graph is absent")
        # Ninja dependency tokens can be relative to its effective -C directory.
        # Preserve tokens and bind actual bytes, never use substring-only paths.
        expected = {source / "include/meshvale/interchange/surface_files.h",
                    source / "include/meshvale/interchange/surface_write.h",
                    geometry_prefix / "include/meshvale/geometry/materialization.h",
                    geometry_prefix / "include/meshvale/geometry/charged_triangulation.h"}
        expected = {path.resolve() for path in expected}
        actual = set()
        for line in dependencies.splitlines():
            if line.startswith("    "):
                token = line.strip()
                candidate = (build / token).resolve()
                if candidate in expected:
                    actual.add(candidate)
                    header_bindings.append({"raw_token": token, "base": str(build),
                                            "actual": reference(candidate),
                                            "retained": retain(runner, candidate, label + "-actual-header")})
        if actual != expected:
            raise RuntimeError("Native dependencies do not bind current Surface headers")
    binaries = list(build.glob("_interchange*.pyd"))
    if len(binaries) != 1:
        raise RuntimeError("Need one actual built extension")
    maps = list(build.glob("surface-extension.map"))
    if len(maps) != 1:
        raise RuntimeError("Missing actual extension link map")
    map_text = maps[0].read_text(encoding="utf-8", errors="replace")
    called = [line for line in map_text.splitlines() if " ?PublishSurfaceBundleNative@interchange@meshvale@@" in line]
    if enabled != bool(called):
        raise RuntimeError("Actual linked publication facade selection differs")
    if enabled and (len(called) != 1 or "surface_file_publish.cpp.obj" not in called[0]):
        raise RuntimeError("Named publication facade lacks the actual producer object")
    links = [line for line in transcript.splitlines() if ".pyd" in line.lower() and "/out:" in line.lower()]
    if len(links) != 1:
        raise RuntimeError("Need actual executed verbose extension link, not only graph commands")
    linked = []
    compiled = compilation_objects(build)
    logged = {(build / line.split("\t")[3]).resolve()
              for line in (build / ".ninja_log").read_text(encoding="utf-8").splitlines()
              if not line.startswith("#") and len(line.split("\t")) == 5}
    expected_direct = {path for path in compiled if "_interchange.dir" in path.parts}
    actual_direct = set()
    archive_membership = {}
    for report in runner.evidence.glob("*-archives.json"):
        for archive in json.loads(report.read_text(encoding="utf-8"))["archives"]:
            for key in ("installed", "producer"):
                archive_membership[Path(archive[key]["path"]).resolve()] = reference(report)
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    system = next((line.split("=", 1)[1].split() for line in cache.splitlines()
                   if line.startswith("CMAKE_CXX_STANDARD_LIBRARIES:STRING=")), [])
    cpython = (Path(sys.base_prefix) / "libs" / ("python" + str(sys.version_info.major) + str(sys.version_info.minor) + ".lib")).resolve()
    for match in re.finditer(r'"([^"\r\n]+\.(?:lib|obj))"|([^\s"]+\.(?:lib|obj))', links[0], re.I):
        token = next(value for value in match.groups() if value)
        if token.lower().startswith("/implib:") or links[0][:match.start()].lower().endswith("/implib:"):
            continue
        if token.lower() in {value.lower() for value in system}:
            linked.append({"token": token, "kind": "cache-declared-system-library"})
            continue
        path = (build / token).resolve(strict=True)
        if path.suffix.lower() == ".obj" and build in path.parents:
            if path not in compiled or path not in logged:
                raise RuntimeError("Actual direct link object lacks its selected compile/execution record")
            actual_direct.add(path)
            kind = "own-direct-object"
        elif path == cpython:
            kind = "selected-cpython-import-library"
        elif path.suffix.lower() == ".lib" and build in path.parents:
            kind = "own-current-build-archive"
            if path not in archive_membership:
                # Private record helpers are not necessarily SDK exports. Audit
                # only this actual link input, using the same member checker.
                audit_label = label + "-link-" + path.stem
                producing_archives(runner, build, build, audit_label, selected=[path])
                archive_membership[path] = reference(runner.evidence / (audit_label + "-archives.json"))
        elif path.suffix.lower() == ".lib" and geometry_prefix in path.parents:
            kind = "selected-native-geometry-archive"
        elif path.suffix.lower() == ".lib" and tiny_prefix in path.parents:
            kind = "selected-tinyobj-archive"
        else:
            raise RuntimeError("Unselected actual extension link input: " + token)
        if path.suffix.lower() == ".lib" and path != cpython and path not in archive_membership:
            raise RuntimeError("Actual selected native archive lacks its producing membership evidence")
        linked.append({"token": token, "kind": kind, "actual": reference(path),
                       "retained": retain(runner, path, label + "-actual-link"),
                       "compile": compiled[path] if path in compiled else None,
                       "archive_membership": archive_membership.get(path)})
    kinds = {row["kind"] for row in linked}
    if not {"selected-native-geometry-archive", "selected-cpython-import-library", "own-direct-object"} <= kinds:
        raise RuntimeError("Executed link lacks selected native/CPython/binding inputs")
    if not expected_direct or not expected_direct <= actual_direct:
        raise RuntimeError("Executed link omitted selected extension binding/controller objects")
    for directory in (source / "include", source / "python", source / "src",
                      geometry_prefix / "include", tiny_prefix / "include"):
        for path in sorted(directory.rglob("*.h")):
            retain(runner, path, label + "-selected-headers")
    retain(runner, Path(sysconfig.get_path("include")) / "Python.h", label + "-python-header")
    write(runner.evidence / (label + "-native.json"), {
        "build": str(build), "source": str(source), "enabled": enabled,
        "current_target_commands_sha256": hashlib.sha256(commands.encode()).hexdigest(),
        "current_deps_sha256": hashlib.sha256(dependencies.encode()).hexdigest(),
        "extension": reference(binaries[0]),
        "link_map": reference(maps[0]), "called_native_facade_rows": called,
        "actual_executed_link": links[0], "actual_inputs": linked,
        "actual_selected_header_dependencies": header_bindings,
        "complete_extension_direct_objects": [reference(path) for path in sorted(expected_direct)],
        "archives": [reference(path) for path in sorted(build.rglob("*.lib"))],
        "note": "Ninja tool outputs are current graph/dependency evidence, not additional execution credit; retained verbose build logs provide actual execution."})
    return binaries[0]


def runtime(runner, helper, work, source, geometry, interchange, label, enabled, python_version):
    environment, evidence = work / (label + "-runtime"), runner.evidence / label
    evidence.mkdir()
    runner.run(sys.executable, "-I", "-B", "-m", "venv", environment)
    python = environment / "Scripts/python.exe"
    house = work / (label + "-wheelhouse")
    house.mkdir()
    wheels = []
    for wheel in (geometry, interchange):
        target = house / wheel.name
        shutil.copyfile(wheel, target)
        wheels.append(target)
    script = Path(__file__).resolve()
    markers = evidence / "target-markers.json"
    runner.run(python, "-I", "-B", script, "runtime-marker", "--output", markers)
    packages = []
    for wheel in wheels:
        metadata = helper["package_metadata"](wheel)
        packages.append({"name": canonical(metadata["Name"]), "version": metadata["Version"],
                         "requires": metadata.get_all("Requires-Dist", [])})
    preinstall = evidence / "preinstall-metadata.json"
    write(preinstall, {"packages": packages, "marker_environment": json.loads(markers.read_text(encoding="utf-8"))})
    write(evidence / "preinstall-active-graph.json", graph(preinstall))
    report = evidence / "pip-install.json"
    runner.run(python, "-I", "-B", "-m", "pip", "install", "--no-index", "--no-compile",
               "--find-links", house, "--report", report, "meshvale-interchange==" + helper["package_metadata"](interchange)["Version"])
    audit_report(report, wheels)
    previous_audit = previous_graph = None
    for phase in ("before", "after"):
        export = evidence / (phase + "-installed.json")
        runner.run(python, "-I", "-B", script, "runtime-export", "--wheels", *wheels, "--output", export)
        actual_audit = json.loads(export.read_text(encoding="utf-8"))
        actual_graph = graph(export)
        write(evidence / (phase + "-active-graph.json"), actual_graph)
        if phase == "after":
            unchanged_runtime(previous_audit, actual_audit, previous_graph, actual_graph)
            break
        previous_audit, previous_graph = actual_audit, actual_graph
        for order in ("import meshvale_geometry; import meshvale_interchange.surface_files",
                      "import meshvale_interchange.surface_files; import meshvale_geometry"):
            runner.run(python, "-I", "-B", "-W", "error", "-c", order, cwd=work)
        if enabled:
            copied = work / (label + "-copied-tests")
            copied.mkdir()
            copies = []
            for name in ("test_surface_files_api.py", "surface_files_fixture.py"):
                original = source / "tests/python" / name
                target = copied / name
                shutil.copyfile(original, target)
                if target.read_bytes() != original.read_bytes():
                    raise RuntimeError("Copied Surface source differs")
                copies.append({"original": reference(original), "copy": reference(target)})
            write(evidence / "copied-sources.json", copies)
            runner.run(python, "-I", "-B", "-W", "error", script, "copied-tests",
                       "--source", copied, "--output", evidence / "surface-eight.json", cwd=work)
        else:
            code = ("import meshvale_interchange._interchange as n\n"
                    "from meshvale_interchange import surface_files as s\n"
                    "assert not hasattr(n, '_surface_prepare')\n"
                    "assert not hasattr(n, 'SurfaceOutput')\n"
                    "try:\n s._surface_native()\n"
                    "except RuntimeError as error:\n"
                    " assert str(error) == 'the Surface Python interface is not enabled in this build'\n"
                    "else:\n raise AssertionError('disabled interface admitted')\n")
            runner.run(python, "-I", "-B", "-W", "error", "-c", code, cwd=work)
            runner.run(python, "-I", "-B", "-W", "error", source / "scripts/stl-files-candidate.py",
                       "runtime-tests", "--python", python_version, "--source", source,
                       "--wheel", interchange, "--evidence", evidence, cwd=work)
        runner.run(python, "-I", "-B", "-m", "pip", "check")
    write(evidence / "result.json", {"status": "PASS", "enabled": enabled,
                                    "wheels": [reference(wheel) for wheel in wheels],
                                    "methods": 8 if enabled else 34})


def unchanged_runtime(before, after, before_graph, after_graph):
    if after != before or after_graph != before_graph:
        raise RuntimeError("Installed bytes/RECORD/generated scripts or active graph changed during tests")


def qualify(args):
    root = Path(__file__).resolve().parents[1]
    helper = run_path(str(root / "scripts/stl-files-candidate.py"))
    helper["ordinary_python"](args.python)
    if sysconfig.get_config_var("Py_DEBUG"):
        raise RuntimeError("Debug Python is outside this ordinary candidate")
    if platform.system() != "Windows" or platform.machine().lower() not in {"amd64", "x86_64"}:
        raise RuntimeError("Only the selected Windows x64 candidate is owned here")
    work = Path(tempfile.mkdtemp(prefix="meshvale-surface-candidate-")).resolve()
    evidence = root / ".local/surface-candidate-evidence"
    evidence.mkdir(parents=True, exist_ok=False)
    environment = os.environ.copy()
    if any(name.startswith("SETUPTOOLS_SCM_PRETEND") for name in environment):
        raise RuntimeError("Version overrides are forbidden")
    for name in list(environment):
        if name.startswith("PIP_"):
            del environment[name]
    environment.update(PIP_CONFIG_FILE=os.devnull, CMAKE_GENERATOR="Ninja",
                       GIT_OPTIONAL_LOCKS="0",
                       CMAKE_BUILD_PARALLEL_LEVEL="2", VCPKG_MAX_CONCURRENCY="2",
                       VCPKG_BINARY_SOURCES="clear", VCPKG_DISABLE_METRICS="1")
    forbidden = ("CMAKE_ARGS", "SKBUILD_CMAKE_ARGS", "CMAKE_PREFIX_PATH", "CMAKE_TOOLCHAIN_FILE", "CL", "_CL_")
    if any(environment.get(name) for name in forbidden):
        raise RuntimeError("Ambient build selector conflicts with exact explicit inputs")
    job = InvocationJob()
    # Keep the job handle strongly reachable until process teardown.
    global _INVOCATION_JOB
    _INVOCATION_JOB = job
    runner = Runner(root, evidence, environment, job)
    result = {"status": "PENDING", "work": str(work), "release_qualified": False,
              "whole_platform_qualified": False, "python": args.python,
              "scope": "Source/SDIST enabled Surface8 and actual default-OFF STL34, Windows ordinary only"}
    observed_sources, observed_sdks = [], []
    try:
        inputs = root / ".local/surface-candidate-inputs"
        identities = {}
        for label, source, expected in (("interchange", root, None),
                ("geometry_native", inputs / "g", NATIVE_GEOMETRY),
                ("geometry_python", inputs / "gp", PYTHON_GEOMETRY),
                ("vcpkg", inputs / "v", VCPKG)):
            if work == source or source.resolve() in work.parents or work in source.resolve().parents:
                raise RuntimeError("Work/source input directories overlap")
            actual = runner.run("git", "-C", source, "rev-parse", "HEAD").strip()
            if (expected and actual != expected) or runner.run("git", "-C", source, "status", "--porcelain").strip():
                raise RuntimeError("Exact clean selected source required: " + label)
            identities[label] = actual
        result["source_identities"] = identities
        source_before = hold(runner, root, "interchange-source", exclude_local=True)
        native_geometry_before = hold(runner, inputs / "g", "geometry-native-source", exclude_local=True)
        python_geometry_before = hold(runner, inputs / "gp", "geometry-python-source", exclude_local=True)
        observed_sources.extend([source_before, native_geometry_before, python_geometry_before])
        write(evidence / "admitted.json", {"identities": identities, "work": str(work),
                    "controller_pid": os.getpid(), "job_handle": job.handle,
                    "environment_selectors": {key: environment[key] for key in environment if key.startswith(("CMAKE_", "VCPKG_", "PIP_"))}})
        checker = run_path(str(root / "scripts/check-native-tests.py"))
        common = helper["native_options"]("qualified") + ["-DCMAKE_TRY_COMPILE_CONFIGURATION=Release"]
        geometry_build, geometry_prefix = work / "geometry-native-build", work / "geometry-native-prefix"
        helper["configure"](runner, inputs / "g", geometry_build, common + ["-DBUILD_TESTING=OFF", "-DCMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON"])
        helper["build_install"](runner, geometry_build, geometry_prefix)
        helper["audit_compilation"](checker, geometry_build, inputs / "g", "MSVC", "")
        check_hold(runner, native_geometry_before, "geometry-native-source")
        geometry_sdk_before = hold(runner, geometry_prefix, "geometry-native-sdk")
        observed_sdks.append(geometry_sdk_before)
        for path in sorted(geometry_prefix.rglob("*")):
            if path.is_file():
                retain(runner, path, "geometry-native-sdk")
        producing_archives(runner, geometry_build, geometry_prefix, "geometry-native")
        parser = dependencies(runner, root, inputs, work)
        tiny_before = hold(runner, parser.parents[1], "tinyobj-sdk")
        observed_sdks.append(tiny_before)
        write(evidence / "native-geometry-sdk.json", helper["sdk_manifest"](geometry_prefix))
        geometry_wheels = work / "geometry-wheels"
        runner.run(sys.executable, "-I", "-B", "-m", "pip", "wheel", inputs / "gp", "--no-deps", "--verbose",
                   "--wheel-dir", geometry_wheels, "--config-settings=build-dir=" + str(work / "geometry-python-build"),
                   "--config-settings=build.verbose=true",
                   "--config-settings=cmake.define.CMAKE_EXPORT_COMPILE_COMMANDS=ON")
        helper["audit_compilation"](checker, work / "geometry-python-build", inputs / "gp", "MSVC", "")
        geometry = helper["one"](geometry_wheels, "*.whl")
        if helper["package_metadata"](geometry)["Version"] != helper["GEOMETRY_PYTHON_VERSION"]:
            raise RuntimeError("Wrong actual Python Geometry source identity")
        check_hold(runner, python_geometry_before, "geometry-python-source",
                   "python/meshvale_geometry/_version.py", helper["GEOMETRY_PYTHON_VERSION"])
        runner.run(sys.executable, "-I", "-B", inputs / "gp/scripts/check-package.py", geometry)
        geometry_payload, _ = wheel_payload(geometry)
        geometry_extensions = [data for name, data in geometry_payload.items() if name.endswith(".pyd")]
        built_geometry = list((work / "geometry-python-build").glob("*.pyd"))
        if (len(geometry_extensions) != 1 or len(built_geometry) != 1
                or geometry_extensions[0] != built_geometry[0].read_bytes()):
            raise RuntimeError("Actual Python Geometry build extension differs from wheel")
        dist = work / "dist"
        runner.run(sys.executable, "-I", "-B", "-m", "build", "--sdist", "--outdir", dist, root)
        sdist = helper["one"](dist, "*.tar.gz")
        runner.run(sys.executable, "-I", "-B", root / "scripts/check-package.py", sdist)
        extracted = helper["extract_source"](sdist).resolve()
        if any((path / ".git").exists() for path in (extracted, *extracted.parents)):
            raise RuntimeError("SDIST extraction is not Git-free")
        with tarfile.open(sdist) as archive:
            pkg = next(row for row in archive if row.name.endswith("/PKG-INFO"))
            version = BytesParser().parsebytes(archive.extractfile(pkg).read())["Version"]
        sdist_before = hold(runner, extracted, "interchange-sdist")
        observed_sources.append(sdist_before)
        source_pairs = []
        required = ["tests/python/test_surface_files_api.py", "tests/python/surface_files_fixture.py",
                    "scripts/surface-python-candidate.py", "scripts/stl-files-candidate.py",
                    "CMakeLists.txt", "python/bindings.cpp", "python/meshvale_interchange/surface_files.py",
                    "python/meshvale_interchange/_surface_file_presets.json", "docs/surface-python-presets.json",
                    "include/meshvale/interchange/surface_write.h", "include/meshvale/interchange/surface_files.h"]
        required += ["python/" + Path(name).stem + suffix for name in sorted(SURFACE_TUS) for suffix in (".cpp", ".h")]
        required += ["src/" + stem + suffix for stem in ("gltf_allocation", "gltf_operation_workers") for suffix in (".cpp", ".h")]
        for name in required:
            original, packaged = root / name, extracted / name
            if original.read_bytes().replace(b"\r\n", b"\n") != packaged.read_bytes().replace(b"\r\n", b"\n"):
                raise RuntimeError("SDIST changed original public test/helper code")
            source_pairs.append({"source": reference(original), "sdist": reference(packaged),
                                 "source_retained": retain(runner, original, "required-source"),
                                 "sdist_retained": retain(runner, packaged, "required-sdist"),
                                 "comparison": "raw-exact" if original.read_bytes() == packaged.read_bytes() else "ordinary Git text canonical code equality; copies bind each origin's actual raw bytes"})
        write(evidence / "source-sdist-code.json", source_pairs)
        artifacts = [sdist, geometry]
        for origin, source in (("source", root), ("sdist", extracted)):
            for enabled in (True, False):
                label = origin + ("-on" if enabled else "-default-off")
                build, wheels = work / (label + "-build"), work / (label + "-wheels")
                config = ["--config-setting=build-dir=" + str(build),
                    "--config-setting=build.verbose=true",
                    "--config-setting=cmake.define.CMAKE_PREFIX_PATH=" + str(geometry_prefix) + ";" + str(parser),
                    "--config-setting=cmake.define.CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
                    "--config-setting=cmake.define.CMAKE_TRY_COMPILE_CONFIGURATION=Release",
                    "--config-setting=cmake.define.CMAKE_EXPORT_COMPILE_COMMANDS=ON",
                    "--config-setting=cmake.define.CMAKE_MODULE_LINKER_FLAGS=/MAP:" + str(build / "surface-extension.map")]
                config += ["--config-setting=cmake.define.MESHVALE_INTERCHANGE_" + flag + "=OFF" for flag in OFF_FEATURES]
                if enabled:
                    config += ["--config-setting=cmake.define.MESHVALE_INTERCHANGE_" + flag + "=ON" for flag in ("SURFACE_OUTPUT", "SURFACE_FILES")]
                transcript = runner.run(sys.executable, "-I", "-B", "-m", "build", "--wheel", "--verbose", "--outdir", wheels, *config, source)
                wheel = helper["one"](wheels, "*.whl")
                if helper["package_metadata"](wheel)["Version"] != version:
                    raise RuntimeError("Git/SDIST/native wheel SCM identities differ")
                runner.run(sys.executable, "-I", "-B", source / "scripts/check-package.py", wheel)
                compile_selection(helper, checker, build, source, enabled)
                sdk = work / (label + "-native-sdk")
                runner.run("cmake", "--install", build, "--config", "Release", "--component", "Unspecified", "--prefix", sdk)
                sdk_before = hold(runner, sdk, label + "-sdk")
                observed_sdks.append(sdk_before)
                producing_archives(runner, build, sdk, label)
                nanobind = list(build.rglob("nanobind-static.lib"))
                if len(nanobind) != 1:
                    raise RuntimeError("Need exact selected nanobind static archive")
                producing_archives(runner, build, build, label + "-nanobind", selected=nanobind)
                extension = native_link_evidence(runner, build, source, enabled, label,
                                                 transcript, geometry_prefix, parser.parents[1])
                payload, _ = wheel_payload(wheel)
                selected = [data for name, data in payload.items() if name.startswith("meshvale_interchange/_interchange.") and name.endswith(".pyd")]
                if len(selected) != 1 or selected[0] != extension.read_bytes():
                    raise RuntimeError("Actual built extension differs from wheel payload")
                artifacts.append(wheel)
                retained = evidence / "artifacts" / label / wheel.name
                retained.parent.mkdir(parents=True)
                shutil.copyfile(wheel, retained)
                runtime(runner, helper, work, source, geometry, wheel, label, enabled, args.python)
                check_hold(runner, sdk_before, label + "-sdk")
        for artifact in (sdist, geometry):
            retained = evidence / "artifacts" / artifact.name
            retained.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(artifact, retained)
        write(evidence / "artifacts.json", [reference(path) for path in artifacts])
        if runner.run("git", "-C", root, "status", "--porcelain").strip():
            raise RuntimeError("Source tracked content changed during qualification")
        check_hold(runner, source_before, "interchange-source",
                   "python/meshvale_interchange/_version.py", version)
        check_hold(runner, sdist_before, "interchange-sdist",
                   "python/meshvale_interchange/_version.py", version)
        check_hold(runner, native_geometry_before, "geometry-native-source-final")
        check_hold(runner, python_geometry_before, "geometry-python-source-final",
                   "python/meshvale_geometry/_version.py", helper["GEOMETRY_PYTHON_VERSION"])
        for index, sdk_before in enumerate(observed_sdks):
            check_hold(runner, sdk_before, "sdk-final-" + str(index))
        result["status"] = "PASS"
    except BaseException as error:
        result.update(status="FAIL", exception_type=type(error).__name__, exception=str(error))
        raise
    finally:
        result["owned_job_at_boundary"] = job.drain(evidence, "final-boundary")
        result["all_owned_children_joined"] = True
        try:
            for index, before in enumerate(observed_sources + observed_sdks):
                write(evidence / "holds" / ("terminal-observed-" + str(index) + ".json"),
                      scope_manifest(Path(before["root"]), bool(before["excluded_top_level"])))
            for directory in ("geometry-wheels", "dist", "source-on-wheels", "source-default-off-wheels",
                              "sdist-on-wheels", "sdist-default-off-wheels"):
                for path in (work / directory).glob("*"):
                    if path.is_file():
                        target = evidence / "retained-candidates" / directory / path.name
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(path, target)
            for name in ("compile_commands.json", "CMakeCache.txt", ".ninja_log", "surface-extension.map"):
                for path in work.rglob(name):
                    target = evidence / "build-records" / path.relative_to(work)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, target)
            write(evidence / "terminal-work-manifest.json", [reference(path) for path in sorted(work.rglob("*")) if path.is_file()])
        except BaseException as error:
            result.update(status="FAIL", evidence_exception=type(error).__name__ + ": " + str(error))
            raise
        finally:
            write(evidence / "result.json", result)


def self_check():
    """Data-only controls: no product imports, install, compiler or subprocess."""
    root = Path(__file__).resolve().parents[1]
    expected = {"test_obj": 8, "test_stl_files_admission": 10,
                "test_stl_files_api": 9, "test_stl_files_commands": 7}
    counts = {}
    for name in (*expected, "test_surface_files_api"):
        tree = ast.parse((root / "tests/python" / (name + ".py")).read_text(encoding="utf-8-sig"))
        methods = [item.name for cls in tree.body if isinstance(cls, ast.ClassDef)
                   for item in cls.body if isinstance(item, ast.FunctionDef) and item.name.startswith("test_")]
        counts[name] = len(methods)
        if name == "test_surface_files_api" and set(methods) != SURFACE_METHODS:
            raise AssertionError("Unchanged Surface owner differs")
    assert {name: counts[name] for name in expected} == expected
    assert sum(expected.values()) == 34 and counts["test_surface_files_api"] == 8
    own = ast.parse(Path(__file__).read_text(encoding="utf-8-sig"))
    for node in ast.walk(own):
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "code" for t in node.targets):
            ast.parse(ast.literal_eval(node.value))
    with tempfile.TemporaryDirectory(prefix="surface-record-control-") as scratch:
        temp = Path(scratch)
        scope = temp / "scope"
        scope.mkdir()
        (scope / "source.h").write_bytes(b"original source")
        fake = SimpleNamespace(evidence=temp / "proof")
        fake.evidence.mkdir()
        before = hold(fake, scope, "data-source")
        version = scope / "_version.py"
        version.write_text("__version__ = version = '1.0'\n", encoding="utf-8")
        check_hold(fake, before, "data-version", "_version.py", "1.0")
        added = scope / "unexpected-ignored-file"
        added.write_bytes(b"not in input snapshot")
        try:
            check_hold(fake, before, "data-added", "_version.py", "1.0")
        except RuntimeError as error:
            assert "filename or byte hold changed" in str(error)
        else:
            raise AssertionError("Added source/SDK filename escaped complete hold")
        added.unlink()
        try:
            check_hold(fake, before, "data-wrong-version", "_version.py", "2.0")
        except RuntimeError as error:
            assert "actual artifact SCM identity" in str(error)
        else:
            raise AssertionError("Wrong generated version escaped hold")
        unchanged_runtime({"generated": "original"}, {"generated": "original"}, {}, {})
        try:
            unchanged_runtime({"generated": "original"}, {"generated": "changed-valid-snapshot"}, {}, {})
        except RuntimeError:
            pass
        else:
            raise AssertionError("Two valid but different installed snapshots escaped equality")
        # Run the actual journal method in an isolated AST namespace with a
        # controlled spawn refusal. No global subprocess patch or real process.
        runner_class = next(node for node in own.body if isinstance(node, ast.ClassDef) and node.name == "Runner")
        run_method = next(node for node in runner_class.body if isinstance(node, ast.FunctionDef) and node.name == "run")
        def spawn_refusal(*args, **kwargs):
            raise OSError("controlled spawn refusal")
        context = {"write": write, "subprocess": SimpleNamespace(Popen=spawn_refusal)}
        exec(compile(ast.Module(body=[run_method], type_ignores=[]), "actual-runner-spawn-control", "exec"), context)
        journal = SimpleNamespace(command_number=0, evidence=fake.evidence, root=temp,
                                  environment={}, job=SimpleNamespace(drain=lambda *args: {"active": 1, "total": 1, "terminated": 0}))
        try:
            context["run"](journal, "controlled-no-process")
        except OSError:
            pass
        else:
            raise AssertionError("Spawn refusal was swallowed")
        record = json.loads((fake.evidence / "command-001.process.json").read_text(encoding="utf-8"))
        assert record["pid"] is None and record["returncode"] is None and record["state"] == "failed"
        assert record["argv"] == ["controlled-no-process"] and record["owned_job"]["active"] == 1
        fixture = temp / "discovery"
        fixture.mkdir()
        for name, count in expected.items():
            body = "import unittest\nclass Original(unittest.TestCase):\n" + "".join(
                f" def test_{index}(self): pass\n" for index in range(count))
            (fixture / (name + ".py")).write_text(body, encoding="utf-8")
        (fixture / "test_surface_files_api.py").write_text(
            "raise AssertionError('Surface must not enter the standalone STL gate')\n", encoding="utf-8")
        stl = ast.parse((root / "scripts/stl-files-candidate.py").read_text(encoding="utf-8-sig"))
        method = next(node for node in stl.body if isinstance(node, ast.FunctionDef) and node.name == "runtime_tests")
        discovery = next(node for node in method.body if isinstance(node, ast.For)
                         and isinstance(node.iter, ast.Name) and node.iter.id == "PYTHON_TEST_COUNTS")
        context = {"loader": unittest.TestLoader(), "suite": unittest.TestSuite(),
                   "args": SimpleNamespace(source=temp), "PYTHON_TEST_COUNTS": expected}
        # Execute the actual changed discovery loop against controlled modules.
        (temp / "tests").mkdir()
        shutil.move(str(fixture), str(temp / "tests/python"))
        exec(compile(ast.Module(body=[discovery], type_ignores=[]), "actual-stl-discovery", "exec"), context)
        assert not context["loader"].errors and context["suite"].countTestCases() == 34
        runtime = temp / "runtime"
        site = runtime / "Lib/site-packages"
        site.mkdir(parents=True)
        member, record = "fixture/payload.bin", "fixture-1.dist-info/RECORD"
        data = b"original-literal-payload"
        text = member + "," + digest(data) + "," + str(len(data)) + "\n" + record + ",,\n"
        wheel = temp / "fixture.whl"
        with zipfile.ZipFile(wheel, "w") as archive:
            archive.writestr(member, data)
            archive.writestr(record, text)
        for name, value in ((member, data), (record, text.encode())):
            path = site / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
        locate = lambda name: site / name
        installed_payload(wheel, locate, runtime)
        (site / member).unlink()
        (site / record).write_text(record + ",,\n", encoding="utf-8")
        try:
            installed_payload(wheel, locate, runtime)
        except RuntimeError as error:
            assert "Missing original wheel payload RECORD row" in str(error)
        else:
            raise AssertionError("Joint payload+RECORD deletion escaped coverage")
    print("Data-only controls passed: unchanged34/8, actual discovery, joint payload/row deletion, complete source hold, generated version, runtime equality, nullable spawn refusal")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    candidate = sub.add_parser("qualify")
    candidate.add_argument("--python", choices=("3.10", "3.13"), required=True)
    export = sub.add_parser("runtime-export")
    export.add_argument("--wheels", type=Path, nargs=2, required=True)
    export.add_argument("--output", type=Path, required=True)
    marker = sub.add_parser("runtime-marker")
    marker.add_argument("--output", type=Path, required=True)
    tests = sub.add_parser("copied-tests")
    tests.add_argument("--source", type=Path, required=True)
    tests.add_argument("--output", type=Path, required=True)
    sub.add_parser("self-check")
    arguments = parser.parse_args()
    {"qualify": qualify, "runtime-export": runtime_export,
     "runtime-marker": lambda args: write(args.output, marker_environment()),
     "copied-tests": copied_tests, "self-check": lambda _: self_check()}[arguments.mode](arguments)
