# Checked retained glTF topology output

| Field | Value |
|---|---|
| ID | GLTF-TOPOLOGY-WRITE-001 |
| Version | 0.1.2 |
| Status | Qualified native development candidate; no stable API/ABI or release |
| Interface | [gltf_topology_write.h](../include/meshvale/interchange/gltf_topology_write.h) |

This contract selects supplied-memory output for an ordered triangle subset of
one [materialized primitive](gltf-materialization.md). It retains original vertex
accessors, raw channels, resources and graph identities. The
[reader/extractor](gltf-read.md) owns source admission and static-local processing
capability; the [unchanged writer](gltf_write.md) remains a separate operation.
This writer verifies preservation and selection, not the caller's reason for
removing a face. The separate native target provides the supplied-memory
operation. It is a development interface with the qualification boundary below;
no stable release is established.

The opt-in [direct translation profile](gltf-translation.md) requires a genuine
retained source proof and revalidates it under explicit writer limits/work before
checked output. Results and successful output owners retain actual validation
facts; the [installed memory example](../examples/consumer/gltf_translation_output.cpp)
demonstrates the native path under qualification. The current static writer
guard and defaults remain unchanged; suppressing a capability refusal is not
animation-preserving output.

## GTW-INPUT-001: Authority and candidate

`GltfTopologyWriteRequest` borrows a strong `GltfMaterializedPrimitive`, a
Geometry `MeshInspection`, and output-to-source face/corner spans until return.
The supplied inspection's `Source()` is the immutable candidate. Its
`IsStorageValid()` must be true; otherwise the fixed diagnostic retains its
first actual typed issue. This operation does not repeat inspection or invent a
delegated Geometry status. The Geometry
[materialization contract](https://github.com/Meshvale/meshvale-geometry/blob/1a364acb0cba4fbf8cd9cccde5c04a1fa4428536/docs/materialization.md)
owns inspection validity and canonical storage.

Source and candidate must share the public execution budget, tested through
`ShareExecutionContext()` and `SharesExecutionBudget()`. An equivalent policy on
a different ledger is foreign. The source's asset, primitive, extraction, channel
mappings and canonical mesh provide the retained identity; shared accessors or
caller-supplied ordinals cannot substitute another primitive. Existing source
capability restrictions remain in force, including selected skin/morph/affecting
animation and extension effects. This operation grants no new admission.

The [reader/extractor](gltf-read.md#typed-primitive-output) owns binary admission.
An immutable source asset can still report `kBinaryAdmissionPending` after its
successful extraction: the extraction retains the completed binary proof
without rewriting that asset's preliminary capability. This writer accepts
that state only through the required strong `GltfMaterializedPrimitive`, which
retains the admitted extraction, or the existing `kReady` state. Other capability
reasons remain unsupported; retaining an asset alone cannot bypass extraction.

The candidate has exactly the same vertices and ordered channel declarations as
the source. Compare complete canonical position bits and every attribute's
scalar alternative, domain, name, semantic, set index, component width, optional
ragged/presence representation, ordered metadata and raw scalar bits. Descriptor
bytes include names, semantics and metadata keys/values. All source attributes
are raw dense vertex channels under the existing transfer profile; unsupported
changes to any declaration or row reject, even on unused vertices. Signed zero
and subnormal bits cannot be replaced by numerical equality. int8/int16 source
channels remain exact widened int32 in canonical storage. Original raw encodings,
normalization flags/derived values, bounds, sparse and interleaved layouts stay
with the retained extraction; this writer neither narrows nor regenerates them.

## GTW-MAP-001: Exact directed selection

The candidate consists only of whole source triangles in ascending source-face
order. `source_faces` has one entry per output triangle; entries are strictly
increasing and in range. `source_corners` has one entry per output corner and
must equal the selected canonical source-face corner at the same local offset.
These are output-to-source maps, not many-to-one original-to-candidate Repair
maps. Verify the actual candidate offsets and directed vertex loops against the
source extraction independently of caller maps. Preserve winding, legal
degenerates, every unused vertex and material binding. No added, reversed,
reordered or modified triangle is admitted.

Build encoded indices from the verified authoritative extraction, never through
floating point or from unchecked maps. Values must fit legal UINT32 indices and
exclude the primitive-restart sentinel. `OriginalCorners()` composes the checked
canonical corner map with extraction `SourceCorners()`; each returned
`GltfSourceCorner` retains original primitive-stream corner and accessor-row
meaning, including strip/fan conversion.

After full verification an identity map with unchanged candidate is a no-op:
copy the exact original JSON into fresh charged output, retain the original
resource inventory and return no `ChangedBinding()`. A changed selection with
zero faces is explicitly unsupported. It cannot emit a zero-count accessor or
implicitly remove a primitive. The extraction's own restrictions on originally
empty/unextractable input remain unchanged.

## GTW-OUTPUT-001: Append and bind

A changed result appends exactly one buffer, one bufferView and one
SCALAR/UINT32 accessor. The buffer uses canonical padded base64 in an
`application/octet-stream` data URI; decoded bytes are little-endian indices,
without an additional unbound binary container. The accessor has positive
corner count, references the appended view and uses no normalization. Omitted
optional byte offsets/targets/bounds retain their ordinary core defaults; this
operation does not manufacture vertex bounds. Redirect only the selected
primitive's `indices` and its mode to TRIANGLES. An already explicit TRIANGLES
mode can retain its exact token; an absent default mode need not be inserted.
A strip/fan changes to explicit TRIANGLES. All instances of the selected mesh
observe the edit; no mesh/instance cloning occurs.

Existing buffers/views/accessors and their table ordinals remain intact,
including old shared indices used by other primitives. Attributes, material,
mesh/primitive/node/scene identities, unrelated skins/morphs/animations and
resource references are retained. Original resources preserve complete captured
bytes, aliases, logical lengths, URI spellings and trailing bytes. `Resources()`
keeps their existing view order and appends one new buffer resource for a changed
result. Its origin is `kDecodedDataUri`, describing the emitted representation,
not an earlier caller binding. The new URI view and decoded index bytes are
owned by the bundle. No resource is removed, compacted or silently re-encoded.

An operation-owned lexical span index uses the immutable original document,
never the parser projection or substring key replacement. Match decoded core
keys and retained node order while honoring escapes, strings and nesting.
Nonoverlapping ordered splice segments append table records and redirect the
selected binding. Merge same-offset insertions explicitly. Preserve all bytes
outside those declared changes: escaped key spelling, whitespace/member order,
large integer/decimal lexemes, opaque extras and unrelated records. Existing
array-closing whitespace can remain before the inserted comma/record. An absent
core table is inserted at its owning object without disturbing old members.
Opaque extras retain bytes; preservation does not interpret their semantics.

The writer does not eagerly reload its output. Independent public reload and
comparison qualify this operation; later filesystem publication must perform
its own acceptance and transactional finalization. Output handles obtained by
reload are output-bound; `OriginalAsset()` and `OriginalPrimitive()` remain
explicitly original-source handles.

## GTW-OWNER-001: Complete immutable output

Success publishes a copy-only `GltfTopologyWriteBundle` owning JSON, checked
face/corner maps, composed original-corner provenance, binding annotation and
the new resource/URI. `Source()` strongly retains the original materialized
primitive and its canonical/source ownership. Candidate inspection and caller
map spans are not retained or needed after return. Original inputs, candidate,
caller context wrapper and capture token can then be destroyed. Independent
bundle copies share storage and charges without allocation. Borrowed spans and
references require an alive, unreassigned visible bundle copy; concurrent reads
of independent copies are supported. Reassignment/destruction of the same
wrapper requires external synchronization. Cleanup allocates nothing.

`ChangedBinding()` is absent for no-op; otherwise it reports original mode and
optional index-accessor identity, output TRIANGLES mode/new index accessor and
appended buffer/view ordinals. The new index accessor is also the appended
accessor ordinal. These declarations describe conversion without altering the
original source handle. A copied-out source owner retains source storage;
output JSON/resources still require an output bundle owner.

## GTW-LIMIT-001: Positive physical limits

Every limit and work field is required, positive and host-representable; zero
never means unlimited. Limits are inclusive. Check counts/extents before byte
traversal or allocation. `max_source_json_bytes/nodes/depth` bound original
lexical input. `max_vertices` bounds source/candidate vertex counts;
`max_channels` includes POSITION. `max_attribute_scalars` and
`max_descriptor_bytes` bound each source/candidate total separately, including
all backing rows and descriptor text. `max_candidate_faces/corners` bound output
maps and geometry. `max_index_bytes` bounds decoded new index storage; checked
multiplication/base64 extent and JSON length precede allocation. Structural
source/candidate mismatches still reject when within these caps.

Nested `GltfWriteLimits` owns output JSON/resource counts and full resource byte
totals, including each exposed alias, as in the unchanged writer. Its
`max_output_requested_bytes` is the single complete fresh writer request cap.
There is no hidden second inspection or read/extraction cap. No-op still
validates all input/correspondence and consumes its real fresh owner/maps/JSON
requests. Configuration fields too large to represent are invalid configuration;
input/output count or representational extent excess is a typed limit failure.

## GTW-ACCOUNT-001: One fresh cap and common ledger

The existing [Interchange allocator](gltf-foundation.md) admits each actual
request before its original allocation call, including seed/control/rebound
blocks, header/padding, lexical index/stack, descriptors, splice plan, maps,
JSON, decoded indices, URI storage and explicit callable/worker/scratch records.
Replacement overlap counts while both blocks live. All these new requests share
one operation cap; insufficient seed allowance is a writer-request limit.
Preexisting source, candidate inspection and unrelated operation charges are
outside this fresh cap but remain on the shared execution ledger. Sharing an
input owner adds no second charge. Either cap can refuse before allocation.
Opaque OS thread/CRT internals outside explicit Meshvale records are excluded;
this is not total process memory or RSS.

`NewRequestedBytes()` reports current live fresh requests after success; freed
scratch is no longer included. It is not cumulative traffic or a peak
measurement. Destruction frees each original allocation before releasing its
charge and ignores stop. Output copies can outlive the original source/context
wrappers while the retained ledger and owners remain valid.

## GTW-EXECUTION-001: Ordered plan and substantial workers

Ordered descriptor/count admission and prefix/range assignment over
already-derived fixed-width records use a serial one-pass O(record count) plan
to establish unique output ranges before payload work. Count caps do not imply
small workloads. The dependent escape-aware lexical scan must preserve nesting,
parent/child order and exact token boundaries, so it is serial with bounded byte
checkpoints; splice ordering is O(splice count log splice count). It does not
build an uncharged mutable DOM or alter reader capture layout.

Substantial scalar/descriptor-string/map verification, index encoding, base64
encoding and output copying use byte-aware disjoint jobs on the source's shared
worker budget. Few faces or a single long channel/JSON segment do not exempt
large work. Base64 job boundaries respect complete input groups and output
ranges. `parallel_min_bytes`, `partition_bytes`, `cancellation_block_bytes` and
`max_jobs` are caller policy. Occupied slots or small work may use caller
fallback. `execution` separately reports actual planned/caller jobs and native
launch/join counts for verification, index encoding, base64 and output copy;
planning estimates never stand in for launches. All started workers join before
return or exception, including partial launch/cancellation. Slot and storage
leases stay alive until joins finish.

Failure phases run in this order: configuration, physical counts and checked
extents, execution-ledger identity, supplied inspection validity, source
capability, face-map cardinality, corner-map cardinality, face-map order/range,
canonical position/channel preservation, then candidate triangle shape,
corner-map values and directed loops in output face/corner order. Changed-empty
selection rejection follows verification; assembly follows that decision.
Within preservation, position rows precede ordered attribute declarations and
scalar rows. A valid-storage quad with one face-map entry and three corner-map
entries fails with `kInvalidCorrespondence` / `kCornerMapShape`; a four-entry
corner map instead reaches the `kTriangleShape` failure. Each phase completes
before later phases can publish a diagnostic.

The supplied fresh token controls admission, descriptor/byte blocks and final
publication. Stopping the original capture/extraction token cannot cancel this
call. Within each ordered verification phase, deterministic first failures use
source position order, then source attribute/descriptor/scalar order, then
output face/corner order. A discovered failure is not replaced by later stop;
when needed, joined cancellation finalizes only the earlier unexamined prefix
without allocation or new worker launch. Cancellation can prevent discovery of
later defects. Actual runtime exceptions take priority after joins/rollback;
expected diagnostics do not allocate strings or expose partial output.

## GTW-FAIL-001: Typed outcomes

| Status | Meaning |
|---|---|
| `kInvalidConfiguration` | Required field zero or configuration extent not representable |
| `kLimitExceeded` | Physical/output count, checked extent or complete writer request cap exceeded |
| `kForeignBudget` | Candidate and source execution ledgers differ |
| `kInvalidCandidateStorage` | Supplied candidate inspection contains issues; first actual issue retained |
| `kInvalidCorrespondence` | Face/corner map shape, ascending order, range or exact source-corner value fails |
| `kPreservationMismatch` | Canonical positions, channels/declarations or directed triangle loops differ |
| `kUnsupportedProfile` | Selected primitive lacks the existing static-local processing capability |
| `kUnsupportedSelection` | Changed zero-face output cannot be represented by this increment |
| `kSourceContractFailure` | Retained source descriptor/provenance or lexical correspondence is inconsistent |
| `kBudgetExceeded` | Shared execution ledger refuses a fresh request |
| `kCanceled` | Fresh stop observed before a prior failure determines the outcome |

Every expected failure has no bundle and a fixed stage/code with optional
attribute/element/JSON span/actual storage issue. No delegated Geometry failure
is fabricated from a caller-supplied inspection. Allocation/backend failures
retain the allocator's exception behavior after noallocating cleanup and joins.

## GTW-BUILD-001: Native target and dependency

`MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE` defaults to `ON` for native builds.
It adds the installed static target `meshvale::gltf_topology_write` and public
[header](../include/meshvale/interchange/gltf_topology_write.h). Configuration
requires callable compiled `MaterializedMesh::ShareExecutionContext()` and
`SharesExecutionBudget()` from the documented
[Geometry producer](https://github.com/Meshvale/meshvale-geometry/tree/1a364acb0cba4fbf8cd9cccde5c04a1fa4428536),
plus the existing [materialization dependencies](gltf-materialization.md).
The configure-time link probe rejects a producer missing those symbols; it
does not silently disable the module or select a different ledger.

Follow [ENVIRONMENT.md](../ENVIRONMENT.md) for compiler, dependency prefixes,
vcpkg and build/install commands. Enable this module explicitly with
`-DMESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE=ON`. Native and installed test
configurations must use the same explicit
`MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE=qualified` or `unsupported`
value for the actual Geometry producer. That setting selects expected test
outcomes; it does not admit a numerical profile. Geometry's
[compiled allocation-profile contract](https://github.com/Meshvale/meshvale-geometry/blob/1a364acb0cba4fbf8cd9cccde5c04a1fa4428536/docs/materialization.md#compiled-allocation-profiles)
owns that admission.

An installed C++20 consumer links the target directly:

```cmake
find_package(MeshvaleInterchange CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE meshvale::gltf_topology_write)
```

The [installed operation example](../examples/consumer/gltf_topology_write.cpp)
composes public Read, Extract, Materialize, face selection, inspection, Write
and a separate reload. It uses no Repair, Eigen or private Geometry interface.
The [link example](../examples/consumer/gltf_topology_write_link.cpp) checks the
installed target without fabricating a nonempty numerical producer on an
unsupported profile.

Explicit `OFF` omits this module's target, header installation and writer tests;
the existing reader, unchanged writer and materialization targets keep their
own availability. The current [Python build](python.md) explicitly sets this
option `OFF`, retains its existing dependency cohort and exposes no glTF
topology writer binding.

## GTW-QUALIFICATION-001: Development evidence and remaining gates

The local Windows x64 MSVC Release evaluation passed all 17 native suites and
9 consumers built and executed against a relocated installed prefix. The writer
fixtures execute independent Read -> Extract -> Materialize -> candidate
inspection/selection -> Write -> Read -> Extract paths. They cover exact no-op,
changed-empty refusal, triangles/strip/fan, shared accessors/instances, escaped
keys/opaque lexemes, full resource bytes, raw normalized/sparse/interleaved
channels, signed extrema/zero/subnormals, unused vertices/degenerates, both maps
and original stream provenance. Rejected bytes/declarations, foreign ledgers
and malformed maps publish nothing.

The qualified native inventory includes a separate required large-resource test
with 16,777,218 physical vertex rows and a new UINT32 index of 16,777,217. The
default unfiltered CTest invocation executes it with an explicit 2048 MiB ledger
setting. `RUN_SERIAL` prevents concurrent tests; the writer's payload workers
remain active. No float surrogate replaces that index. Focused tests observe
complete original allocation requests and their peak, exact-peak and one-byte-
lower cap outcomes, every request-ordinal fault, shared-ledger saturation with a
prior output alive, fresh/old tokens, real joined workers in all four payload
phases, partial launch and deterministic first failure after late cancellation.
These observations are separate from RSS or cumulative allocation traffic.

The [tested development revision](https://github.com/Meshvale/meshvale-interchange/commit/2119506a38cf75955ed25a66e9f34fe7c1db3a58)
passes [native and package checks](https://github.com/Meshvale/meshvale-interchange/actions/runs/38158711623)
and [PR checks](https://github.com/Meshvale/meshvale-interchange/actions/runs/38158714986).
Qualified Windows x64 MSVC Ninja Release and Linux x64 Clang 18.1.3 Debug
builds execute all 17 native and 9 installed tests. Matching Windows ASan,
Linux ASan/UBSan and real-worker TSan lanes likewise execute their required
suites; GNU and AppleClang unsupported profiles verify explicit refusal and
installed linking. Git-free source packaging preserves all 173 selected source
files plus two generated metadata files, and a fresh build from that package
executes all 17 native and 9 relocated installed tests. These bounded results
do not establish stable API compatibility, Python writer bindings, every
compiler configuration or a supported release. No performance guarantee is made.

This increment excludes arbitrary channel/vertex mutation, selected
skin/morph/animation processing, GLB asset conversion, filesystem publication,
Python bindings and reported Repair API/single/batch command composition. Those
capabilities require their own qualified contracts. No Repair, private Geometry,
Eigen or parser interface is part of this public header.
