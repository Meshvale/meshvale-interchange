# Retained glTF workflow support

| Field | Value |
|---|---|
| ID | GLTF-WORKFLOW-SUPPORT-001 |
| Version | 0.1.5 |
| Status | Draft native implementation under qualification |
| Interfaces | Reader/extractor owners: [gltf.h](../include/meshvale/interchange/gltf.h); optional output inspection: [gltf_workflow_support.h](../include/meshvale/interchange/gltf_workflow_support.h) |

This additive contract owns completely capped supplied-memory read and extraction
entrypoints, attributable live read/extraction counters and independent inspection
of a reloaded topology output. The [reader/extractor](gltf-read.md) continues to own source admission,
binary decoding and processing capability. The [materialization](gltf-materialization.md)
and [topology writer](gltf_topology_write.md) contracts own their existing
operations. These declarations do not establish an implemented workflow,
filesystem publication, GLB conversion, Python API or stable release.

The opt-in [direct translation profile](gltf-translation.md) supports retained
source proof and independent comparison of the actual reloaded animation under
explicit inspection limits/work. Results and successful inspection owners expose
actual validation facts. The [installed memory example](../examples/consumer/gltf_translation_output.cpp)
demonstrates changed and identity selections. Static remains the default;
request-counter scopes and the broader qualification boundary remain unchanged.

## GWS-READ-001: Additive complete read cap

`ReadGltfAssetCapped` and `GltfReadOperationLimits` are declared in the base
[gltf.h](../include/meshvale/interchange/gltf.h), independently of optional
materialization/topology-writer interfaces. The entrypoint takes: existing `source` admission
limits and required positive, host-representable `max_new_requested_bytes`.
Its read semantics and `GltfReadResult` remain reader-owned. All source/work fields
retain their existing required meaning. Invalid added configuration reports the
existing invalid-limits diagnostic. Exceeding the fresh request cap, including
insufficient seed space, reports `kUnsupportedProfile/kLimitExceeded`, as other
reader representational limits do. Ledger refusal remains
`kBudgetExceeded/kPayloadBudget`; cancellation retains its existing diagnostic.
No asset is published on failure. Actual allocation/runtime exceptions propagate
after cleanup and worker joins.

One fresh operation allocation owner covers retained JSON, numeric/string/node
storage, state and rebound shared controls, resources, graph/metadata inventories,
projection/parser allocations, explicit worker/callable records and temporary
scratch. Admission precedes each original allocation call and includes requested
header/padding and overlapping old/new replacement blocks. Parser callbacks use
this same owner. Its seed is included; no later state constructor creates an
uncapped replacement. The shared execution ledger separately includes prior
input and concurrent operation charges. This measures explicit requested blocks,
not cumulative allocation traffic, process memory or opaque OS/CRT internals.

Legacy `ReadGltfAsset` remains uncapped at the operation level, with its existing
source limits, shared execution ledger, admission and structure layouts intact.
It does not silently acquire an unlimited or guessed operation counter.

## GWS-EXTRACT-001: Fully accounted additive extraction

`ExtractGltfPrimitiveCapped`, declared in the base
[gltf.h](../include/meshvale/interchange/gltf.h), takes the existing asset, strong
primitive, explicit
triangle/value request and `GltfExtractionLimits`. Its required
`max_output_requested_bytes` remains the single complete extraction request cap;
no additional cap struct or required legacy field is introduced. Source admission,
role/layout/bounds checks, raw encodings, normalization, triangle conversion,
capability, diagnostics and resource/source preservation remain owned by the
[reader/extractor](gltf-read.md). This is an allocation/execution route, not a new
processing profile. It uses the source asset's existing work policy and ledger
with the supplied fresh token. Failures retain existing extraction diagnostics,
publish no extraction and join every started worker; actual runtime/allocation
exceptions propagate after rollback and joins.

All fresh explicit Meshvale requests share the retained extraction operation:
seed/control/rebound storage, raw/normalized values, bounds, sparse destinations,
role inventories, canonical triangle/corner provenance, temporary scratch,
native entry/callable/handle records and per-job exception/diagnostic slots.
Admission includes headers, padding and replacement overlap before the original
allocation call. Native callable state is charged before launch and retained
through all joins, including partial launch. A charged vector of standard-thread
objects alone does not establish this guarantee. Opaque OS/CRT thread internals,
runtime exception-object internals and prior input/ledger/token construction stay
outside the explicit-request accounting boundary. Preexisting input charges
remain authoritative on the shared ledger and outside the extraction cap.

Legacy `ExtractGltfPrimitive` retains its existing operation cap, decoder,
work/admission behavior and documented runtime/thread-storage exclusions. Its
live counter remains actual within that retained legacy scope; the getter does
not manufacture charges for excluded storage. Both getters use their real
operation owners. A workflow requiring the stronger explicit-record accounting
must invoke the new entrypoint for initial and reloaded extraction, rather than
infer which route ran from a counter value or binary admission alone.

## GWS-COUNTER-001: Live attributable owners

The additive `GltfAsset::NewRequestedBytes()` returns an optional live count:
present for an asset produced by `ReadGltfAssetCapped`, absent for a legacy
uncapped read. Absence never means zero. The
`GltfPrimitiveExtraction::NewRequestedBytes()` returns its existing mandatory
cap's live request count for either route, with the route's declared exclusions.
The additive route's explicit worker/callable accounting is owned by
GWS-EXTRACT-001; a legacy count does not assert that stronger scope.
These methods are implemented alongside the draft capped entrypoints and declared
in [gltf.h](../include/meshvale/interchange/gltf.h). Their complete accounting and
execution qualification remains required.

Counts come from the retained operation allocation owner, including charged
controls and padding, rather than changes in the shared ledger. Released scratch
is excluded. Copies and other strong references share one counter without
charging twice. The counter can change when separately retained allocations from
that same operation are released; it is not an exclusive-wrapper footprint,
peak measurement or cumulative total. Source read, extraction, materialization,
writer and inspection counters are distinct: an extraction counter excludes its
retained source asset. Consumers combining live stages count each operation once,
retain its owner while counting it, and use checked subtraction for subsequent
caps. Cleanup frees original blocks before releasing charges and ignores stop.

## GWS-INPUT-001: Independent expected and actual owners

`GltfTopologyOutputInspectionRequest` borrows the original strong materialized
`source`, already supplied `candidate` inspection, independent output-to-source
face/corner spans, actual writer `output`, independently `reloaded_asset` and
its selected `reloaded_extraction`. The caller performs read and extraction
before this call; no hidden reload or Geometry inspection occurs.

The original source, output's strong original source, candidate and reload must
share the same public execution ledger. The reloaded extraction must belong to
the supplied reloaded asset and select the original mesh/primitive identity.
Matching values or a different ledger with equal limits cannot replace those
ownership checks. `reloaded_asset.OriginalJson()` must exactly match
`output.Json()`. Caller buffers or writer binding annotations cannot replace the
actual retained reload. Reload may use the capped or legacy reader; a composing
workflow requiring attributable read counts must explicitly require capped
owners itself.

Original identity is an exact owner relation in addition to the common ledger:
`output.OriginalAsset()` must share the retained asset owner of
`source.SourceAsset()`. `output.OriginalPrimitive()` must share the original
primitive's asset owner and match `source.SourcePrimitive()`'s mesh and primitive
indices. The retained `output.Source()` must have those same original asset and
primitive owner/index associations through its `SourceAsset()` and
`SourcePrimitive()`. A separately read equal-byte original asset or equal-valued
foreign primitive on the same ledger fails `kSourceContractFailure/kSourceIdentity`.
For actual output, `reloaded_extraction` must retain the exact supplied
`reloaded_asset` owner and its selected mesh/primitive indices must match the
original selection's indices, now in that output asset. Another reload owner or
another selected primitive fails `kSourceContractFailure/kReloadIdentity`, even
when JSON and numerical values happen to match. No original strong handle is
reused as an output-bound handle; writer annotations do not establish either
identity relation.

The candidate inspection must be storage-valid; otherwise retain its first
actual `StorageIssue`, without inventing a delegated Geometry status. The
[Geometry materialization owner](https://github.com/Meshvale/meshvale-geometry/blob/1a364acb0cba4fbf8cd9cccde5c04a1fa4428536/docs/materialization.md)
owns that validity. A successful source extraction remains the binary admission
proof even when its immutable asset still reports `kBinaryAdmissionPending`.
No new source capability or selected mutation is admitted here.

## GWS-VERIFY-001: Complete selected preservation

Expected values derive from the original source, actual inspected candidate and
independently supplied maps. The inspector does not reuse the writer's verified
flags, binding annotation, splice plan, generated index buffer, copied maps or
private construction helpers as its expected-value oracle. It performs these
complete inventories:

- Source/candidate: every canonical position bit, ordered attribute descriptor
  and backing scalar bit, including unused rows, metadata, optional ragged and
  presence representation. Signed raw source channels retain the exact
  materialization correspondence owned by the transfer contract.
- Correspondence: complete map lengths, strictly ascending in-range source
  faces, canonical source-corner values and actual directed candidate triangle
  loops. Identity selection and changed selection are independently determined.
  A changed zero-face selection remains unsupported.
- Lexical preservation: compare the retained original and actual output
  documents, preserving every byte outside the writer contract's permitted
  selected binding changes and single appended buffer/view/accessor. This
  includes opaque records, decimal/integer lexemes, escaped keys, whitespace,
  old member order, unrelated graph/material/skin/morph/animation records and
  shared accessors. A no-op requires exact original JSON and no append. New
  records must satisfy the writer-owned UINT32/SCALAR, little-endian and
  canonical padded base64 data-URI representation, independently derived from
  candidate indices. Structural equivalence alone is insufficient.
- Resources: original, bundle and actual reload inventories preserve every old
  identity, relative old-view order, origin, authored URI spelling, logical length, alias
  relationship and full captured bytes, including trailing bytes. A changed
  selection adds exactly the expected decoded data-URI buffer; its full bytes
  equal the independently encoded directed candidate indices. No old resource
  may disappear or be shortened. Exposed aliases count separately in limits.
  The bundle appends its new buffer after all old exposed views; a reader places
  buffers before images. The inspector locates the new reload buffer by its
  independently expected table identity and compares all old views in their
  original relative order. The bundle's new buffer associates `buffer_index`
  with its own table index, while the reader's ordinary buffer view leaves that
  optional alias association absent.
- Reloaded primitive: selected mesh/primitive and material identity, complete
  raw channels, semantics/set indices, component encoding, shape, normalization
  flags, element counts, bounds, base/sparse/interleaved provenance and sparse
  destinations remain original. Compare normalized derived values bitwise when
  the source requested them; missing requested normalized output fails. Compare
  exact selected canonical triangle indices against the candidate. No-op keeps
  original source-index representation and mode; changed output has the
  independently verified appended UINT32 index descriptor and TRIANGLES mode.
- Provenance: own the independently composed original-corner map and compare
  bundle maps/provenance/binding annotations with that result. A changed reload's
  TRIANGLES stream corners have output stream ordinals and selected accessor
  rows, not the original strip/fan stream ordinals. Both meanings are checked
  without conflating them.

Coverage is `kComplete` only after the whole named inventory passes; unfinished
sections remain `kNotPerformed`. Failure preserves the completed prefix in the
fixed result, never sampled or partial success. All sections must be complete
before publishing an inspection owner. This preservation check grants no
manifold, self-intersection, solid-orientation, duplicate-policy or application
report guarantee.

## GWS-ORDER-001: Typed deterministic first failure

Check required configuration, physical limits/overflow, common ledger and strong
identity, then supplied candidate validity. Run source/candidate, correspondence,
lexical, resources, reloaded primitive and provenance phases in that order.
Within correspondence, map lengths precede face order/range, corner values,
triangle shape and directed loops. Within inventory phases use original ordered
records, attribute/channel ordinal, row/component or byte offset. Diagnostics
retain the earliest typed location in that order, including resource and JSON
span where applicable. A section becomes complete only after joined reduction.

All started workers join. Actual launch/backend/runtime exceptions propagate
before an expected result, with rollback. An already discovered mismatch is not
lost to a later stop: after joins, bounded allocation-free diagnostic finalization
checks the necessary earlier prefix to establish the true first location.
Otherwise stop produces `kCanceled`. Limit/cap/ledger refusal produces no owner;
`kInspectionRequestLimit` identifies the complete fresh inspection cap and
`kRequestBudget` identifies shared-ledger refusal.

## GWS-OWNER-001: Charged immutable inspection

Success returns a copy-only `GltfTopologyOutputInspection` strongly retaining
source, candidate inspection, actual writer output and actual reload/extraction.
It owns charged face/corner and composed original-corner copies plus coverage.
Caller map spans and context wrappers may be destroyed after return. Input
operations' live counters remain separate; inspection `NewRequestedBytes()`
counts only its own complete fresh operation. No output copy or uncharged DOM is
required to retain those strong owners.

All inspection limits and work fields are required positive and
host-representable. Counts are inclusive and admitted before traversal/allocation.
JSON limits apply separately to original, bundle and reload; resources and full
byte totals separately to each exposed inventory. Scalar/descriptor limits apply
separately to source, candidate and reloaded channels; channels include POSITION,
and descriptor bytes include URI/name/semantic/metadata text. Candidate faces and
corners bound maps; index bytes bound selected encoded indices. Every explicit
fresh request, control/rebound allocation, lexical index, correspondence copy,
scratch and worker/callable record shares `max_new_requested_bytes` and the
source ledger. Preexisting input allocations remain on that ledger but outside
this new cap. Destruction requires no allocation and releases bytes after free.

Independent copies support concurrent const reads. Views require an alive,
unreassigned visible owner; destruction/reassignment of the same wrapper needs
external synchronization. Each operation uses a fresh supplied stop token, not
the original capture/extraction token.

## GWS-EXECUTION-001: Ordered structure and byte workers

Ordered fixed-width descriptor admission and prefix/range assignment are serial
O(record count) passes so each job receives a unique ordered range. Count caps
do not imply smallness. Escape-aware lexical structure remains a dependent serial
scan with bounded byte checkpoints; ordered splice comparison can require
O(record count log record count) sorting. These plans do not excuse substantial
string, metadata, scalar, resource or lexical-segment byte comparison.

Those payloads and map/provenance checks use byte-aware disjoint jobs on the
shared worker budget, including one long channel/resource/lexical segment or few
large faces. Caller thresholds and partitions are explicit. Saturated budgets
or small work may run on the caller. Six execution fields report actual launched,
joined and caller jobs independently; estimates never stand in for native
launches. Explicit worker records and exception slots are charged before launch,
and retained until all normal or partial-launch handles join. There is no
throughput claim or dependency-internal parallelism claim.

## Required qualification

The draft requires original-request cap and rollback observation across complete
read/additive-extraction/inspection paths, parser callbacks, rebound controls,
explicit native worker/callable/handle/exception records, replacement overlap,
allocation faults, cancellation, shared-ledger concurrency and retained copies.
Independent supplied-memory fixtures must cover triangles/strips/fans, shared
old accessors, all admitted raw encodings, normalization, sparse/interleaved
layouts, signed zero/subnormals, large index precision, opaque lexemes, full
resource bytes/aliases and changed/no-op output. Corrupt actual reloads, maps,
candidate storage and writer annotations independently; confirm exact first
failure and ownership after original handles/context wrappers are released.
Require real joined byte workers and serial/parallel equivalence on skewed work,
installed consumers, source-package correspondence and applicable matched native,
ASan/UBSan/TSan gates. The capped reader/extractor and independent output
inspector implementations are under qualification. The inspector uses its own
bounded lexical index, source/candidate comparison and directed index expectation;
it does not use the writer's construction helpers as its oracle. No complete
support qualification is claimed.
