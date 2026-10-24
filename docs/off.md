# Indexed ASCII OFF polygon interchange

| Field | Value |
|---|---|
| ID | OFF-IO-001 |
| Version | 0.1.0 |
| Status | Implemented development interface; qualification in progress |
| Owner | Supplied-byte base OFF admission, canonical ownership and serialization |

This module selects an explicitly bounded ASCII OFF profile through
`meshvale/interchange/off.h` and exported `meshvale::off` in the existing installed
`MeshvaleInterchange` package. It uses the matching Geometry materialization and
execution producer, with existing package dependencies and no additional SDK.
Binary OFF, richer attributes, filesystem capture/publication, Python, commands
and supported releases have separate gates.

## OFF-WIRE-001: Base ASCII records

The upstream [Geomview OFF description](https://sources.debian.org/src/geomview/1.9.5-5/doc/geomview.html/OFF.html)
defines indexed polygon records and optional richer payloads; its
[common syntax](https://sources.debian.org/src/geomview/1.9.5-5/doc/geomview.html/Common-syntax.html)
also permits whitespace and comments. The policies below deliberately select a
narrower Meshvale profile. They are not a claim to admit every valid OFF file.

Require exact uppercase `OFF` alone on the first logical data line, then one
count line containing unsigned decimal vertex, face and edge counts. Each vertex
occupies one data line with exactly three finite decimal/exponent coordinates.
Each face occupies one data line containing its unsigned corner count followed
by exactly that many zero-based vertex indices. Counts/indices have no signs;
check uint64 and host extents before use. Require at least three corners per
face, bounded per-face/total counts, and indices below the declared vertex count.

Space and tab separate fields. Accept CR, LF and CRLF line endings, blank lines,
and `#` comments to line end, including after a complete record. Comment content
is bounded printable ASCII plus tab; the marker itself is not comment content.
The final data line need not have a line ending. No BOM, NUL, non-ASCII payload,
references, braces, omitted marker, wrapped records or concatenated objects are
admitted. Missing/truncated records and invalid integers/numbers are malformed.
Trailing noncomment data is refused without a partial owner.

Reject prefixed forms such as `COFF`, `NOFF`, `CNOFF`, `STOFF`, `4OFF`, `nOFF` and
the `BINARY` family as unsupported variants. Extra vertex fields and face-color
suffixes are unsupported payloads; never silently discard them or consume them
as another record. Lines, points and faces with fewer than three corners are
outside this polygon profile. Source, line, token and comment byte caps are
inclusive and enforced while scanning.
The comment cap applies to each comment's content, excluding `#`; the line cap
applies to each physical line, excluding CR/LF. The source cap bounds aggregate
input independently.

The required edge count is syntactic provenance only. Retain its unsigned value;
do not allocate or iterate from it, infer connectivity, or certify it. Large
representable edge counts alone are not a resource request. Canonical writing
explicitly emits zero as an unknown edge count, including after Repair.

## OFF-GEOMETRY-001: Indexed polygons without conversion

Create a charged canonical `geometry::MaterializedMesh` directly from binary64
positions, complete polygon offsets/indices and no attributes, then retain a full
successful storage inspection. Keep original supplied bytes and declared edge
count in an immutable `OffDocument`. Do not detour through a legacy mutable Mesh
or retain a permanent second decoded geometry copy.

Preserve vertex row order and every unused row, shared vertex IDs, face order,
each polygon's starting corner and winding. Mixed triangles/quads/n-gons,
concave/nonplanar polygons and finite repeated/coincident vertices are retained
when canonical storage is valid. No welding, vertex compaction, rotation,
triangulation, normal synthesis or geometric repair occurs. Storage inspection
does not certify planarity, area, intersections, manifoldness or closed volume.
Units are unknown; do not rescale. Empty and point-only meshes are admitted when
their complete canonical storage is valid.

Numeric syntax is locale-independent decimal/exponent with optional sign for
coordinates. Reject hex, NaN, infinity and conversion overflow/underflow reported
as out of range; do not replace finite nonzero values with zero. Preserve signed
zero and all admitted finite binary64 bits. ASCII output uses sufficient decimal
digits for exact binary64 reload. Qualify extremes and subnormals explicitly.
Lexical scans observe byte checkpoints; a standard-library numeric conversion
checks stop immediately before and after one admitted token and is indivisible
within its configured `max_token_bytes` bound.

## OFF-WRITE-001: Explicit canonical text

`WriteOff` accepts a strong canonical mesh, explicit canonical-ASCII rewrite and
unknown-edge-count-zero policies, fresh positive limits/work policy and stop
token. Invalid or missing policies refuse before encoding. Inspect the actual
strong source under its retained shared execution context. Every face must have
at least three corners and all positions must be finite. Refuse every source
attribute, including empty channels and authored metadata; there is no drop flag.

Serialize all supplied vertices and polygon indices in their original order.
Return complete owned bytes and facts recording actual vertex/face/corner counts,
base ASCII profile, canonical lexical rewrite, zero unknown edge count and
identity correspondence at this supplied-mesh serialization boundary. Identity
means output ordinal i maps to supplied ordinal i in each domain; redundant
identity arrays are not required. The facts do not claim correspondence through
an earlier edit or source/candidate equivalence.

Canonical output omits comments and original whitespace/numeric spelling. The
required rewrite policy makes that lexical change explicit; original bytes remain
available from the document. If optional source-document provenance is supplied,
record its declared edge count without claiming source text preservation. A writer
does not retain caller options, source document, mesh or a fresh inspection after
completion. Its output remains usable after those caller owners/context die.
Serialization alone is not an independently reloaded or published file bundle.

## OFF-OWNERSHIP-001: Bounds, cancellation and failure

`ReadOff` takes borrowed immutable bytes, required limits/work policy, shared
execution context and fresh stop token. Both synchronous functions return typed
status/diagnostic, actual execution observations and either one complete immutable
owner or no owner. Preserve delegated Geometry stage/status/diagnostics, including
genuine unsupported allocation profiles. All-empty numerical storage follows
Geometry's actual empty-input admission rather than an automatic profile skip.

Limits are positive inclusive bounds for source/output bytes, vertices, faces,
total/per-face corners, line/token/comment bytes, codec fresh requests, delegated
materialization/inspection and total declared fresh-request allowance. Check all
products, host extents and aggregate cap sums before allocation. Codec owner/
control storage, retained source/output, temporary decode and allocator overhead/
overlapping growth are charged. Geometry caps remain separate live subsets;
copies are not charged twice and these are not process RSS bounds. Writers use
the strong source ledger and cannot bypass retained headroom through a new one.

Reuse the existing capped allocator/worker implementation and original backend
lifetime contract. Borrowed backend state outlives its last block. Owner copies
share immutable storage without allocation; last release frees real original
blocks. Borrowed views require a live unreassigned owner; independent copies may
be read concurrently. Temporary decoded storage is released before return.

Serial codec work reports zero codec workers; delegated worker statistics remain
actual. Check pre-stop, bounded byte/token/record loops and the final noalloc
handoff. Any worker path reserves through the shared ledger and joins every
started worker on every exit. Expected malformed/unsupported/limit/budget/
cancellation/storage/delegated failures return no partial owner. Unexpected
exceptions propagate after cleanup and joins. Cleanup does not allocate or check
cancellation. Successful serialization has no filesystem side effect.

## OFF-ACCEPTANCE-001: Independent codec and operation evidence

Use literal wire/geometry oracles for empty, point-only and mixed polygons,
unused/shared vertices, winding/start order, repeated IDs, numeric bits, comments
and edge-count provenance/canonicalization. Prove unsupported variants/channels,
malformed/truncated/range controls, exact caps and one-less refusals, real allocator
failures, occupied shared headroom, cancellation and owner use after original
bytes/options/context destruction. Observe caller numerical modes before fixture
restoration. Run installed public headers, normal/sanitizer and actual applicable
qualified/unsupported profiles with source/compiler/dependency/notice identities.

A separate installed workflow may compose the actual existing charged Repair
operation with this codec. A cyclic duplicate quad plus unrelated triangle must
produce independently expected complete face/corner maps, preserve all vertex
rows, and write/reload the real accepted candidate. Unchanged, reversed-winding
and equal-coordinate/different-ID controls distinguish the operation's equivalence
from geometric welding. Codec identity maps never replace Repair provenance.
Interchange acquires no Repair dependency; file publication, reports, Python and
CLI/batch workflows require their own contracts and acceptance.

## Installed use

Follow the [native build instructions](../ENVIRONMENT.md) to install matching
Geometry, tinyobjloader and Interchange packages. The public [header](../include/meshvale/interchange/off.h)
defines required limits, work policies, owners, results and writer policies.
Consume the exported target with `find_package(MeshvaleInterchange CONFIG REQUIRED)`
and link `meshvale::off`. It is part of the base native package; optional glTF
workflow modules need not be enabled.

The [codec consumer](../examples/off-consumer/main.cpp) reads supplied bytes,
inspects complete polygon storage and writes/reloads physical output. It runs in
the parent [installed consumer build](../examples/consumer/CMakeLists.txt). The
standalone [existing-Repair example](../examples/off-repair-consumer/README.md)
additionally needs a matching installed Repair package. Its checked correspondence
comes from the actual Repair operation; file I/O remains caller-owned example
code. Neither example establishes a verified file publisher or stable release.
