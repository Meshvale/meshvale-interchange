# Third-party dependency inventory

| Dependency | Pinned input | Configuration | License / notice |
|---|---|---|---|
| tinyobjloader | `2.0.0rc13`, source commit `2945a967c5303b2c8c14174117c45f3302591150` | Double precision; OBJ triangulation disabled | [MIT terms](https://github.com/tinyobjloader/tinyobjloader/blob/2945a967c5303b2c8c14174117c45f3302591150/LICENSE); vcpkg installs copyright under its package share directory |
| Meshvale Geometry | Development revision `c298d3cd46829aed309f3152aad99101932e1ea8` | Installed C++20 headers and optional Python records | Apache-2.0, its installed LICENSE/[NOTICE](licenses/meshvale-geometry-notice.txt) |
| nanobind | `3.1.0` | Optional static Python binding runtime | [BSD-3-Clause](licenses/nanobind.txt) |
| robin-map | Included by pinned nanobind | Binding implementation dependency | [MIT](licenses/robin-map.txt) |
| scikit-build-core / setuptools-scm | `1.1.1` / `10.3.4` | Python build/version tooling; not bundled runtime | BSD-3-Clause / MIT |

The vcpkg manifest pins registry baseline `cb5a41b03cde4086d554b3f3e2eac39206b16eda`. vcpkg host build helpers are development tooling, not public runtime APIs. Original Meshvale Interchange code is Apache-2.0. Python wheels bundle native dependencies and include their [installed tinyobjloader copyright](licenses/tinyobjloader.txt) and binding notices. Any release that redistributes a dependency must include its actual installed notices; linking a table is not a substitute for package-content review. No third-party source is vendored in this repository.
