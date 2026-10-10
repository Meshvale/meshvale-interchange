# SPDX-License-Identifier: Apache-2.0
"""Inspect Interchange wheel/source archives against the product publication list."""
import argparse
from pathlib import Path, PurePosixPath
import tarfile
import zipfile


def inspect(path):
    if path.suffix == ".whl":
        with zipfile.ZipFile(path) as archive:
            members = archive.infolist()
            names = [member.filename for member in members if not member.is_dir()]
        for member in members:
            name = member.filename
            parts = PurePosixPath(name).parts
            assert not PurePosixPath(name).is_absolute() and ".." not in parts, name
            assert parts[0] == "meshvale_interchange" or parts[0].endswith(".dist-info"), name
            if member.is_dir():
                # Repair tools emit structural directories within existing namespaces.
                assert len(parts) == 1 or parts[0].endswith(".dist-info"), name
                continue
            if parts[0] == "meshvale_interchange":
                assert len(parts) == 2 and (parts[1] in {"__init__.py","_version.py"} or
                    (parts[1].startswith("_interchange.") and parts[1].endswith((".pyd",".so")))), name
            else:
                assert parts[1:] in [("METADATA",), ("WHEEL",), ("RECORD",),
                    ("licenses", "LICENSE"), ("licenses", "NOTICE"),
                    ("licenses", "licenses", "nanobind.txt"),
                    ("licenses", "licenses", "robin-map.txt"),
                    ("licenses", "licenses", "meshvale-geometry-notice.txt"),
                    ("licenses", "licenses", "tinyobjloader.txt")], name
        for license in ["LICENSE","NOTICE","nanobind.txt","robin-map.txt","meshvale-geometry-notice.txt","tinyobjloader.txt"]:
            assert any(name.endswith("/"+license) and ".dist-info/licenses/" in name for name in names), license
    else:
        with tarfile.open(path,"r:gz") as archive:
            members = archive.getmembers()
        names = []
        roots = {".clang-format","CMakeLists.txt","pyproject.toml","vcpkg.json","README.md","AGENTS.md","ENVIRONMENT.md",
                 "environment.example.json","LICENSE","NOTICE","THIRD_PARTY.md","CHANGELOG.md","PKG-INFO"}
        directories = {"cmake","include","src","python","docs","examples","tests","licenses","scripts"}
        for member in members:
            assert member.isfile(),member.name
            parts = PurePosixPath(member.name).parts
            assert not PurePosixPath(member.name).is_absolute() and ".." not in parts and len(parts)>=2,member.name
            name = "/".join(parts[1:]); names.append(name)
            assert name in roots or parts[1] in directories,name
            assert not any(part in {".local",".scratch","__pycache__","references","build",".github"} for part in parts),name
            assert not name.endswith((".pyc",".pyd",".so",".obj",".log")),name
        for required in [".clang-format","python/meshvale_interchange/_version.py","python/bindings.cpp","tests/python/test_obj.py","src/obj_read.cpp",
                         "include/meshvale/interchange/obj.h","include/meshvale/interchange/obj.hpp",
                         "include/meshvale/interchange/obj_files.h","include/meshvale/interchange/obj_files.hpp",
                         "src/obj_file_detail.h","tests/headers/obj_h.cpp","tests/headers/obj_hpp.cpp",
                         "tests/headers/obj_files_h.cpp","tests/headers/obj_files_hpp.cpp",
                         "scripts/check-cpp-format.py","scripts/portable-preview.py","vcpkg.json"]:
            assert required in names,required
        assert "src/obj_file_detail.hpp" not in names
    print(f"Package content check passed: {path.name}; {len(names)} files")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives",nargs="+",type=Path)
    for path in parser.parse_args().archives: inspect(path)
