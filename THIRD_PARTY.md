# Third-party dependency inventory

| Dependency | Pinned input | Configuration | License / notice |
|---|---|---|---|
| tinyobjloader | `2.0.0rc13`, source commit `2945a967c5303b2c8c14174117c45f3302591150` | Double precision; OBJ triangulation disabled | [MIT terms](https://github.com/tinyobjloader/tinyobjloader/blob/2945a967c5303b2c8c14174117c45f3302591150/LICENSE); vcpkg installs copyright under its package share directory |
| Meshvale Geometry | Development revision `026f4b169f3128b88e3d5ab08529b48ba0454c12` | Compiled C++20 library and optional per-extension Python records | Apache-2.0, its installed LICENSE/[NOTICE](licenses/meshvale-geometry-notice.txt) |
| nanobind | `3.1.0` | Optional static Python binding runtime | [BSD-3-Clause](licenses/nanobind.txt) |
| robin-map | Included by pinned nanobind | Binding implementation dependency | [MIT](licenses/robin-map.txt) |
| scikit-build-core / setuptools-scm | `1.1.1` / `10.3.4` | Python build/version tooling; not bundled runtime | BSD-3-Clause / MIT |

The vcpkg manifest pins registry baseline `cb5a41b03cde4086d554b3f3e2eac39206b16eda`. vcpkg host build helpers are development tooling, not public runtime APIs. Original Meshvale Interchange code is Apache-2.0. Python wheels bundle native dependencies and include their [installed tinyobjloader copyright](licenses/tinyobjloader.txt) and binding notices. Any release that redistributes a dependency must include its actual installed notices; linking a table is not a substitute for package-content review. No third-party source is vendored in this repository.

## glTF parser foundation

These selected private build inputs support the [parser/allocation foundation](docs/gltf-foundation.md).
There is no public glTF reader, extraction or writer API yet. The complete
[proposed asset contract](docs/gltf.md) remains future work.

| Dependency | Pinned input | Constraints | Bundled notice |
|---|---|---|---|
| cgltf private reader | 1.15, commit `360db1a95480fe102ae9c69b27c5d101167ff5ba` | Retained document/resources; independent bounded typed/sparse extraction; no parser-pointer exposure. The evaluated float sparse unpack helper does not preserve the tested interleaved base/packed sparse replacement layout. Reader success does not establish writer fidelity. | [MIT terms and copyright](licenses/cgltf.txt) |
| jsoncons private SAX parser | 1.10.0, commit `9e57e421285422d7aeeb5aa7dc839f6a19121ee6` | Explicit lossless number/bignum options and raw-number serialization; decoded-key duplicate rejection; UTF8 and finite preflight; comments disabled. Default settings do not establish preservation. | [Boost Software License 1.0 and copyright](licenses/jsoncons.txt) |

The [pinned jsoncons options reference](https://github.com/danielaparker/jsoncons/blob/9e57e421285422d7aeeb5aa7dc839f6a19121ee6/doc/ref/corelib/basic_json_options.md)
owns upstream configuration semantics. Future exact opaque numeric tags must not be
converted to double or replaced with caller-authored raw numeric text. These
dependencies do not implement unknown extensions, skinning, animation evaluation
or publication by themselves.

The owning [CMake acquisition](cmake/GltfDependencies.cmake) verifies SHA256 for
public archives and explicit offline archive inputs. Upstream headers are private;
no third-party source is vendored or installed as a new public Interchange API.
Native distributions and Python license metadata include the exact notices above.
The foundation uses neither cgltf sparse unpacking nor parser file-loading helpers.
Retained documents, independent typed extraction and serialization are separate
implementation gates; their constraints in the table do not imply completed support.
