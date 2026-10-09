# Python OBJ text, files and verified bundles

| Field | Value |
|---|---|
| ID | OBJ-PYTHON-001 |
| Version | 0.2.0 |
| Status | Development interface; no released package |
| Owner | Python asset/result shape, ownership, callbacks and packaging |

`meshvale_interchange` exposes the existing native [OBJ text contract](obj.md) and [file/bundle contract](obj-files.md) through owned Python values. It adds no format support or preservation claims beyond those contracts. Geometry meshes are canonical immutable `meshvale_geometry.Mesh` snapshots exchanged through its [record protocol](https://github.com/Meshvale/meshvale-geometry/blob/519eaf2f4eda24c39eaa3ace7f213e5571239fe2/docs/python.md); no Mesh C++ object crosses extension runtimes.

## Interfaces and values

| Function | Result |
|---|---|
| `read_obj(obj, mtl=b"")` | `ObjImportResult(document, diagnostics)` |
| `write_obj(document)` | `ObjExportResult(text, diagnostics)` |
| `read_obj_file(input, *, resource_root=None, cancellation=None)` | `ObjFileResult(asset, diagnostics)` |
| `publish_obj_bundle(asset, destination, *, cancellation=None, on_phase=None, supplemental_files=(), on_verified=None)` | `ObjBundleResult(outcome, phase, entry, diagnostics)` |

`ObjPart(object, groups)` stores object name and a tuple of group strings. `ObjDocument(mesh, parts, material_names, material_library)` stores a canonical Mesh, tuples of parts/names and opaque material-library bytes. `ObjResource(path, bytes)` stores a relative pathlib Path and owned immutable bytes. `ObjFileAsset(document, obj_path, material_libraries, resources)` stores the document, relative OBJ path, ordered library-path tuple and resource tuple. Both document and asset have `with_mesh(mesh)` to construct a replacement carrying the same other data. Constructors normalize tuple/byte fields to owned immutable values and reject incompatible types. These frozen values do not represent arbitrary scenes or skin binding.

Results are frozen values with nullable document/text/asset/entry and detached diagnostic dictionaries (`code`, `subject`, nullable `element`). `ObjText(obj, mtl)` contains OBJ text as a string and raw MTL bytes. Text inputs accept strings or bytes. Strings encode as UTF-8 with surrogateescape; textual names and output OBJ decode with the same policy so undecodable source bytes are not silently replaced. Material/resource bytes retain their exact content. Paths accept `str`, `bytes` or `os.PathLike` through native filesystem encoding; embedded NUL is rejected before any native conversion can truncate it. Texture bytes remain opaque and do not imply image/shader validity.

Reading imports an owned native snapshot, exports an owned mesh record, then imports that record into Geometry. Writing copies the Geometry mesh through its record into native storage. Each mesh direction therefore makes two numeric buffer copies; metadata/resources have their own copied storage. Frozen Python assets and returned meshes survive source-buffer/file/object destruction. The adapter neither borrows a mutable Geometry vector nor registers a second Mesh type. Native parser, export and filesystem work releases the GIL after input copying; all Python access and progress callbacks hold the GIL.

## Failure, cancellation and publication

Native parse, format/subset, resource and verification failures return their native diagnostics with a missing document/text/asset or `failed` bundle outcome. Extra UV sets or unsupported channels fail explicitly on OBJ export. Bad Python representations raise `TypeError`/`ValueError`/`OverflowError` before calling the operation; allocation failures may raise `MemoryError`. Returned inspection/diagnostic data can be edited without changing source/candidate meshes.

`Cancellation()` owns native cooperative stop state. `request_stop()` returns whether this request first changed the state; `stop_requested` reports it. Pass the same token to read/publication or request it from another thread. Cancellation and callback ordering retain the native contract. A callback receives one of `preflight`, `staging`, `verification`, `publication` synchronously before phase work. Callback exceptions become `obj.progress_callback_failed` and a failed publication, with native cleanup. No callback executes after committed publication.

Existing destinations are rejected. Native staging, inventory/reload verification and no-replace directory publication remain authoritative. The binding and Python wrapper prepare the successful result/relative entry before native publication and reuse it on success. `failed`/`cancelled` outcomes do not report a completed destination. Python process termination or asynchronous interpreter interruption can prevent result delivery after a filesystem commit; inspecting the destination may then be necessary. Publication does not promise crash durability or hostile concurrent filesystem safety. Supplemental files are owned `ObjResource` records subject to native path/collision/inventory checks, not a selected workflow report schema.

`on_verified` is an optional callable receiving a frozen `ObjFileAsset` reloaded from verified staged content and a tuple of frozen `ObjResource` records carrying exact verified bytes, ordered by UTF-8 relative path. Return an iterable of additional `ObjResource` files. Inputs are owned copies and survive callback/native-source destruction. Returned files use the native collision/readback/inventory rules above; callback representation errors and exceptions fail publication with `obj.verified_callback_failed`. No callback runs after commit. The inventory excludes the callback's returned files, so a receipt can hash its asset payload without hashing itself. No shared report/profile is inferred from a receipt.

## Build, installation and example

The development distribution is `meshvale-interchange`; import is `meshvale_interchange`. Build with C++20, CMake 3.24+, ordinary GIL-enabled CPython 3.10+, pinned [Python build/runtime requirements](../pyproject.toml), installed Geometry native headers at revision `519eaf2f4eda24c39eaa3ace7f213e5571239fe2` and the [pinned double-precision tinyobjloader](../THIRD_PARTY.md). The exact Python Geometry dependency is `0.0.1.dev23+g519eaf2f4`; build its candidate wheel first. No private checkout or automatic sibling build is required. Native-only builds remain independent of Python. Both Geometry/Interchange native snapshot packages still use `0.0.0`.

In the [configured native dependency environment](../ENVIRONMENT.md), pass installed package prefixes to the optional wheel build:

```sh
python -m pip wheel . --no-deps --wheel-dir .local/wheels --config-settings=cmake.define.CMAKE_PREFIX_PATH="$GEOMETRY_PREFIX;$TINYOBJLOADER_PREFIX"
python -m pip install --no-index --find-links "$CANDIDATE_WHEELS" meshvale-interchange
python -m unittest discover -s tests/python -v
python examples/python/obj_bundle.py
```

The wheel statically incorporates Interchange, tinyobjloader and nanobind with their actual notices. It contains Python code/extension and license metadata, excluding native install trees/tests/build logs. Source archives select product-owned build/test/docs files and generated source-version metadata; they rebuild without Git. Exact `vX.Y.Z` tags own release versions; untagged builds use full-history development versions. Wheels are Python/platform specific and retain dynamic platform C++ runtime requirements. Free-threaded/stable-ABI wheels and a supported release matrix are not established by this experiment. Focused installed tests exercise polygon/seam/resource fidelity, representation errors, cancellation/callback failures, source protection, publication races and both extension import orders. Shared reports, CLI/batch and glTF/GLB remain separate work.

```python
from meshvale_interchange import read_obj_file, publish_obj_bundle

loaded = read_obj_file(input_path)
if loaded.asset is not None:
    inspected = loaded.asset.document.mesh.inspect_topology()
    result = publish_obj_bundle(loaded.asset, new_output_directory)
    print(result.outcome, result.diagnostics)
```

See [the runnable bundle example](../examples/python/obj_bundle.py), [focused Python tests](../tests/python/test_obj.py) and [changelog](../CHANGELOG.md).
