# Owned Python surface output

| Field | Value |
|---|---|
| ID | SURFACE-PYTHON-001 |
| Version | 0.1.2 |
| Status | Unimplemented development contract; names and examples below are not available functions |
| Owner | Proposed `meshvale_interchange.surface_files` module |

This contract selects one owned operation for polygon-to-surface encoding,
inspection of actual output bytes and optional verified publication. It covers
the four encodings and two representations in [the native surface contract](surface-output.md),
with its numerical, channel, material and resource restrictions. It adds no
geometry algorithm, repair, normal generation, indexing, welding or coordinate
transformation. Implementation and installed/platform qualification remain open.
The names and worksheet below are proposals pending interface review.

The native `MESHVALE_INTERCHANGE_SURFACE_OUTPUT` and
`MESHVALE_INTERCHANGE_SURFACE_FILES` options remain opt-in and default `OFF`.
The proposed Python module requires both targets and the ordinary GIL-enabled
Python build. Enabling either native flag alone does not provide this interface.
Existing [STL capture/rewrite](stl-files.md) remains a separate, broader codec
operation: finite degenerate facets accepted there need not pass Surface admission.

## One prepared operation

The proposed entry point is:

```python
operation = prepare_surface(
    options, preset=preset, execution=None, cancellation=None, publication=None
)
result = operation.run(mesh_record, catalog=None)
```

`prepare_surface` validates closed configuration and creates one `PreparedSurface`
with a preallocated, reachable `result` holder before any payload capture or
publication. `run` is synchronous and single-use, including after refusal or
exception; a second call raises `RuntimeError`. Preparation has no mesh input,
file reads, stage creation or export. No public per-phase executor, mutable
native handle, proof constructor or generic exporter framework is introduced.

Preparation creates the Python operation/result holders before admitting native
workspace. If valid configuration reaches a bridge request limit or root payload
budget denial, it returns an already finished operation with the corresponding
ingress `"limit-exceeded"` or `"budget-exceeded"` observation at
`"prepare.workspace"`. No source was supplied, captured or validated and no native
delegate was called. Inspect `operation.result`; `run` on this finished operation
raises the ordinary state `RuntimeError` before inspecting arguments. Genuine
Python or native allocation failure can raise `MemoryError` during preparation,
in which case no operation is returned. A ready operation follows normal run
validation and cancellation ordering. Preparation does not act on a stopped
external cancellation source.

`preset` is the explicit name `"bounded"` from the
[complete finite worksheet](surface-python-presets.json), or an immutable
`SurfacePreset.from_mapping(mapping)` with exactly that worksheet's preset shape.
No implicit preset, partial override, extra key, clamping or guessed resource
limit is permitted. Selecting a finite preset does not promise every input below
its count limits can fit its actual payload/work budgets.

`SurfaceExecutionContext(preset="bounded")` owns a Geometry execution context
inside this extension. Copies share its worker/payload ledger. Omitted execution
creates one context from the selected preset for this operation. Explicit execution
must be this type; a Geometry `Mesh`/execution object or an independently registered
extension type is not exchanged. Read-only metrics are `worker_budget`,
`active_workers`, `peak_workers`, `tracked_payload_budget_bytes`,
`active_tracked_payload_bytes` and `peak_tracked_payload_bytes`.
Use the existing Interchange `Cancellation`, whose `request_stop()` is cooperative
and idempotent; a stopped source remains stopped. There are no Python callbacks
on native workers and no phase/provider callback in this first interface.

The caller must retain `operation` or `operation.result` before entering `run`;
there is no convenience call that discards this holder. `operation.state` is
`"ready"`, `"running"` or `"finished"`. One atomic single-use admission rejects
concurrent/repeated run attempts before those attempts capture any input.
During a run, its data
properties raise `RuntimeError`; only execution metrics and cancellation may be
accessed concurrently. After return/exception it is an immutable observation.
Its native state is filled only by this operation, never by caller-set proof flags.
Publish `finished` and expose the final snapshot only after all launched work
joins. Getters never race changing native facts or return half-populated data.

## Closed options and policy projection

`SurfaceOptions` is a frozen keyword-only value with these exact fields, all
required except where an inactive field must explicitly be `None` or an empty
tuple. Sequence fields are tuples of the frozen rows named below.

| Field | Admitted value and native projection |
|---|---|
| `encoding` | `"gltf-json"`, `"glb"`, `"stl-ascii"`, `"stl-binary"` → `SurfaceEncoding` |
| `representation` | `"strict-bare"` or `"annotated-bundle-v1"` → `SurfaceRepresentation` |
| `acknowledge_triangle_conversion` | explicit bool, required true for this route even when empty |
| `acknowledge_corner_expansion` | explicit bool, required true for this route even when empty |
| `acknowledge_unused_vertices` | explicit bool; required true if a source vertex has no wire row |
| `channels` | `SurfaceChannel(source_attribute, role, acknowledge_ordinary_view_loss)`; every source attribute exactly once, in ordinal order |
| `positions` | `"exact32"`, `"bounded-rne32"`, or ASCII-STL-only `"exact64"` → `SurfaceNarrowing` |
| `max_position_linf_error` | explicit finite nonnegative binary64 bound; no decimal tolerance inference |
| `max_uv_component_errors` | explicit tuple with one finite nonnegative binary64 bound per UV binding, in that order; empty for STL |
| `coordinates` | glTF: `"declared-meters-right-handed-y-up"`; STL: `"stl-units-unspecified"` |
| `uv` | `SurfaceUv(source_attribute, source_set, wire_set)` for glTF, empty for STL |
| `materials` | `SurfaceMaterial(source_id, catalog_material)` for glTF catalog use, otherwise empty |
| `catalog_uv` | `SurfaceCatalogUv(catalog_set, source_set)` for glTF catalog use, otherwise empty |
| `corner_normal_attribute` | explicit source ordinal for STL; `None` for glTF |
| `declared_unit` | STL label as UTF-8 string, empty for unknown; empty for glTF; no scaling |
| `ascii_name` | explicit codec-admitted printable ASCII STL name for ASCII; empty for other encodings |
| `binary_header` | exactly 80 raw bytes for binary STL, empty bytes for other encodings; codec header restrictions still apply |

Channel roles are `"bundle-only"`, `"uv"`, `"stl-corner-normal"` and
`"material-id"`, mapped directly to `SurfaceChannelRole`. They authorize no
unsupported descriptor shape. Source ordinals/counts are strict built-in integers
in native uint64/host extent; source/set/wire UV indices are uint32 and material
`source_id` is int32. Booleans are not integer fields. Bool fields accept only
actual bool. Numeric error bounds accept built-in float only and retain its
binary64 bits; integers, subclasses, NaN/infinity and negative bounds refuse.
Strings are built-in str with strict UTF-8 encoding, not implicitly coerced.
Byte fields are bytes, not arbitrary string encodings.

`TypeError`, `ValueError` and `OverflowError` distinguish wrong Python shape,
invalid configuration and out-of-range extent. Validate all fields/preset branch
shapes and publication configuration before stop handling or source capture.
Native first-invalid ordering within each called delegate remains its contract.
Malformed storage, unsupported geometry/roles, limits, budgets and cooperative
cancellation reached after configuration produce owned result observations.
An invalid later request field cannot be hidden by a pre-stopped source.

## Input record and bounded read leases

`mesh_record` has exactly Geometry's process-local `meshvale.mesh/1` dictionary
shape, described by [the record interface](https://github.com/Meshvale/meshvale-geometry/blob/48298b24be7c132816fb5580d22d58bde61558d0/docs/python.md#geo-python-002-record-version-and-shape).
It is a buffer exchange protocol, not JSON/file serialization or C++ ABI.
An existing immutable Geometry Mesh can explicitly supply `mesh.to_record()`;
that call's allocation/copy occurs outside this operation and is not retroactively
charged. The operation accepts the record itself, not a native Mesh pointer.

Positions are flat native-endian float64 xyz triples, face offsets and corner
vertices native-endian uint64. Attribute dictionaries have the exact domain,
name, semantic, nullable set_index, components, scalar_type, values, nullable
offsets/present and metadata fields. All seven record scalar types remain
available: F32/F64/I32/U8/U16/U32/U64, with raw values, missing rows, ragged rows,
set indices and source attribute order preserved. Metadata is a string dictionary;
it does not represent arbitrary native duplicate-key or ordered metadata byte
sequences. Metadata uses the record adapter's canonical key order. This record
domain restriction is explicit, not a claim of every possible native snapshot.

Acquire standard `Py_buffer` read leases with format, dimensions and strides.
Every numeric buffer is one-dimensional, direct, C-contiguous and has the exact
native format/itemsize required by its field. No flattening, endian conversion,
numeric casting, truncation, inferred missingness or implicit normal preparation
occurs. Accept native `@`/`=` prefixes and the record's documented equal-width
format aliases. Reject indirect, strided, multidimensional, foreign-endian or
released buffers. Empty buffers obey the same format rules. Check byte length,
shape/stride products, alignment and uint64/size_t/ptrdiff_t extents without
overflow before dereferencing or allocating payload storage.

An otherwise valid unaligned buffer uses a bounded, charged aligned staging
copy of its exact scalar bytes; never reinterpret an unaligned typed pointer.
Aligned buffers may be borrowed directly during materialization. Writable
buffers are admitted under the same no-concurrent-mutation rule as the record
interface. Retain all exporters and leases until every reader has joined.
Read-only memoryviews alone do not prove that another alias cannot mutate their
backing. The caller must not mutate record dictionaries, lists, buffers or any
alias from entry to `run` until it returns or raises. No lease extends between
prepare and run; after joined capture no source Python owner is borrowed by the
native result. Source destruction/release then cannot change accepted data.

Validate bounded row/string/metadata/option descriptors and aggregate sizes
before copies. Charge module-owned descriptor arrays, UTF-8 copies, alignment
staging, buffer-lease arrays, paths, actual emitted-member copies and overlapping
owner/control storage through payload leases on the same root. A lease is an
allocation admission, not storage; free storage before releasing its charge.
Use native `MaterializeMesh` once for the original and inspect that genuine owner.
Do not run the legacy `from_record` vector-cloning parser before materialization,
make an uncharged intermediate Mesh or create a fresh context per phase.
Python interpreter object overhead/caches, caller-owned input and the caller's
record creation are outside native requested-payload accounting; bound the
module's admitted buffer/descriptor counts anyway. No RSS or zero-copy claim.

## Genuine catalog and explicit source association

For glTF output, `catalog` is either `None` or a frozen
`SurfaceCatalog(json, resources)` input. `json` is a contiguous one-dimensional
byte buffer of actual glTF 2.0 JSON. `resources` is an ordered tuple of
`SurfaceResource(kind, table_index, bytes)` with kind `"buffer"` or `"image"`,
uint64 table ordinal and a byte buffer; bindings are exact, not URI lookups.
The same buffer admission/read-lease rules and aggregate ingress caps apply.
There is no filesystem/network loader, URI fetch, caller-created `GltfAsset`
constructor, opaque catalog token or cross-extension native-handle shortcut.

The operation invokes `ReadGltfAssetCapped` on these actual bytes with its explicit
`GltfReadOperationLimits`, `GltfWorkPolicy` and the same root Geometry context.
It uses the native retained-core-metadata admission profile; deformation and
direct-translation processing are not selected for a material catalog. The real
read diagnostic and optional genuine GltfAsset are retained. Failed capture
prevents encoding; catalog data is not certified by JSON schema or expected hash.
Native duplicate/extra/missing resource, media and selected-object restrictions
apply. JSON/resource buffers are input to this native JSON reader, independently
of whether the output encoding is JSON glTF or GLB.

Catalog absence requires no material-ID role and empty material/catalog-UV maps.
Catalog presence requires one native-admitted face signed-I32 material-ID channel
and an explicit binding for each actually used ID to a real catalog material
ordinal; duplicates, missing or unused mappings refuse. This association is an
explicit caller instruction checked against source rows and genuine catalog
definitions, not a claim that a Mesh record intrinsically identifies an asset.
Retain both original source and catalog owners; Surface verifies their shared
ledger. Each catalog effective baseColorTexture texCoord, including default zero,
maps through catalog-set → source-set → contiguous wire-set. A UV mapping is not
guessed from equal numbers. Native selected core PBR/baseColor material/image/
sampler fields, defaults, unsupported extras/extensions and exact provenance rules
remain [the catalog contract](surface-output.md#gltf-20-uv-and-materialresource-binding).
Unselected catalog objects remain source context; they are not claimed as output.

Selected PNG/JPEG bytes are retained and structurally/hash checked; no raster
decode, pixel/shading/color-management equivalence or arbitrary glTF scene
preservation is promised. JSON output uses native local image members; GLB uses
native embedded image extents. No caller-authored output URI or unrelated source
buffer copy is invented. STL requires catalog=None and empty UV/material maps.

## Complete dispositions, numerical policies and refusals

Annotated output retains original/triangulated record storage and native complete
maps in verified `surface.json`/`surface.bin`. Every omitted ordinary-view channel
has its own bundle-only loss acknowledgment. A viewer ignoring the capsule sees
only the declared projection. Bare output refuses bundle-only channels, metadata,
missing/ragged descriptors and other native unrepresentable data. It does not
promise original polygon or indexed-vertex identity. The original source and
triangle/corner/wire maps are never inferred from equal positions. No standalone
bundle reconstruction interface is introduced.

Dense finite UV pairs retain native shape rules and exact source-set ordering;
glTF maps source sets 0/2/5 to wire sets 0/1/2 only when explicitly bound that way.
glTF normal omission remains an explicit ordinary-view loss; no generated or
normalized NORMAL is supplied. Exact32 and BoundedRne32 position modes apply to
glTF/GLB and binary STL; ASCII STL additionally admits Exact64. Actual native
finite/range/error, coincidence, collapse and exact orientation guards apply.
The numeric bounds are explicit request policy, not values supplied by a preset.

STL requires an explicit dense corner normal F32/F64 vector3 descriptor, without
set/offset/presence. Each triangle's three source corner vectors must be bit-equal
and finite. Binary normal representability must be exact F32 even when positions
use bounded rounding; ASCII normals retain exact finite F64 values. Preserve raw
zero, nonunit and opposed values where native admission allows them; no universal
unit-normal/manufacturing claim. Missing normal is the genuine NormalAbsent
refusal, including attribute-free empty input. Explicit empty dense descriptors
are a separate real zero-facet case. Masked or varying normals receive native
shape/constant refusals, not mask clearing, filling, interpolation or averaging.

The following complete example specifies the proposed interface; it has not run
and cannot run until the module is implemented and qualified. The normal values
are authored input. Exact64 ASCII positions select no position narrowing.

```python
from array import array

# Proposed imports: unavailable in the current package.
from meshvale_interchange.surface_files import (
    SurfaceChannel, SurfaceOptions, prepare_surface,
)

record = {
    "schema": "meshvale.mesh/1",
    "positions": array("d", [0, 0, 0, 1, 0, 0, 0, 1, 0]),
    "face_offsets": array("Q", [0, 3]),
    "corner_vertices": array("Q", [0, 1, 2]),
    "attributes": [{
        "domain": "corner",
        "name": "authored_normal",
        "semantic": "NORMAL",
        "set_index": None,
        "components": 3,
        "scalar_type": "float32",
        "values": array("f", [0, 0, 1, 0, 0, 1, 0, 0, 1]),
        "offsets": None,
        "present": None,
        "metadata": {},
    }],
}
options = SurfaceOptions(
    encoding="stl-ascii",
    representation="annotated-bundle-v1",
    acknowledge_triangle_conversion=True,
    acknowledge_corner_expansion=True,
    acknowledge_unused_vertices=False,
    channels=(SurfaceChannel(
        source_attribute=0,
        role="stl-corner-normal",
        acknowledge_ordinary_view_loss=False,
    ),),
    positions="exact64",
    max_position_linf_error=0.0,
    max_uv_component_errors=(),
    coordinates="stl-units-unspecified",
    uv=(),
    materials=(),
    catalog_uv=(),
    corner_normal_attribute=0,
    declared_unit="",
    ascii_name="authored-triangle",
    binary_header=b"",
)
operation = prepare_surface(
    options, preset="bounded", execution=None,
    cancellation=None, publication=None,
)
held_result = operation.result  # Reachable before any native commit.
result = (operation.run(record, catalog=None)
          if operation.state == "ready" else held_result)
assert result is held_result
print(result.complete, result.terminal_phase, result.phase_states)
if result.encode is not None:
    print("encode", result.encode.status)
if result.inspection is not None:
    print("inspection", result.inspection.status)
if result.output is not None:
    for member in result.output.members:
        # Explicit caller-owned copy, outside the native payload accounting.
        payload = bytes(member.bytes)
        print(member.path, member.kind, len(payload), payload[:40])
```

No expected success or output spelling is substituted for the returned status
and actual member bytes. `publication=None` performs no filesystem publication.

## Actual phases, results and publication

One run follows materialization → source inspection → retained charged
triangulation → optional genuine catalog read → native encode → own actual member
bytes → native independent inspection → optional physical publication. Stop on
the first unsuccessful required phase; never synthesize later observations.
The original is materialized once; no second triangulation is used to validate
its source maps. All actual native worker launches join before phase return,
lease release, exception propagation or publication. Release the GIL during safe
native work after pinning inputs; do not access Python containers from workers.

`SurfaceResult` has read-only `complete`, `terminal_phase`, `phase_states`, `ingress`,
`materialization`, `source_inspection`, `triangulation`, `catalog_read`, `encode`,
`output_capture`, `inspection`, `publication`, `output`, `verified` and `bundle`
properties.
The phase names are `ingress`, `materialization`, `source-inspection`,
`triangulation`, `catalog-read`, `encode`, `output-capture`, `inspection`,
`publication`, `complete`.
`phase_states` is an immutable mapping over those phase names (excluding
`complete`) with `"not-called"`, `"returned"` or `"threw"` values after a run.
It distinguishes an uncalled phase from a called native operation that threw
without returning normal facts. `complete` requires genuine successful memory inspection and, when requested,
actual published outcome. Each uncalled phase is None, distinct from called
failure and a called phase with an unavailable optional fact.
If a native delegate throws, its normal-result property is None and its state
is `"threw"`; retain earlier returned observations/owners, and invent no partial
result for the throwing call.

Ingress observation records status (`captured`, `limit-exceeded`, `budget-exceeded`
or `canceled`), nullable first offending field/row, admitted buffer/descriptor/
staging byte counts and actually reached copy/stop work. Representation exceptions
leave only reached ingress observations; no native capture status is invented.
Ingress also covers actual preparation workspace admission. Its expected resource
refusal sets ingress to `"returned"`, with zero source/export/copy counters and
all delegate properties `None`/`"not-called"`. Bridge request-cap denial retains
its native operation-limit provenance; root payload denial is a distinct budget
outcome. Checked arithmetic failures and unexpected allocation/logic exceptions
are never relabelled as either normal refusal. During run, the same distinction
retains actual partial ingress counters rather than the preparation zeros.
Native phase values expose their native public enum names as strings (for example
`kSuccess`), exact diagnostics, nullable observations and actual execution counts;
enum ordinals are not a new wire schema. The owners are:

| Property | Actual retained native data |
|---|---|
| materialization/source_inspection | MeshMaterializationResult / MeshInspectionResult status, diagnostic, execution and returned owner; owner-request bytes are available only when that owner exists |
| triangulation | ChargedTriangulationResult status/diagnostic/facts and genuine charged owner/maps |
| catalog_read | GltfReadResult diagnostic, optional genuine catalog and only actual optional observations |
| encode | SurfaceEncodeResult status/diagnostic/optional SurfaceFacts and output |
| output_capture | Binding-owned, distinct charged copies of the actual encoded member bytes; actual copy/allocation/join observations, separate from native inspection |
| inspection | SurfaceInspectionResult status/diagnostic/optional SurfaceFacts and independent actual-byte proof |
| publication | SurfaceFilePublicationResult outcome/diagnostic/cleanup/SurfaceFileFacts and optional committed owner |

`output_capture` is `None` until encoding succeeds and member capture is entered.
Its nullable `status` is `"captured"`, `"limit-exceeded"`, `"budget-exceeded"`
or `"canceled"`. The observation exposes `allocated_payload_bytes`,
`allocated_request_bytes`, `active_payload_bytes`, `copied_bytes`,
`byte_checkpoints`, `planned_jobs`, `launched_workers`, `joined_workers` and
`caller_jobs`. The three allocation/live counters describe only the contiguous
copied member-byte staging block. Its request bytes include that block's actual
allocator overhead. Member descriptors, job records and worker scratch are also
charged through the same bridge/root; they are outside these three counters,
which are not total phase or bridge allocations. Cumulative allocation/copy work
is distinct from live member-byte storage. Freeing the
temporary capture storage after the independent inspector joins makes live
payload zero without erasing completed or partial work. Returned inspection
proofs retain their own genuine native backing.

Expected capture refusal sets `"output-capture"` to `"returned"`, retains the
actual native `allocation_code` and `allocation_provenance` enum names when
available, and leaves native inspection and publication `None`/`"not-called"`.
Only actual operation-cap denial maps to `"limit-exceeded"`; actual root-ledger
denial maps to `"budget-exceeded"`. Neither is a successful independent proof.
Unrelated exceptions set this binding phase to `"threw"`, retain reached copy
work with `status=None`, and propagate without relabeling arithmetic, invalid
objects or genuine allocation failure. A throwing native inspector instead
leaves output capture `"returned"`/`"captured"` and inspection `"threw"`.
Every launched copy worker joins before either outcome. No member-capture
exception or refusal can discard the earlier genuine encoded output.

`output` retains genuine SurfaceOutput even if a later phase fails, keeping its
original/triangulated native owners alive. It exposes `encoding`,
`representation`, `members`, `wire_maps`, `triangulation_maps` and `facts`.
Each immutable member has `path`, `kind` and read-only `bytes`.
`wire_maps` contains `wire_triangle_faces`, `wire_triangle_sources`,
`wire_corner_corners`, `wire_corner_sources`, `wire_vertex_sources`,
`source_vertex_offsets`, `source_vertex_wire_members`, `source_face_offsets`,
`source_face_wire_triangles`, `source_corner_offsets` and
`source_corner_wire_corners`; `triangulation_maps` contains `face_sources`,
`corner_sources` and `face_output_offsets`. Every map is a read-only native-endian
uint64 buffer owned by its genuine native state. No Python re-triangulation or
map synthesis fills these properties.
`verified` retains genuine SurfaceOutputInspection and exposes its actual
captured `members`, `facts` and expected `output`. `bundle` retains
SurfacePublishedBundle and exposes its fresh physical `verified` proof and
immutable `receipt` with `path`, `bytes`, `sha256`. Python buffer views are read-only and
retain an exporting strong owner, so deleting the result cannot invalidate a
live view. Optional Python copies/diagnostic framing may fail with MemoryError;
they cannot change native facts or manufacture an accepted proof.
Retained charged triangulation has `source_capture=None`; do not fabricate a
zero-byte completed capture. Counts and each optional native delegate/core fact
remain unavailable until that native field was actually reached.

`publication` is None or frozen `SurfacePublication(destination, receipt_path)`.
`destination` uses os.fspath native str/bytes semantics with nonempty/no-NUL and
native length checks. `receipt_path` is the explicit native-admitted relative
path (normally `"publication.json"`); no overwrite/merge/arbitrary supplemental
files/provider text or callback is available. Validate path/inventory caps
before stage creation. The supplied record/catalog has no source filesystem
identity; this interface makes no captured-source-file protection claim.
Publication still uses native path/identity/no-follow/no-replace and typed cleanup
guarantees, including existing destination/race/unsupported-path refusals.

The publisher stages the inspected exact payload, closes/rereads it, establishes
closed inventory and freshly runs independent inspection. Its neutral
`meshvale.surface-publication/1` receipt describes reached precommit payload facts.
It is not a shared application report. The canonical disabled SurfaceReceiptProvider
is used; no synthetic Repair report or predicted commit/cleanup enters receipt
bytes. Final receipt verification, actual commit and cleanup are later typed facts.
Keep SurfacePublishedBundle alive for its borrowed SurfaceFileReceiptView
path/bytes/SHA256; do not detach a raw span from its strong owner.

Prepare all mandatory fallible wrapper/result/owner capacity before native commit.
Commit-to-holder transfer uses only allocation-free/nonthrowing native owner
copies into the already reachable `operation.result`. If Python return/framing
fails afterwards, raise `SurfaceDeliveryError` with that same result when it can
be delivered; the caller already retains the operation and can inspect its
result after the exception. No rollback or unpublished outcome may be claimed
after actual commit. Arbitrary interpreter termination is not a delivery guarantee.
This also applies to late `MemoryError` and `KeyboardInterrupt`. Interpreter
signals are observed only at safe Python boundaries; they are not evidence that
native stop was requested or that publication was canceled. Finish native joins
and store the actual outcome before propagating a pending Python exception.

## Complete finite resources and execution

The worksheet lists every active native field and every canonical-zero inactive
field for all four encodings. Representation and numeric policies remain explicit
request choices; a preset never supplies loss acknowledgments or error tolerances.
`delegates.kind` is the JSON selector for the corresponding native variant, not
an added native field. JSON glTF has all-zero unused GLB limits/work; glTF encode
has all-zero glTF-read/STL work; STL has all-zero glTF/GLB work. Native inactive
direct-translation objects are explicitly all zero. Never fill inactive branches
with convenient positive defaults or count their phases as called.

New ingress limits are inclusive: buffer views count acquired exports, per-buffer
and aggregate bytes count actual addressed payload, attributes/metadata/options
count admitted rows, descriptors/options count owned UTF-8 and backing bytes,
catalog binding count includes every supplied resource, and destination native
code units include the terminator. `max_bridge_new_requested_bytes` covers actual
live module-owned requests, including staging and output-copy overlap.
Ingress work bounds cancellation checkpoints by bytes and descriptor items;
native delegates retain their own independently selected work caps.

`max_operation_requested_allowance` is checked containment, not a reservation:
it covers the sum of the bridge cap, source materialization/inspection caps,
retained triangulation's source-inspection/output/output-inspection/core/bridge
caps, optional catalog-read cap, E allowance, memory-V allowance and optional P
allowance. Retained triangulation's unused legacy source-capture request cap is
not counted as a call. P already includes fresh physical V; do not count it again.
For bounded publication with a catalog this sum is 1,040,187,392 bytes, within
the worksheet's 1,073,741,824-byte containment allowance. Each delegate's own
containment inequalities remain mandatory. Existing retained occupancy and
concurrent callers still consume actual shared root budget; admission can refuse
even when every individual configured limit is valid.

Actual requested/retained bytes, stage return snapshots, context lifetime peaks,
worker slots and work counters are separate observations. Aliases are not added;
limits/containment sums are not measured peaks or RSS. No independent Python pool,
new backend, hard cancellation latency or performance guarantee is selected.
Future qualification must show real serial/multicore agreement and worker use,
not infer parallel execution from configured thresholds.

## Qualification and file-command continuation

Implementation requires independent literal record/map/physical-byte authorities,
all encodings/representations/applicable numeric modes, rich seven-scalar and
material/image/UV binding controls, deliberate output mutations and genuine
refusals. Exercise bounded ingress, unaligned/writable buffers, source/result/view
lifetimes, shared budgets, actual stop/joins and postcommit delivery through this
same interface. Preserve existing native fault gates, and qualify normal installed,
copied-runner, archive/source-package and feature-enabled hosted routes. No existing
native consumer or configured workflow proves this Python module is delivered.

A separate captured-file Repair interface, module/console single command and
ordered batch remain required future work. This buffer module is a prerequisite,
not their replacement. OBJ import retains authored normal presence descriptors;
it cannot directly satisfy the STL dense-normal shape. A separate contract must
define explicit complete-only projection with default refusal, preserve the
original masked owner and exact source bits/maps, identify the derived dense
channel/export source and record provenance. Other channels, MTL/images, units,
numeric/loss/report meanings, source protection, whole-batch validation and exit/
delivery semantics must be selected there before implementation. No implicit
mask removal or attribute-free OFF normal generation is authorized here.
This design does not claim full format fidelity, scene preservation, supported
releases or complete application/MVP delivery.
