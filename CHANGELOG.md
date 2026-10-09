# Changelog

## Unreleased

### Added

- Native OBJ/MTL text import/export preserving mixed polygon loops, corner UVs/normals and missingness, material bindings, object/group membership and smoothing.
- Reload verification before export success, explicit unsupported-channel rejection and raw storage diagnostics.
- Resource-aware OBJ file import retaining multiple MTL locations and referenced texture bytes inside an explicit resource root.
- Verified new output bundles with no-replace publication, cancellation/failure cleanup and optional supplemental files.
- Pinned double-precision parser dependency, CMake installation and separate installed text/file consumers.
- Optional Python OBJ text/file/bundle interface with owned Geometry snapshots, frozen asset/resource values, cooperative cancellation and publication callbacks. Candidate source archives and platform-specific wheels retain dependency notices; CLI, additional formats and releases remain forthcoming.

- Verified-content callback for computing receipts from reloaded assets and exact staged bytes, with checked supplemental files included in the same no-replace publication.

### Changed

- Geometry evaluation/runtime pin uses the verified canonical-header candidate; public names and OBJ behavior retain their existing contracts.

- Canonical C++ headers use `.h`, with legacy public `.hpp` forwarding includes retained for source compatibility; native builds disable language extensions and compile each installed header independently.

- Geometry evaluation/runtime pin now uses the report-capable public candidate documented in the package and adapter contracts.
