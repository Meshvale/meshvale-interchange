# Native format SDK setup

This guide prepares dependencies for format-adapter development. OBJ/MTL is the
currently available adapter: its [text contract](obj.md) and [file contract](obj-files.md)
own supported behavior. Installing a SDK does not add a Meshvale reader or writer.
STL and USD adapters remain forthcoming; optional [FBX import](fbx.md) now
has its own bounded native interface; the [glTF contract](gltf.md) tracks
that separate adapter. Ordinary OBJ builds need none of the FBX/USD inputs below.

| Format | Development input | Configuration | Current product capability |
|---|---|---|---|
| OBJ/MTL | tinyobjloader 2.0.0rc13, commit `2945a967c5303b2c8c14174117c45f3302591150` | Double precision, static library, no implicit triangulation | Implemented subset; see owning contracts |
| STL | No mandatory external SDK | ASCII/binary variants need separate admission and export proof | Adapter forthcoming |
| FBX | Autodesk FBX SDK 2020.3.11, VS2022 Windows package | Match architecture, configuration and C++ runtime | Optional static polygon import; see [FBX contract](fbx.md) |
| USD | OpenUSD v26.08, vcpkg usd 26.8#1 | Shared native core, optional rendering/Python disabled | Native setup evaluation; adapter forthcoming |

Keep installation prefixes, downloaded packages and build logs outside tracked
source. Use symbolic environment values; [ENVIRONMENT.md](../ENVIRONMENT.md)
owns ordinary Interchange configuration. Record hashes, compiler, configuration,
features and native consumer results locally. Pin package inputs in the owning
build when adopting an adapter; this guide adds no unused runtime dependency.

## Ownership and extension boundary

Interchange owns format parsing, serialization, source assets, resources and
conversion diagnostics. Geometry owns numerical storage and algorithms. Keep
upstream headers, USD stages and FBX managers in private adapter implementations;
public results retain explicit owners and source correspondence. A processing
mesh is a selected view of an asset, not a replacement for its scene model.

Use focused per-format modules and composition. Share a small adapter strategy
and explicit dispatch only for operations that multiple implemented adapters
actually share. Reader, writer, extraction and preservation capabilities are
separate. Optional SDK targets must retain their runtime/plugin closure without
making every product consumer install those SDKs.

## OBJ

Follow the existing [native setup](../ENVIRONMENT.md) and
[dependency inventory](../THIRD_PARTY.md). The manifest selects tinyobjloader's
double feature; `find_package(tinyobjloader CONFIG REQUIRED)` supplies
`tinyobjloader::tinyobjloader_double` and `TINYOBJLOADER_USE_DOUBLE`. Headers and
library must come from the same package. Match producer/consumer runtime and
configuration. A native dependency check should assert `tinyobj::real_t` is
`double` and retain four corners with triangulation disabled. The installed
[Interchange consumer](../examples/consumer/main.cpp) exercises the actual adapter.

## STL

No vendor SDK is required to develop a STL adapter. ASCII and binary variants
need their own bounded reader/writer qualification. Welding coincident triangle
corners is a processing decision, not an implicit import requirement. Units,
materials and polygon boundaries absent from ordinary STL require visible
conversion outcomes. A binary parser must check triangle-count arithmetic and
record extent rather than trusting the header.

[stl_reader v2.0](https://github.com/sreiter/stl_reader/tree/a130fe0b2ac15d7c2fd642bf1dcbdec600e69151)
is an evaluated BSD-2-Clause alternative, not a selected dependency. It merges
matching coordinates and provides no writer. Its default behavior therefore
needs an explicit preservation decision before adoption. No stl_reader port is
present in the registry baseline used below.

## OpenUSD

Use a bootstrapped vcpkg installation matching registry baseline
`cb5a41b03cde4086d554b3f3e2eac39206b16eda`. A stale executable can reject the
registry's tool metadata; bootstrap a compatible installation rather than
editing registry metadata. The [pinned usd port](https://github.com/microsoft/vcpkg/blob/cb5a41b03cde4086d554b3f3e2eac39206b16eda/ports/usd/portfile.cmake)
requires shared linkage. Use a separate SDK manifest directory with this input:

```json
{
  "name": "usd-sdk-development",
  "version-string": "0",
  "builtin-baseline": "cb5a41b03cde4086d554b3f3e2eac39206b16eda",
  "dependencies": [{ "name": "usd", "default-features": false }]
}
```

For Windows x64 Release with the dynamic MSVC runtime, an overlay triplet named
`x64-windows-usd-release.cmake` can contain:

```cmake
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
```

Choose absolute `SDK_MANIFEST_ROOT`, `SDK_INSTALLED_DIR` and `SDK_TRIPLET_DIR`
values in your shell. In a matching compiler environment, install separately
from Interchange's static OBJ dependencies:

```sh
vcpkg install --x-manifest-root="$SDK_MANIFEST_ROOT" --x-install-root="$SDK_INSTALLED_DIR" --overlay-triplets="$SDK_TRIPLET_DIR" --triplet=x64-windows-usd-release
```

PowerShell uses `$env:SDK_MANIFEST_ROOT` and equivalent environment expansion.
`VCPKG_MAX_CONCURRENCY` can cap build workers. This registry selects native
OpenUSD 26.08, oneTBB 2023.1.0 and zlib 1.3.2#2; oneTBB's default dependency also
adds hwloc 2.11.2. Imaging, Python, examples, tests, tutorials and USD tools are
disabled by this port configuration. Native core execution and validation
libraries can still be included; do not describe the package as only usdGeom.

A separate C++20 project consumes the installed exports:

```cmake
find_package(pxr CONFIG REQUIRED)
target_link_libraries(your_usd_consumer PRIVATE usdGeom usd sdf vt gf tf)
```

Configure with vcpkg's toolchain, the matching triplet/installed directory and
`VCPKG_MANIFEST_MODE=OFF`. At runtime preserve installed plugin/resource layout
and make the installed dependency `bin` directory discoverable. Do not copy just
one DLL. Verify native headers, linking and execution by authoring an in-memory
four-point quad, exporting USDA and USDC, destroying the authored stage, reopening
both files and comparing points, face counts and indices. A Python usd-core
import does not establish C++ usability or Meshvale adapter fidelity.

For alternatives consult the [official v26.08 build instructions](https://github.com/PixarAnimationStudios/OpenUSD/blob/v26.08/BUILDING.md).
This setup is evaluated for Windows x64; other platforms require their own
configuration/runtime proof. Standard USD mesh points use float32 and face
indices use signed integers: exporting Geometry doubles/large indices needs
explicit rounding and representability outcomes. A small quad smoke check does
not prove composition, primvars, instances, animation, resource resolution or USDZ.

## Autodesk FBX SDK

Obtain the exact SDK through [Autodesk's download page](https://aps.autodesk.com/developer/overview/fbx-sdk).
The evaluated 2020.3.11 VS2022 Windows installer has SHA256
`ad2f2724850c2b54ea97453e02249c449a7e46d061640d59bc894d1bc0080ad6`
and a verified Autodesk signer. Inspect the actual package version/signature and
packaged agreement before installing to a chosen `FBXSDK_ROOT`. This is a vendor
licensed SDK; the package's agreement governs installation, use and distribution.

This package has `include/fbxsdk.h` and `lib/x64`/`lib/arm64`, each with Debug and
Release variants. Do not assume older `vs2015` directory layouts. Shared Windows
x64 Release linkage can use a private imported target:

```cmake
add_library(autodesk_fbx_sdk SHARED IMPORTED)
set_target_properties(autodesk_fbx_sdk PROPERTIES
  IMPORTED_IMPLIB "${FBXSDK_ROOT}/lib/x64/release/libfbxsdk.lib"
  IMPORTED_LOCATION "${FBXSDK_ROOT}/lib/x64/release/libfbxsdk.dll"
  INTERFACE_INCLUDE_DIRECTORIES "${FBXSDK_ROOT}/include"
  INTERFACE_COMPILE_DEFINITIONS FBXSDK_SHARED)
```

Match compiler, architecture, C++ runtime and configuration. Static `-md`/`-mt`
variants have separate dependent libraries to inspect. Retain the actual SDK
agreement, third-party terms and required product acknowledgement when adopting
it; the public source checkout does not redistribute SDK packages. Verify the
installed native setup by creating a manager/scene/quad, exporting and reimporting
FBX and checking topology. Animation, skeletons, morphs, materials and axis/unit
semantics need separate adapter proof.

[OpenFBX](https://github.com/nem0/OpenFBX/tree/82a43d9191f2250145fddc219b4083667c33f2a5)
is an evaluated MIT importer alternative, not Autodesk's SDK or a writer. It is
not selected by this guide.

## Notices and completion

OpenUSD v26.08 uses the [Tomorrow Open Source Technology License 1.0](https://github.com/PixarAnimationStudios/OpenUSD/blob/v26.08/LICENSE.txt),
including its specific trademark terms and upstream third-party notices.
tinyobjloader's installed copyright includes MIT and earcut ISC material. Keep
actual installed dependency notices with distributed artifacts; a link alone is
not a bundled notice. Vendor FBX distribution follows its own packaged agreement.

Dependency setup proof records configure/build/link/run results from a separate
native consumer. Product adapter proof additionally covers public behavior,
unsupported/loss outcomes, limits, malformed inputs, cancellation and admitted
round trips. Report these separately, including unexecuted installation steps.
