# Changelog

## Unreleased

### Added

- Opt-in native JSON glTF file capture through `meshvale::gltf_files`, with
  immutable exact entry/resource bytes, unique path inventory, capped canonical
  asset ownership and joined shared-budget I/O. The installed example retains
  data after source-file removal. GLB capture additionally retains complete
  JSON/BIN/opaque chunks, checks embedded buffer binding and padding, and delegates
  canonical extraction under the shared limits. JSON topology publication stages
  full physical bytes, independently verifies reloaded output and commits a new
  bundle without replacement, including checked static and computed supplements.
  Its installed example deletes the original source and reopens committed bytes.
  GLB publication remains unavailable; the development interface is under
  qualification.

- Additive fully capped glTF supplied-memory read and extraction routes with
  live attributable owner counters. The optional
  `meshvale::gltf_workflow_support` target independently checks canonical data,
  topology correspondence, JSON preservation, resource bytes, reloaded channels
  and source provenance against retained inputs. Public installed examples
  exercise owner lifetime and cleanup. This development API remains under
  qualification; it adds no filesystem publication or Python glTF operation.

- Checked supplied-memory glTF triangle-selection output through
  `meshvale::gltf_topology_write`, with inspected candidate validation, owned
  correspondence and append-only UINT32 index resources. Original vertex/channel
  accessors and unrelated resources remain retained. Native builds require the
  compiled Geometry execution-sharing seam; Python packaging keeps this module
  disabled. Native, installed, source-package and bounded sanitizer checks pass;
  stable interfaces and supported releases remain pending.

- Static raw glTF transfer with explicit operation limits, shared
  Geometry accounting, inspected canonical storage and retained source/channel
  correspondence. The separate `meshvale::gltf_materialization` target requires
  Geometry's materialization target and its compiled numerical allocation profile.
  This does not add edited writing or Python glTF operations.

- Native unchanged glTF memory output preserving exact original JSON bytes and
  full immutable resource identities/bytes through retained ownership, explicit
  operation limits, fresh cancellation and joined shared-budget copy workers.

- Native supplied-memory glTF retention and selected static primitive extraction
  with owner-bound handles, exact original numbers, owned resources, typed raw/
  normalized channels, sparse provenance and triangle strip/fan corner maps.
  The finite processing profile reports unsupported scene effects explicitly;
  edited writing, publication and Python glTF remain separate.

- Native supplied-byte GLB container capture and assembly with owned JSON/BIN/
  opaque chunks, canonical output padding, explicit limits and joined copy
  partitions under Geometry's shared execution budget. Wire-container success
  does not establish glTF asset/resource validity or add Python format support.

- Optional native USD mesh extraction from selected default-time USDA/USDC meshes, preserving polygon loops and supported normal/UV interpolation with owned source tables. The separate SDK-backed target reports unsupported scene features; the default Python package remains OBJ-only.

- Optional native FBX static polygon import with owned source bytes, normal/UV layer tables and axis/unit metadata. The separate target requires a locally licensed Autodesk SDK and reports unsupported scene features; the default Python package remains OBJ-only.

- Private immutable JSON capture retains original bytes, exact number spellings,
  decoded Unicode strings and ordered relationships under charged shared
  ownership. Duplicate keys, malformed input, explicit limits and cancellation
  fail without returning partial data. This does not add a public glTF reader or
  admit glTF metadata, resources or primitive roles.

- Private glTF parser/allocation foundation with SHA256-pinned cgltf/jsoncons inputs, shared Geometry execution accounting and bundled notices. Public asset reading, extraction and writing remain forthcoming.

- Portable development wheel candidates for ordinary CPython 3.13 on Linux x86_64 manylinux_2_28 and Windows x64, with independent source-archive rebuilds, dependency inspection and clean installed OBJ checks. Candidates remain CI artifacts rather than supported releases.

- Native OBJ/MTL text import/export preserving mixed polygon loops, corner UVs/normals and missingness, material bindings, object/group membership and smoothing.
- Reload verification before export success, explicit unsupported-channel rejection and raw storage diagnostics.
- Resource-aware OBJ file import retaining multiple MTL locations and referenced texture bytes inside an explicit resource root.
- Verified new output bundles with no-replace publication, cancellation/failure cleanup and optional supplemental files.
- Pinned double-precision parser dependency, CMake installation and separate installed text/file consumers.
- Optional Python OBJ text/file/bundle interface with owned Geometry snapshots, frozen asset/resource values, cooperative cancellation and publication callbacks. Candidate source archives and platform-specific wheels retain dependency notices; CLI, additional formats and releases remain forthcoming.

- Verified-content callback for computing receipts from reloaded assets and exact staged bytes, with checked supplemental files included in the same no-replace publication.

### Changed

- Canonical numerical attribute access now uses Geometry's Eigen-owned `ScalarBuffer<T>` alternatives, with a compiled-interface configuration check. The matched Geometry producer is `ec38fabd783a578947d43856b0c7a7fd54ce8533` (Python `0.0.1.dev69+gec38fabd7`). Native variant callers must update typed access and rebuild against the matching Geometry producer; owned Python mesh records retain their existing schema and bits.

- Native position access uses Geometry's owned PositionBuffer interface at `920be542502652b1d16c5f90414cec6495ff62b4` (Python `0.0.1.dev57+g920be5425`), preserving canonical mesh records and scalar bits. Linked distributions retain the producer's Eigen notices; older native snapshots are rejected during configuration.

- The optional binding compiles Geometry's installed record implementation through its per-extension factory and links the compiled native package, with exact matched producer requirements; mesh records and operation behavior are unchanged.

- The exact Python/native Geometry source requirement now uses the tested portable candidate `d1b0ec5ddf37d6ff648ddd57fe8c7caefbd78832` (`0.0.1.dev41+gd1b0ec5dd` for Python), preserving existing mesh records and OBJ behavior.

- Geometry evaluation/runtime pin uses the native pooled-editor candidate while retaining the existing immutable mesh record protocol and OBJ behavior. Python CI builds the Geometry native package before installing it, including its compiled editing target.

- Geometry evaluation/runtime pin uses the verified canonical-header candidate; public names and OBJ behavior retain their existing contracts.

- Canonical C++ interfaces use `.h` exclusively; the `obj.hpp` and `obj_files.hpp` forwarding headers are removed. Source callers must update their include paths. Shared non-template file helpers now compile from `obj_file_detail.cpp`, with unchanged OBJ behavior; native and installed builds compile each public header independently.

- Geometry evaluation/runtime pin now uses the report-capable public candidate documented in the package and adapter contracts.
