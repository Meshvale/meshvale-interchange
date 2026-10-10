# Changelog

## Unreleased

### Added

- Portable development wheel candidates for ordinary CPython 3.13 on Linux x86_64 manylinux_2_28 and Windows x64, with independent source-archive rebuilds, dependency inspection and clean installed OBJ checks. Candidates remain CI artifacts rather than supported releases.

- Native OBJ/MTL text import/export preserving mixed polygon loops, corner UVs/normals and missingness, material bindings, object/group membership and smoothing.
- Reload verification before export success, explicit unsupported-channel rejection and raw storage diagnostics.
- Resource-aware OBJ file import retaining multiple MTL locations and referenced texture bytes inside an explicit resource root.
- Verified new output bundles with no-replace publication, cancellation/failure cleanup and optional supplemental files.
- Pinned double-precision parser dependency, CMake installation and separate installed text/file consumers.
- Optional Python OBJ text/file/bundle interface with owned Geometry snapshots, frozen asset/resource values, cooperative cancellation and publication callbacks. Candidate source archives and platform-specific wheels retain dependency notices; CLI, additional formats and releases remain forthcoming.

- Verified-content callback for computing receipts from reloaded assets and exact staged bytes, with checked supplemental files included in the same no-replace publication.

### Changed

- The exact Python/native Geometry source requirement now uses the tested portable candidate `a27cae686ea4f4c6dabeebfc984f0265602e3b1d` (`0.0.1.dev35+ga27cae686` for Python), preserving existing mesh records and OBJ behavior.

- Geometry evaluation/runtime pin uses the native pooled-editor candidate while retaining the existing immutable mesh record protocol and OBJ behavior. Python CI builds the Geometry native package before installing it, including its compiled editing target.

- Geometry evaluation/runtime pin uses the verified canonical-header candidate; public names and OBJ behavior retain their existing contracts.

- Canonical C++ headers use `.h`, with legacy public `.hpp` forwarding includes retained for source compatibility; native builds disable language extensions and compile each installed header independently.

- Geometry evaluation/runtime pin now uses the report-capable public candidate documented in the package and adapter contracts.
