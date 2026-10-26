# Direct translation-animation processing contract

| Field | Value |
|---|---|
| ID | GLTF-DIRECT-TRANSLATION-001 |
| Version | 0.1.0 |
| Status | Development extraction foundation implemented; remaining interfaces and full qualification pending |
| Interfaces | Native extraction in [gltf.h](../include/meshvale/interchange/gltf.h); planned [checked writing](gltf_topology_write.md), [output inspection](gltf-workflow-support.md) and [file publication](gltf-files.md) additions |

The extraction foundation is available in [gltf.h](../include/meshvale/interchange/gltf.h):
explicit profile selection, translation limits/work policy, actual extraction
observations and an owned admission proof. The [installed extraction example](../examples/consumer/gltf_translation.cpp)
demonstrates that native subset. Additions below for translation-aware checked
writing, output inspection and file publication remain planned. Current static
defaults, writer guards and output behavior remain unchanged. Full workflow
qualification, supported platforms and a stable release remain pending.

The selected profile permits checked whole-triangle selection on a mesh with
direct node-translation tracks, retaining the authored animation and resources.
It operates on local geometry. It does not evaluate or bake transforms, resample
or blend clips, play animation, perform skinning or process morph targets.
Interchange verifies the supplied selection; it does not choose faces to remove.

## GDT-PROFILE-001: Selection and affected data

Append `GltfProcessingProfile::kDirectTranslationTrianglesV1 = 1`; existing
`kStaticLocalTrianglesV1 = 0` remains the default. A trailing
`GltfExtractionRequest.processing_profile` selects it explicitly, alongside the
existing required triangle/value policies. An unknown selection is invalid.
`GltfAsset::Capability(handle)` remains a static-profile preview; it is not
admission proof for the new profile.

For the selected primitive's mesh, examine every authored mesh-user node across
all scenes, including inactive and unreachable nodes. Determine every strict
ancestor of those users from the already admitted forest. Scan channels in every
animation, not a caller-selected clip or preferred scene instance.

- Every channel targeting a mesh user is affected. Only direct `translation`
  with LINEAR or STEP interpolation is admitted. Omitted interpolation resolves
  to the core LINEAR default. The same destination in different clips is allowed;
  duplicate node/path destinations within one clip retain reader rejection.
- Any channel targeting a strict ancestor is unsupported. This takes precedence
  when one mesh user is also another user's ancestor. A direct-user-only test
  cannot establish the profile.
- Skin on any user, selected JOINTS_/WEIGHTS_ channels, primitive morph targets,
  mesh/node weights and other affecting morph bindings remain unsupported.
  Every other existing capability restriction still applies, including optional
  extensions and unknown roles hidden behind an earlier animation reason.
  Merely waiving `kAnimationBinding` is insufficient.
- A channel without `target.node` is not an affected channel. Unrelated tracks
  and resources retain the existing opaque-preservation rules. A sampler shared
  with an affected translation is validated for that use. No complete-asset
  animation validity or rendering claim follows.
- Affecting deferred extensions remain unsupported. A layout enabled by a legal
  deferred extension is not classified as malformed merely because the selected
  core profile excludes it.

Build membership with bounded node flags and checked ancestor traversal. No
unbounded recursive walk or users-times-tracks search is admitted. Every actual
repeated parent-link traversal consumes max_relationship_steps; a deep legal
document that exceeds this allowance receives the real logical-limit refusal
instead of unbounded work.

A static primitive may use the new profile with zero affected tracks, after the
actual complete inventory scan. Existing static selection does not perform that
scan or acquire a translation proof. Shared mesh selection edits the one mesh
primitive once; all its retained users reference the same result. No instance
cloning, scene flattening or implicit graph-reference renumbering is selected.

## GDT-ACCESSORS-001: Dense finite translation samples

For each distinct affected `(animation index, sampler index)`, admit and validate
one sampler, recording all its channels. Different samplers or clips count
separately even when their accessor slices coincide.

Input requires dense bufferView storage, FLOAT component type 5126, SCALAR shape,
normalized absent/false and at least one element. Output requires dense FLOAT
VEC3, normalized absent/false and the same count. Sparse storage is a profile
exclusion. Offsets, alignment, ranges and cross-role use follow the existing
accessor owner. Separate packed animation accessors may share a valid
animation-only view; vertex stride rules do not become animation layout rules.

For core data without a permitting extension, animation bufferViews must not
define byteStride. An animation-targeted node must not define matrix; its TRS
and nonanimated properties remain authored. Existing reader failures retain
their precedence, including malformed declarations outside the selected track.

All decoded components must be finite binary32. Translation accepts finite
negative values, either zero sign and subnormals without magnitude clamping or
normalization. Input time starts at a numerically nonnegative value and strictly
increases within each sampler. One-key LINEAR/STEP samplers are allowed. Negative
zero at the first key is allowed; a subsequent positive zero is not greater.
No time comparison crosses a sampler or clip boundary.

Input min/max are mandatory one-component metadata and must match the first/last
decoded times after the existing binary32 rounding. Optional output min/max have
three components and, when present, match real component extrema. Bounds use
numeric equality for the two zero signs; original key bits and metadata lexemes
are preserved. Missing/wrong bounds, invalid selected type/count/normalization,
nonfinite data and invalid time order are malformed at the validating operation.
An earlier owning reader representation/admission failure remains its actual
failure, rather than being rewritten by a validator that never ran.

CUBICSPLINE, affected rotation/scale/weights, sparse streams and affecting
unimplemented extensions are outside this profile. Payloads behind an unsupported
selection are not certified as globally valid. The underlying rules are the
[glTF 2.0 accessor bounds and animations](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#animations);
retaining records is not a substitute for these selected checks.

## GDT-PROOF-001: Owned admission and checked output

The extraction owner retains immutable private proof bound to the exact original
asset, selected handle, complete mesh-user/ancestor membership and ordered
channel/sampler/accessor/resource descriptors. It retains the actual source
resource owners and validated sample facts. Equal replacement bytes, caller
booleans or a copied view cannot manufacture the proof or an uncomputed hash.

Planned read-only interfaces are:

- `GltfPrimitiveExtraction::ProcessingProfile()` returns its actual selection.
  `DirectTranslation()` returns an optional borrowed const
  `GltfDirectTranslationProofView`, valid only with its extraction owner.
- The view exposes const `mesh_users`, `node_membership` and `tracks` spans.
  Membership is one byte per source node: kMeshUser=1, kStrictAncestor=2; both
  bits may be set. `GltfDirectTranslationTrackView` exposes uint64
  animation_index,channel_index,sampler_index,node_index,input_accessor,
  output_accessor,key_count and `GltfDirectTranslationInterpolation`
  (kLinear=0,kStep=1). Views do not authorize writing.
- `GltfAsset::DirectTranslationWork()` exposes the const captured translation
  work policy. Native callers must supply it when constructing an asset intended
  for new-profile extraction. An old source with zero new work fields does not
  receive invented defaults.
- Extraction, checked-writer and output-inspection results gain an optional
  `direct_translation` observation. Their successful owners expose a read-only
  optional `DirectTranslationFacts()` getter. Failed results retain actual
  called observations even when no successful owner exists.
- `GltfFilePublicationResult` gains inline optional `translation_extraction` and
  `translation_inspection` observations from its actual staged delegates,
  including failures. A verified-content callback can obtain successful facts
  from the real inspection and its `ReloadedExtraction()` owner.

The planned getter types are explicit:

| Getter | Return type |
|---|---|
| GltfPrimitiveExtraction::ProcessingProfile() | GltfProcessingProfile |
| GltfPrimitiveExtraction::DirectTranslation() | std::optional<GltfDirectTranslationProofView> |
| GltfAsset::DirectTranslationWork() | const GltfDirectTranslationWorkPolicy& |
| Extraction, writer bundle and inspection owner DirectTranslationFacts() | const GltfDirectTranslationObservation* |

The proof view contains `std::span<const std::uint64_t> mesh_users`,
`std::span<const std::uint8_t> node_membership` and
`std::span<const GltfDirectTranslationTrackView> tracks`. Optional proof and fact
pointers are absent/null for static selection. A result observation has type
`std::optional<GltfDirectTranslationObservation>`; it is absent before helper
entry, not a default successful value. A successful owner's facts equal its real
returned observation. Captured work is observable without running validation.
Getters allocate/checkpoint/reserve nothing; borrowed references/spans/pointers
need an alive, unreassigned owner. Concurrent const access through separate
immutable copies is allowed; reassignment/destruction of the same object needs
caller synchronization.

[Materialization](gltf-materialization.md) keeps the strong source extraction and
proof. Animation does not become Geometry attributes. `WriteGltfTopologySelection`
accepts the new selection only through that actual source owner, independently
revalidates it and compares its proof. Missing, foreign, stale or incomplete
authority refuses before output construction. Static writing retains its current
independent capability guard.

The checked writer keeps existing vertex/channel bits, ordered face/corner
correspondence, lexical splice limits, original table ordinals and full resource
retention. Changed output redirects only the selected primitive's index binding
under the existing writer rules; identity output remains exact. It preserves the
complete ordered animation inventory, affected node/ancestor transforms, track
references, metadata lexemes and original key slices.

Read and extract actual output independently using the same processing profile.
Output inspection compares both real proofs and retained representation; it must
not copy source admission onto the reload. Physical publication propagates the
source profile into staged extraction and repeats inspection on actual staged
members. Existing GLB chunk, padding, unused-byte and source-lifetime rules remain
in force. Failed publication is not a published owner; no callback, mandatory
reread, report patch or allocation after commit establishes success.

Adding fields changes native ABI despite source-compatible trailing defaults.
Consumers must rebuild against matching headers/libraries. An equal development
package version alone does not establish feature or ABI compatibility.

## GDT-LIMITS-001: Explicit limits and work

Add trailing `direct_translation` members of type `GltfDirectTranslationLimits`
to extraction, topology-write and topology-output-inspection limits. Add a
`GltfDirectTranslationWorkPolicy` tail to GltfWorkPolicy, topology-write work and
topology-output-inspection work. Static mode ignores the new zero tails and runs
no new animation validation. New mode requires all its fields positive,
host-representable and checked before work. Callers supply admitted explicit
values; implementation does not inject defaults or silently replace configured
ceilings. The effective allowance intersection below is not a preset mutation.

All ten GltfDirectTranslationLimits fields have type `std::uint64_t` and native
default zero. New-profile use rejects zero values; the static mode ignores its
new zero tails, preserving the existing default behavior.

The following finite reference configuration is explicit caller configuration,
not native defaults or a measured memory guarantee:

| Limit field | Unit | Reference value |
|---|---|---:|
| max_node_rows | visited source node rows |65,536|
| max_mesh_users | selected mesh users |65,536|
| max_channel_references | scanned authored channels |4,096|
| max_distinct_samplers | admitted affected clip/sampler pairs |4,096|
| max_keys_per_sampler | keys |65,536|
| max_total_keys | sum across admitted samplers |262,144|
| max_relationship_steps | actual parent-link traversals |1,048,576|
| max_metadata_bytes | canonical logical metadata footprint |2,097,152|
| max_decoded_bytes | canonical sample extent |4,194,304|
| max_new_requested_bytes | actual fresh validator requests |8,388,608|

Logical metadata footprint is `N+16*U+64*T+64*S` for node flags, users, scanned
channel descriptors and distinct admitted samplers. The reference maxima give
1,638,400 bytes below 2 MiB. Canonical decoded extent is 16 times the key sum,
at most 4 MiB. These are checked logical limits, not sizeof/RSS or unique IO.
Real allocations include accounting/control, alignment, temporary requests and
replacement overlap, all under the actual request scope and execution ledger.
Independent maxima do not promise simultaneous fit.

Work has exactly parallel_min_keys,partition_keys,cancellation_block_keys,max_jobs,
all positive uint64. The reference values are 16,384/16,384/256/64. A serial
reference uses INT64_MAX/16,384/256/1 on a host representing those fields. Native
defaults remain zero; no global session or worker pool is added. Substantial
validation partitions the total sample work, including many small samplers, on
the caller's existing Geometry context. Dependent inventory/metadata work may
remain serial. All launched workers join on failure, stop and exception.

## GDT-ACCOUNTING-001: Operation containment

Each validator has a separate request scope on the exact same execution ledger,
stop token and per-call allocation/worker backends. `WithRequestLimit` cannot be
nested on a capped context. Let C be the real owning operation allowance and L
its ordinary disjoint live requests. Before even a validator accounting seed,
derive E=min(configured validator ceiling,C-L) with checked subtraction. Zero
remainder refuses without a validator observation. A positive E too small for its
real seed receives actual allocation admission failure, not fabricated work.
Configured values stay unchanged; called facts record effective_request_cap=E.

For transient writer/inspection validation, freeze ordinary growth after earlier
workers join. Its live bytes V satisfy L+V<=C. Release transient validation before
ordinary growth resumes. For extraction, validate before allocating ordinary
buffers, freeze retained proof bytes P, then allocate ordinary extraction under
C-P. Insufficient remainder refuses. Extraction NewRequestedBytes includes its
actual disjoint frozen proof plus ordinary live requests exactly once. No input
owner, shared copy or sampled counter is added again.

Whole extraction/writer/inspection caps retain their meaning; for example a
caller may select 40/72/40 MiB respectively with an 8 MiB validator ceiling.
An owning allowance smaller than 8 MiB limits validation before any allocation.
These are not reservations or a new aggregate framework. Current-live successful
writer/inspection getters exclude already freed transient validation; an earlier
observation can retain its actual sampled value after those bytes are freed.

Publication `files.max_new_requested_bytes` remains file-module-only. Staged
extraction/inspection retain independent caps on the shared ledger; do not add
their ceilings to the file cap or synthesize an unavailable publisher total.
Shared-ledger refusal remains different from operation request-ceiling refusal.
Unexpected backend/runtime exceptions propagate after joins/best-effort cleanup,
without a normal result or invented cleanup tuple. Backend state must outlive the
last actual free under the existing allocation contract.

## GDT-FACTS-001: Actual observations and partial work

`GltfDirectTranslationObservation` has status (GltfStatus), diagnostic (actual
GltfDiagnostic), complete (bool), work (the four selected fields), execution and
the uint64 counters below. Diagnostic.status equals status; absent actual source
locations remain absent. Execution contains exactly planned_jobs,launched_workers,
joined_workers,caller_jobs. It records the actual joined schedule, not successful
jobs or inferred serial work. A null observation means the validator was not
called. Complete requires accepted full validation/comparison, not merely all
items having started.

| Counter | Exact meaning |
|---|---|
| node_rows | Source node rows actually visited once in membership scan. |
| mesh_users | Visited rows identified as users, including a later refused user. |
| scanned_channels | Channel rows entered after a stop checkpoint; offending row counts. |
| affected_channels | Entered rows classified as affected, including later refusal. |
| distinct_samplers | Distinct affected descriptors actually admitted/stored, not successfully validated samplers. |
| relationship_steps | Actual parent-link traversals, including repeated visits. |
| keys_checked | Unique main clip/sampler/key work items entered after their checkpoint; failed/partial items count, not only valid or complete keys. |
| decoded_bytes_checked | Four bytes per binary32 scalar actually loaded/tested, including a failing scalar and predecessor rereads; excludes metadata and unexamined bytes. |
| new_requested_bytes | Actual validator-scope live requests sampled after joins before proof transfer/release; not final operation live bytes, peak, traffic or RSS. |
| effective_request_cap | Actual E applied before validator allocation. |

One shared sampler is decoded once across its referring channels. Different
sampler IDs sharing accessors still count separate work. A partition beginning
at key j>0 reads/checks that same sampler's j-1 time: its four bytes count, but it
does not add another main key. Within a partition retain the prior time locally.
Never cross sampler/clip boundaries. Actual rereads may exceed the canonical
16*key logical extent; interrupted/failed items can examine fewer components.
Neither number is fabricated as the actual examined-byte count.

Semantic errors record the minimum authored clip/earliest-referring-channel/key/
component and do not themselves cancel remaining planned sample work. Failed
items may stop their remaining scalar checks. After complete joined work without
interruption, select the deterministic first semantic diagnostic. Compare bounds
only after valid samples; bound errors name actual metadata spans, not invented
key coordinates. Observed cancellation/interruption retains its actual execution
diagnostic; unexpected exceptions escape. No globally first malformed-key claim
is made for a partial interrupted scan.

Joined partitions can finish out of order. Failed/canceled metadata and sample
counts describe actual work, possibly noncontiguous and schedule-dependent. They
are not a contiguous validated prefix and must not be normalized to serial or
fixed failure telemetry. Earlier completed facts survive later errors without
claiming that later stages ran.

## GDT-ERRORS-001: Phase-specific status projection

Use existing status/diagnostic enums, with new validator rule IDs 315 (membership/
profile),316 (sample/bounds/proof data),317 (limits). Existing decoder rules keep
their own IDs. The actual inner diagnostic is retained in the observation beside
the operation's outer diagnostic; no unsupported resource/span field is invented.

E below is extraction; W is new-profile writer verification; V is new-profile
output validation/comparison at kReloadedPrimitive, after existing earlier
lexical/resource phases. Earlier failures retain their real precedence and leave
unreached observations absent.

| Condition | E / inner validator | W status / code | V status / code |
|---|---|---|---|
| zero/unrepresentable configuration, before helper | kInvalidLimits / kZeroLimit or kLimitNotRepresentable; unknown profile kInvalidRequest/kInvalidRequest | kInvalidConfiguration / kRequiredFieldZero or kConfigurationExtent at kAdmission | kInvalidConfiguration / kRequiredFieldZero or kConfigurationExtent at kAdmission |
| metadata/key/logical-byte count limit | kUnsupportedProfile / kLimitExceeded | kLimitExceeded / kCountLimit | kLimitExceeded / kCountLimit |
| actual request ceiling/insufficient remainder | kUnsupportedProfile / kLimitExceeded | kLimitExceeded / kWriterRequestLimit | kLimitExceeded / kInspectionRequestLimit |
| actual shared payload refusal | kBudgetExceeded / kPayloadBudget | kBudgetExceeded / kRequestBudget | kBudgetExceeded / kRequestBudget |
| observed stop | kCanceled / kCanceled | kCanceled / kStopRequested | kCanceled / kStopRequested |
| outside selected profile | kUnsupportedProfile / kUnsupportedEnum | kUnsupportedProfile / kSourceCapability | kUnsupportedSelection / kSourceIdentity |
| malformed selected core data | kMalformed / actual CoreFieldRequired,CoreArrayLength,CoreFieldType,CoreRange,BinaryLayout,BinaryValue,ResourceExtent or GraphRelationship | kSourceContractFailure / kSourceDescriptor | kSourceContractFailure / kSourceProvenance |
| actual bound-source/proof comparison differs | kResourceBindingMismatch / actual BinaryValue or GraphRelationship | kSourceContractFailure / kSourceProvenance | kOutputMismatch / kSourceProvenance |
| missing/incomplete/foreign authority before helper | actual existing primitive/owner guard, such as kInvalidSelection/kInvalidSelection | kSourceContractFailure / kSourceProvenance at kAdmission | kSourceContractFailure / kSourceIdentity or kReloadIdentity at kAdmission |
| foreign execution ledger before helper | actual existing extraction guard | kForeignBudget / kForeignBudget at kAdmission | kForeignBudget / kForeignBudget at kAdmission |

Unless marked kAdmission, W rows use kVerification and V rows kReloadedPrimitive.
Select by actual failure kind/code: a logical cap's kUnsupportedProfile/
kLimitExceeded does not become a writer unsupported-selection result. Success
is E kAccepted/kNone, W/V kSuccess/kNone. A later ordinary failure retains the
earlier actual observation. No normal E/W/V result is manufactured for an escaping
exception.

Staged extraction maps its real GltfStatus to GltfFileStatus as follows:
kCanceled,kBudgetExceeded,kUnsupportedProfile,kMalformed retain those names;
kInvalidLimits becomes kInvalidConfiguration; kForeignOwner becomes
kSourceContractFailure; other failures become kInvalidRequest. Its file
diagnostic is kExtraction/kDelegatedExtraction with the actual gltf_diagnostic.

Staged inspection maps kInvalidConfiguration,kLimitExceeded,kForeignBudget,
kSourceContractFailure,kBudgetExceeded,kCanceled to identically named file
statuses; kUnsupportedSelection becomes kUnsupportedProfile; other failures
become kPreservationMismatch. Its diagnostic is kInspection/kDelegatedInspection
with the actual inspection_status/inspection_diagnostic. Cancellation has actual
publication outcome kCanceled; other normal failures kFailed. Cleanup remains
the actual returned cleanup observation. This library selects no application
report schema, command exit policy or repair operation.

## GDT-QUALIFICATION-001: Pending evidence boundary

Implementation qualification must exercise original dyadic LINEAR/STEP inputs,
multiple shared users including inactive users/ancestors, changed/identity output,
JSON and GLB resources/tails/chunks, source removal after capture, independently
decoded reload and re-readable same-count corruption controls. Cover real
malformed versus unsupported outcomes, one-under operation caps versus occupied
shared ledger, tiny owning allowances, backend failures and interior stop with
actual joined/nonprefix observations. Equal bytes or default-zero counters do not
substitute for the actual native paths.

Current static tests and installed consumers do not qualify this pending profile.
Matching source/package/header/library and applicable numerical/toolchain evidence
are required separately. No animation renderer, image/shader equivalence, broader
skinning/morph/animation completeness or stable ABI/platform release is selected.
