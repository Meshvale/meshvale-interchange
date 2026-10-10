# OBJ/MTL text interchange

Contract version **0.1.2**, development interface. [obj.h](../include/meshvale/interchange/obj.h) owns C++ type shapes. This contract describes the in-memory adapter; the [file/bundle contract](obj-files.md) owns filesystem import, texture resolution and staged output publication. The optional Python interface has its [own contract](python.md); CLI remains forthcoming.

## Import

`read_obj` takes OBJ text and the combined MTL text supplied by the caller. It uses tinyobjloader `2.0.0rc13` with double precision and triangulation disabled. OBJ `mtllib` filenames are descriptive: this interface performs no file access. MTL bytes are retained verbatim, and material names/face assignments are resolved from the supplied text. Referenced texture/resource files have not been loaded or verified.

Supported OBJ directives are `v` (three coordinates), `vt` (one or two coordinates), `vn` (three coordinates), polygon `f`, `o`, `g`, `s`, `usemtl` and `mtllib`, plus comments/blank lines. Numeric records must have finite decimal values. Positive and negative face references are supported. Vertex color/weight extensions, a third texture coordinate, line/point/freeform primitives, continuations and unknown directives are rejected with feature diagnostics. These are initial subset limits, not claims about OBJ generally.

Positions retain source vertex order, including unused/distinct coincident vertices. Faces retain source order and directed corner loops, including concave polygons; neither triangulation nor normalization occurs. The adapter cross-checks lexical face counts/sizes against parser output to prevent a successful parse from concealing dropped faces. Structurally defective positive vertex references can remain in the raw document with inspection diagnostics. UV/normal references are checked before lookup; an invalid reference cannot become a fabricated missing value. Negative references that the parser cannot resolve cause import failure.

Smoothing groups are `off` or decimal integers from zero through `2147483647`, matching the selected parser's integer range. OBJ reference tokens use nonzero signed 32-bit integers. No larger index/group range is claimed by this adapter even though geometry storage uses wider indices.

Corner channels are double-precision `obj.uv0` (`texcoord`, set 0, width 2) and `obj.normal` (`normal`, width 3), with explicit presence masks. One-component OBJ UV records acquire the format's default second component zero. UV/normal pool indices and unreferenced pool entries are not retained as authored mesh elements. Face channels are `obj.material` (`material_index`, int32, `-1` unbound), `obj.part` (`label`, uint32 referencing `parts`) and `obj.smoothing_group` (`label`, uint32, zero off). Part records contain the active object name and ordered group list; repeated identical membership uses one record. Supplied material definitions remain opaque bytes: there is no generic scene/PBR/material evaluation claim.

Parser warnings remain diagnostics; a returned document is raw data, not a validity guarantee. Unsupported syntax, unresolved material assignment, inconsistent parser output or unsafe attribute lookup returns no document. Callers must inspect diagnostics and check storage before processing. Allocation failures propagate as C++ exceptions.

## Export and fidelity

`write_obj` exports this document subset to a deterministic OBJ/MTL text pair. The OBJ references `materials.mtl`; MTL bytes and material ordering remain unchanged. Face order, loops, positions, missingness, object/group membership, smoothing and material bindings are retained. UV/normal pools are expanded per authored corner, so file indices and unreferenced UV/normal records are not preserved. Empty `usemtl` clears a previous assignment for this adapter; some external OBJ tools may use a different unbound-material convention.

Only the five named adapter channels above are supported. Other channels, including extra UV sets, skins, custom attributes and vertex colors, cause export failure before any text is returned. An absent channel means no UVs/normals, unbound materials, default part or smoothing off. Provided channels must match the documented domain/type/width/set, be dense and have no metadata; presence is supported only for corner UVs/normals. Every authored floating value must be finite. Part/material references must be valid, names must not introduce directives/comments, and material names must agree with the retained MTL text.

Double values use classic-locale decimal text at `max_digits10`. Before returning success, the writer reloads its text pair and verifies discrete structure and supported semantics. For each scalar, require absolute difference at most `8 * epsilon(double) * max(1, abs(source))`; positions use the same coordinate units as input, UVs/normals are dimensionless. A different unit/scale policy is not implied. Unsupported or out-of-bound reload results return no text. Tests include non-dyadic decimals and extreme magnitudes; simple dyadic fixtures alone are insufficient proof. This bound concerns text round trip, not geometric repair or triangulation.

No single-UV/four-influence limit is imposed on geometry storage. The OBJ writer's channel limit is format-specific; broader adapters must define their own capabilities. Export text success does not mean resource resolution or file publication completed.

## Dependencies and native consumption

C++20, CMake 3.24+, installed `MeshvaleGeometry` snapshot `0.0.0` from revision `848fe6b4fb3300a2feb7db53ccd703a644eb7aa8`, and tinyobjloader `2.0.0rc13` with its `double` feature are required. [vcpkg.json](../vcpkg.json) pins the registry and version. The build consumes installed dependencies and does not fetch source directly. Tinyobjloader types stay out of the public headers. Dependencies retain their own licenses; see [THIRD_PARTY.md](../THIRD_PARTY.md).

Use vcpkg manifest mode through its CMake toolchain, with tool locations provided through environment/configuration rather than repository paths. Install/export target `meshvale::interchange` and a separate native consumer will be tested before a release. Snapshot versions are not a released compatibility promise.

## C++ header interface

Use `<meshvale/interchange/obj.h>`, the self-contained C++20 interface. The former `obj.hpp` forwarding include has been removed; existing source callers must update their include path. Public types, functions and behavior retain their contracts. Non-template operation and file-helper implementations are compiled from `.cpp` files; installed header checks and separate native consumers verify the `.h` interface. Development snapshots do not promise a stable binary ABI.

The documented Geometry producer provides the compiled owned `PositionBuffer` interface. The build checks that seam because native development version `0.0.0` alone does not identify a compatible source interface, and verifies retained notices against the installed producer. Geometry keeps Eigen private: consuming this installed package requires neither Eigen headers nor a source checkout. Native installs and wheels include the producer's retained Eigen license and attribution files.
