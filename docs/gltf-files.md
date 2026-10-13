# Retained glTF/GLB files and verified bundles

| Field | Value |
|---|---|
| ID | GLTF-FILES-001 |
| Version | 0.1.0 |
| Status | Draft declaration; implementation and qualification pending |
| Interface | [gltf_files.h](../include/meshvale/interchange/gltf_files.h) |
| Selected target | Optional `meshvale::gltf_files`, requiring `meshvale::gltf_workflow_support`; build/export implementation pending |

This additive interface selects immutable native file capture and same-format
verified topology-output publication. The [reader](gltf-read.md) owns glTF
admission and primitive decoding; [capped workflow support](gltf-workflow-support.md)
owns read/extraction accounting and independent output inspection; the
[topology writer](gltf_topology_write.md) owns supplied-memory edits; the
[GLB container](glb.md) owns wire capture/assembly. This draft adds no implemented
file operation, format conversion, Python binding, application report schema,
command or supported release. The base reader header remains independent of the
optional file module.

## GFILE-CAPTURE-001: Strong file source

`ReadGltfFileAsset` takes an input path, read options, explicit limits/work policy,
installed Geometry execution context and fresh stop token. Detect format from
bytes: GLB magic selects GLB wire validation; otherwise pass the bytes to JSON
asset admission. A suffix neither overrides content nor proves validity.

Empty `resource_root` selects the entry's parent. An explicit enclosing directory
allows relative references outside that parent but inside the chosen root. The
entry must be inside the root, with a portable relative path. Capture regular
files only. Reject symbolic links and Windows reparse aliases during root,
directory and file traversal, even when their targets would be inside the root.
Canonical targets must remain inside the root as an additional check.

The copy-only immutable `GltfFileAsset` strongly retains the admitted `Asset()`,
source `Format()`, relative `EntryPath()`, complete physical `EntryBytes()`, and
UTF-8 path-sorted unique `Files()` including the entry once. For GLB it also owns
the complete immutable `Container()`; this pointer is null for JSON. Retain all
physical entry bytes, original buffer tails, resources and explicit opaque files.
No absolute source path is needed in the returned snapshot.

Both returned owner types are copy-only; copying shares storage/charges without
allocation. All spans, views, pointers and references require an alive,
unreassigned visible owner copy. Independent copies and concurrent const reads
are supported. Destruction/reassignment of the same wrapper requires external
synchronization. Caller path/options/context wrappers may be destroyed after
return; retained owners keep their underlying data and ledger alive. No returned
owner keeps a source/staging file handle open.

Capture modifies no source. Inputs must remain stable throughout capture. Open
and admit observed file size before payload growth; check file identity, size and
available modification metadata before and after bounded reads. Short reads,
unexpected extra bytes or an observed identity/metadata change report
`kFileChanged`. This is not an atomic snapshot of a concurrently edited directory,
and it cannot detect a hostile change that restores every observed property.
Only successful capture returns a file asset, with `kSuccess`, `kComplete` and
`kNone` diagnostic fields. Failure/cancellation returns no file asset; actual allocation/native runtime
exceptions propagate after cleanup and worker joins.

## GFILE-RESOURCE-001: Relative URI and opaque-file profile

Resolve every core external buffer/image URI, including unused records, relative
to the entry path. Decode valid percent escapes exactly once, validate UTF-8, then
normalize relative dot segments. Keep authored URI spellings and JSON bytes.
Reject malformed escapes as `kUriMalformed`; excluded URI forms use
`kUriUnsupported`. The selected profile rejects encoded slash/backslash, schemes,
authority, query/fragment, absolute/drive/network paths, decoded NUL/control
characters, literal backslash and root escape. It performs no network access.

Portable names additionally reject Windows-reserved components, prohibited
characters, trailing dots/spaces, ASCII case aliases and file/directory prefix
collisions. Unicode filenames are accepted without promising cross-filesystem
normalization or case equivalence. After lexical admission, canonical targets
must remain within the root. OBJ's backslash/texture-reference policy is not this
URI policy.

Deduplicate by the decoded normalized **relative destination path**. For example,
`%61.bin` and `a.bin` can share one captured file while distinct resource table
identities still receive bindings. Two different logical paths that happen to
refer to one source inode, such as hard links, remain distinct published paths;
changing those names would break unchanged URIs. Symbolic/reparse aliases remain
unsupported. Resource binding counts and unique physical-file counts are
separate. Exact duplicate explicit
opaque paths share a file; differing ASCII-case names fail. A resource referring
to the entry can be captured, but a changed entry cannot be published unless the
actual independent resource-byte checks still pass.

Data URIs remain embedded and are decoded by the reader. Buffer-view images retain
their buffer aliases and are not separate files. For JSON files, URI-less buffers
have no selected filesystem binding and are unsupported. GLB binding belongs to
GFILE-GLB-001. No resource is externalized, packed or rewritten during publication;
the writer's appended UINT32 index data remains inline.

`kRejectOpaqueExtensions`, the default policy, rejects nonempty extension
declarations or extension objects anywhere in the document, because their
external dependency rules are not known to this module. `kExplicitOpaqueInventory`
allows retained extension records only under the caller's explicit declaration
that `opaque_source_files` enumerates all their external dependencies. Those
paths are portable relative file paths, not URI spellings; capture them exactly
under the same root. An empty explicit inventory declares no opaque external
dependencies. Nonempty opaque paths require the explicit policy. Retain that
policy on the file owner. This conditional preservation does not establish
extension semantics or automatic discovery; application `extras` likewise have
no inferred dependency semantics. Required-extension and processing admission
remain reader-owned, even when files are explicitly enumerated.

## GFILE-GLB-001: Asset/container coupling and exact padding

Initial GLB capture retains the entire original JSON chunk, including its padding,
as `Asset().OriginalJson()`. `Container()` and `EntryBytes()` retain all original
wire bytes and ordered chunks. Couple the first buffer whose `uri` property is
absent to the actual BIN chunk only when it is buffer zero. A missing required BIN
is malformed. URI-less buffers at later indices, or a BIN with no permitted
buffer-zero binding, are outside this selected profile.

Independently require BIN length at least its declared logical buffer length,
with at most three extra zero padding bytes. Check logical extent before supplying
the full BIN span to the reader; reader buffer/image layout admission still runs.
The wire container's structural acceptance alone proves none of these facts.
The [Khronos specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#glb-file-format-specification)
owns JSON-space/BIN-zero alignment padding and buffer-zero coupling. The selected
file profile preserves every admitted non-JSON chunk type, order and full bytes,
including opaque chunks and original BIN padding.

Unchanged topology output publishes the exact original container bytes. Changed
output replaces only JSON with the topology writer's exact `Json()` and assembles
the remaining original chunks through `WriteGlbContainer`. Added JSON alignment
spaces are the only permitted container padding change. Do not append a BIN,
merge resources, trim original trailing whitespace or reinterpret opaque chunks.

During staged verification capture the actual staged GLB, prove its entire JSON
chunk equals writer JSON followed by independently derived zero-to-three spaces,
and feed the actual chunk's exact writer-length prefix to `ReadGltfAssetCapped`.
For unchanged output the prefix is the entire original JSON chunk and additional
padding length is zero. This establishes the existing inspector's exact reload
JSON equality without pretending that padding bytes disappeared from the file.
The published owner's `Inspection()` retains this actual staged-prefix reload;
`Files()` and `Container()` retain the complete physical bytes. A later ordinary
`ReadGltfFileAsset` again captures the entire JSON chunk; do not equate its padded
`OriginalJson()` length with that verified prefix without checking the padding.

## GFILE-LIMIT-001: Explicit file requests and delegated owners

Every file/work field and every nested delegated limit/work field is required
positive and host-representable, including container fields for a JSON operation.
Use the delegated contract's stricter representational ceiling when applicable.
All physical count/size arithmetic is checked and limits are inclusive.

`max_files` counts unique entry/core/opaque/supplemental files. `max_file_bytes`
and `max_total_file_bytes` count full physical bytes per file and summed once per
unique path, including GLB headers/padding, buffer tails and reports. Binding
records, including multiple core records resolving to one path and the BIN
binding, are bounded by `max_resource_bindings`. Data-URI and buffer-view aliases
do not create file binding records. `max_path_bytes` and `max_total_path_bytes`
bound decoded normalized relative UTF-8 names per unique path and summed once;
authored URI text remains subject to the reader's own string/document limits.
`max_native_path_code_units` separately bounds each native absolute working path,
including terminator, in Windows `wchar_t` units or POSIX `char` units. Derived
directory/path plans must fit the complete explicit request cap.

One fresh file operation owner covers every explicit file-module request:
seed/state/control/rebound allocations, retained bytes and path/binding records,
UTF-8/UTF-16 conversion and native canonical-path buffers, inventory/sort scratch,
IO block buffers, private native handle/job/callable records and per-job exception/
diagnostic slots. Admit requested headers/alignment and replacement overlap before
each original block allocation. Use charged allocators/native path buffers;
creating fresh internal `std::filesystem::path`, stream buffers or ordinary
allocator vectors does not satisfy this guarantee. Destruction requires no new
allocation and releases tracked bytes after free.

Caller-created input/destination/root paths, option/functions and initial spans
preexist the operation and are borrowed. Callback-created supplement strings/
vectors and callback computation belong to the caller. Validate their count/path/
byte limits after return, then import them into charged operation storage before
writing. Never retain their unowned views or copy a `std::function` into uncharged
operation state. Opaque OS/kernel handles/enumerators, filesystem/CRT internals,
native thread-runtime and exception-object internals are outside explicit-request
accounting. They do not exempt Meshvale-created path/handle/worker records.

`NewRequestedBytes()` reports only the file owner's live explicit new requests.
Delegated capped asset reads, capped extraction and inspection use their existing
fresh caps and retained counters; GLB uses its existing declared request-limit
scope and exclusions. All use the same authoritative execution ledger, including
retained inputs and overlapping outputs. No combined end-to-end request cap,
process memory/RSS cap or stronger GLB counter is inferred from a file counter.

## GFILE-EXECUTION-001: Filesystem actions and byte work

Ordered path/URI/chunk planning, file metadata/handle operations and native
finalization are serial structural actions. IO proceeds in explicit positive
`io_block_bytes` blocks with bounded cancellation checkpoints. Substantial byte
copy/readback/comparison receives disjoint byte-aware jobs under the shared
worker budget, even for a single large file. Caller thresholds, partitions,
cancellation block and `max_jobs` are explicit. Small or occupied-budget work can
run on the caller; count limits do not imply smallness or justify serial payload
comparison. All started workers join before freeing any used path/byte/scratch
or handle state, including partial launch failure.

`GltfFileExecution` records actual file-module capture and byte-verification
planned/launched/joined/caller jobs. It does not substitute estimates for native
launches or sum delegated operations into invented measurements. Delegated
operations retain their own declared execution scope. There is no throughput or
dependency-internal parallelism promise.

## GFILE-PUBLISH-001: Verify actual staged output, then commit

`PublishGltfTopologyBundle` takes a strong source file asset and a successful
`GltfTopologyOutputInspection`, new destination directory, options, explicit
publication limits/work and fresh stop token. The file source's asset must be the
exact original asset owner retained by that inspection's source; matching JSON
bytes are insufficient. Require the common execution ledger. Candidate, maps,
original primitive, writer bundle and expected provenance come from the strong
inspection, not reconstructed mutable records or caller certification flags.

Publication reads no original source filesystem. Preserve source format and
relative entry path. The destination's parent must exist; an existing destination,
including an empty directory, file or dangling link, fails. Preflight all unique
paths, binding/count/byte limits and collisions before creating an exclusive
temporary sibling directory. Stage exact entry/resource/opaque/static-supplement
bytes as ordinary files. Reject extra files, links, special entries or directories
not required by the planned paths.

Reload **actual staged bytes** using the same relative URI/BIN mapping and capped
asset reader. Retarget the selected primitive by the original mesh/primitive
indices. Use capped extraction with `kConvertStripOrFan` for an unchanged original
strip/fan, otherwise `kRequireTriangles`. Request raw-and-normalized values if
the original source extraction has a required normalized channel view; otherwise
raw values suffice. The explicit extraction cap still applies. Invoke independent
output inspection with original source, actual candidate/maps/output and this
actual staged reload/extraction. Do not rerun Repair, materialization or Geometry
inspection, or accept the earlier memory inspection as staged verification.

Also verify exact physical entry/container bytes, chunk relationships, every full
resource/opaque/supplement byte and complete staged inventory. Byte equality is
not replaced by hashes, file size, a serializer round trip or sampled checks.
All verification sections must complete after joined reduction before commit.

Allocate the returned immutable `GltfPublishedBundle` and its metadata, close
handles and join workers before no-replace native finalization. Finalization is
the last fallible action. Success retains source, actual staged-byte inspection,
physical entry/container and complete path-sorted file inventory; entry names
remain relative to the caller's requested final root. No callback or allocation
follows successful finalization.

The [OBJ bundle contract](obj-files.md#obj-bundle-001-verify-before-publication)
describes the existing platform no-replace mechanisms. Reuse them privately with
format-neutral handling while retaining OBJ behavior. Unsupported platforms or
filesystems fail without an overwrite/copy fallback; racing destinations remain
untouched. Verification of actual staged bytes followed by native publication
establishes the committed bytes under the stated concurrency assumptions.
Independent tests must reopen the finalized entry. A mandatory fallible reload
after commit cannot decide success while promising rollback.

This is not crash/power-loss durability, crash cleanup, network filesystem
ambiguity resolution or protection against hostile concurrent staging mutation.
Images/opaque chunks retain bytes without establishing image, shader or unknown
extension equivalence. No file operation expands the processing profile.

## GFILE-SUPPLEMENT-001: Neutral verified-content callback

Static `supplemental_files` are borrowed until return and admitted/imported before
staging. They must not collide with entry/source or each other. `on_phase` runs
synchronously before its phase's work; no callback runs after commit.

After initial actual staged reload/inspection, exact-byte and complete-inventory
verification, optional `on_verified` receives the actual inspection owner,
format/relative entry, complete GLB container if present, and path-sorted exact
entry/resource/opaque/static-supplement bytes. It receives no staging path.
References/views borrow the publisher through the callback. A caller may copy
the immutable inspection owner, but that proves staged verification only.

The callback returns caller-owned `GltfFileSupplement` values. Admit/import their
paths/bytes, reject collisions, write and read back each dynamic supplement, then
recheck all original verified bytes and the complete inventory. Dynamic files
are excluded from callback input, avoiding self-referential receipts, and become
visible only if finalization succeeds. This is a neutral supplemental-file seam;
application report schemas, hash choices and success predicates stay with callers.
Callbacks may not mutate source or staging content. Allocation exceptions from
callbacks propagate; other callback exceptions produce `kCallbackFailure` with
the applicable fixed callback code and cleanup outcome.

## GFILE-FAILURE-001: First diagnostics and cleanup

Validate configuration first; then physical count/overflow and portable path
plans, common ledger/strong publication source identity, file capture/staging,
container/read/extraction, independent inspection, supplemental/inventory checks
and native publication in their declared order. Within file byte checks use
UTF-8 path order and first byte offset; core binding admission uses buffer then
image/table-index order. Fixed diagnostics retain actual delegated outcomes and
applicable file/resource/byte ordinals, never allocating absolute-path messages.

Every started worker joins. Actual native launch/backend/runtime exceptions
propagate before an expected result after rollback attempts. An already known
mismatch remains authoritative over later cancellation through allocation-free
earlier-prefix finalization after joins. Otherwise stop produces `kCanceled`.
`kFileRequestLimit` identifies file explicit-request refusal; `kRequestBudget`
identifies shared-ledger refusal. Delegated limits keep their actual diagnostics.

Cancellation is cooperative before each phase and IO operation. Before successful
finalization it publishes no bundle and attempts staging removal. After native
finalization succeeds the outcome is `kPublished`; a later stop cannot undo it.
Only that outcome returns a published owner with a success diagnostic. Expected
failure/cancellation returns no owner, keeps its primary diagnostic and separately
reports cleanup `kNotRequired`, `kRemoved` or `kFailed` plus system error if available.
Failed cleanup does not establish rollback and never establishes publication.

Propagating allocation/native runtime exceptions preserve the actual exception
after joins and best-effort staging cleanup; they return no result/cleanup claim.
Callers translate them into their own execution outcomes without asserting that
an accepted memory candidate completed a file workflow. The immutable accepted
input/candidate remains available on publication failure. A no-op may explicitly
publish to a new destination.

## Required qualification

The declarations require separate JSON and GLB implementation gates, independent
literal resource/path/container expectations, changed/no-op committed-byte reopen,
shared URI paths, symbolic/reparse link rejection, opaque inventories, normalized
channels,
image aliases/tails, JSON/BIN padding and unknown chunk preservation. Prove
inclusive limits, every own original-request fault, worker/callable/handle storage,
partial launches, native/occupied execution, source-owner destruction/protection,
callback/supplement failures, each cancellation phase, existing/racing destination
and cleanup failure. Re-run OBJ publication tests if shared machinery changes.
Installed examples, exact matching sanitizer/source-package and hosted evidence
must qualify actual operations; declaration, staging or wire-only success does not.
