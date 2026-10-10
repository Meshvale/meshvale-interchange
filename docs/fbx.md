# Optional FBX polygon import

`FBX-POLYGON-001` owns the optional native `ReadFbxFile` interface in
[fbx.h](../include/meshvale/interchange/fbx.h). It reads Autodesk FBX files into
owned polygon meshes; it provides no writer, Python binding or scene round trip.

## Geometry and attributes

Mesh control points, polygon order, polygon sizes and corner indices are copied
without welding, triangulation, coordinate conversion or normalization. Meshes
retain their ordered node-name path and child-index path, which distinguish
duplicate names. Identity group nodes with no attribute or a null attribute are
traversed; other nonmesh node attributes are rejected. Source axis descriptors
and the unit scale in centimeters are metadata; coordinates remain in source
local space. The complete original FBX bytes are retained in the returned asset.

Normals and multiple UV elements support `by_control_point`,
`by_polygon_vertex`, `by_polygon` and `all_same` mapping, with
`index_to_direct` reference for UVs, and `direct` or `index_to_direct` reference
for normals. Direct UV reference is explicitly unsupported by this slice; an
asset containing it is rejected. The corresponding Geometry channel uses
respectively vertex, corner or face rows (`all_same` is expanded to face rows). Normal rows
have three binary64 components; UV rows have two. Source direct tables, complete
index arrays for indexed references, element names, mapping/reference mode and
the normal's fourth component are also copied separately, including unused table entries. The
adapter does not infer missing rows, normalize normals or merge UV seams.
Invalid mapping cardinalities or indices reject the entire asset.

These source tables describe the SDK's decoded model, which may differ from an
authored file's serialization. The source bytes retain that serialization. The
evaluated fixtures cover FBX 7.7 binary and ASCII, including indexed UVs, direct
and indexed normals, and rejection of direct UVs; earlier variants and arbitrary
application exports remain unevaluated.

The first slice rejects nonidentity local, geometric and pivot transforms on any
node, including mesh ancestors. It rejects nonmesh geometry, mesh instancing,
animation stacks/curves, deformers including skin and blend shapes, materials,
textures/videos, poses and user-defined node/mesh properties. Layers other than
normal/UV elements are unsupported. These outcomes have diagnostics and no
partial asset. Arbitrary metadata, lights, cameras, rigging and scene composition
are outside this interface. SDK parsing does not establish complete authored-file
fidelity: callers needing unsupported scene data must retain the original bytes.

## Ownership and failures

`FbxAsset` owns every returned value and survives destruction of SDK objects.
SDK types stay private. Imports through this module are serialized because the
upstream SDK does not guarantee thread safety. Callers using the SDK separately
must coordinate their own SDK access. Expected I/O, malformed, unsupported and
limit outcomes return diagnostics with no asset. C++ allocation failures propagate.

The input is read once into owned bytes, with the byte limit checked before SDK
parsing. A custom read-only SDK stream prevents file reopening. Import settings
disable extraction of embedded resources. No external resource resolution is
offered. Count limits apply during conversion, after SDK parsing; they are not a
bound on the SDK's internal allocations, process memory or parser work. There is
no cancellation or untrusted-input sandbox guarantee.

## Native dependency and consumption

Enable `MESHVALE_INTERCHANGE_FBX=ON` and set `FBXSDK_ROOT` to a local licensed
Autodesk FBX SDK **2020.3.11 VS2022** installation. This initial dependency setup
supports Windows x64 Release with the shared SDK and dynamic MSVC CRT (`/MD`).
It rejects other compiler, architecture, configuration and SDK versions rather
than selecting an untested library. Keep `libfbxsdk.dll` from the matching SDK
release directory on `PATH` when running.

The separate installed package is `MeshvaleInterchangeFbx`; its target is
`meshvale::interchange_fbx`. Consumers set `FBXSDK_ROOT` and use
`find_package(MeshvaleInterchangeFbx 0.0.0 EXACT CONFIG REQUIRED)`. The ordinary
OBJ target and a disabled build do not search for or link FBX. The optional
installed [example](../examples/fbx-consumer/main.cpp) reads a file supplied by
the caller. SDK binaries, headers and vendor agreement are not redistributed by
this project. The package does not install a vendor DLL or acquire a license.

Read the SDK's exact packaged agreement before installing or using it. Its
proprietary terms apply separately from this project's Apache-2.0 source license.
The required Autodesk acknowledgement accompanies enabled native outputs; a
downstream distributor must apply its own applicable vendor obligations.
