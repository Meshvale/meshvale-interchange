# SPDX-License-Identifier: Apache-2.0
"""Prepare and verify the bounded CPython 3.13 development wheel candidates."""
import argparse
from email.parser import Parser
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import sysconfig
import zipfile

GEOMETRY_REVISION = "0851e693a2be42deeba0b328b6f1b75205adf48f"
GEOMETRY_VERSION = "0.0.1.dev56+g0851e693a"
VCPKG_REVISION = "cb5a41b03cde4086d554b3f3e2eac39206b16eda"
PACKAGE = Path(__file__).resolve().parents[1]


def run(*args, cwd=None):
    subprocess.run([str(arg) for arg in args], check=True, cwd=cwd)


def checkout(repository, revision, destination):
    if not destination.exists():
        run("git", "clone", "--filter=blob:none", "--no-checkout", repository, destination)
    run("git", "checkout", "--detach", revision, cwd=destination)
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=destination, text=True).strip()
    assert actual == revision, actual
    assert not subprocess.check_output(["git", "status", "--porcelain"], cwd=destination), destination


def dependency_root():
    return Path(os.environ["MESHVALE_DEPS"]).resolve()


def repair(wheel, destination, report):
    windows = sys.platform == "win32"
    tool = [sys.executable, "-m", "delvewheel"] if windows else ["auditwheel"]
    # Both libraries belong to the documented external x64 MSVC v14 runtime.
    exclusion = ["--exclude", "msvcp140.dll;msvcp140_atomic_wait.dll"] if windows else []
    destination.mkdir(parents=True, exist_ok=True)
    with report.open("w", encoding="utf-8") as stream:
        stream.write("Unrepaired SHA256: " + hashlib.sha256(wheel.read_bytes()).hexdigest() + "\n")
        stream.flush()
        subprocess.run([*tool, "show", *exclusion, str(wheel)], stdout=stream, check=True)
        if not windows:
            with tempfile.TemporaryDirectory() as scratch, zipfile.ZipFile(wheel) as archive:
                for name in archive.namelist():
                    if name.endswith(".so"):
                        binary = Path(archive.extract(name, scratch))
                        subprocess.run(["objdump", "-T", str(binary)], stdout=stream, check=True)
        policy = [] if windows else ["--plat", "manylinux_2_28_x86_64"]
        run(*tool, "repair", *exclusion, *policy, "--wheel-dir", destination, wheel)
        for repaired in destination.glob("*.whl"):
            subprocess.run([*tool, "show", *exclusion, str(repaired)], stdout=stream, check=True)


def prepare():
    assert sys.version_info[:2] == (3, 13), sys.version
    assert not sysconfig.get_config_var("Py_GIL_DISABLED"), "ordinary GIL-enabled CPython required"
    root = dependency_root()
    root.mkdir(parents=True, exist_ok=True)
    geometry = root / "geometry"
    checkout("https://github.com/Meshvale/meshvale-geometry.git", GEOMETRY_REVISION, geometry)
    assert (geometry / "NOTICE").read_text().strip() == (PACKAGE / "licenses/meshvale-geometry-notice.txt").read_text().strip()
    vcpkg = root / "vcpkg"
    checkout("https://github.com/microsoft/vcpkg.git", VCPKG_REVISION, vcpkg)
    windows = sys.platform == "win32"
    tool = vcpkg / ("vcpkg.exe" if windows else "vcpkg")
    if not tool.exists():
        if windows:
            run("cmd", "/c", vcpkg / "bootstrap-vcpkg.bat", "-disableMetrics")
        else:
            run("bash", vcpkg / "bootstrap-vcpkg.sh", "-disableMetrics")
    triplet = "x64-windows-static-md" if windows else "x64-linux"
    run(tool, "install", "--vcpkg-root=" + str(vcpkg), "--triplet", triplet, "--x-manifest-root=" + str(PACKAGE),
        "--x-install-root=" + str(root / "dependencies"), "--disable-metrics")
    actual_notice = root / "dependencies" / triplet / "share/tinyobjloader/copyright"
    assert actual_notice.read_text().strip() == (PACKAGE / "licenses/tinyobjloader.txt").read_text().strip()
    run("cmake", "-S", geometry, "-B", root / "native", "-DBUILD_TESTING=OFF", "-DCMAKE_BUILD_TYPE=Release")
    run("cmake", "--build", root / "native", "--config", "Release", "--parallel", "2")
    run("cmake", "--install", root / "native", "--config", "Release", "--prefix", root / "prefix")
    for notice in ["eigen-mpl2.txt", "eigen-apache.txt", "eigen-notices.txt"]:
        installed = root / "prefix/share/MeshvaleGeometry/licenses" / notice
        assert installed.read_text() == (PACKAGE / "licenses" / notice).read_text(), notice
    run(sys.executable, "-m", "pip", "wheel", geometry, "--no-deps", "--verbose",
        "--config-settings=cmake.version===4.3.1", "--wheel-dir", root / "raw")
    wheel, = (root / "raw").glob("meshvale_geometry-*.whl")
    assert wheel.name.startswith("meshvale_geometry-" + GEOMETRY_VERSION + "-cp313-cp313-"), wheel.name
    output = Path(os.environ["MESHVALE_PREVIEW_DIR"])
    output.mkdir(parents=True, exist_ok=True)
    repair(wheel, root / "wheels", output / "geometry-dependencies.txt")
    for path in (root / "wheels").glob("*.whl"):
        run(sys.executable, geometry / "scripts/check-package.py", path)
        target = output / "upstream"
        target.mkdir(exist_ok=True)
        shutil.copy2(path, target / path.name)
    compiler = next((root / "native/CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    (output / "compiler.txt").write_text(compiler.read_text(), encoding="utf-8")
    with (output / "native-tools.txt").open("w", encoding="utf-8") as stream:
        for command in [["cmake", "--version"], [str(tool), "version"], [sys.executable, "-m", "pip", "freeze"]]:
            subprocess.run(command, stdout=stream, check=True)
        if not windows:
            subprocess.run(["ldd", "--version"], stdout=stream, check=True)
            subprocess.run(["rpm", "-q", "zip"], stdout=stream, check=True)
    (output / "native-inputs.json").write_text(json.dumps({"geometry_revision": GEOMETRY_REVISION,
        "geometry_version": GEOMETRY_VERSION, "geometry_native_version": "0.0.0",
        "vcpkg_revision": VCPKG_REVISION, "triplet": triplet,
        "tinyobjloader": "2.0.0rc13", "precision": "double", "linkage": "static-PIC",
        "tinyobjloader_notice_sha256": hashlib.sha256(actual_notice.read_bytes()).hexdigest()}, indent=2) + "\n")


def before_test():
    root = dependency_root()
    run(sys.executable, "-m", "pip", "install", "--no-index", "--find-links", root / "wheels",
        "meshvale-geometry==" + GEOMETRY_VERSION)
    run(sys.executable, "-m", "pip", "install", "--no-deps", root / "geometry/examples/record-consumer",
        "--config-settings=cmake.version===4.3.1",
        "--config-settings=cmake.define.CMAKE_PREFIX_PATH=" + str(root / "prefix"))


def test():
    root = dependency_root()
    geometry = root / "geometry"
    for owner, directory in [(geometry, "tests/python"), (geometry, "tests/reports"), (PACKAGE, "tests/python")]:
        run(sys.executable, "-I", "-m", "unittest", "discover", "-s", owner / directory, "-v")
    run(sys.executable, "-I", PACKAGE / "examples/python/obj_bundle.py")
    run(sys.executable, "-I", geometry / "examples/python/inspect_mesh.py")
    for order in ["geometry-first", "consumer-first"]:
        run(sys.executable, "-I", geometry / "examples/record-consumer/check.py", order)
    for imports in ["import meshvale_geometry; import meshvale_interchange",
                    "import meshvale_interchange; import meshvale_geometry"]:
        run(sys.executable, "-I", "-c", imports + "; assert meshvale_interchange.Mesh is meshvale_geometry.Mesh; "
            "assert meshvale_geometry.__version__ == '" + GEOMETRY_VERSION + "'; "
            "assert meshvale_interchange.read_obj('v 0 0 0\\nv 1 0 0\\nv 0 1 0\\nf 1 2 3\\n').document.mesh.face_count == 1")
    run(sys.executable, "-m", "pip", "check")


def manifest(output):
    artifacts = []
    versions = set()
    for path in sorted(output.rglob("*")):
        if path.suffix != ".whl" and not path.name.endswith(".tar.gz"):
            continue
        item = {"path": path.relative_to(output).as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        if path.suffix == ".whl":
            with zipfile.ZipFile(path) as archive:
                names = archive.namelist()
                info = next(name for name in names if name.endswith(".dist-info/METADATA"))
                item["package_metadata"] = archive.read(info).decode()
                metadata = Parser().parsestr(item["package_metadata"])
                item["wheel_metadata"] = archive.read(next(name for name in names if name.endswith(".dist-info/WHEEL"))).decode()
                item["native_files"] = [name for name in names if name.endswith((".so", ".pyd", ".dll")) or ".so." in name]
                item["license_files"] = [name for name in names if ".dist-info/licenses/" in name and not name.endswith("/")]
                item["license_sha256"] = {}
                for name in item["license_files"]:
                    license_name = Path(name).name
                    reference = PACKAGE / license_name if license_name in {"LICENSE", "NOTICE"} else PACKAGE / "licenses" / license_name
                    if metadata["Name"] == "meshvale-geometry" and license_name == "NOTICE":
                        reference = PACKAGE / "licenses/meshvale-geometry-notice.txt"
                    content = archive.read(name)
                    assert content.decode().strip().replace("\r\n", "\n") == reference.read_text().strip(), name
                    item["license_sha256"][name] = hashlib.sha256(content).hexdigest()
            assert "-cp313-cp313-" in path.name, path.name
            assert "manylinux_2_28_x86_64" in path.name if sys.platform != "win32" else path.name.endswith("-win_amd64.whl"), path.name
            if metadata["Name"] == "meshvale-geometry":
                assert metadata["Version"] == GEOMETRY_VERSION
            else:
                assert metadata["Name"] == "meshvale-interchange"
                assert metadata.get_all("Requires-Dist") == ["meshvale-geometry==" + GEOMETRY_VERSION]
                versions.add(metadata["Version"])
        else:
            with tarfile.open(path) as archive:
                info = next(member for member in archive.getmembers() if member.name.endswith("/PKG-INFO"))
                versions.add(Parser().parsestr(archive.extractfile(info).read().decode())["Version"])
        artifacts.append(item)
    assert len(artifacts) == 5 and len(versions) == 1, (artifacts, versions)
    data = {"candidate_only": True, "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
            "package_version": next(iter(versions)), "geometry_revision": GEOMETRY_REVISION,
            "geometry_version": GEOMETRY_VERSION, "cibuildwheel": "4.2.0", "selected_build": os.environ["CIBW_BUILD"], "artifacts": artifacts}
    (output / "manifest.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["prepare", "before-test", "test", "repair", "manifest"])
    parser.add_argument("--wheel", type=Path)
    parser.add_argument("--destination", type=Path)
    parser.add_argument("--output", type=Path)
    options = parser.parse_args()
    if options.mode == "repair":
        repair(options.wheel, options.destination, Path(os.environ["MESHVALE_PREVIEW_DIR"]) / "interchange-dependencies.txt")
    elif options.mode == "manifest":
        manifest(options.output)
    else:
        {"prepare": prepare, "before-test": before_test, "test": test}[options.mode]()
