# Retained glTF deformation contract

| Field | Value |
|---|---|
| ID | INTERCHANGE-GLTF-DEFORMATION-001 |
| Version | 0.4.0 |
| Status | Development implementation present; full qualification and release availability pending |
| Owner | glTF deformation admission, retained typed authority and checked preservation |

This contract selects an explicit deformation-retained-v1 processing profile for
index-only triangle selection in glTF JSON and GLB. It does not evaluate skinning,
play animation, bake transforms, compact vertices or export a different format.
Static and direct-translation processing keep their defaults and existing wire.

The numerical rotation policy below is implemented for this development profile.
The Repair contract defines its complete positive application preset tables.
Full workflow and dependency-cohort qualification remain pending; the presence of
source APIs does not establish a supported compiler, ABI, Python runtime or
release. Existing development operations keep their own status.

## GDEF-READ-001: Explicit source interpretation

Append kRetainedCoreDeformationV1=1 to GltfAdmissionProfile, retaining metadata=0.
Append kDeformationRetainedTrianglesV1=2 to GltfProcessingProfile, retaining static0
and translation1. New GltfReadRequest defaults to metadata admission and contains
an explicit GltfDeformationPolicy. The new ReadGltfAssetCapped overload accepts that
request; its original overload retains metadata semantics.

Only these read/processing pairs are compatible:

| Read admission | Processing |
|---|---|
| retained-core-metadata-v1 | static-local-triangles-v1 or direct-translation-v1 |
| retained-core-deformation-v1 | deformation-retained-v1 |

Cross-pairs return InvalidRequest/AdmissionProfileMismatch before dependent work.
A successful legacy read is not upgraded during extraction. Capabilities are
previews, never typed proof or a numerical-thread attestation. The profile-aware
GltfAsset::Capability overload returns the selected profile; new matching data
still requires real binary admission. The old overload keeps its static meaning.

The new interpretation selects Khronos glTF source
[1c84b58ba3338be4731ea546fa4138dcbb93d42d](https://github.com/KhronosGroup/glTF/blob/1c84b58ba3338be4731ea546fa4138dcbb93d42d/specification/2.0/Specification.adoc),
Specification.adoc blob aeaab65b1291d722ee610db247ae43c575ed63e1. Its version is
retained with the real asset/proof; the older rendered registry is not mixed into
the role rules. Under the new read interpretation, targetless sibling primitives
are legal; target-bearing siblings must agree on target count. Every target
semantic requires a corresponding base. Matrix nodes may have weights-only
animation; matrix+translation/rotation/scale fails the actual read. Legacy read
behavior remains unchanged.

## GDEF-DATA-001: Complete typed catalog

GltfDeformationProof is immutable, privately constructed and source-bound. Its
copy/assignment/destructor and borrowed getters are noexcept and allocation-free.
Copies share real owned storage and charges. Borrowed spans/references require
an alive, unreassigned visible owner; independent copies permit concurrent reads.
Reassigning the same copy requires external synchronization. Views, requested
enums, hashes and equal bytes cannot manufacture accepted authority.

The proof retains exact asset/primitive/read/process/revision identity, original
triangle/value requests, all role/use catalogs and the original execution ledger.
Extraction owns the proof; materialization already retains extraction. The asset
does not cache a proof that retains itself. Materialized actual associations and
writer/inspection source owners remain genuine, independently checked objects.

The catalog exposes all known core accessor roles and every shared/inactive user,
not only the active scene or first skin. Accessor records include type/shape,
normalization, exact bounds, dense/strided layout, sparse selectors/replacements,
implicit-zero source, raw values and separately requested derived interpretation.
One payload may have multiple separately validated uses. Float32 raw words use a
byte-word view; this preserves signaling-NaN bits in unconstrained extra IBM rows
without aliasing or arithmetic. Available derived values never replace raw data.

| Role | Required admission and preservation |
|---|---|
| JOINTS_n / WEIGHTS_n | All paired consecutive sets from0, valid indexed spelling, every4-lane set and every authored zero lane; no total4-influence cap, truncation, sorting, merging or normalization |
| Joints | Unnormalized uint8/uint16 VEC4, all vertex counts equal; every lane in range for every applicable real skin, including zero-weight lanes; nonzero-weight joint references unique per row |
| Weights | float32 or unorm8/unorm16 VEC4, finite/nonnegative; all-integer mixed widths require exact dequantized sum1 using uint8*257+uint16=65535; any floating set uses advisory sum guidance |
| Skins/users | Ordered joints, optional skeleton, ancestors/scene membership and all shared/inactive/unreachable users; no fabricated one-skin-per-mesh or skin for an unskinned primitive |
| Inverse bind matrices | float32 MAT4, count>=joints; associated rows finite/affine; absence retained as absence with derived identity; unused extra rows retain exact raw words |
| Morph POSITION/NORMAL/TANGENT | float32 VEC3 delta roles, base required, equal vertex counts, finite; POSITION bounds; deltas are not ordinary unit normals and tangent deltas have no handedness component |
| Morph UV/color | All core sets, permitted float/snorm8/16/unorm8/16; UV VEC2, color VEC3/VEC4 with independent base/delta widths; no clamping |
| Morph omission/defaults | Preserve target/property omission versus authored zero, targetless primitives, sparse zero origins, mesh/node weight absence/overrides and target ordering |
| Core animation | TRS and weights, LINEAR/STEP/CUBICSPLINE, all clips/users/joint/ancestor effects, legal normalized integer rotation/weight encodings, complete frame/tangent grouping |
| Shared roles | Every real accessor/sampler/user edge must satisfy its role; shared bytes cannot hide an incompatible use |
| Unknown data | Exact typed row-local transfer requires explicit declarations; unknown affecting extensions remain legal but deferred; unrelated opaque extras retain bytes without typed-validation claims |

A weights channel without a target node is ignored as a core animation target.
Its catalog entry retains the actual accessor rows and uses, with `node=null`,
`affects_selected_deformation=false` and `values_per_key=0`. This zero denotes an
unbound, inapplicable target width; it does not infer a mesh or group the source
scalars into zero-width frames. Applicable type, finite-value, layout and shared
use checks still apply. Node-bound weights retain the exact positive morph
target count and complete cardinality checks. Extension-defined targets remain
outside this core interpretation.

Dense vertex accessor offsets are multiples of four. For this pinned admission,
a vertex component width smaller than four bytes additionally requires an
explicit `bufferView.byteStride`, even if a packed VEC4 row already occupies a
multiple of four bytes. A present stride also satisfies the core alignment and
extent rules. This requirement applies to dense base streams; sparse replacement
rows remain tightly packed under their separate rules. Legacy admission keeps
its existing behavior.
Nonvertex/sparse selector/replacement views obey the core no-byteStride rule.
Animation time inputs are finite nonnegative increasing float32 SCALAR with bounds.
Output count arithmetic includes key count, target count and cubic factor3; cubic
requires2+ keys. Quaternion value acceptance follows GDEF-QUATERNION-001 below. Cubic tangents are not unit
quaternions. No resampling, quaternion sign change or source normalization occurs.

Known core type/count/reference/layout violations are Malformed. Legal unowned
features are UnsupportedProfile. Caller configuration, resource limits, budget,
stop and source identity retain distinct diagnostics and actual phases.

## GDEF-MAP-001: Canonical and retained associations

Every source vertex row remains. Existing Geometry materialization/inspection and
charged rich storage are reused. Positions widen exactly to double, signed8/16
channels widen exactly to int32, other representable raw scalars retain their
types/bits. Joint channels use truthful joint_indices/joint_weights semantics.
Morph deltas never masquerade as ordinary unit normals. Bindings, animation,
raw packing, default absence and nonrepresentable typed channels remain in the
owned catalog/sidecar, tied to checked unchanged vertex identities.

DeformationAssociations() on GltfMaterializedPrimitive reports actual source
mesh/primitive/accessor/target/set/semantic, canonical destination/attribute and
transfer encoding. The writer checks that actual candidate and correspondence
retain the complete association. No mutable raw Mesh is staged to bypass the
existing charged seam. Canonical ragged0/2/5 rows and dense glTF VEC4 zero lanes
remain different representations; no float16 scalar type is selected.

## GDEF-WRITE-001: Memory and physical verification

For this profile, a vertex-row-local declaration name has one exact source-qualified
form: gltf/M/P/base/L:HEX or gltf/M/P/morph/T/L:HEX. M/P are original
mesh/primitive-within-mesh indices, T is target index and L is the UTF-8 byte length
of the complete decoded authored semantic. Integers are canonical decimal without
leading zeroes. HEX is exactly2*L lowercase hexadecimal digits encoding those
UTF-8 bytes, including decoded control characters; it is not the JSON token spelling
or Unicode normalization. This ASCII suffix is injective and can pass through
command arguments without embedded NUL. Original decoded semantics and exact JSON
spelling remain separately retained. Invalid Unicode and unsupported string
representations keep the existing reader distinction/diagnostic; encoding names
does not grant source admission. The existing decoded-NUL integer-member-name
rule still returns UnsupportedProfile/UnsupportedCoreString; invalid Unicode
remains Malformed/UnicodeScalar. Such refusal creates no canonical name/proof.
Equivalent admitted JSON escape spellings yield one decoded name while OriginalJson
retains each actual original spelling. Check2*L and every prefix/suffix addition before
allocation; encoded bytes count against the original declaration-name, descriptor,
request and shared-ledger parents. Every nonposition
canonical channel uses that name. The association view exposes it alongside the
authored semantic. Morph deltas use gltf.morph.delta semantics with known explicit
row-local transfer, never ordinary NORMAL semantics. Core joint semantics remain
joint_indices/joint_weights. Typed sidecar entries use the same source-qualified
names and checked original vertex identity.

GltfExtractionRequest.deformation_row_local borrows exact vertex-row-local
declarations until return; successful proof stores charged copies. Unknown roles
anywhere in the catalog that may affect the edit require their exact declaration;
unknown face/corner references cannot be made safe by a blanket assertion. Repair
projects only the actual selected canonical names into its existing row-local
options while retaining every source declaration in the proof. The Python/command
row_local_attributes field keeps its existing [domain,name] shape; new-profile
vertex names use this grammar. Old profiles retain their names/semantics. Known
core malformed data and unowned extension effects are not waived by declarations.

New policy overloads of MaterializeGltfPrimitive, WriteGltfTopologySelection and
InspectGltfTopologyOutput preserve existing signatures/defaults for old profiles.
Each new policy overload requires all five arguments, including an explicit final
stop token; it has no default stop. The existing three/four-argument overload and
its stop default remain unchanged. This keeps old all-brace calls in their old
overload; the compatibility requirement still needs actual compilation. A typed
policy call omitting its fifth stop is not a valid new API call.
A new-profile source passed to a no-policy overload fails configuration before its
validator. New policy limits are required positive. Expected failure returns no
successful owner; actual called observations remain available.

Identity output retains exact original JSON; GLB identity retains complete wire.
Changed output only appends the permitted index records/resource and changes
the selected index binding/mode. Old vertex/accessor/resource identities, padding,
tails and ordered opaque chunks remain. Empty changed selection remains unsupported.

Independent output read/extraction/inspection owns its own authority and catalog.
It compares source/candidate/output/reload, complete raw roles, associations and
maps; it cannot borrow the writer's proof as its own. File publication uses the
existing retained capture/staging/provider/no-replace engine. Actual staged
read/extract/inspection and complete physical bytes/inventory precede native
commit. Conditional receipt construction stays precommit. No mandatory postcommit
allocation, callback, report repair or reread establishes success.

GltfFileVerifiedContent::staged_deformation is a borrowed const pointer to the
publisher's own actual staged-deformation observation. It is set only after real
physical capture, expected source/candidate/output inspection and staged byte
verification, before the synchronous on_verified callback or verified-files
provider runs. It is null for legacy or unreached paths. The pointer and facts
remain stable only through that callback/provider return; retain a charged owned
copy for a report. The generic inspection keeps its separate output-comparison
facts. Neither observation establishes native commit, and later failure retains
the publisher's already returned staged facts without postcommit report repair.

## GDEF-OBSERVE-001: Actual called facts

The fixed inline observation has status,diagnostic,phase,complete,location,counts,
predicates,new_requested_bytes,effective_request_cap,work,execution,numeric,
weight_sums,quaternions. Each parent result exposes an optional observation even without a
successful owner. Null means its helper did not return, not zero work. A called
failure copies actual facts after joins before fallible publication/projection.
Unexpected exceptions propagate after owning joins/rollback without a fabricated
normal result.

Floating/mixed weight observations are computed only by actual payload admission.
Their fixed summary has scope,complete,floating_rows_expected,rows_evaluated,
advisory_rows,all_zero_rows,first_evaluated,first_advisory. Scope is all core floating
influence rows in the actual asset, once per primitive/vertex, not per skin/user.
Each sample has mesh,primitive,vertex,set_count,nonzero_weights,sum_bits,
deviation_bits,epsilon_bits,advisory,all_zero. Word fields are exact uint64 binary64
representations from the performed calculation. The bounded two samples do not
replace full owned raw/canonical data. A getter never recomputes observed facts.
Partial completion can be noncontiguous; later failure retains actual summaries.

The numerical profile separates proved integer/bit paths from unproved arithmetic
and delegates. Every actual executing caller/worker establishes strict supported
compiled behavior, query availability, nearest-even rounding, FTZ/DAZ disabled and
all six exceptions masked before unproved work. No production control setting,
clearing, exception suppression, hardware-fault catch or forced-serial workaround.
Refusal is UnsupportedProfile/NumericalProfile at the reached gate. The numeric
record has profile,compiled_profile,guard_calls,established_calls,exact_blocks,
exception_masks,refusal; the refusal retains its own real unit/snapshot. Initial
Windows x64 candidate paths need genuine qualification; no runtime is qualified
by this contract.

The initial compiled candidate requires IEC559 binary32/binary64 and strictly
compiled operations without contraction/reassociation. For each actual SSE2 path,
read MXCSR: rounding bits13..14=0, FTZ bit15=0, DAZ bit6=0 and all mask bits7..12=1.
The canonical mask is (MXCSR>>7)&63, safe value63. Required-unit bits are SSE2=1,
x87=2. Any used x87 requires control-word mask bits0..5=1 and independently
qualified arithmetic/precision/query support; it is not enabled by the initial
SSE2 tuple. Unknown/other unit paths refuse before the delegate. A combined CRT
answer or Geometry allocation capability does not establish actual used units.
Existing sticky status flags are permitted when every required exception is masked.

Configuration/source/profile/extent/seed checks retain their precedence, then
pre-stop before substantial work. Gate refusal precedence is compiled-unsupported,
unqueryable, rounding, flush, exceptions; each returns UnsupportedProfile /
NumericalProfile at NumericGate, with the actual reached location. A stop noticed
after a successful gate precedes the unproved call. Every worker/caller-fallback
and bounded arithmetic block queries its own actual controls. Joined failures
select deterministic source-partition order; actual partial work need not be a
prefix. The bounded aggregate mask sample uses the smallest actual logical
partition, caller before workers when both queried; a refusal owns its own snapshot.
Pure exact classification may have required_units0 with unqueried masks and is
not an attestation of floating safety. No operation changes/restores any controls.

For floating/mixed weight rows, interpret lanes in increasing set/lane order and
left-fold strict binary64 additions in that order. Compute sum-1, absolute
deviation and epsilon=(binary64 value of decimal2e-7)*nonzero_count by the stated
strict operations; both signed zeros count zero. Deviation>epsilon is advisory
only, including all-zero rows. All-integer sets instead use exact checked integer
accumulation, uint8*257 plus uint16 at denominator65535. No advisory rejection,
normalization, getter recomputation or inflation of semantic scalar_checks occurs.

## GDEF-QUATERNION-001: Exact source squared-norm admission

The immutable policy is gltf-quaternion-source-squared-v1. It is a selected
numerical-admission allowance, not exact mathematical unit equality or an assumed
authoring/quantization history. No tolerance override input, normalization,
sign canonicalization or playback evaluation is accepted.

Let S be the exact sum of four source/effective component squares. Admit a VALUE
iff1-R<=S<=1+R, inclusively. R is a squared-norm allowance; evaluation error E is
separate and never widens that band.

| Role/encoding | Exact authority | R | E for guarded filter |
|---|---|---|---|
| Authored node.rotation | Original decimal XYZW, each within[-1,1] | 2000001/1000000000000 | 1/2097152 |
| Animation float32 | Finite raw IEEE words, normalized absent/false | 33554433/281474976710656 | 1/140737488355328 |
| snorm8 | max(code,-127)/127 | 255/16129 | 1/140737488355328 |
| unorm8 | code/255 | 511/65025 | 1/140737488355328 |
| snorm16 | max(code,-32767)/32767 | 65535/1073676289 | 1/140737488355328 |
| unorm16 | code/65535 | 131071/4294836225 | 1/140737488355328 |
| Rotation cubic in/out tangents | Legal typed finite FLOAT/normalized codes | No unit predicate | No unit filter |

LINEAR/STEP every value and cubic row3k+1 receive the unit predicate. Cubic
rows3k/3k+2 receive finite/encoding checks, with no FLOAT[-1,1] tangent rule.
All shared actual interpretations are checked after sparse expansion.

NodeRotationSources() borrows exact authored number views and separate successful
projected words. Authored absence has no source tuple; a matrix node has no inferred
rotation projection. A successful new-read asset exposes QuaternionPolicy();
legacy/unsuccessful source authority does not. The retained proof binds the same
policy, source primitive/read/normative identity. Projection is not source-unit
authority. Existing required node binary32 conversion and its earlier failures
remain distinct and retain their actual diagnostic/stage.

For supported values, establish the actual-thread numerical gate first. Form
a_i in XYZW order: exact widening of node RN32 projection or raw FLOAT, or one
direct RN64 numerator/denominator division per normalized integer component.
No reciprocal multiplication or cached differently decoded double is substituted.
Perform four separate products p_i=RN64(a_i*a_i), then
s1=RN64(p0+p1), s2=RN64(s1+p2), s3=RN64(s2+p3), without contraction,
reassociation or parallel scalar reduction. This quaternion-local division does
not change general accessor-derived values or the weight advisory fold.

Decode s3 as a dyadic and compare endpoints using integer arithmetic:
wholly inside iff s3-E>=1-R and s3+E<=1+R; wholly outside iff s3+E<1-R
or s3-E>1+R. Every overlap uses exact source comparison. No sqrt/hypot/library
norm is used. FLOAT value abs(component)>=2 is an exact early nonunit proof,
before squaring that component; finite large tangents do not take that path.

The exact helpers are bounded: normalized sums fit uint64; FLOAT dyadics use
10 uint32 limbs; node decimal source permits256 canonical coefficient digits
and effective fractional power512 with128 uint32 limbs; endpoint comparison
uses16 uint32 limbs. Source lexemes/zeros are never rewritten. Larger caller
reader limits do not widen this compiled envelope; valid outside-envelope data
is UnsupportedProfile/UnsupportedNumberRepresentation. Integer inner loops
checkpoint stop within16 steps, including inner products. Rows partition by
scalar and token/limb work; shared workers, actual joins and parent charging remain.
Exact fallback never bypasses an unsafe numerical-thread gate.

Actual source nonunit/range is Malformed (CoreRange for node, BinaryValue for
animation). Host state is UnsupportedProfile/NumericalProfile. First source
refusal has role,encoding,node,animation,sampler,key,accessor,row,reason; reason
is range/nonfinite/nonunit/representation, never a host-state label.
The fixed summary has policy,complete,node_values_checked,animation_values_checked,
tangent_rows_checked,rows_admitted,first_refusal. It counts actual source/value
interpretations, not exact mathematical unit claims; absence is not a checked row.
A unit checker not entered leaves quaternions null. A genuinely called empty-role
checker may return complete zero counts without treating absent defaults as rows.
Compare-only stages never
invent unit checks. Partial and later-failure copies stay inline/immutable.

New quaternion helpers use the existing uint32 diagnostic.rule_id namespace
0x00010001..0x00010006, only under this processing/policy origin:
NodeExactRange, AnimationNonfinite, FloatMagnitudeNonunit, ExactBandNonunit,
FilterBandNonunit, RepresentationEnvelope in that order. Earlier core/schema/
projection diagnoses preserve their original rule IDs and phase; legacy IDs are
never reinterpreted. FloatMagnitudeNonunit requires animation-value/FLOAT,
Malformed/BinaryValue at the animations phase and the actual source row. Existing
codes/status values and observation keys do not grow for these IDs. Qualified
callers must inspect origin/profile/code/phase, not a number without context.

Independent memory/staged reads establish their own same policy authority.
Writer/inspectors compare exact raw/source/policy bindings and reject stale/foreign
authority. Equal summaries/counts/requested strings cannot stand for those owners.

## GDEF-RESOURCE-001: Explicit policies and failure boundaries

GltfDeformationPolicy contains18 required-positive inclusive limits and8 work fields.
max_influence_sets counts paired sets PER primitive; max_lanes_per_vertex admits
all4 lanes per actual set. Checked products/extents precede allocation. All
descriptor/control/worker/decoded/scratch ownership uses the original real ledger.
A validator uses the minimum of its configured cap and actual owning remaining
allowance. Existing materialization's three positive caps are passed unchanged
after their checked sum fits. Copies do not duplicate charges; live, peak, traffic,
retained and aggregate observations are not interchangeable.

Parallel work uses real shared leases and bounded scalar/byte partitions, including
uneven rows. Every launched worker joins before return, exceptions, callbacks or
backing release. Actual partial counters need not be a contiguous prefix.
`max_jobs` caps each genuine dependency-phase plan. Catalog decoding has one
collection-wide plan across prepared accessors, followed by one dependent
role/fold plan. It does not borrow the source reader's scheduling policy or create
separate tiny plans for individual accessors. Each plan uses its actual new
profile work extents, eligibility, checkpoints and shared leases. The four
execution counters aggregate actual plans and dispatches across those phases;
their totals may exceed the per-plan `max_jobs`. With `max_jobs=1`, each admitted
phase has a genuine caller dispatch. Caller counts are never inferred by
subtracting launched workers from a planned count. Normalized derived values
belong to the role phase and require its actual executing-thread numerical guard.
Configuration/identity/seed/stop/diagnostic precedence is explicit; cleanup neither
allocates nor follows stop. GPU work and platform expansion are not selected.

## GDEF-API-001: Declaration and availability boundary

The new public header is gltf_deformation.h; base POD/result declarations remain
in gltf.h. Existing optional materialization, topology-write, workflow-support and
file owners keep their purposes. There is no new generic framework, optional
Surface/STL dependency or Geometry triangulator. New ABI/layout changes require
a matching rebuilt dependency cohort; unchanged source call forms are not a
binary compatibility promise. Development implementations are present;
qualified runtime support and dependency-cohort availability remain pending.


<a id="gdef-work-001-proposed-new-profile-scheduling-units"></a>
## GDEF-WORK-001: New-profile scheduling units

For deformation-retained-v1 only, scalar-named scheduling fields use S=C+I+R:
C counts actual source-scalar predicate/interpretation visits; I counts exact-helper
inner iterations processing a32-bit word or word-pair, including multiply, carry,
shift, compare, copy and update; R counts graph/role/provenance/reference visits.
These primitive categories are exclusive; repeated passes count again. Authored
max_scalar_values, observed scalar_checks and max_relationship_steps retain their
semantic definitions. No new report counter or resource leaf is added.

B counts actual scheduled lexical/transfer/comparison/relocation byte visits,
including repeated passes. A byte visit and the distinct helper steps it causes
belong to separate axes; B is neither allocated bytes nor max_decoded_bytes.
Build checked conservative C/I/R/B extents from bounded input and the actual finite
loop plan before substantial work. Eligible work satisfies S>=parallel_min_scalars
OR B>=parallel_min_bytes. Planned jobs=min(max_jobs,independent_task_count,
max(ceil(S/partition_scalars),ceil(B/partition_bytes))); checked ceil division must
not use overflowing n+p-1. Partition targets group independent units, not additional
hard resource ceilings. Max-job coarsening or one indivisible bounded carry/product
may exceed the target; checkpoints still apply. Many independent long rows/tokens
must not be processed serially merely because the implementation missed helper work.

Scalar, byte and reference loops honor their actual policy checkpoints; quaternion
inner helper loops additionally use min(compiled16,max_relationship_block), at
every loop nesting/pass. Original shared-context worker leases and joins apply.
Planner overflow is the existing read/extract UnsupportedProfile/SizeOverflow
category (outer writer/inspection/materialization limit adaptation stays its owner),
never a new ResourceLimit enum, wrap, truncation or a changed mode.
The development implementation uses this explicit work-unit interface; old
profiles and their work-policy meanings are unchanged. Full workflow and
dependency-cohort qualification remain pending.


Weight-sum observations belong only to actual payload producer roles source,
reload and publication_reload. All read, transfer, writer and comparison-only
roles return weight_sums=null, including a successfully called empty node/unit
reader. A real complete-zero payload summary is different from an unperformed
weight fold. Actual payload partial/later-failure summaries remain retained.
