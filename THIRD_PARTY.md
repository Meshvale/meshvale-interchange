# Third-party dependency inventory

| Dependency | Pinned input | Configuration | License / notice |
|---|---|---|---|
| tinyobjloader | `2.0.0rc13`, source commit `2945a967c5303b2c8c14174117c45f3302591150` | Double precision; OBJ triangulation disabled | [MIT terms](https://github.com/tinyobjloader/tinyobjloader/blob/2945a967c5303b2c8c14174117c45f3302591150/LICENSE); vcpkg installs copyright under its package share directory |
| Meshvale Geometry | Development revision `920be542502652b1d16c5f90414cec6495ff62b4` | Compiled C++20 library and optional per-extension Python records | Apache-2.0, its installed LICENSE/[NOTICE](licenses/meshvale-geometry-notice.txt) |
| Eigen | `3.4.1`, through the exact compiled Geometry producer | Private unmodified Core implementation; `EIGEN_MPL2_ONLY`, internal threading disabled, fast-math disabled; [MPL-2.0](licenses/eigen-mpl2.txt), [Apache-2.0](licenses/eigen-apache.txt) and [included upstream notices](licenses/eigen-notices.txt); [source](https://gitlab.com/libeigen/eigen/-/tree/3.4.1). No Eigen types or headers are required by installed consumers |
| nanobind | `3.1.0` | Optional static Python binding runtime | [BSD-3-Clause](licenses/nanobind.txt) |
| robin-map | Included by pinned nanobind | Binding implementation dependency | [MIT](licenses/robin-map.txt) |
| scikit-build-core / setuptools-scm | `1.1.1` / `10.3.4` | Python build/version tooling; not bundled runtime | BSD-3-Clause / MIT |

The vcpkg manifest pins registry baseline `cb5a41b03cde4086d554b3f3e2eac39206b16eda`. vcpkg host build helpers are development tooling, not public runtime APIs. Original Meshvale Interchange code is Apache-2.0. Python wheels bundle native dependencies and include their [installed tinyobjloader copyright](licenses/tinyobjloader.txt) and binding notices. Any release that redistributes a dependency must include its actual installed notices; linking a table is not a substitute for package-content review. No third-party source is vendored in this repository.

## glTF adapter candidates

These are evaluated inputs to the [proposed asset contract](docs/gltf.md), not
selected or installed runtime dependencies. No glTF adapter is implemented.

| Candidate | Evaluated input | Required constraints | License input |
|---|---|---|---|
| cgltf reader | 1.15, commit `360db1a95480fe102ae9c69b27c5d101167ff5ba` | Retained document/resources; independent bounded typed/sparse extraction; no parser-pointer exposure. The evaluated float sparse unpack helper does not preserve the tested interleaved base/packed sparse replacement layout. Reader success does not establish writer fidelity. | [Pinned MIT terms and copyright](https://github.com/jkuhlmann/cgltf/blob/360db1a95480fe102ae9c69b27c5d101167ff5ba/LICENSE) |
| jsoncons serializer | 1.10.0, commit `9e57e421285422d7aeeb5aa7dc839f6a19121ee6` | Explicit lossless number/bignum options and raw-number serialization; decoded-key duplicate rejection; UTF8 and finite preflight; comments disabled. Default settings do not establish preservation. | [Pinned Boost Software License 1.0 and copyright](https://github.com/danielaparker/jsoncons/blob/9e57e421285422d7aeeb5aa7dc839f6a19121ee6/LICENSE) |

The [pinned jsoncons options reference](https://github.com/danielaparker/jsoncons/blob/9e57e421285422d7aeeb5aa7dc839f6a19121ee6/doc/ref/corelib/basic_json_options.md)
owns upstream configuration semantics. Exact opaque numeric tags must not be
converted to double or replaced with caller-authored raw numeric text. These
candidates do not implement unknown extensions, skinning, animation evaluation
or publication by themselves.

Before adoption, the owning build/package changes must pin actual dependency
inputs, document the admitted configuration and include required notices in
distributed source/artifacts. This candidate table is not a bundled notice or
a redistribution grant. Default Python manifests and package contents remain the OBJ
dependency set above.

## Optional native FBX module

The opt-in [FBX module](docs/fbx.md) selects a locally licensed Autodesk FBX
SDK2020.3.11 VS2022 Windows x64 shared Release SDK and dynamic MSVC CRT. Its
proprietary packaged agreement governs SDK use separately from this repository
Apache-2.0 source license. The required [Autodesk acknowledgement](licenses/optional/fbx-acknowledgement.txt)
is retained in source archives and enabled native installs. Vendor SDK headers,
libraries, DLLs and agreement are not redistributed by this project. Base OBJ
installs and current Python wheels include no FBX runtime or optional acknowledgement.
