# Optional native USD polygon extraction

The compiled `meshvale::interchange_usd` target supplies
[`ReadUsdMesh`](../include/meshvale/interchange/usd.h). It reads one explicitly
selected absolute mesh prim path from a local `.usda` or `.usdc` regular file.
This is a default-time polygon-cage extraction operation. It does not write USD,
evaluate a subdivision surface, flatten a scene or preserve stage composition.
The optional module has no Python binding.

## Ownership and coordinates

The result owns a Geometry mesh, supported primvar source tables and indices,
and selected metadata. The SDK stage and root layer are destroyed before return;
no SDK object, pointer, view or global stage-cache entry escapes. Output copies
own their storage. Positions retain authored order and remain in mesh-local
space. Finite `point3f` positions widen exactly to Geometry's binary64 position
buffer, including signed zero; nonfinite positions produce
`usd.nonfinite_position`. Normals and UVs retain their binary32/binary64 scalar
storage; they are not normalized, welded or regenerated.

Face counts become offsets and face-vertex indices become corner indices without
triangulation or winding changes. Counts smaller than three, negative counts,
counts that do not exhaust the index array, and negative/out-of-range vertex
indices fail explicitly. Degenerate/repeated vertex rings are retained for the
caller's Geometry inspection. An empty mesh is representable.

`up_axis` and `meters_per_unit` retain evaluated stage values, including USD
fallbacks when unauthored. `orientation`, `double_sided` and
`subdivision_scheme` likewise retain evaluated mesh values. `local_to_world`
records the evaluated default-time USD matrix in row-major storage using USD's
row-vector convention. Coordinates, normals, winding and units are not converted
or transformed. A subdivision scheme remains metadata about the extracted cage;
the result does not represent its evaluated surface. Purpose, visibility,
extents, arbitrary metadata, hierarchy and sibling prims are not preserved.

## Normal and UV channels

Supported directly authored arrays are built-in `normals`, `normal3f[]` and
`normal3d[]` primvars, and `texCoord2f[]`/`texCoord2d[]` primvars. Common unroled
`float2[]`/`double2[]` primvars named `st` or prefixed `st:` are also UV channels.
Other primvars, half encodings, scalar values, element sizes other than one,
inherited ancestor primvars and simultaneous built-in/primvar `normals` fail
explicitly. Missing or blocked authored normal/primvar values fail rather than
creating a fabricated channel. Multiple supported named UV channels are retained
without assuming one UV set.

| USD interpolation | Expanded Geometry domain |
|---|---|
| `constant` | Face, repeating the one source value per face |
| `uniform` | Face |
| `vertex` / `varying` | Vertex |
| `faceVarying` | Corner |

No interpolation computation occurs. Required row cardinalities are checked
against the selected polygon mesh. Indexed primvars validate every signed source
index before expansion. Each `UsdPrimvarSource` retains the exact original typed
value table, optional index list, type name, interpolation and
`unauthored_values_index`, including unused authored values. These records retain
index sharing and face-varying seam topology that dense Geometry rows alone cannot
describe. They are import provenance: subsequent changes to the mesh do not update
the original source table. A valid `unauthoredValuesIndex` entry becomes a zero in
the Geometry presence mask while its placeholder values remain in storage. Other
negative indices and invalid sentinel declarations fail; nothing is repaired.

## Source loading and unsupported cases

`SdfLayer::OpenAsAnonymous` reads a fresh root rather than consuming a previously
cached root layer. The caller must keep the file stable throughout the call.
`source_bytes` admits the file size observed before SDK parsing; it is not an
immutable byte capture or a hard parser/read limit if another actor changes the
file. There is no source mutation or export. An installed native test replaces a
file at the same path while retaining a separately cached SDK stage and verifies
that both USDA and USDC reads see the replacement.

Before creating a stage, the adapter rejects sublayers, references, payloads,
inherits, specializes, variants and value clips, including internal references.
Only then does it create a stage with `LoadNone`. This prevents composition asset
loading through those rejected arcs. Every authored time-sample field anywhere in
the root layer rejects the file; the adapter does not choose an animation frame.
USDZ, generic `.usd` extension dispatch and third-party root formats are rejected.
Normal OpenUSD format/plugin discovery and schema resources are still used. SDK
plugins and resolver configuration must be trusted and stable; this is not a
sandbox for adversarial native plugins or files.

Selected meshes or ancestors with authored instancing, material binding or
skeleton binding are rejected. All selected-mesh relationships and unsupported
authored attributes reject extraction. This includes holes, creases, corner
sharpness, velocities, accelerations, scene skinning and custom attributes.
Supported mesh attributes and static transforms are admitted; unrelated scene
objects are outside extraction coverage. `usd.selected_mesh_only` and
`usd.local_space_cage` accompany successful results and state those limits.

Limits bound observed source size and retained position/face/corner/primvar
cardinalities. `attribute_scalars` counts retained source scalars, retained index
entries and expanded channel scalars together. Zero limits are invalid. Checked
integer arithmetic precedes our output allocations; SDK parsing and intermediate
SDK arrays necessarily occur before output admission and are outside this budget.
The SDK's allocator, caches, plugins, file mapping and internal worker pool are
also outside it. The adapter traverses one selected mesh serially to retain
deterministic row correspondence; no Meshvale parallel or cancellation guarantee
is made. OpenUSD's existing worker configuration remains caller-owned.

Expected failures return no document and one Geometry diagnostic. Codes begin
with `usd.` and distinguish unavailable source, malformed input, invalid
selection/topology/indices, limits and unsupported features. Allocation exceptions
propagate. Each call owns a separate stage; callers must synchronize changes to
SDK global plugin/resolver configuration and to their source files.

## Build and installed consumption

The default build disables this module and needs no OpenUSD. Enable it with
`-DMESHVALE_INTERCHANGE_USD=ON` and supply an installed OpenUSD **26.08** package
(CMake package `pxr` **0.26.8**, exact), compatible installed Geometry and the
existing OBJ build dependencies through `CMAKE_PREFIX_PATH` or vcpkg. See the
[SDK setup guide](format-sdks.md) for preparation. Match the producer's compiler,
configuration and C++ runtime. The adapter uses compiled C++20 implementation
files; its public header exposes neither OpenUSD nor Eigen types.

An enabled native install adds the separate `MeshvaleInterchangeUsd` CMake
package, `meshvale::interchange_usd` static library and public USD header. Its
consumer requires matching native OpenUSD libraries and runtime/plugin resources;
installing Meshvale does not redistribute that runtime. The base Interchange
package has no USD dependency. The
[installed example](../examples/usd/CMakeLists.txt) consumes only installed
packages and runs with a file path and absolute mesh prim path. On Windows the
matching OpenUSD runtime `bin` directory must be on `PATH`; preserve its installed
plugin-resource layout. Other SDK/plugin search configuration belongs to OpenUSD.

Enabled configuration checks the installed USD, oneTBB, hwloc and zlib copyright
texts against the retained optional dependency notices, allowing only newline
differences. `MESHVALE_USD_DEPENDENCY_NOTICE_ROOT` can point to the installed share
root containing each dependency's `copyright` file if it is outside the default
OpenUSD CMake package layout. The native USD install includes those four notices
under `share/MeshvaleInterchangeUsd/licenses`. Disabled native builds and existing
Python wheels do not bundle the optional SDK notices or runtime.

The evaluated local combination is Windows x64, MSVC Release with dynamic CRT and
shared OpenUSD26.08 without Python or imaging. Source/native tests include actual
USDA/USDC inputs and the public adapter; this does not establish a supported
release, other platforms, scene round-trip fidelity or a USD writer.

The SDK semantics follow the official
[primvar documentation](https://openusd.org/dev/api/class_usd_geom_primvar.html)
and [layer interface](https://openusd.org/dev/api/class_sdf_layer.html).
