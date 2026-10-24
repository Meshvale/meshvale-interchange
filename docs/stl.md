# Native STL triangle interchange

| Field | Value |
|---|---|
| ID | STL-IO-001 |
| Version | 0.1.1 |
| Status | Development implementation under qualification |
| Owner | Supplied-byte ASCII/binary STL admission, canonical ownership and serialization |

This module reads and writes an explicitly selected STL encoding through
`meshvale/interchange/stl.h`. The exported `meshvale::stl` target belongs to the
existing installed `MeshvaleInterchange` package and uses its matching Geometry
materialization/execution producer. There is no new SDK, parser dependency or
separate package. Existing package configuration dependencies remain applicable.
Filesystem capture/publication, Python bindings, application commands and a
supported release are separate work.

## STL-WIRE-001: Explicit encodings and finite profile

`ReadStl` accepts borrowed supplied bytes, an explicit ASCII or little-endian
binary encoding, required limits/work policy, a shared Geometry execution
context and a fresh stop token. It returns a typed status/diagnostic, execution
observations and either one complete immutable `StlDocument` or no document.
Unknown enum values and invalid configuration fail before parsing/allocation.
No filename or `solid` prefix guesses the encoding.

The ASCII profile admits one lowercase `solid` block, ordered `facet normal`,
`outer loop`, exactly three `vertex` rows, `endloop`, `endfacet` and a matching
`endsolid` name. Empty solids are allowed. Keywords are case-sensitive; token
whitespace is ASCII space, tab, CR or LF. Opening/closing names are bounded
printable ASCII, exclude their line ending and must match exactly; one optional
space after the keyword is the name delimiter. Names can be empty. Numeric
tokens use locale-independent decimal/exponent syntax, including an optional
leading sign; hexadecimal tokens, NaN, infinity, comments, attribute extensions
and concatenated solids are outside this profile. Reject trailing non-whitespace.
Source, line, token and name byte limits are inclusive and checked while scanning.

Binary has an opaque 80-byte header, little-endian uint32 facet count and 50
bytes per facet: twelve binary32 components and one uint16 attribute word.
Validate `84 + 50 * count` with checked arithmetic before reading/allocating
records. Truncation is malformed; extra bytes and nonzero attribute words are
unsupported variants. Retain the complete header without interpreting material,
color or units. Color dialects and variable attribute extensions are not mapped.

These are Meshvale admission policies. Wire references are the
[IANA registration](https://www.iana.org/assignments/media-types/model/stl) and
the historical 3D Systems manual reproduced in
[EP0747203A2, pages160–162](https://data.epo.org/publication-server/rest/v1.2/patents/EP0747203NWA2/document.pdf).
Manufacturing recommendations in historical sources are not mesh-validity
claims made by this codec.

## STL-GEOMETRY-001: Faithful triangle soup

Facet and corner order, winding, finite degenerate triangles and duplicate-looking
facets are retained. Import creates three distinct vertex identities per facet;
face i has corners/vertices `3*i`, `3*i+1`, `3*i+2`. There is no position comparison,
welding, deduplication, triangulation or inferred connectivity. Importing two
coincident facets therefore creates six vertices, not one indexed shared triangle.
Existing Repair duplicate equivalence still requires the same vertex-index loop;
this codec does not establish successful duplicate removal for STL triangle soup.
An explicit indexing/weld conversion would require a separate contract and maps.

Build a charged canonical `geometry::MaterializedMesh` directly, with complete
inspection. Do not create an uncharged legacy Mesh or retain a second permanent
decoded geometry copy. Canonical positions and imported facet normals are
binary64. The normals form one dense face-domain three-component attribute,
named `facet_normal` with semantic `normal`, no set index, offsets, presence
mask or extra metadata. Empty input retains the same empty normal-channel shape.
Original bytes, encoding and header/name remain strongly owned separately.

Finite zero, nonunit and opposed facet normals are preserved without
normalization, regeneration or winding changes. A facet normal is not three
authored corner normals. Normal correctness, manifoldness, closed volume,
intersections and print readiness are not certified. Units remain unknown and
coordinates are never rescaled.

## STL-NUMERIC-001: Precision without silent quantization

Binary32 values widen exactly to canonical binary64, including signed zero and
subnormals, independent of caller rounding or subnormal-flush modes. Exact
binary32 admission/encoding uses bit representations without changing the
caller's floating-point environment. ASCII decimal conversion is bounded, locale-independent binary64;
invalid syntax, nonfinite results and out-of-range conversion fail explicitly.
Do not silently replace overflow/underflow by infinity/zero. Preserve finite
negative coordinates and signed zero; do not inherit historical positive-only
manufacturing restrictions.

ASCII serialization uses enough decimal digits for exact binary64 reload and
preserves signed zero. Binary serialization requires every coordinate and normal
to be exactly representable as finite binary32, including its zero sign. Otherwise
return unsupported numeric conversion with no output. Approximate narrowing,
tolerance-based equality and normal generation are not part of this version.
Canonical ASCII serialization preserves values and ordering, not original
whitespace or numeric spelling; original wire is available from the document.

## STL-WRITE-001: Explicit representational conversion

`WriteStl` accepts a strong canonical `MaterializedMesh`, explicit output encoding,
destination metadata, face-normal attribute selection and a mandatory corner-
expansion acknowledgment. It derives the actual shared execution context from
that mesh and takes fresh limits/work/stop inputs. Caller borrowed metadata must
remain alive until the synchronous operation and all joins complete.

Every face must have exactly three corners. Select exactly one dense finite
three-component face-normal channel with float32 or float64 values and supported
normal semantics. Missing normals, invalid storage, ragged/presence metadata,
other attributes, UVs, colors, materials, vertex/corner normals and unrepresented
channel metadata are refused. No generic attribute-drop or automatic normal
synthesis flag is provided.

Serialization retains source face/corner order and expands every indexed corner
to a wire vertex. The explicit acknowledgment covers loss of shared vertex
identity and omission of unused input vertices. Return actual source vertex/face
counts and an owned emitted-corner-to-source-vertex mapping; face/corner order is
identity. Never present this as topology-preserving indexed round trip. The
output owner remains usable after caller mesh/context/options destruction.

Output metadata is always explicit: exactly 80 binary header bytes or one bounded
ASCII name. Same-encoding callers can pass the retained source metadata. No
implicit truncation or transfer of a binary header into an ASCII name occurs.
Cross-encoding serialization reports replacement of source-only metadata when
source-document provenance is supplied; otherwise no source-header preservation
is claimed. The result records its actual encoding, normal selection, expansion
and metadata policy; it does not invent a shared report/1 receipt.

## STL-OWNERSHIP-001: Bounds, execution and failure

Every configured limit is required and positive. Limits cover source/output,
facets, vertices/corners/offsets, tokens/names/lines, codec allocations, delegated
materialization/inspection and total declared fresh-request allowance. Check all
cardinality products, host representations and the sum of required operation
caps before work. The codec cap includes its owner/control storage, retained
source/output/map bytes, decode scratch, allocator overhead and overlapping
growth; delegated Geometry caps remain explicit. Actual live operation subsets
are separately attributable, not a sum of shared copies or a process RSS bound.

Use the existing Geometry execution ledger and private capped allocator/worker
mechanisms. A writer keeps the input mesh's ledger; it cannot bypass retained
headroom through a new unrelated context. Original borrowed allocation backend
state must outlive its last block, following the existing native contract.
Strong document/output copies share immutable storage and charge until last
release; borrowed views require a live unreassigned owner. No fallible work or
allocation is added to owner-copy/destruction/handoff paths.

Serial codec loops are an acceptable first implementation with honest zero
launched/joined codec workers. Work policy defines bounded positive byte/token/
facet cancellation blocks; delegated Geometry work retains its actual counters.
Any parallel codec path must reserve through the existing worker ledger, preserve
deterministic ordering and join every started worker on every exit. Do not claim
parallel execution from configured policy alone.

Observe pre-stop, bounded scan/decode/write checkpoints and the pre-handoff stop.
Lexical scans and name comparisons observe the configured byte blocks. Decimal
conversion observes stop immediately before and after one already-admitted
numeric token; its standard-library conversion is indivisible and bounded by
`max_token_bytes`. Select that limit to bound cancellation latency through a
conversion. No checkpoints inside the standard-library conversion are claimed.
Typed malformed, unsupported, limit, budget, cancellation, invalid-storage and
delegated Geometry failures return no partial document/output. Preserve actual
delegated stage/status/diagnostics, including unsupported numerical/allocation
profiles. Unexpected exceptions propagate after cleanup and joins. Cleanup never
allocates or checks cancellation. No filesystem side effect or atomic publication
promise follows from successful serialization.

## STL-ACCEPTANCE-001: Executable evidence

Independent literal ASCII and packed binary fixtures must prove empty/multiple
facets, no welding, winding/degeneracy, raw normals, negative/signed-zero/subnormal
values, names/headers, numeric conversion and exact output reload. Exercise
malformed/truncated/extra/nonzero-attribute variants, inclusive limits and one-less
refusals, true allocator/ledger failures, source/context/options destruction,
copies/last release, cancellation, concurrent operations and actual worker joins
if used. Writer controls include quads, unsupported attributes, absent normals
and nonexact binary32 values; refusals produce no output.

An installed public-header consumer demonstrates read, inspect, write and actual
reload through the same interfaces. Its own new-path file writes are caller-owned
I/O, not the verified publisher. Source/package contents, notices, producer/CRT
identities, strict compiler flags, independent source reviews and actual applicable
normal/sanitizer/unsupported-profile public jobs precede qualification. Recognition
of a suffix, SDK setup or a parser smoke test does not establish these capabilities.
