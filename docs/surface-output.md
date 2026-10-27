# Supplied surface output and independent inspection

| Field | Value |
|---|---|
| ID | SURFACE-OUTPUT-001 |
| Version | 0.1.0 |
| Status | Native source implementation and qualification in progress |
| Owner | surface_write.h and the optional surface_output target |

This document defines a supplied-memory encoder and a separate actual-byte
inspector for general glTF 2.0 JSON/GLB and the bounded ASCII/binary STL profile.
It does not establish completed native, installed, archive, platform or release
qualification. Filesystem publication, Python, application commands and batches
are separate work. Existing strict topology-selection writers remain guarded.

The [public interface](../include/meshvale/interchange/surface_write.h) owns the
closed C++ types/enum values. The [bundle schema](surface-bundle.schema.json)
owns JSON shape; every binary/range/map/numeric rule below is also mandatory.
The [glTF reader](gltf-read.md), [GLB container](glb.md) and [STL codec](stl.md)
retain their existing limits and failure behavior. The ordinary native view is
distinct from the original-storage recovery provided by the verified sidecar.

## Native target and dependencies

`MESHVALE_INTERCHANGE_SURFACE_OUTPUT` defaults to `OFF`. With it enabled, build
and install `meshvale::surface_output` and its public header. It requires matching
installed `meshvale::interchange`, `meshvale::stl`, `meshvale::materialization`
and `meshvale::charged_triangulation` targets, C++20 and the same compiler/runtime
configuration. Existing base/optional targets retain their default behavior.
The ON-only check compiles and links both charged triangulation overloads and
their compiled strong-owner getters; a package version alone cannot identify
development interface availability or authenticate arbitrary mixed SDKs.

Follow [ENVIRONMENT.md](../ENVIRONMENT.md) for portable dependency/toolchain inputs.
In that same environment add `-DMESHVALE_INTERCHANGE_SURFACE_OUTPUT=ON`. A native
consumer uses `find_package(MeshvaleInterchange 0.0.0 EXACT CONFIG REQUIRED)` and
links `meshvale::surface_output`. No private checkout or source include fallback
is part of installed consumption. Matching native producer inputs, new actual
qualification and all retained third-party notices remain required. Native
numerical targets require strict floating-point compilation; this is not a
supported-platform or stable-ABI claim.

## Input and proof boundary

The encoder borrows one genuine, storage-valid `MeshInspection` of the original
canonical `MaterializedMesh`, and one genuine accepted `ChargedTriangulation`.
They must share the actual Geometry execution budget, not merely equal limits.
Its output inspection and mesh must be genuine strong handles. No caller-created
map, accepted flag, topology/file asset or identity number substitutes for them.

The charged result has no original-source getter. Therefore correspondence is
**established**, not assumed from object identity: check six counts, unchanged
vertex rows/bits, every triangle face/corner map and face-output range, all source
loop ranges and complete descriptor/value/offset/presence/ordered-metadata
correspondence for every scalar type. Source face and corner rows must all be
covered by their actual triangle maps. An empty face range remains represented.
The comparison uses the documented charged transfer rules, never a second
triangulation or a chosen unique ear order. Signed zero, NaN attribute payloads,
missing rows, ragged empty rows, arbitrary descriptor bytes and metadata order
are compared as storage. Position storage must already be finite and valid.

Legacy callers first explicitly materialize/inspect the original once, then use
the retained charged overload. The legacy charged overload's hidden original
capture is not reconstructed after return. This interface adds no generic converter
or new Geometry identity getter. Retaining both real inputs closes the original
storage lifetime; their pre-existing charges remain on the same root ledger.

## Four encodings and representation

`GltfSurfaceOptions` admits only glTF 2.0 JSON or GLB 2. `StlSurfaceOptions`
admits only this Interchange codec's ASCII or binary STL profile. Destination and
limit variants must match. No detection, implicit transform or writer fallback.

Every source attribute ordinal occurs exactly once in the closed `channels`
array, in source ordinal order. A role is an instruction, not an admission bypass;
its descriptor/backing must satisfy the role below. Every applicable loss must
be explicitly acknowledged before planning. No default drop, unknown extras or
implicit extension policy exists.

`kAnnotatedBundleV1` emits ordinary native members plus exactly `surface.json`
and `surface.bin`. Ordinary viewers see only the declared projection. Original
and triangulated storage, all maps, descriptor bytes and metadata are recovered
only after independent capsule verification. The sidecar is not a glTF extension
and is not required for ordinary glTF file validity. A viewer ignoring it does
not recover omitted channels, original identities or normal shading semantics.

`kStrictBare` emits only ordinary native members. It refuses any `BundleOnly`
role, descriptor metadata, missing/ragged backing, custom scalar payload or
original UV set remap that the selected native view cannot represent. Its claim
is the explicit native projection, never original polygon/vertex identity or a
seven-type canonical round trip. Triangle conversion, per-corner expansion,
unused vertices and selected finite numeric narrowing still require their
separate acknowledgments. For bare glTF, UV descriptors must be dense vertex or
corner TEXCOORD float32/float64 pairs with empty metadata and contiguous source
sets identical to wire sets; arbitrary descriptor-name preservation is not
promised. A material-ID channel is bundle-required because native ordinals do
not preserve signed source IDs. For bare STL, the sole channel is the selected
dense corner normal, with empty metadata. All other channels cause refusal.
These are exact first-profile restrictions, not a generic silent loss switch.

## Complete rich-channel disposition

The same original 12-vertex, 19-channel source and genuine charged result feed
each encoding. The original and triangulated capsule snapshots contain all
seven scalar types with raw bits, offset/presence optionality, ordered metadata,
set indices, names and semantics. Neither snapshot is a simplified fixture.

| Original ordinal(s) | glTF JSON / GLB ordinary view | ASCII / binary STL ordinary view | Capsule |
|---|---|---|---|
| positions, unused vertex 11, distinct coincident IDs | used positions quantized once by source vertex, expanded per triangle corner; unused rows have no native accessor | triangle corner positions under selected numeric policy; no weld | all original/triangle F64 bits, unused rows and maps |
| 0 dense F32, 1 dense F64, 2 dense I32, 3 dense U8, 4 dense U16, 5 dense U32, 6 dense U64 | `BundleOnly`, each explicit ordinary-view loss acknowledgment | `BundleOnly`, each explicit acknowledgment | complete dense backing and presence |
| 7 ragged F32, 8 ragged F64, 9 ragged I32, 10 ragged U8, 11 ragged U16, 12 ragged U32, 13 ragged U64 | `BundleOnly`; no flattening/coercion | `BundleOnly` | every offsets row and all raw backing, including missing/empty rows |
| 14 corner nonunit NORMAL | omit native NORMAL only with explicit loss acknowledgment; no generated/normalized normals | `StlCornerNormal`: each triangle's three vectors must be present, finite and bit-identical; retain raw selected vector | all original nonunit vectors/metadata; no shading-equivalence claim |
| 15 UV source set 0, 16 source 2, 17 source 5 | `Uv`, dense finite pairs; source→wire 0→0, 2→1, 5→2, explicit numeric policy/bounds and seam expansion | `BundleOnly` | all original UV bits/descriptors plus both mapping directions |
| 18 signed face material IDs 17,-4,1000003 | `MaterialId`, explicit source-ID→real catalog material; stable first-use grouping | `BundleOnly`; STL has no material channel | signed IDs, original faces and complete selected definitions/resource graph |

No payload interpretation is assigned to the opaque channels. Bit-preserving
capsule storage includes NaN payloads and negative zero there. Nonfinite values
are refused only when selected as an actual numeric native field. Every new
source attribute requires an explicit rule and admission, not table fallthrough.

## Wire topology and maps

One wire vertex per triangulated corner, no deduplication or welding. glTF uses
one unique index per corner, each triangle's local three indices in original
orientation, unsigned-int indices avoiding the reserved maximum. STL facet
order is charged triangle order. For glTF, stable first appearance of selected
material determines primitive order; triangles within a primitive retain charged
order. With no material channel use one primitive for nonempty geometry.

All maps refer to stable ordinals: wire triangle→charged triangle→original face;
wire corner→charged corner→original corner; wire vertex→original vertex.
Primitive-local accessor rows and indices have explicit global offsets. Inverse
CSR includes every original vertex, including unused and coincident distinct
IDs; an empty inverse row is not omitted. `FaceSources`, `CornerSources` and
`FaceOutputOffsets` from Geometry are retained separately from the wire reorder.
No identity is inferred from equal positions. glTF primitive grouping cannot
change source material attribution or primitive/accessor counts.

Every POSITION/NORMAL/UV accessor in a primitive has the same row count. No
zero-count accessor is emitted. True-empty/point-only glTF has the exact logical
shape {"asset":{"version":"2.0"},"scenes":[{}],"scene":0}: omit root.nodes and
the empty scene's nodes property. Never emit either as an empty array. Omit all
optional positive-cardinality root arrays when empty: extensionsUsed,
extensionsRequired, accessors, animations, buffers, bufferViews, cameras, images,
materials, meshes, nodes, samplers, scenes, skins and textures. Here scenes is
the real nonempty default-scene array; unsupported arrays are absent. No mesh,
primitive, extraction, zero accessor or buffer is invented. Empty JSON has no
geometry.bin; empty GLB contains only its actual JSON chunk. The capsule retains
all source rows. `acknowledge_unused_vertices` is required when
any original vertex has no wire row. Triangle/corner-expansion acknowledgment is
required even if the particular count happens to be zero: it selects the route.

STL requires an explicitly selected normal descriptor even for zero facets.
An explicit empty dense corner descriptor yields a real zero-facet codec call.
A genuinely attribute-free empty source yields `kUnsupported/kNormalAbsent` at
Disposition, with no codec call, output or numeric facts. This selected-profile
refusal is not malformed STL. No empty-case normal-generation exception exists.
Rich point-only zero-row transfer retains its explicit descriptor and is valid
after the same descriptor checks. Normal absence precedes requested stop only
after the bounded Disposition phase is reached, not before Configuration.

## glTF 2.0 UV and material/resource binding

Indexed semantics start at zero without gaps under the pinned glTF 2.0 rule.
Each selected source set is unique; sort by source set to choose contiguous wire
sets. The caller supplies the exact map and the encoder verifies that canonical
mapping (rich 0/2/5→0/1/2). Store source attribute ordinal, original set, wire set,
primitive/accessor binding and inverse in the capsule. No fabricated UV set,
canonical source rename or glTF 2.1 upgrade. A catalog set is separately bound to
one canonical source set. Apply the complete composition to **every actually
selected** baseColorTexture effective texCoord, including default zero. Never
copy a catalog's numeric texCoord without remapping.

Catalog absent requires no material-ID role and empty material/catalog-UV maps.
Catalog present is a real retained `GltfAsset` on the same budget; every used
signed I32 source ID has exactly one explicit binding to an actual catalog
material ordinal. No unused mapping or duplicate ID is admitted. Traverse only
the bound material/resource subgraph. First-use source-ID order determines output
material ordinals; two IDs mapping to one catalog definition remain two output
material entries to keep inverse attribution unambiguous. No default material is
invented for an unknown ID. The original source ID bits survive in the capsule.

The exact selected material object keys are `name`, `pbrMetallicRoughness`,
`alphaMode` and `doubleSided`. The last two admit only absent/default OPAQUE and
false, including those explicit defaults in the real selected DATA catalog.
PBR keys are `baseColorFactor`, `metallicFactor`, `roughnessFactor`, and
`baseColorTexture`; absent factors retain glTF's actual defaults. A texture-info
admits only `index` and `texCoord`; a texture only `name`, `sampler` and `source`; a sampler
only `name`, `magFilter`, `minFilter`, `wrapS`, `wrapT` with actual glTF 2.0 enums
and defaults. Selected images admit `name`, `uri`, `mimeType`, `bufferView` only,
with valid mutually exclusive source forms and actual captured bytes. PNG/JPEG
MIME only; media declaration must agree with retained image resource provenance.
For image bufferViews the selected closure also admits raw bufferView
`buffer/byteOffset/byteLength/name` and buffer `uri/byteLength/name` definitions;
target/stride/extensions/extras refuse. Their original definitions/provenance are
retained, while only the actual selected image slice is copied, not unrelated
buffer payload. Catalog extensionsUsed/extensionsRequired must be absent/empty.
All other models/texture roles, nondefault alpha/double-sided/alphaCutoff,
extensions/extras and unknown selected-object fields are typed unsupported, including
KHR_texture_transform. Unrelated catalog objects remain retained source context
but are neither copied into the ordinary output nor claimed as selected content.

Selected image bytes and raw selected definitions are retained, hash-bound and
resource-index/source-ordinal mapped; no definitions are inferred from material
IDs. JSON emits unique local image members; GLB embeds selected images in the BIN
buffer and records logical buffer-view extents. No data URI/network resolution,
path traversal or caller-authored URI is emitted. Actual byte copying/rebasing is
owned and charged. Image coverage is byte/structural-reference preservation;
neither operation raster-decodes, validates every PNG/JPEG internal structure,
or proves pixels, shading, color management or visual equivalence. Positive PNG
fixture conformance needs its independent DATA authority. This restriction must
be visible to external readers and future publication receipts.

## STL profile, units and normals

No orientation, axes, UV or unit conversion is performed. glTF requires the
explicit declaration that source values already use meters/right-handed/Y-up;
the encoder cannot establish their real-world meaning. STL permits a bounded
UTF-8 `declared_unit` label stored in the capsule only; bare STL cannot claim it
embeds a unit. Unknown/required axis/unit conversion is unsupported.

The corner-normal role requires three finite components, no set/offset/presence,
and equality of all three corner vectors **by source scalar bits** per charged
triangle. F32 raw vectors widen exactly for the derived face-normal channel;
F64 vectors require selected finite exact representability for binary STL.
ASCII retains exact finite F64 vector values. Normal rounding is not enabled by
the position policy. The derived materialized mesh carries only actual encoded
positions/triangles and this actual face normal. Existing `WriteStl` re-inspects
it and remains fully guarded; original custom attributes live only in capsule.

This nonunit raw-facet-vector rule is the existing bounded codec profile. It is
not a universal STL semantic, unit-normal fidelity, manufacturing/solid/print
claim. glTF NORMAL's unit constraint is preserved by omitting the nonunit rich
normal under explicit loss acknowledgment. No normalization or fabricated normal.

## Strong owners, facts and temporal availability

All public strong handles are copy-only, non-null, private-constructor owners.
Copies allocate nothing, share immutable actual storage and its original charges.
They cannot be manufactured from a caller-supplied boolean or JSON report.
`SurfaceOutput` retains genuine original/triangulated/catalog owners and its
actual encoded members, maps and facts. `SurfaceOutputInspection` retains the
expected output and newly captured actual members plus completed typed proof.
It is constructed only after independent actual decode and full comparison.

Temporary GLB/STL output and decoder owners do not escape either operation.
Before success, copy actual wire/captured bytes and actual typed observations
into the new capped state, then release those temporary owners. Inspection
consumes actual ReadGlbContainer/ReadGltfAssetCapped/ExtractGltfPrimitiveCapped or
ReadStl results. It does not promise decoded-owner getters after return. Copied
facts record actual return observations; releasing a decoder cannot invalidate
the retained captured-byte proof. No fake `GltfFileAsset` or old topology proof.

`SurfaceFacts` can be returned on expected failure after its actual state was
allocated. It exposes only completed count records, actual attempted/completed
numeric observations and real delegate call records. Unreached records are
absent; a zero is not a synthesized delegate call. Facts may keep the entire
operation owner alive, including successful output bytes. Its requested-byte
getter and the output/proof getter observe the same distinct operation ownership
where shared; never sum them. They exclude pre-existing input/catalog storage,
already released temporary delegates, allocator heap/runtime overhead and RSS.

The private state is an acyclic ownership DAG. Facts() returns an allocation-free
strong value, not a reference to a self-owning embedded handle. Output/facts alias
one common allocated immutable operation state/control block (facts points to
its embedded facts subobject); no state contains a shared_ptr back to itself.
Failure uses a facts-only owner after temporary output storage is cleared.
The inspection state retains an expected output from the earlier operation,
never the reverse. All own operation storage remains until the last alias dies,
so its retained request getter is stable and counted once. Private request-limit
seed/allocator backend are real owned lifetime-stable state, never a caller-frame
observer pointer or optional-NRVO-dependent scope escaping with containers.
No peak getter is promised. Requested-byte facts for GLB are absent because the
existing public API has no such getter; its actual worker/status/offset facts
remain available. Other delegate byte facts exist only when the real getter does.

Borrowed spans/strings/ref getters need their visible handle alive and unreassigned.
Separate live copies support concurrent reads. Assignment/destruction cannot race
the same handle or views. Borrowed input/options/catalog remain unchanged through
return and all joins. No worker outlives any operation scope. Independent calls
on the same ledger share actual budget/worker slots and use fresh stop tokens.

## Accounting, containment and execution

Every active scalar count/byte/work cap is positive and inclusive, with checked
uint64/size_t/ptrdiff_t and actual delegated host extents. All-zero inactive
delegate objects are canonical and ignored; nonzero unused objects are invalid
to prevent pretending they ran. Numeric error bounds are finite nonnegative and
may be zero. Validate active nested objects and checked allowance sums before
stop, source traversal or allocation. Optional material/image/table counts are
still positive caps when zero actual objects are present.

Own encoding scope E and inspection scope V each use an actual separate fresh
request-limited allocation context on the original root ledger. It owns all
JSON token/number/string/DOM storage, metadata/descriptors, hash state, exact
integer/predicate limbs, sort/group/inverse maps, planning, nested vectors,
member/capsule storage, control blocks, scratch, worker records/failures and
state freeze. Rebind/growth includes old+new overlap before the actual request.
No nlohmann/std default allocator tree, uncontrolled stringstream, hidden global
cache, TLS/default allocator escape or post-return charging is permitted.

Admission allowance (checked arithmetic, no clamping):

* JSON E = own E cap.
* GLB E = own E cap + actual GlbLimits.max_requested_bytes.
* STL E = own E cap + derived MeshMaterializationLimits.max_new_requested_bytes
  + StlWriteLimits.max_total_new_requested_bytes. The codec total already covers
  its own codec+inspection caps; do not add those again.
* JSON V = own V cap + GltfReadOperationLimits.max_new_requested_bytes
  + one GltfExtractionLimits.max_output_requested_bytes.
* GLB V = JSON V + actual GlbLimits.max_requested_bytes.
* STL V = own V cap + StlReadLimits.max_total_new_requested_bytes (already codec
  + materialization + inspection). No duplicated nested cap.

The configured total allowance must cover the applicable sum; it is a
conservative containment requirement, not a reservation, peak or promise that a
ledger that small succeeds. Existing input occupancy and unrelated same-ledger owners
still apply. The GLB/read and read/extract overlaps are genuinely admitted.
Primitive extractions are sequential and destroyed before the next; their
retained comparison observations are copied into own V before destruction, so
there is at most one extraction cap live. Independent prototype validation must
prove that destruction/exception order, not silently multiply/omit owners.

Budget denial is `kBudgetExceeded/kRequestBudget` without asserting operation
cap versus shared-ledger attribution when underlying allocation cannot distinguish
them. Logical extents/physical host ceilings have their distinct Limit statuses.
Unexpected bad_alloc/thread/backend/runtime exceptions propagate after all joins
and cleanup; no normal result is fabricated. No OS-runtime/RSS guarantee.

Reserve/admit the actual typed delegate/profile observation slots before each
call/query, using checked bounded counts. Fill them after return without an
allocation; if reservation itself refuses, the call never ran. Facts freezing,
strong alias handoff and unexpected-exception capture do not use a fallible
emergency JSON/detail path. This prevents a later cap failure from silently
erasing an already completed delegate record on a normal expected return.

Large source comparison, descriptors/payload copying, per-vertex quantization,
map/CSR filling, expected/actual field comparisons and hashing use disjoint work
through existing private operation worker machinery, with real joined telemetry.
Exact new-coincidence validation uses the charged private sort/group/anchor
predicate in [numeric conversion](#numeric-conversion), not mandatory all-pairs. Its O(V+C+U log U) work includes
collecting U used IDs from C corners and V original rows; all same-executor sort,
group and anchor comparisons charge max_coincidence_comparisons. Private keys
canonicalize signed zero for comparison only; source/native bits, IDs, maps and
output order remain untouched. No welding, hidden unbounded comparator or new
dependency/executor/default is introduced.
Serial prefix/CSR accumulation, JSON extent scanning, stable group discovery and
ordered earliest-error reduction are dependency phases; they still use bounded
cancellation checks. No second executor, callback framework, ignored job bound,
cap clamping or fake launched counters. Actual delegate counters remain distinct
from new-module counters. Caller work is valid with zero launched workers.

The count limits include `max_coincidence_comparisons` and `max_relationship_steps`
positive u64 fields. Charge actual comparisons/graph
edges; refuse before the next step, preserve examined counts and no accepted
owner. This is a logical complexity ceiling, not a memory cap or prefix-job claim.

## Filesystem publication boundary

No filesystem action occurs in these two APIs. A later publisher must take this
genuine `SurfaceOutputInspection`, not an old strict-subset type, and own a closed
member inventory. Before stage creation it validates all encoder/inspector/
publisher caps, paths, no-replace mode and callbacks even if stop is requested.
Stage writes actual members into a fresh owned staging directory; it reopens,
captures and independently inspects every actual physical member with fresh V
work/caps. A pre-stage in-memory proof cannot stand for that reread. Unknown,
missing, symlink/reparse/unsafe members refuse by its explicit platform contract.

Publication receipt contains exact hashes/lengths/closed roles of all payload
members including surface.json, excludes its own hash, and distinguishes actual
write/reread/inspection/precommit observations. A receipt provider, if selected
by the later file interface, runs after actual staged proof and before generated
receipt write/close. After the provider, repeat complete closed-inventory and raw
payload/receipt verification. Every provider, worker join and fallible owner
preparation finishes before the native no-replace commit; only a no-throw actual
committed-result handoff follows it. Rename/commit/cleanup outcomes are typed
return observations after they actually happen, never predicted success in a
precommit receipt. Failure/cancel cleanup and retained-owner lifetime need real
fault controls. No generic transaction or fake `GltfFileAsset`. A separate full
file interface/schema/provider contract and independent review remain mandatory;
file, installed package, Repair, Python, single/batch and independently qualified
platform routes remain future obligations.

## Count and work limits

## Count dictionary

max_vertices/faces/corners/face_offsets/attributes/scalars/attribute_bytes/
attribute_offsets/presence_bytes/metadata_entries/descriptor_bytes apply to each
complete original and triangulated snapshot independently. Scalar counts include
all backing, even missing rows. Attribute bytes are scalarcount*actualtypewidth.
Descriptor bytes are name+semantic+metadata key/value raw bytes (not hex length).
Face offsets count includes the final sentinel. Offsets and presence include
present-empty versus absent distinctions. Enforce caps on actual inspected counts
before constructing descriptor or payload copies; no retained-source cap guess.

max_wire_primitives/vertices/corners bound the total expanded ordinary output;
triangles=corners/3. A glTF primitive separately must fit its actual u32 index and
glTF component/count constraints. max_map_entries is the aggregate count across
all Geometry-map copies, forward wire maps and inverse offsets/member arrays.
max_members includes ordinary + capsule members, never a future receipt.
max_member_bytes applies per member, max_total_member_bytes to their actual sum.
max_resources/max_resource_bytes bound selected image count/aggregate exact image
bytes, including alias copies for distinct selected source image ordinals.
max_json_bytes bounds ordinary glTF JSON; max_binary_bytes bounds ordinary
geometry/image logical binary; max_capsule_json_bytes/max_capsule_binary_bytes
separately bound sidecar serialization. Their output-byte overlap still belongs
to own request cap; no byte cap is a promise of equal allocation usage.

max_material_bindings bounds signed-ID bindings and each selected definition
table; max_numeric_token_bytes bounds any ordinary/capsule numeric token,
including raw catalog factor tokens and decimal dyadic numerators; max_json_nodes/
depth apply independently to capsule parsing and selected catalog parsing.
max_numeric_integer_bits bounds every exact-number/predicate intermediate. Use
checked significant-bit upper bounds before growth; if a bound cannot prove the
intermediate fits, return LimitExceeded/CountLimit before that request rather
than allocating beyond the declared logical intermediate ceiling. This ceiling
is conservative admitted-work extent, not an assertion that the mathematical
final answer itself needs that many bits. Own request cap covers actual allocated
limb capacities/control/old+new growth. No hidden unlimited big-integer backend.
max_coincidence_comparisons bounds EVERY actual completed bounded sort-record
key(+ID), adjacent/group-key and original anchor-position comparison in the exact
sort/group predicate ([numeric conversion](#numeric-conversion)), not hypothetical all-pairs work. Before each,
admit one permit; denied comparison runs/counts none, admitted noexcept bounded
comparison completes/counts even if stop is newly requested. Concurrent permits
prevent overshoot; real joins/reduction preserve actual counts. No clamping.
max_relationship_steps similarly bounds actual selected graph/map edges.
source_scalar_pairs counts actual whole paired scalar checks, including every
performed repeated/unretired wire copy and actually loaded anchor source pairs;
cached sort-key comparisons are not fresh source loads. descriptor_byte_pairs
counts actual compared descriptor bytes. No completed-prefix/bytes-never-read
claim. O(V+C+U log U) sort/group/anchor storage/overlap is charged before allocation.

Numeric summaries independently retain the SAME uniquely referenced logical
source-component units across PreparedFields/E DecodedWire/V DecodedWire. Every
wire occurrence is checked/raw-counted; only a fully verified complete group
retires count/change/max/witness. Private provisional/failed/stopped groups stay
unretired. Latest Kind transitions per actual reached field to fresh decoded
state, not relabelled prepared facts; unreached fields retain prior state/absence.
See [phase results](#phase-results) for exact complete/partial/zero and operation-ordinal rules.

Work surface.max_jobs/partition/cancellation values are positive inclusive actual
policy fields; no default/clamp to make planned jobs fit. Glb/Gltf/Stl work objects
active only when their actual delegate is used; inactive objects all-zero. The
glTF direct-translation tails are all-zero/static and ignored by its existing
static reader/extractor; this surface module is not the animated profile.


## Numeric conversion

## Storage support, finite classification and narrowing

The compiled profile requires eight-bit bytes, actual IEEE binary32/binary64
object representations and exact-width unsigned integers. SurfaceNumericSupport
reports only those representation predicates, not a platform/toolchain support
floor. After configuration validation, unsupported storage returns
Unsupported/NumericProfile before traversal, allocation or stop. It is not
silently accepted via reinterpretation. Actual compiler/SDK cohorts still require
qualification. No inherited GNU/Windows execution is implied by this proposal.

Finite/sign/zero/exponent/significand are read as unsigned representation bits.
No floating equality, cast, lrint, arithmetic comparison, current rounding mode,
DAZ or FTZ state determines conversion or error acceptance. Opaque attribute
NaNs are capsule bytes; native fields must be finite. Signed zero is preserved.

For exact32, finite F64 must have exactly equal binary32 mathematical value and
sign-zero representation. For boundedRne32, write the exact positive magnitude
as integer significand times a power of two. Choose the enclosing binary32
quantum: 2^-149 for subnormals, otherwise the normal exponent's 24-bit precision.
Divide by that exact quantum into integer quotient and remainder. Less than half
rounds down, greater than half up, exactly half chooses the even quotient; carry
renormalizes. Apply sign after magnitude rounding; preserve source zero sign.
Reject source magnitude above the finite binary32 maximum **before** rounding,
as well as carry overflow/nonfinite. Reject nonzero source rounding to zero,
including exact half-min-subnormal. Finite nonzero subnormal results are allowed.
F32 source fields copy exact bits; widen to F64 only by bit decomposition.

The position encoding×mode matrix is closed: glTF JSON, GLB and binary STL
admit Exact32 or BoundedRne32; ASCII STL admits ALL THREE Exact32, BoundedRne32
and Exact64. Exact64 is ASCII-only. A known mode incompatible with its encoding
returns InvalidConfiguration/EncodingLimitsMismatch in Configuration, before
stop, source traversal or delegates; it is not UnknownEnum for a known selector.
ASCII Exact32/BoundedRne32 first computes the selected exact/RNE32 target under
the existing finite/range/underflow/bound/geometry rules, then BIT-WIDENS that
F32 word to the exact F64 codec field, without a host floating cast. Actual ASCII
decimal serialization/readback is F64: compare every actual decoded F64 word
against that exact bit-widened chosen 32-bit target, with no second cast/narrowing;
position error remains against the original source F64. ASCII Exact64 retains
the original finite F64 bits with exact decimal round-trip. Raw ASCII normals
remain exact finite F64 independently of position mode. UV bounds remain mandatory
per selected UV even in Exact32; Exact32 requires zero measured error regardless
of larger bound. UVs retain the same 32-bit selector/own bounds for glTF, and ASCII
has no UV slots. An unadmitted scalar-type conversion is Unsupported/ChannelShape,
not implicit integer→float conversion.

Position conversion occurs once for each used original vertex and component.
Every seam copy reuses those exact resulting bits. UV conversion occurs once per
selected source row/component; corner rows remain separate. Unused vertex rows
are retained only in the capsule. No rounding of unused data is fabricated.

UVs use the same exact32/boundedRne32 selector as glTF positions, with their own
per-set bounds. Exact64 permits ASCII STL positions only, where UV list is empty.

## Actual wire serialization and delegated ASCII profile

The bit rule extends to emitted words and derived textual fields. Store/load
binary32 words by unsigned bits plus explicit little-endian bytes. No host
float→double conversion widens a subnormal for min/max, UV, normals or comparison.
Compute POSITION min/max by exact sign/exponent/significand ordering; signed-zero
ties use -0 minimum/+0 maximum when present. Format glTF min/max as exact decimal
dyadics using bounded integer powers of two/five, with explicit signed-zero token.
Do not use a floating ostream/to_chars or arithmetic promotion. Catalog factor
tokens are retained exact admitted decimals rather than newly floating-formatted.
Ordinary resource length/offset/index JSON tokens use exact integer formatting.

Existing binary STL Narrow/Widen already use unsigned representation fields;
the new adapter supplies exact widened prepared words, not a cast under DAZ.
Existing ASCII writer uses FiniteDoubleToChars in numeric_format_detail, with
max_digits10 output. Its actual source path/qualification remains a dependency,
not a broad standard-library/Fenv assumption. The encoder additionally verifies
actual emitted ASCII numeric tokens with a bounded integer decimal→RNE64 parser,
including sign-zero, and actual binary words by explicit LE loads, before a
successful output/capsule advertises DecodedWire summaries. This check is an
encoder-side wire guard, not independent reload acceptance.

Existing ASCII ReadStl uses std::from_chars(double). Actual glTF ReadFloatBits and
cgltf ProjectedFloat also call std::from_chars(float); lexical helpers can have
the same seam. Bit-RNE alone proves none of those under arbitrary rounding/FTZ/
DAZ state. The selected profile therefore requires a **positively established
nearest-even, FTZ-disabled, DAZ-disabled baseline on every actual execution/
parsing thread** immediately before each unproved numeric read/extract/lexical
delegate. This includes source material-factor projection, actual ReadGltfAsset-
Capped, each actual ExtractGltfPrimitiveCapped, actual wire factor projection and
ASCII ReadStl. GLB's integer container reader does not itself prove its embedded
glTF floating metadata. No caller-only snapshot stands for another worker.

If state is unqueryable/unestablished or any condition fails, return Unsupported/
NumericProfile at the reached Catalog/AssetRead/PrimitiveRead/Compare stage
**before that delegate**, with no invented call/proof and a reached
NumericDelegateProfiles record. Unknown fields are absent, not guessed true.
The new operation changes no caller/global/per-thread FP state. Existing serial
lexical parsers run on the actual guarded calling thread. The selected wire's
accessors are nonnormalized FLOAT and UNSIGNED_INT, so their permitted worker
decode is checked bit-copy/integer work, not float promotion/normalization. If
implementation/cohort source inspection cannot establish that restriction, or
delegates numerical work to an unqueryable thread, it must refuse before such
delegation; it cannot infer worker state, silently clamp work serial or claim a
caller check covers it. Exact source/thread-path proof and actual qualification
are mandatory, with no global existing-reader strengthening selected here.

Profile statuses are closed: EstablishedBaseline (all three facts true),
UnsupportedRounding (observed nearest_even=false), UnsupportedFlushState
(observed FTZ/DAZ not disabled), Unqueryable (one or more required observations
unavailable). In precedence, unqueryable query first, then rounding, then flush.
After an established profile, an immediately noticed stop returns Canceled before
the call; no delegate observation is invented. The snapshot is synchronous, not
a guarantee against caller signal handlers or an unsupported concurrent mutation
of that thread's FP controls during the call.

Independent exact-decimal/word verification **and** actual selected native reader/
extraction bit/value comparison under the qualified baseline are both mandatory
before inspection success. A profile refusal can leave earlier captured/source/
completed delegate facts reached; the refused call remains absent. Encoding's
optional catalog projection can have a real profile observation, while an
uninvoked ASCII reader cannot. Encode alone is no native reload proof. No global
codec change, arbitrary-state decoder claim, new platform floor or hidden state
reset. Native permissiveness/vendor charconv prose is not a substitute for actual
cohort controls, including JSON min/max, signed-zero/subnormal accessor/normal/UV
words and ASCII token/readback under recorded actual parsing-thread conditions.

## Catalog factor precision

The admitted catalog is native/core binary32 metadata plus separately retained
raw JSON, not an exact-decimal/F64 material-value promise. For each selected
authored baseColor RGBA, metallic and roughness token, observe the existing
ReadFloatBits on the genuine catalog's private immutable JsonSnapshot under the
guarded baseline; no new public getter or fake asset is invented. Existing
gltf_detail::Access::State(catalog).document and ReadFloatBits are the concrete
private seam. Check source token size and exact [0,1] bounds before that call.
Absent fields contribute actual glTF default binary32 words with authored=false
and no fabricated lexical call. SurfaceCoreFactorFacts records actual observed
six words/authored/observed flags and actual lexical count. Its global operation-
local collector ordinal joins per-authored-field profiles using factor_component
0..5, as closed in [phase results](#phase-results); actual native failure retains its real diagnostic.
Defaults or profile-only refusals do not fabricate native calls, while a truly
entered zero-lexical collector remains a real partial/complete observation. These are explicit new-operation projections,
not claims that the old asset stored a material getter or was parsed in this
new call's FP environment.

Copy selected authored factor tokens into ordinary output; do not rewrite them
through F64. Independently project actual generated/reloaded factor tokens through
the guarded native core-F32 path and compare **all six words** to the observed
source core-F32 words, while raw selected source fragments stay exact in capsule.
Common decimal0.1 is admitted: its core binary32 value is approximate to decimal
0.1 but no new writer-induced factor-word change is allowed. Neither exact raw
decimal→F64 preservation nor shader/rendered equivalence follows. No blanket
exact-decimal-F32 rejection or new factor-error tolerance is selected. Unreadable/
out-of-range/nonfinite native factors refuse with actual typed observations.

## Bounds and exact dyadic observations

The caller supplies one finite nonnegative L-infinity position bound in declared
source units and one finite nonnegative component bound per selected UV set.
Their IEEE F64 bits define exact dyadics; -0 is the same zero bound. No default
epsilon, relative-error escape, norm averaging or scene-scale tolerance.
Compute |exact decoded native value − exact source value| as an integer dyadic
and compare exactly to the relevant bound. Inspect independently computes this
from actual reloaded bytes, not from expected conversion output or writer facts.

Public error magnitude uses 34 little-endian u64 words, used_words and signed
exponent2. Zero uses all-zero words/used_words=0/exponent2=0; otherwise normalize
to odd low word and nonzero high used word. An exact difference of two finite
binary64 values needs at most 2099 magnitude bits (common denominator 2^-1074,
largest possible finite difference below 2^1025); 34 words provide 2176 bits.
The inline representation does not bound predicate integers or their allocator;
max_numeric_integer_bits and the real request cap independently admit those.
JSON emits normalized unsigned decimal numerator and exponent2. Integer tokens
are parsed with bounded exact integers, never through binary64/JavaScript Number.

All summary kinds use the unique referenced logical source-component groups
defined in [phase results](#phase-results): POSITION by used original ID+XYZ, UV by selected attribute/
used vertex-or-corner row+component. PreparedFields records actual one-source
conversion; E/V DecodedWire are fresh observations, never promoted counts.
Every actual wire copy is independently source-paired/checked; only after all
copies and complete membership/type/exact-target/error prerequisites pass does
the group retire once into examined_values/changed_values/max/witness. Raw
source_scalar_pairs includes all actually performed repeated/unretired paired
checks plus other actual correspondence work; no decoder-internal load is
invented. Zero output has zero groups, not conversions of unused capsule rows.

Each field transitions to fresh partial DecodedWire only when its verification
actually begins. Other fields retain their prior reached state/absence. Private
provisional group state stays charged, not reported as retired. Partial failed/
stopped/inconsistent groups add no summary count/max/witness, but keep actual raw
work. complete waits all groups plus numeric geometry/field prerequisites; later
unrelated failure need not erase a reached complete numeric observation. These
metrics certify no accepted output/proof by themselves. Nonzero maximum uses
first logical source-row/component witness; zero has none. changed_values counts
retired groups whose decoded/widened bits differ from source, not operations or
wire copies; F32 exact copies have changed=0. Signed-zero change is forbidden,
not hidden by zero numerical error. Capsule metrics are complete DecodedWire only.

## Geometry predicates after conversion

Use exact rational/dyadic positions, never tolerance decisions or floating cross
products. Preserve distinct original IDs and original coincident positions; no
welding. New coincidence means two used original IDs have equal decoded
positions but unequal original exact positions. Use private records for U used
IDs, collected from C corners and V original rows. Each record has three u64
key words plus its original ID: zero-extended finite F32 words for glTF/binary
STL, FULL finite F64 words for ALL ASCII position modes, including Exact32/
BoundedRne32 after exact bit-widening of their 32-bit target. Never narrow ASCII merely
for sort or take an Exact64 identity shortcut. PreparedFields uses the actual
prepared selected-representation keys;
E/V decoded observations build keys from actual decoded words after all copies/
XYZ prerequisites are verified, not substituted prepared values. Missing/partial
decoded coordinates do not fabricate keys or completed geometry predicates.
For equality/sorting ONLY, canonicalize either signed zero word to +0. Preserve
actual source/native words, IDs, maps and output order; no weld/native-buffer sort.

Deterministic bounded bottom-up merge sort orders the unsigned three-word key,
then original ID. Its single admitted scratch array, old/new growth overlap,
group ranges, source-ID lookup and used-ID collection are real same-ledger own
storage; no default-allocator hash map, new dependency/executor or inferred peak.
Use admitted same-executor merge jobs and actual joins. Dependency-ordered merge/
group-prefix work remains bounded serial work with cancellation; substantial
independent anchor checks use planned jobs, including within a large key group.

Adjacent-key equality (without ID tie-break) partitions exactly equal decoded
positions. A group's anchor is its smallest original ID. Compare each other
member's original exact F64 XYZ to that anchor, canonicalizing signed zero only
in equality keys. Finite F64 words otherwise uniquely represent exact values;
no floating equality/epsilon. All originals in a group are equal iff every one
equals its anchor, so at most g-1 anchor comparisons give the same all-pairs
predicate. Pre-existing coincident distinct IDs stay permitted. This establishes
complete equivalence, not merely a spatial/hash heuristic.

For complete joined work, reduce each group's lowest differing candidate in
source-anchor order: the lowest source ID and its first differing member are
the globally lexicographically earliest offending pair. Sorted-key order alone
is not source order. Partial concurrency reports only the lowest actually
established pair under existing error/stop precedence, never a global-earliest
claim through an unfinished lower-ID chunk. Diagnostic subject is Vertex with
index=anchor, secondary_index=candidate, no invented component. Sorting completes
before the anchor phase; raw/retired counters never imply a completed prefix.

Complexity is O(V+C+U log U), storage O(V+U), with U<=V. The C term accounts for
collecting/validating used IDs from actual corners; required V scans/index maps
are not hidden. No mandatory quadratic pair scan is retained.

max_coincidence_comparisons and WorkCounts.coincidence_comparisons replace their
old pair-named fields. Count EVERY completed sort-record key comparison (including
ID tie-break), adjacent/group-key equality, and anchor-position tuple comparison.
Sort and group equality are distinct: ID only breaks sort ties, never separates
an equal-position group. Each comparison is bounded <=three component words plus
ID, bit-only/noexcept. Admit one permit BEFORE it; denied comparison runs/counts
none. An admitted comparison completes/counts before responding to a new stop.
Concurrent permits prevent cap overshoot; actual completed counts are joined/
reduced before normal return. Poll before permits and bounded moves/fills. Group
index fill and moves are real bounded work, not fake comparisons. Existing
LimitExceeded/CountLimit handles the cap; no clamp/retry/new refusal status.

Actual whole original scalar pair checks inside the anchor test also contribute
source_scalar_pairs, only for components actually loaded before short-circuit.
Cached sort-key comparisons are coincidence_comparisons, not fresh source reads.
Descriptor/raw source checks elsewhere remain counted when actually performed.

For every charged triangle, check source and decoded endpoints distinct, decoded
edges nonzero and exact decoded cross product nonzero. Let source normal
N=(b−a)×(c−a) and decoded normal N'. Require exact N·N'>0. This rejects local
collapse/reversal/right-angle degeneracy without a normalization or unit normal.
Actual source degenerate triangles are unsupported. Descriptor normal channel
is independent of these orientation predicates; it is not used to mask geometry.

For corresponding barycentric coordinates λ_i≥0, Σλ_i=1, encoded position
error is Σλ_i δ_i. Therefore each component's error is at most max_i||δ_i||∞,
bounded by the caller's position bound; the same per-primitive argument applies
to selected UV components. This is a correspondence bound for each displaced
piecewise-linear triangle. It does not prove exact source-plane preservation,
global nonintersection, minimum separation/angle, manifold/solid topology,
texture filtering, shading or rendered equivalence. No unexplained area/angle
threshold is added. Typed predicate subjects identify actual source IDs/faces.

## Raw STL normals and ASCII

The three selected corner vectors per triangle must be source-bit-identical;
missing/nonfinite/varying normal data refuses. Binary STL normals must already
be exactly F32-representable, even if positions use bounded narrowing. ASCII
preserves their exact finite F64 values. No normal generation, normalization,
rounding permission or unit-vector assertion. Existing WriteStl's guarded bit
narrowing/finite decimal formatter is used after the derived mesh is real; its
actual refusal remains a delegate observation, never a fabricated success.

## Required independent numeric/data controls (future, not run)

Use original rich19 channels and independently reviewed translated variant,
not attribute-free triangles. Exact controls: original dyadic positions/UVs,
all four encodings, both raw-bit snapshots, all maps and selected real catalog.
Bounded-positive control: independently derive non-F32-exact source positions,
RNE32 results/errors and nonzero maxima; keep every original rich channel and
validate actual decoded bytes. The translated rich fixture provides a bounded-positive source with retained
original channels. Its source-data checks do not establish actual encoder/
inspector qualification or a general numeric policy.

Negative controls: ties even/odd, near-min-subnormal/half-underflow/signed zero,
finite max/out-of-range, exact-only rejection, one-field bound just below actual
error, used-ID new coincidence, edge/triangle collapse and negative/zero exact
normal dot, seam reuse and old coincident IDs. Caller rounding/DAZ/FTZ state must
remain unchanged across actual encode+inspect. Actual decoded-byte tests use an
independent Fraction/bit oracle and rehashed malicious mutations, with no shared
encoder conversion helper accepted as independent proof. These are required
future controls, not reported passing tests or a general loss-policy standard.

## Phase results

## Precedence

1. Configuration, in declaration order: closed selectors/variant; positive active
   own caps; active nested delegate caps/work; checked host extents/allowance sums;
   finite nonnegative field bounds and active array cardinalities; canonical zero
   inactive delegates. First invalid field wins, before stop or source traversal.
2. Actual numeric storage-support profile, then constant-time genuine-source
   storage-validity and actual ledger compatibility.
   Invalid storage and foreign owner precede initial stop. No source counts are
   invented here. Source original IsStorageValid() is an actual completed proof.
3. Initial requested stop returns Canceled before allocation or O(n) traversal.
4. Allocate real own scope/facts state, then ordered phase traversal. In each
   phase, check closed count/extent before touching that extent; deterministic
   earliest subject is lowest source ordinal/component/member order. Poll at
   bounded chunk boundaries. A defect already examined and established wins over
   a stop subsequently noticed; no scan continues to find a preferred error.
5. Concurrent expected failures select the lowest actual examined subject among
   completed chunks, not a fictitious completed prefix. Unexamined values cannot
   become counters or diagnostics. All actual launched workers join. A worker or
   launch exception is rethrown after joins, taking precedence over normal-result
   construction; no synthetic normal result is returned.
6. A delegate's actual returned error is frozen before checking later stop. A
   success followed by stop before final handoff returns Canceled without output/
   inspection, retaining reached facts only. Final handoff occurs only after all
   comparisons, hashes, delegates, releases and joins. No post-handoff validation.

No rule claims deterministic identical examined counts across parallel schedules;
semantic success/output order and earliest *established* errors are deterministic.
Stop never relabels an already returned native error as malformed input.

## Encoding phase order and facts

| Stage | Work actually performed | Available after completion |
|---|---|---|
| Configuration | closed options/caps/work/allowance admission | no facts owner/delegate |
| Source | actual original valid proof/ledger, physical count scan | OriginalCounts only after full checked scan |
| Correspondence | complete original↔charged bit/row/map comparison | TriangleCounts after full checked scan; genuine input handles retained |
| Disposition | exact rule coverage; selected shapes, bare/loss/normal/units rules | no delegate or wire bytes; no numeric summary yet |
| Catalog | real selected definitions/effective UV/resource closure | no actual encoded member; selected graph counts only when complete |
| Plan | stable primitives, complete expansion/inverse extents and allowance | planned wire counts are not yet WireCounts; public WireCounts remains absent |
| Numeric | actual per-source-vertex/per-selected-UV bit conversion, exact sort/group/anchor and edge/triangle predicates | PositionNumeric/UvNumeric actual examined/changed counts, complete flag only if all selected values and geometry predicates finished |
| Encode | actual wire generation; optional derived materialization+STL codec or GLB assembler; actual generated-field byte verification; capsule/hash assembly | actual delegate observations; final WireCounts only after members/maps actually complete; successful numeric summaries now DecodedWire |
| Complete | release temporary delegates, final stop check, frozen owner handoff | Success + output + complete facts; diagnostic Complete/None |

Numeric summaries use one logical unit in every Kind. POSITION keys are
(used original vertex ID, XYZ component); UV keys are (selected source attribute,
used source-domain row, UV component). Vertex UV rows follow used original
vertex IDs; corner UV rows follow original corner IDs through genuine charged/
wire maps. Distinct coincident IDs stay distinct; unused vertex/UV rows are
capsule-only. Zero-output/point-only domains contain zero logical groups, even
when original backing exists. No extraction/conversion call is invented for them.

E PreparedFields retires a group after its actual one source conversion and
finite/type/range/error checks. Its complete flag waits for all selected groups
and required geometry predicates. E DecodedWire is freshly recomputed from actual
emitted words/tokens; V DecodedWire is independently recomputed from actual reload/
extraction. Neither relabels or copies prepared counts/metrics.

For decoded observations, a logical group contains ALL actual wire copies of
that source component, across primitives/seams/facets. Every occurrence is read,
source-paired and checked, must agree in selected-format bits including signed
zero, and meet complete validated membership/type/correspondence/exact-RNE-or-
exact64/error prerequisites. Matching the first copy or any merely in-bound value
is insufficient. Retire the group once only after every copy passes. Only retired
groups contribute examined_values/changed_values/max/witness. A failed, stopped,
inconsistent or incomplete group contributes none of those metrics. changed_values
counts retired groups whose decoded/widened bits differ from original source;
already-F32 exact copies have changed=0. Nonzero maximum uses the first logical
source-row/component tie witness; zero has no maximum witness.

source_scalar_pairs separately counts EVERY actual whole-scalar paired check,
including repeated/unretired wire copies and other actual source/capsule/
correspondence checks. Count only after both scalar words were actually loaded;
do not invent native decoder-internal loads before the module obtains a paired
value, multiply a byte prefix by sizeof(T), or reduce raw work by coalescing.
Rich cardinalities illustrate units: POSITION logical33 versus wire54, each
corner-UV logical24 versus wire36. They are not executed aggregate counter values.

The property is the latest reached observation SEPARATELY for each field. When
actual decoded-field verification for that field begins, create fresh
Kind=DecodedWire, zero retired counts/no witness/complete=false. Do not promote
all UV fields when only POSITION was reached. Unreached fields retain their
prior reached Kind/state or remain absent; UvNumeric contains only actually
reached fields identified by source_attribute, without fabricated placeholders.
Concurrent history of both kinds is not promised. Real earlier delegate/count
records are unchanged. Once a reached zero domain's shape and required numeric
predicates are established, its complete-zero summary is legitimate.

Private per-group provisional state is charged. V can pair/check in PrimitiveRead
while each real decoder is alive, store bounded provisional comparisons and
release that decoder. Retirement waits until Compare validates complete capsule/
wire-map membership and occurrence counts. Provisional state is not retired
facts. Stopped/failed groups remain unretired; other actually retired groups can
remain, with no source-row/job prefix claim. If a bounded reduction itself is
stopped, verified-but-unretired groups are not silently counted.

complete=true waits for EVERY logical group retired and all required numeric
geometry/field prerequisites completed. A later unrelated material/digest failure
may retain a reached complete numeric observation, but no accepted output/proof.
The capsule contains complete DecodedWire summaries only. Encoder wire checks
are not an independent native reload proof.

## Independent inspection phase order

Configuration→Source (expected genuine owner and ledger)→Capture→ContainerRead
(GLB only)→AssetRead (glTF)→PrimitiveRead (each actual primitive, sequential) or
AssetRead (actual STL codec)→CapsuleRead (bundle only)→Compare→Complete.
Capture validates fixed closed logical paths/inventory, captures actual bytes and
hashes them under own V cap. It cannot pass expected bytes directly to a reader.
Comparison with expected member digests occurs at Compare, after actual bounded
readers; a matching digest cannot replace those readers. Malformed mutations
therefore exercise actual decoding without a forged expected strong owner.
Capsule-internal member hashes are checked at CapsuleRead; internally rehashed
mutations exercise relational checks but cannot bypass the final expected-owner
member comparison. No public digest-bypass or fake-proof test hook exists.

ContainerRead establishes only actual GLB structure. AssetRead uses actual
JSON/BIN/external resources; no fabricated GltfAsset. Each primitive's actual
typed extraction is compared before it is released. Accessor min/max, component
type, counts, resource logical extents, zero/space padding, material indices and
all selected graph semantics are checked against this contract. Native-reader
permissiveness does not relax pinned glTF 2.0 requirements. STL read compares the
actual expanded facets/raw normal values, never inferred source identity.

CapsuleRead uses the closed schema plus all binary/relational rules. It compares
both complete snapshots/maps against actual retained original/charged getters;
then independently compares actual native decoded fields/resources against the
capsule's projection and numeric facts. Expected encoder map arrays, byte equality
or matching sidecar hashes alone cannot construct a proof. An inspector must not
call the encoder or use its private conversion function as the numeric oracle.

Inspector PositionNumeric/UvNumeric describes actual source vs **decoded actual
wire** measurements, freshly evaluated with independent exact bit/dyadic logic.
Its Kind is DecodedWire with the SAME logical group units/retirement rules
above, not a wire-occurrence count or copied writer observation. Every actual
copy still contributes its performed raw paired work. Decode observations and
captured-member hashes are actual facts; complete=true waits for full group,
geometry and field prerequisite coverage. Success returns inspection and complete facts.
Any other status returns no inspection even if some actual readers succeeded.

Every unproved floating lexical/read/extract call has the [numeric conversion](#numeric-conversion) baseline
gate on the actual execution/parsing thread immediately before the call. This
includes source material projection at Catalog, actual glTF read/extract/wire
factor projection, and ASCII ReadStl. Failed/unqueryable profile returns
Unsupported/NumericProfile; NumericDelegateProfiles contains only actual observed
fields and the refused kind/ordinal, while that delegate call is absent.
Established profile followed by newly noticed stop returns Canceled/StopRequested
before the call. Earlier reached facts remain; future facts are not fabricated.

## Code→status mapping (closed)

| Codes | Status |
|---|---|
| RequiredFieldZero, ConfigurationExtent, UnknownEnum, EncodingLimitsMismatch, InvalidBound, AllowanceTooSmall | InvalidConfiguration |
| ForeignLedger, InvalidSourceStorage, SourceCorrespondence | InvalidStorage |
| ChannelCoverage, ChannelRole, ChannelShape, ChannelMetadata, BareUnrepresented, LossNotAcknowledged, UvMap, UvReference, NormalAbsent, NormalMissing, NormalNotConstant, Nonfinite, MaterialIds, MaterialBinding, CatalogField, CatalogExtension, ResourceBinding, ResourceMedia, NumericRange, NonzeroUnderflow, Exactness, ErrorBound, NewCoincidence, CollapsedEdge, CollapsedTriangle, Orientation, Units, NumericProfile | Unsupported |
| SourceCount, TriangleCount, CountLimit, DescriptorLimit, OutputLimit, MapLimit, SizeOverflow | LimitExceeded |
| RequestBudget | BudgetExceeded |
| StopRequested | Canceled |
| GeometryDelegate, StlDelegate, GlbDelegate, GltfDelegate | mapping below |
| MemberInventory, MemberPath, MemberDigest, JsonSyntax, Schema, BinarySlice, CapsuleSource, WireShape, WireValue, WireMap, MaterialReference, ResourceBytes | VerificationFailed |
| None | Success only, Stage Complete, all subject/index fields absent |

SourceCount/TriangleCount name which genuine source exceeds a positive physical
cap; they do not assert malformed input. Inspector malformed capsule/native input
maps to VerificationFailed, preserving actual delegate diagnostic, not a new
encoder Unsupported claim.

Delegate normal returns are preserved in the corresponding union with the
actual delegate kind/call ordinal and actual primitive ordinal if applicable:

* Geometry success only when a real mesh owner exists. InvalidConfiguration or
  InvalidTransport after validated internal construction is DelegateFailure;
  LimitExceeded→LimitExceeded, UnsupportedAllocationProfile→Unsupported,
  BudgetExceeded→BudgetExceeded, Canceled→Canceled, InvalidSourceStorage or
  InvalidSelection→DelegateFailure. Preserve actual MaterializationDiagnostic.
* STL success requires its real owner. LimitExceeded→LimitExceeded,
  BudgetExceeded→BudgetExceeded, Canceled→Canceled. All other normal errors during
  encoding→DelegateFailure; during actual inspection→VerificationFailed. Retain
  actual StlStatus/Diagnostic/Execution; byte facts only on actual owner getter.
* GLB Success requires actual container. LimitExceeded→LimitExceeded,
  BudgetExceeded→BudgetExceeded, Canceled→Canceled. Other errors during encode→
  DelegateFailure, actual read→VerificationFailed. Retain status/offset/workers;
  never invent requested bytes or joined/planned counters for this API.
* glTF Accepted requires actual reader/extraction owner. BudgetExceeded→
  BudgetExceeded, Canceled→Canceled. All other actual read/extract refusals→
  VerificationFailed. Keep actual diagnostic incl resource/span/rule. An
  UnsupportedProfile from the current reader is not silently called malformed.

SourceMaterialFactors uses real ReadFloatBits on the retained source catalog
(or its inline GltfFailure). Source projection refusal maps Unsupported with
actual diagnostic; BudgetExceeded/Canceled keep their meanings. WireMaterialFactors
failure maps VerificationFailed except budget/stop. A successful/partial
SurfaceCoreFactorFacts is a real entered collector, NOT an invented GltfAsset/
native-read result or AcceptedDiagnostic. The operation-local identity and exact
partial/default/profile/native-call rules below govern this payload.

Delegate failure at the outer level uses its *Delegate code even when outer
status is BudgetExceeded/LimitExceeded/Canceled/Unsupported; the inner union is
the attribution. Direct own RequestBudget has no invented inner diagnostic.
Expected operation cap vs root denial is not distinguished without a real API
fact. Fixed optional subject fields mean: none→all absent; row subjects need
index; component only numeric scalar subject; secondary_index is pair/edge's
second original vertex or a documented resource relation, never an address.

## Operation-local invocation/profile identity

Each Encode or Inspect has its own globally unique, checked zero-based logical
invocation/reached-attempt sequence. call_ordinal shares ONE domain across both
arrays, not a per-kind/material/primitive counter, thread/job ID, or cross-operation
identity. Reserve actual capped delegate/profile slots first. Assign the ordinal
at actual reached dispatch/entry, before the requisite real query/call; failed
reservation or an unreached call consumes none. Bulk jobs inside an invocation
do not consume more ordinals. Arrays are returned in ordinal order, not worker
completion order; numerical ordinals do not promise stability across executions.

A nonfactor actual native call has exactly one DelegateView, with its ordinal.
Its real prerequisite profile query has ProfileView(call_ordinal,null factor
component) with the same stage/kind/primitive, if that delegate needs a profile.
Integer-only calls may have no profile. Profile refusal or stop noticed after an
established profile leaves that actual profile entry and NO native DelegateView.
An initial stop before dispatch/query has neither entry/ordinal. Thus DelegateView
ordinals can have gaps due to actual profile-only reached attempts. No retry or
duplicate native invocation is implied. After assignment a nonfactor query runs
and is recorded before any new stop check; an integer-only ordinal is assigned
at actual call entry after the pre-call stop check, avoiding invisible normal
return ordinals. Unexpected exceptions still escape after joins, not fake results.

SourceMaterialFactors/WireMaterialFactors each denote ONE actually entered
collector invocation. Its single DelegateView exists even if lexical_calls=0,
including all-default success, stop before first field, or profile-only refusal.
This actual collector entry consumes one ordinal; its inner ReadFloatBits calls
do not consume additional global ordinals. The six fixed fields are RGBA0..3,
metallic4, roughness5, visited in that order. Every authored field reached for a
potential native call has its actual-thread profile immediately before the call.
ProfileView.factor_component=that uint32 index0..5; all these profiles share the
collector ordinal. No profile/default lexical call exists for an absent default.

Profile full key is (call_ordinal,factor_component): at most one null entry for
nonfactor, at most one entry per reached authored component for a collector.
Order is ordinal then fixed component order. Delegate full key is call_ordinal,
at most one. Joining a factor profile always finds its actually entered collector
DelegateView, whose kind/SurfaceCoreFactorFacts.material supplies the namespace:
source collector material is source catalog ordinal; wire collector material is
wire material ordinal. primitive is null for BOTH factor kinds, never overloaded
with material. GltfExtract primitive is its global actual wire primitive ordinal;
other nonprimitive kinds leave it null. Stage reflects actual Catalog/Encode/
Compare role; stage alone is not a namespace or invocation key.

Source collectors follow stable first-use source catalog ordinal discovery and
are source-unique within that operation. Reusing their complete cached facts for
another source ID/wire material bound to the same source definition does not enter
a collector, assign an ordinal, query a profile or fabricate a lexical call.
Actual per-wire factor collectors remain distinct for every reached wire material
ordinal, even if source definitions are reused. Reusing a retained complete source
observation from an earlier operation is data reuse, not a new event in this
operation's arrays; its provenance remains on that earlier owner's Facts().
Any actual source re-projection in the new operation follows the same source-
unique collector rules. Do not synthesize events merely to fill a material table.

Within a collector: a reached default sets authored=false, observed=true and
its real normative default word; it increments no lexical_calls and gets no
profile. For an authored field, lexical_calls increments ONLY upon actual
ReadFloatBits entry, including actual GltfFailure. Profile queries/refusals,
defaults and stop-before-native do not increment it. Success sets observed=true
with the real returned word. Actual GltfFailure retains its real diagnostic,
leaves that field observed=false, and complete=false; no later field is visited.
Profile refusal/stop leaves current/future fields unobserved, complete=false,
earlier reached words/defaults intact, and failure=null unless an actual native
GltfFailure occurred. authored may be true for the reached refused field; future
zero/default-initialized authored/word slots are NOT absence/default facts while
observed=false. complete=true requires all six visited/observed. Capped storage
is reserved before collector/query entry; recording uses no emergency allocation.

Examples are semantic illustrations, NOT executions:

* All six defaults: one actual collector DelegateView, six observed=true,
  authored=false, lexical_calls0, complete=true, failure=null, no profiles/native
  ReadFloatBits calls. This is actual collector work, not a fabricated reader.
* Source material7 collector ordinal0 reaches four defaults then authored
  metallic component4 profile refuses: Profile(0,4), primitive null; collector
  material7/ordinal0 persists with first four observed, component4 authored but
  unobserved, future5 unobserved, lexical_calls0/complete=false/failure=null.
  No native ReadFloatBits failure or GltfAsset is invented.
* Established profile then stop: same intended ordinal/component profile remains;
  ordinary native delegate is absent, or factor collector stays partial; native
  lexical count is unchanged. Initial pre-dispatch stop has no such profile.
* Actual field2 GltfFailure after two authored successes: collector lexical_calls3,
  observed0/1=true, observed2/future=false, real failure diagnostic, and profiles
  only for actual reached authored fields. No successful word for field2.
* An actual GltfRead ordinal0 and GltfExtract ordinal1/primitive0 can each have a
  null-component profile plus native record. A later wire-material0 collector
  gets a new ordinal, primitive null and component-keyed profiles. Its material0
  does not collide with primitive0 or any source-material0 namespace.

## Failure/nullability and lifetime

Success has exactly one accepted output/proof. Every non-success has no accepted
owner. Facts are absent before their actual capped owner allocation, otherwise
only actual completed/partial observations; unreached delegates absent. Escaping
exceptions have no normal result. Output and facts share one operation ownership;
retained requested bytes are counted once across all copies. Failure facts are
bounded telemetry, never a partially accepted mesh, member set or proof. Cleanup
does not allocate/poll stop and runs after joins. No filesystem callback exists.

This interface intentionally assigns no publication, receipt/1, report/1, command
exit or Python result shape. The later staged publisher packet must close those
temporal surfaces without projecting future commit observations backwards.

## Bundle wire and binary rules

## Native member layout

Canonical relative paths are `surface.gltf`, `surface.glb` or `surface.stl` for the
one selected main member; JSON glTF optionally has `geometry.bin`; selected JSON
images have `images/image-<wire-image-ordinal>.png` or `.jpg` with canonical decimal
ordinal. Annotated output additionally has `surface.json` and `surface.bin`.
No caller path, absolute path, drive, backslash, dot segment, empty segment,
percent alias, case alias, duplicate path or extra member is admitted. Expected
output fixes the exact closed logical set; supplied bytes are borrowed until
captured, not read from a filesystem. Names do not imply publication safety.

JSON is valid shortest unambiguous UTF-8, no BOM/lone surrogate/nonfinite number
or duplicate decoded key. Order/whitespace is not semantic except raw selected
source-definition fragments. Native glTF uses asset.version `2.0`, no extensions/
extras, one identity node per actual mesh group, one declared default scene,
TRIANGLES mode and exact core selected material subset. No transforms, skins,
morphs, animation, inherited scene or source asset preservation is claimed.
Source here is a surface, not a whole retained scene asset.

Wire POSITION is FLOAT/VEC3 with actual decoded min/max, no normalized/sparse
accessor; UVs are FLOAT/VEC2 TEXCOORD_0..N-1, same count as POSITION. Indices are
UNSIGNED_INT/SCALAR, unique 0..rowcount-1 per primitive, triangle order preserved.
No index is UINT32_MAX; a primitive too large for this range refuses rather than
silently splitting/grouping differently. Every bufferView/accessor offset,
length, stride/alignment and logical extent is explicit and independently
checked against pinned glTF 2.0, even if the current reader is permissive.
No byteStride for tightly packed blocks. No zero-count accessors or empty buffer.

JSON geometry.bin orders primitive POSITION, ordered UV sets, then indices,
with four-byte zero alignment between blocks. GLB uses exactly JSON then BIN
chunks when binary content exists; selected images follow geometry in BIN,
four-byte aligned, exact bufferView bytes/MIME. A content-free asset has JSON only
and no buffer declaration. Logical buffer length excludes at most three final
zero BIN padding bytes; JSON chunk final padding is spaces. Header/chunk lengths
are actual little-endian u32, checked before construction. All core JSON/GLB
logical geometry/maps/material/resource content is the same. No source GLB opaque
chunks are copied because there is no retained source container claim.

STL uses the existing guarded codec output after actual derived materialization.
Binary has actual caller-supplied exactly80 header bytes, little-endian u32 facet
count, twelve F32 fields/facet, zero attribute word, no trailing bytes. ASCII
uses the explicit bounded single-line codec name and exact finite decimal tokens;
no implicit numeric rounding. Both store actual ordered raw facet normal and
three actual converted positions. Metadata selection follows the actual codec:
binary name empty, ASCII binary header empty. Declared units exist only in capsule.

## Capsule JSON shape and exact integer rules

Top-level schema string is exactly `meshvale.surface-bundle/1`, representation exactly
`annotated-bundle-v1`. All object key sets are closed. Every uint64 count/index/
offset/length is a canonical unsigned decimal **string**, no sign/leading zero,
and checked <=18446744073709551615 before host conversion. This includes zero.
u32 fields (component count/set indices) use exact JSON integers in 0..4294967295;
signed material IDs use exact integers in -2147483648..2147483647. Parser keeps
exact integer tokens, not a float round trip. Dyadic numerator is canonical
nonnegative decimal integer string within its own explicit length/bit ceiling;
exponent2 is a bounded signed integer. There are no NaN/Infinity JSON numbers.

Names, semantics and metadata key/value storage may contain arbitrary bytes,
including NUL or non-UTF8: encode lowercase even-length hexadecimal, never Unicode
normalization/replacement. Metadata is an **ordered** entry array; duplicate
decoded keys are invalid under source storage rules, order is preserved. Source
attribute order, optional set, scalar tag, component count, offsets and presence
are exact. Null offsets/presence differs from a present empty slice. Positions
are complete F64 XYZ rows; attributes use exactly F32/F64/I32/U8/U16/U32/U64 tags.

Both original and triangulated mesh objects contain complete positions, face
offsets, corner vertices and all attribute descriptors/backing. Counts include
all backing scalars, not just present rows; missing rows' storage is not discarded.
`face_offsets` count=faces+1, starts0/endscorners, ordered; all corner indices in
vertex range. Dense scalar count=domainrows*components. Ragged offsets length=
domainrows+1, ordered/endsexactvalues; each row extent divisible by components.
Presence, when present, has exactly domainrows bytes with values0/1. Positions
and topology follow actual strong-owner values; sourceStorageValid is not a
caller-supplied JSON flag. No hashes alone establish storage correspondence.

## Binary payload: little endian, complete ranges, no holes

surface.bin contains no header and no host object images/pointers. Canonical range
order: original positions/faceOffsets/corners, then each source attribute's values,
optional offsets and optional presence; same sequence for triangulated mesh;
Geometry faceSources/cornerSources/faceOutputOffsets; wire maps and inverse CSR
in the property order listed below; then selected catalog definitions and image
bytes in first-appearance ordinal order. Every nonzero range begins at an
eight-byte boundary. Alignment bytes are zero. Zero-length ranges use the current
aligned cursor and consume zero bytes. Final payload has no unexplained trailing
bytes. Ranges never overlap; counts*width and alignment/addition are checked
against uint64, host and explicit caps before access/allocation.

Scalar widths: f32/i32/u32=4, f64/u64=8, u16=2, u8=1. Raw IEEE scalar object bits
are written little-endian preserving all payload bits; topology/maps/offsets are
LE u64 even on another host index representation; presence is raw u8. Slice JSON
has exactly offset,count,bytes,scalar; bytes=count*width. Each descriptor type
must agree with its slice. Each byte of the binary is assigned to one declared
range or the prescribed zero padding. No arbitrary unknown ranges are allowed.

Wire maps in order: triangleSources (wire triangle→charged triangle),
triangleFaces (→original face), cornerSources (wire corner→charged corner),
cornerCorners (→original corner), vertexSources (wire vertex→original vertex),
sourceVertexOffsets/sourceVertexMembers,
sourceFaceOffsets/sourceFaceMembers,
sourceCornerOffsets/sourceCornerMembers.
Each CSR offsets vector has source domain count+1, starts0 and ends exact member
length; members enumerate every corresponding wire occurrence exactly once in
ascending wire ordinal. Unused original vertices have real empty rows. Empty
domains still have a one-element zero offsets vector. Composed maps must equal
the actual Geometry source maps; no map is merely assumed from positional equality.

Primitive records contain wire/global offsets/counts and actual accessor indices
for POSITION/UV/indices plus optional wire material. They partition wire rows and
triangles exactly with no overlap/gap. UV records include original attribute/set,
wire set, complete per-primitive accessor binding. Reverse UV mapping is explicit
and must be the mathematical inverse, not a second independent claim. No duplicate
wire/source sets, absent set guess or fabricated intermediate set. Source catalog
effective texCoord(default0)→sourceSet→wireSet composition is checked for every
selected texture info. Every relationship edge charges max_relationship_steps.

## Material/source/resource capsule

The catalog object has nullable original catalog JSON SHA256 and source material
attribute, explicit signedID→sourceMaterial→wireMaterial records, catalogUV→source
set mappings, selected raw definitions and image provenance. Catalog absent means
all associated arrays empty and nullable fields null. Selected definition records
have kind(material/texture/image/sampler/buffer-view/buffer), source ordinal,
nullable wire ordinal (source-only buffer/view context has none) and a raw
UTF-8 JSON slice from the actual captured catalog. Definitions are complete
selected objects, not synthetic IDs; the [published catalog whitelist](#gltf-20-uv-and-materialresource-binding) is applied to
every selected object. The actual ordinary output object is checked semantically
against these source definitions plus the explicit index/UV rewrites.

Factor JSON numeric tokens are retained after exact [0,1] admission, alongside
actual observed native core-F32 words and authored/default flags ([numeric conversion](#numeric-conversion)).
The actual wire core-F32 words must match those observed source words; raw decimal
retention does not assert exact-decimal/F64 material semantics or shading fidelity.
Material definition records have source_core_f32 and wire_core_f32 six-word hex
arrays; nonmaterial definitions have those fields null. Image records bind source image/resource ordinal, MIME,
original resource origin/table index, exact raw image slice, wire image ordinal
and actual ordinary member or BIN range. Raw URI/name bytes and selected source
definitions survive in their original fragments. All selected images are actual
captured bytes on the retained catalog budget, not newly fetched/generated.
Two distinct source images remain distinct even if their bytes are equal; reuse
of the same catalog image shares its one wire image record. Same for texture and
sampler objects, by actual source ordinal, not content hashing. Two source IDs
mapped to one material retain two wire materials as explicitly selected above.

The full original catalog document hash records provenance, not a promise that
unrelated catalog meshes/images/scenes are in this surface bundle. Only selected
definitions/resources are recoverable. External URIs are never followed during
inspection/publication. Byte-preservation scope does not raster-decode images.

## Hash DAG and inventory

The capsule members array lists all ordinary payload members plus surface.bin,
with exact role/path/u64 length/SHA256 lowercase64hex. It excludes surface.json
and any future publication receipt. surface.bin never contains surface.json or
its hash. Thus hashes form an acyclic payload→sidecar DAG. Native output getter
may separately report surface.json's actual digest. A future receipt hashes all
payload members including surface.json and excludes itself. It cannot predict
postcommit facts. Logical path equality is exact; no filesystem alias claim.

Capsule wire counts use `listed_members` and `listed_member_bytes`, the exact
members array sum excluding surface.json; there is no self-length fixed point.
Native SurfaceWireCounts.Members/MemberBytes are actual complete counts after
serialization and include surface.json. This deliberate projection difference
must not be relabelled or used to predict the sidecar's own length/hash.

Inspection captures every actual member under V before use; actual readers run
under delegated caps. It verifies internal hashes, complete ranges/schema and
original/charged correspondence, independently decoded numeric/resource/maps,
then final expected-owner exact member lengths/digests. A writer's own facts,
matching member hash or schema validation is never independent native decode.
Actual mutated bytes with recomputed internal sidecar hashes still face actual
decode/relational predicates and final expected-owner comparison. No mutation is
relabelled as a new genuine expected output or accepted proof.
