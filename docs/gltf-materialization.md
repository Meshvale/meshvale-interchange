# Static raw glTF materialization

| Field | Value |
|---|---|
| ID | GLTF-MATERIALIZATION-001 |
| Version | 0.3.0 |
| Status | Development interface |
| Owner | Interchange |

This contract specifies
[`gltf_materialization.h`](../include/meshvale/interchange/gltf_materialization.h).
The separately installed `meshvale::gltf_materialization` target transfers
admitted static raw primitives into inspected canonical Geometry storage. The
[reader/extractor](gltf-read.md) retains its current behavior. This contract defines
the adapter's additional behavior without changing glTF admission. Numerical
capture follows the linked Geometry compiled allocation profile; unsupported
producers retain explicit delegated failure. This is a development interface,
without a stable release guarantee.

The [selected direct translation contract](gltf-translation.md) requires future
materialization to retain the strong extraction owner and its admission proof;
borrowed views or caller flags cannot recreate it. Implementation and
qualification are pending. Current static raw transfer and numerical-profile
behavior remain unchanged; animation is not converted into Geometry attributes.

## GTM-INPUT-001: Operation and required policy

`MaterializeGltfPrimitive` synchronously consumes a successful
`GltfPrimitiveExtraction`, required limits, Geometry materialization work policy
and a fresh stop token. It uses the extraction's retained execution context.
There is no caller asset/primitive/context tuple, implicit read or extraction,
scene transformation, instance cloning or semantic conversion option. The
source extraction's explicit triangle and value requests remain authoritative.

All fields of `GltfMaterializationLimits` and the work policy must be positive
and representable by the compiled host extents. `max_channels` counts every
source channel, including POSITION, and is checked before descriptor allocation.
`max_bridge_new_requested_bytes` bounds the bridge's own new requests. Nested
capture and inspection limits retain the
[Geometry contract](https://github.com/Meshvale/meshvale-geometry/blob/48bb348112f49a07aec03bc69b9875ea6b62128a/docs/materialization.md)
semantics. Limits are inclusive; zero does not select an unlimited default.

## GTM-MAP-001: Complete selected raw channels

POSITION's borrowed float span becomes Geometry position ingress. The existing
uint32 triangle-corner span becomes triangle ingress. Every other admitted
channel becomes one dense vertex attribute in source channel order. Its complete
authored semantic is both the attribute name and semantic; its set index narrows
only after an exact uint32 range check. Component width follows the admitted
scalar/vector shape. Dense channels have absent ragged offsets and presence
arrays. The adapter manufactures no attribute metadata from opaque extras.

| Raw source | Canonical Geometry storage |
|---|---|
| POSITION float XYZ | Exact widened double XYZ |
| Admitted attribute float, uint8, uint16 | Same scalar type and bits |
| Attribute int8 or int16 | Exact sign extension into int32 |
| Triangle uint32 corner indices | Exact widened uint64 corners and triangle offsets |

Geometry owns the exact widening and canonical storage behavior. Transfer does
not stage another numerical copy or export a mutable `Mesh`. Each source channel
has one `GltfMaterializedChannel`, in source channel order: source ordinal,
positions or vertex-attribute destination, optional destination attribute ordinal
and exact/position-widened/signed-widened encoding correspondence. POSITION has
no destination attribute ordinal. Other destination ordinals follow their order
after excluding POSITION. Source indices are provenance, not another attribute. The current extraction
profile admits uint32 as a source index encoding; it rejects uint32 custom
attributes. The bridge does not expand that admission boundary.

Canonical attributes remain **raw**. Normalization declarations and available
derived binary64 values remain accessible through `SourceExtraction().Channels()`.
A raw-only extraction does not acquire derived values during transfer; a
raw-and-normalized extraction retains them. No clamp, renormalization, generated
interpreted attribute or replacement of raw storage occurs. Geometry's open
semantic labels do not select an interpretation. Downstream semantic consumers
must explicitly choose raw or normalized values.

Original float POSITION bits, raw component encodings/bounds, sparse selectors,
base/packed layout, original index encoding, source mode and source stream-corner/
accessor-row maps remain visible through the extraction. Degenerate triangles,
unused vertices and material annotations retain their existing meanings.
The original JSON, resources, graph and opaque data stay with `SourceAsset()`.

## GTM-OWNER-001: Published owner and inspection

Success returns a copy-only immutable `GltfMaterializedPrimitive`. `Mesh()` is
the retained inspection's `Source()`; `Inspection()` is storage-valid. The
adapter publishes only after capture, completed valid inspection and its final
stop check. `SourceExtraction()`, `SourceAsset()` and `SourcePrimitive()` retain
the original selected primitive identity and complete source ownership. They
are not an instance clone or reconstructed identity inferred from shared accessors.

Copying the owner shares storage and charges without allocation. Borrowed
references/spans require their visible owner to remain alive and unreassigned.
Independent owner copies support concurrent reads and calls; reassignment or
destruction of the same wrapper requires external synchronization. Inputs,
parser objects, original source handles and the caller context wrapper may be
destroyed after return. A copied-out Geometry mesh keeps its canonical storage;
source provenance still requires an adapter/source owner.

## GTM-ACCOUNT-001: Three new caps, one retained ledger

Bridge state/control blocks, descriptors, mappings and scratch use a fresh
operation allocator and the bridge cap. Capture and inspection each use their
own new-operation cap on the same original execution ledger. Existing source
charges remain live and are charged once, outside each new-operation cap.
New/old replacement and source/candidate overlap remain bounded by the common
ledger. The
[Interchange allocator](gltf-foundation.md) and linked Geometry contract own
actual request/header/padding admission and destruction order.

`NewRequestedBytes()` reports three separate **live post-success** observations:
bridge, materialization and inspection. Scratch already freed is no longer live.
The observations are not cumulative traffic, a combined operation peak/cap or
RSS. A shared source owner is not recharged by copying the adapter. Cleanup frees
storage before releasing its charge and does not allocate or observe cancellation.

## GTM-FAIL-001: Fixed stage-aware outcomes

Expected failures return no partial primitive. The fixed diagnostic identifies
bridge, capture, inspection or publication stage, optional source channel, and
any actual delegated Geometry outcome. It allocates no detail strings.

| Adapter status | Meaning |
|---|---|
| `kInvalidConfiguration` | Required field zero or configuration extent not representable |
| `kLimitExceeded` | Channel count or bridge new-request cap, including too-small accounting-seed allowance |
| `kUnsupportedRepresentation` | Authored set index cannot be represented by the destination descriptor |
| `kBudgetExceeded` | Bridge request rejected by the common execution ledger |
| `kCanceled` | Fresh operation stop observed |
| `kSourceContractFailure` | Inconsistent descriptor/provenance in the unforgeable extracted source |
| `kGeometryFailure` | Capture or inspection returned an actual failure; exact Geometry status/diagnostic retained |
| `kInvalidStorage` | Inspection succeeded but contains issues; first ordered typed `StorageIssue` retained |

For `kInvalidStorage`, the diagnostic records Geometry's actual `kSuccess` and
its original diagnostic. It does not fabricate a Geometry failure. A genuine
delegated unsupported allocation profile stays visible as that Geometry outcome;
it is not malformed glTF. Runtime/allocation exceptions retain their owning
contracts after rollback and every join; there is no emergency allocating report.

The capture/extraction token cannot cancel this later operation. Descriptor
preparation, capture, inspection and publication use the supplied fresh token.
Checkpoints bound descriptor-entry work. Already discovered failure precedence
follows the owning stage; a final stop cannot replace an observed invalid-storage
issue. Cancellation can prevent discovery of later defects. Cleanup ignores stop.

## GTM-EXECUTION-001: Planning and payload workers

Descriptor planning/publication runs as one ordered serial pass. It constructs
fixed-width borrowed descriptors and destination ordinals in O(channel count),
under `max_channels`, without scanning/copying numerical or semantic-string bytes.
Stable source order and direct descriptor publication avoid a separate prefix/
scratch phase. This choice is not a constant-time or measured throughput claim.

Substantial numeric/triangle capture and storage inspection delegate to Geometry's
qualified byte-aware workers and the caller's work policy, including large single
rows. They use the shared worker ledger; no competing adapter pool is launched.
`execution.capture` and `execution.inspection` report actual delegated planned,
launched, joined and caller jobs. Every started worker joins before return or
exception; small work or unavailable slots may run on the caller. Representative
large channel inventories require assessment before a performance claim.

## GTM-SCOPE-001: Capability and qualification boundary

The adapter covers every channel admitted by the existing static local extraction
profile. Its unsupported joint/weight, morph, animation, custom-matrix and
non-triangle capabilities remain unchanged. Storage-valid inspection does not
certify geometric quality or complete rendering semantics.

Qualification requires actual Read→Extract→Materialize→Inspect and
face-selection correspondence, independent bit/provenance fixtures, original
request/cap/fault/lifetime/worker evidence, matching native/sanitizer producers,
relocated installed consumers and source/package regression. Serial original
request observation and real-worker sanitizer acceptance are separate gates.
Unqualified numerical producers must retain explicit unsupported outcomes.

Direct charged Repair, broader skin/weight/morph/animation/material/image
processing, edited primitive re-embedding, filesystem publication, Python and
batch workflows require separate implementations. This static adapter does not establish those capabilities or a stable release.

## GDEF-TRANSFER-PROJECTION-001: Retained deformation selection

The [retained deformation contract](gltf-deformation.md) selects the opt-in
deformation-retained-v1 extension for raw canonical transfer and source authority.
Development implementation is present; full workflow and dependency-cohort
qualification and release availability remain pending. Existing static/direct-translation clauses, defaults and
unchanged-request wire keep their current meaning. The new contract owns the
additional source/read/processing authority, policies, roles and called facts.
There is no implicit source upgrade or widening of a legacy schema/preset.
