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
            notice_contents = {name: archive.read(name) for name in names if ".dist-info/licenses/" in name}
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
                    ("licenses", "licenses", "cgltf.txt"),
                    ("licenses", "licenses", "jsoncons.txt"),
                    ("licenses", "licenses", "eigen-mpl2.txt"),
                    ("licenses", "licenses", "eigen-apache.txt"),
                    ("licenses", "licenses", "eigen-notices.txt"),
                    ("licenses", "licenses", "meshvale-geometry-notice.txt"),
                    ("licenses", "licenses", "tinyobjloader.txt")], name
        for license in ["cgltf.txt","jsoncons.txt","eigen-mpl2.txt","eigen-apache.txt","eigen-notices.txt","LICENSE","NOTICE","nanobind.txt","robin-map.txt","meshvale-geometry-notice.txt","tinyobjloader.txt"]:
            matching = [name for name in notice_contents if name.endswith("/" + license)]
            assert len(matching) == 1, (license, matching)
            reference = Path(__file__).resolve().parents[1] / (license if license in {"LICENSE", "NOTICE"} else "licenses/" + license)
            assert notice_contents[matching[0]].decode().replace("\r\n", "\n") == reference.read_text(), license
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
            assert not name.endswith((".pyc",".pyd",".so",".obj",".log",".dll",".lib",".exe")),name
        for required in [".clang-format","python/meshvale_interchange/_version.py","python/bindings.cpp","tests/python/test_obj.py","src/obj_read.cpp",
                         "include/meshvale/interchange/obj.h",
                         "include/meshvale/interchange/stl.h", "docs/stl.md",
                         "src/stl.cpp", "src/stl_test_internal.h",
                         "tests/header_stl.cpp", "tests/stl.cpp", "tests/stl_requests.cpp",
                         "tests/stl_fixture.h", "examples/stl-consumer/CMakeLists.txt",
                         "examples/stl-consumer/main.cpp",
                         "include/meshvale/interchange/off.h", "docs/off.md",
                         "src/off.cpp", "src/off_test_internal.h",
                         "tests/header_off.cpp", "tests/off.cpp", "tests/off_requests.cpp",
                         "tests/off_fixture.h", "examples/off-consumer/CMakeLists.txt",
                         "examples/off-consumer/main.cpp",
                         "examples/off-repair-consumer/CMakeLists.txt",
                         "examples/off-repair-consumer/main.cpp",
                         "examples/off-repair-consumer/README.md",
                         "include/meshvale/interchange/off_files.h", "docs/off-files.md",
                         "cmake/OffFiles.cmake", "src/off_files.cpp",
                         "src/off_file_internal.h", "src/off_file_paths.cpp",
                         "src/off_file_native.cpp", "src/off_file_publish_native.cpp",
                         "tests/headers/off_files_h.cpp",
                         "tests/off_files.cpp", "tests/off_files_requests.cpp",
                         "tests/off_files_fixture.h",
                         "examples/off-files-consumer/CMakeLists.txt",
                         "examples/off-files-consumer/main.cpp",
                         "examples/off-files-consumer/README.md",
                         "include/meshvale/interchange/obj_files.h",
                         "src/obj_file_detail.h","src/obj_file_detail.cpp","tests/headers/obj_h.cpp",
                         "tests/headers/obj_files_h.cpp",
                         "include/meshvale/interchange/glb.h", "src/glb.cpp",
                         "src/glb_detail.h", "docs/glb.md", "tests/glb.cpp",
                         "tests/headers/glb_h.cpp", "examples/consumer/glb.cpp",
                         "include/meshvale/interchange/gltf.h", "docs/gltf-read.md",
                         "include/meshvale/interchange/gltf_files.h", "docs/gltf-files.md",
                         "cmake/GltfFiles.cmake", "src/gltf_files.cpp",
                         "src/gltf_file_paths.cpp", "src/gltf_file_native.cpp",
                         "src/gltf_files_internal.h", "src/gltf_files_test_internal.h",
                         "src/gltf_file_read_provider.h", "tests/gltf_files.cpp",
                         "tests/gltf_files_glb.cpp", "tests/gltf_files_glb_requests.cpp",
                         "src/file_publication_native.h", "src/file_publication_native.cpp",
                         "src/gltf_file_publish.cpp", "src/gltf_file_publish_internal.h",
                         "src/gltf_file_publish_native.cpp", "tests/gltf_file_publish_requests.cpp",
                         "tests/gltf_file_publish.cpp",
                         "tests/gltf_file_publish_glb.cpp",
                         "tests/gltf_file_publish_glb_requests.cpp",
                         "tests/gltf_file_publish_glb_fixture.h",
                         "tests/gltf_file_provider_fixture.h",
                         "tests/gltf_file_verified_provider.cpp",
                         "tests/gltf_file_verified_provider_requests.cpp",
                         "tests/gltf_execution_budget.cpp",
                         "examples/consumer/gltf_file_publish.cpp",
                         "examples/consumer/gltf_file_publish_glb.cpp",
                         "tests/headers/gltf_files_h.cpp", "examples/consumer/gltf_files.cpp",
                         "examples/consumer/gltf_files_link.cpp",
                         "src/gltf_operation_workers.h", "src/gltf_operation_workers.cpp",
                         "tests/gltf_workflow_read.cpp", "examples/consumer/gltf_workflow_read.cpp",
                         "include/meshvale/interchange/gltf_workflow_support.h",
                         "docs/gltf-workflow-support.md", "cmake/GltfWorkflowSupport.cmake",
                         "src/gltf_workflow_support.cpp", "src/gltf_workflow_support_geometry.cpp",
                         "src/gltf_workflow_support_lexical.cpp", "src/gltf_workflow_support_reload.cpp",
                         "src/gltf_workflow_support_internal.h", "src/gltf_workflow_support_test_internal.h",
                         "src/gltf_topology_bundle_test_internal.h",
                         "tests/gltf_workflow_inspection.cpp", "tests/gltf_workflow_inspection_requests.cpp",
                         "tests/gltf_workflow_inspection_support.cpp", "tests/gltf_workflow_inspection_support.h",
                         "tests/headers/gltf_workflow_support_h.cpp",
                         "examples/consumer/gltf_workflow_support.cpp",
                         "examples/consumer/gltf_workflow_support_link.cpp",
                         "tests/headers/gltf_h.cpp", "examples/consumer/gltf.cpp",
                         "include/meshvale/interchange/gltf_write.h", "docs/gltf_write.md",
                         "include/meshvale/interchange/gltf_materialization.h", "docs/gltf-materialization.md",
                         "src/gltf_materialization.cpp", "src/gltf_materialization_test_internal.h",
                         "tests/gltf_materialization.cpp", "tests/gltf_materialization_oracle.py",
                         "tests/headers/gltf_materialization_h.cpp",
                         "examples/consumer/gltf_materialization.cpp",
                         "examples/consumer/gltf_materialization_unsupported.cpp",
                         "include/meshvale/interchange/gltf_topology_write.h",
                         "docs/gltf_topology_write.md", "cmake/GltfTopologyWrite.cmake",
                         "src/gltf_topology_write.cpp", "src/gltf_topology_write_lexical.cpp",
                         "src/gltf_topology_write_workers.cpp",
                         "src/gltf_topology_write_internal.h", "src/gltf_topology_write_workers.h",
                         "tests/gltf_topology_write.cpp", "tests/headers/gltf_topology_write_h.cpp",
                         "examples/consumer/gltf_topology_write.cpp",
                         "examples/consumer/gltf_topology_write_link.cpp",
                         "tests/gltf_topology_write_semantics/main.cpp",
                         "tests/gltf_topology_write_semantics/fixture_support.h",
                         "tests/gltf_topology_write_semantics/fixture_support.cpp",
                         "tests/gltf_topology_write_semantics/fixture_data.cpp",
                         "tests/gltf_topology_write_semantics/raw_channels.cpp",
                         "tests/gltf_topology_write_semantics/written_checks.cpp",
                         "tests/gltf_topology_write_semantics/selection_lifetimes.cpp",
                         "tests/gltf_topology_write_semantics/rejected_candidates.cpp",
                         "tests/gltf_topology_write_semantics/lexical_preservation.cpp",
                         "tests/gltf_topology_write_semantics/check_fixtures.py",
                         "tests/gltf_materialization_semantics/main.cpp",
                         "tests/gltf_materialization_semantics/fixture_support.h",
                         "tests/gltf_materialization_semantics/fixture_support.cpp",
                         "tests/gltf_materialization_semantics/fixture_data.cpp",
                         "tests/gltf_materialization_semantics/raw_channels.cpp",
                         "tests/gltf_materialization_semantics/topology_provenance.cpp",
                         "tests/gltf_materialization_semantics/lifetimes.cpp",
                         "tests/gltf_materialization_semantics/check_fixtures.py",
                         "cmake/MaterializationTestProfile.cmake", "scripts/check-native-tests.py",
                         "cmake/sanitizer-triplets/x64-windows-static-md-asan.cmake",
                         "src/gltf_write.cpp", "src/gltf_write_detail.h",
                         "tests/gltf_write.cpp", "tests/gltf_write_oracle.cpp", "tests/gltf_write_oracle.py",
                         "tests/headers/gltf_write_h.cpp", "examples/consumer/gltf_write.cpp",
                         "src/gltf_read.cpp", "src/gltf_asset.cpp", "src/gltf_asset_state.h",
                         "src/gltf_metadata.h", "src/gltf_metadata.cpp", "src/gltf_admission.cpp",
                         "src/gltf_graph.cpp", "src/gltf_graph_declarations.cpp",
                         "src/gltf_resources.h", "src/gltf_resources.cpp",
                         "src/gltf_accessor.h", "src/gltf_accessor.cpp", "src/gltf_extract.cpp",
                         "tests/gltf_read.cpp", "tests/gltf_resources.cpp", "tests/gltf_metadata.cpp",
                         "tests/gltf_accessor.cpp", "tests/gltf_extract.cpp", "tests/gltf_accessor_oracle.py",
                         "scripts/check-cpp-format.py","scripts/portable-preview.py","vcpkg.json",
                         "cmake/GltfDependencies.cmake","src/gltf_allocation.h",
                         "src/gltf_allocation.cpp","src/gltf_parser.cpp","src/cgltf_reader.cpp",
                         "src/gltf_json_snapshot.h","src/gltf_json_snapshot.cpp",
                         "tests/gltf_json_snapshot.cpp","tests/gltf_json_oracle.py",
                         "licenses/cgltf.txt","licenses/jsoncons.txt"]:
            assert required in names,required
        for license in ["eigen-mpl2.txt", "eigen-apache.txt", "eigen-notices.txt", "meshvale-geometry-notice.txt"]:
            assert "licenses/" + license in names, license
        for required in ['include/meshvale/interchange/usd.h', 'src/usd_read.cpp', 'cmake/usd.cmake', 'cmake/usd-config.cmake.in', 'docs/usd.md', 'licenses/optional/usd.txt', 'licenses/optional/tbb.txt', 'licenses/optional/hwloc.txt', 'licenses/optional/zlib.txt']:
            assert required in names, required
        for required in ['include/meshvale/interchange/fbx.h', 'src/fbx_read.cpp', 'cmake/fbx.cmake', 'cmake/fbx-config.cmake.in', 'docs/fbx.md', 'licenses/optional/fbx-acknowledgement.txt']:
            assert required in names, required
        assert not any(name.endswith(".hpp") for name in names)
    print(f"Package content check passed: {path.name}; {len(names)} files")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives",nargs="+",type=Path)
    for path in parser.parse_args().archives: inspect(path)
