# Retained STL files, verified rewrites and commands

| Field | Value |
|---|---|
| ID | STL-FILES-001 |
| Version | 0.1.1 |
| Status | Implemented development interface; evaluated configurations only; no supported release |
| Native interface | `meshvale/interchange/stl_files.h`, optional `meshvale::stl_files` target |
| Python/application owner | `meshvale_interchange.stl_files`; `meshvale-stl` |

This contract owns the implemented development interfaces for capture, read and
verified new-directory rewrite of one STL file. The [existing STL codec](stl.md)
owns admitted syntax, numeric values, triangle-soup storage and serialization.
Its complete admitted domain includes
empty documents and finite degenerate facets. The file/API/command development
profiles have their own [evaluation workflow](../.github/workflows/stl-files-candidates.yml).
Existing supplied-byte or supplied-surface success alone establishes no
file/API/command qualification.

The selected option `MESHVALE_INTERCHANGE_STL_FILES`, initially OFF, requires
`meshvale::stl` and the matching installed Geometry execution/materialization
producer. Only an enabled implementation installs the new native header/target.
No Surface, triangulation, glTF, Repair, additional STL SDK or public generic
transaction dependency is selected. The native SDK/source identity is separate
from the [existing Python owned-record dependency](python.md#build-installation-and-example).
The evaluated Python development cohort is described in the
[walkthrough](stl-file-walkthrough.md). Changing dependency pins still requires
selecting and qualifying matching native and record-provider inputs; this
contract changes none.

## SFILE-FIDELITY-001: Explicit, codec-preserving choices

Source and output encodings are explicitly `ascii` or `binary` (little endian).
No suffix, content prefix or locale selects an encoding. Rewriting calls actual
`WriteStl` on the captured document's genuine `MaterializedMesh`, selecting its
sole imported face-normal attribute, passing the genuine source document as
`source_document`, and acknowledging corner expansion explicitly. No replacement
mesh or caller-supplied serialized output is accepted by this rewrite profile.

Preserve every facet, ordered corner, winding, position bit and raw normal bit.
Normals may be zero, nonunit or opposed to winding; do not generate, normalize,
average or repeat them into purported authored corner normals. Retain distinct
vertex identities `3*i`, `3*i+1`, `3*i+2`, including coincident coordinates and
finite degenerate facets. There is no Repair, welding, indexing, topology recovery,
triangulation, basis conversion or rescaling operation. Print readiness, manifoldness,
closed volume and physically correct normals are not certified.

`StlFileMetadataMode` selects `kRetain` or `kReplace`. Retain requires equal
source/output encoding and empty replacement fields; it passes the complete source
ASCII name or opaque 80-byte binary header unchanged. Replace requires exactly
the output metadata: ASCII accepts a bounded printable ASCII name, including
empty, with an empty binary header; binary requires exactly 80 bytes and an empty
ASCII name. Cross-encoding requires replace. Same-encoding replace is allowed.
Actual `StlWriteFacts.metadata_policy` remains authoritative: supplying identical
same-encoding metadata with replace still yields `kRetainedSourceMetadata`, as the
existing writer compares actual values. Record both requested mode and actual fact.
Never truncate, decode, reinterpret or transfer source-only metadata implicitly.

ASCII output is canonical text with exact binary64 reload, including signed zero;
whitespace and numeric spelling may change. Binary output requires exact finite
binary32 representation of every position and normal, including zero sign and
subnormal values. Unsupported conversion returns no output; no tolerance or
approximate narrowing is selected. Binary attribute words are zero; nonzero source
words, trailing bytes, color dialects and variable attributes retain their codec
refusals. Header bytes remain opaque and establish no color or unit semantics.

`StlFileUnitKind` is `kUnknown` or `kLabel`. Unknown requires an empty label;
label requires a nonempty bounded scalar UTF-8 label without NUL/control characters.
The label is caller provenance in the receipt/result only. It neither interprets
bare STL as a known unit nor changes coordinates. Every job must select one kind.

## SFILE-NATIVE-001: Capture, document and rewrite owners

All native names are in `meshvale::interchange`. New structures use required
explicit inputs; no default finite preset is embedded in the native library.
`std::stop_token stop = {}` is the only optional operation argument below.

```cpp
StlFileCaptureResult CaptureStlFile(
    const std::filesystem::path& input, const StlFileLimits& limits,
    const StlFileWorkPolicy& work, const geometry::ExecutionContext& context,
    std::stop_token stop = {});
StlFileReadResult DecodeStlFile(
    const StlCapturedFile& captured, StlEncoding encoding,
    const StlReadLimits& limits, const StlWorkPolicy& work,
    std::stop_token stop = {});
StlFileReadResult ReadStlFileAsset(
    const std::filesystem::path& input, StlEncoding encoding,
    const StlFileReadLimits& limits, const StlFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop = {});
StlFileRewriteResult RewriteStlFile(
    const StlFileAsset& source, const std::filesystem::path& destination,
    const StlFileRewriteOptions& options,
    const StlFileRewriteLimits& limits, const StlFileRewriteWorkPolicy& work,
    std::stop_token stop = {});
```

`StlFileReadLimits` contains `files: StlFileLimits` and `read: StlReadLimits`;
`StlFileReadWorkPolicy` contains `files: StlFileWorkPolicy` and `read: StlWorkPolicy`.
`StlFileRewriteOptions` has `encoding: StlEncoding`,
`metadata_mode: StlFileMetadataMode`, `ascii_name: string_view`,
`binary_header: span<const byte>`, `acknowledge_corner_expansion: bool`,
`unit_kind: StlFileUnitKind`, `unit_label: string_view`, and
`report: optional<StlFileReportProvider>`. Encoding must be one of the two admitted
values, acknowledgment must be true, and metadata/unit fields obey the rules above.
All borrowed paths, configuration, source, provider state and backing remain
unchanged/readable through synchronous return and every join. The provider may
initially mutate its own backing synchronously as specified below.

Copy-only strong owners have implementation-private constructors and share
immutable charged storage. Copy, assignment,
destruction and handoff are nonthrowing and allocate nothing. Every view/reference
below borrows a live unreassigned visible owner; independent immutable copies
support concurrent reads. Backend state outlives its last block. Native handles
are not retained. An opaque charged isolation token retains native logical path
components and observed identity solely for later domain checks; no absolute
locator is exposed or emitted. All `NewRequestedBytes()` results are
live attributed requests, not cumulative allocation, RSS or a global peak.

| Owner | Exact getters, all `const noexcept` |
|---|---|
| `StlCapturedFile` | `EntryPath(): string_view`, `EntryBytes(): span<const byte>`, `Sha256(): array<byte,32>`, `ShareExecutionContext(): geometry::ExecutionContext`, `NewRequestedBytes(): uint64_t` |
| `StlFileAsset` | `Document(): const StlDocument&`, `EntryPath(): string_view`, `EntryBytes(): span<const byte>`, `Sha256(): array<byte,32>`, `NewRequestedBytes(): uint64_t` |
| `StlVerifiedRewrite` | `Source(): const StlFileAsset&`, `Output(): const StlOutput&`, `Document(): const StlDocument&`, `EntryPath(): string_view`, `EntryBytes(): span<const byte>`, `Sha256(): array<byte,32>`, `NewRequestedBytes(): uint64_t` |
| `StlPublishedBundle` | `Verified(): const StlVerifiedRewrite&`, `ReceiptPath(): string_view`, `ReceiptBytes(): span<const byte>`, `ReceiptSha256(): array<byte,32>`, `NewRequestedBytes(): uint64_t` |

Asset bytes alias `Document().OriginalBytes()`. `DecodeStlFile` calls `ReadStl`
with the captured owner's actual retained ledger. The codec copies original bytes;
capture/decode overlap is charged. An explicitly retained capture can therefore
coexist with the document copy. The path convenience and prepared workflows release
their capture payload after successful decode, keeping its immutable byte/hash
facts and the asset instead. They retain the capture on expected decode failure.
No second decoded mesh is kept. Neither successful capture nor a hash proves parsing.
`ReadStlFileAsset` validates both file and codec configuration, including encoding,
before calling capture; an invalid codec configuration cannot read a source first.

`StlVerifiedRewrite` is created only after actual staged close, reread, fresh codec
parse and independent byte/value/normal/order/metadata checks. It retains source,
writer output and the fresh document. Its entry bytes alias the fresh document;
writer output is separately retained and charged. A verified owner proves this
stage, regardless of whether a later provider, inventory or commit fails.
A bundle is exposed only after true commit. Its state, receipt and mandatory
return metadata are allocated and filled before that commit.

## SFILE-CAPTURE-001: Native paths and source protection

Validate all configuration, native extents and the logical path before capture.
Relative paths are made absolute against one captured working directory without
resolving away dot/parent components before alias admission. Reject embedded NUL,
unsupported namespaces and every symlink/reparse traversal, including an alias
hidden by `alias/../file`. Windows admits ordinary local drive paths, including
Unicode, with native wide strings; device/network namespaces are unsupported.
POSIX uses native regular-file handles; Linux canonical-handle comparison requires
`/proc/self/fd`. Unsupported no-replace or handle-path capability is a typed refusal.

The portable UTF-8 basename rejects empty/dot names, separators, NUL/control
characters, `/\\:<>"|?*`, trailing dot/space and reserved Windows device stems,
including extensions and numbered COM/LPT stems 1 through 9. Unicode scalars are
admitted without normalization or full Unicode case-equivalence guarantees.
The entry cannot collide under ASCII case folding with `stl-publication.json`.

Open a regular file without following aliases, admit its observed size before
payload allocation, read capped blocks, require exact EOF, check identity/size/
available modification metadata before and after, and check close. Compare the
handle's canonical path with the admitted logical plan in addition to component
checks. Capture writes nothing. A failed/changed/partial read has no captured
owner or successful hash. This is no atomic snapshot against an editor restoring
every observed metadata field, or a hostile filesystem actor.

Before any capture, a path-based read/rewrite API and every prepared single/batch
operation admit all logical source/destination domains. Read has no destination;
rewrite requires a new directory under an existing admitted parent. Reject existing
destinations of every kind, destination equality/ancestor/descendant overlap with
any source entry domain, overlapping destination directories,
source aliases and known equal file identities. An STL source domain is its one
entry, not every sibling in the parent directory; a disjoint output sibling is
allowed. Repeated source files in a batch
are refused. On Windows compare normalized separators and ASCII-folded components;
on POSIX compare native components. Do not infer Unicode aliases are distinct when
the native filesystem identifies them as equal. Capture-only API still admits its
source domain. `RewriteStlFile` uses its source's retained logical isolation token
internally; it never reopens that source and must work after source removal.

Logical isolation precedes payload open; later handle identity/path checks and
precommit destination checks are additional race controls. Preserve logical paths
for alias checks before canonical handle comparison. Source/destination admission
never uses `resolve()` to hide aliases. No source or destination overwrite is allowed.
For overlap checks, fold dot/parent components only after checking every original
traversed prefix for aliases; retain the original traversal for those checks.
The private implementation may reuse existing bounded native file/path primitives
and the native no-replace primitive; this introduces no public transaction framework.

## SFILE-VERIFY-001: Write, fresh parse and independent comparison

`RewriteStlFile` derives the actual context from
`source.Document().Mesh().ShareExecutionContext()`. Capture, retained source,
writer inspection/output, staged reread, fresh `ReadStl` materialization/inspection,
comparison, receipt and prepared-result owners share that ledger. Equal options
on a new unrelated context cannot substitute. No Surface proof or geometric
admission beyond the codec is used.

Before stage creation validate every nested bound/policy, source metadata under
current limits, exact output metadata, source shape and declared request sums.
Call `WriteStl` with the genuine source as above and retain its actual status,
diagnostic, execution and complete output/facts/map. A codec failure makes no
stage. Write exact output bytes to an exclusively created temporary sibling of
the destination and check actual write/close success. Read those physical bytes
from a fresh handle, check extent/EOF/metadata/close, and pass them to fresh
`ReadStl` with explicit output encoding and `fresh_read` limits/work on the same
ledger. There is no cached parse or copy of the writer's planned values as proof.

Perform all the following against actual fresh owners with bounded independent
loops; successful codec parsing alone is insufficient:

- Compare every staged byte with `StlOutput::Bytes()` and independently compute
  complete source/output byte lengths and SHA256. Hash agreement supplements exact
  byte comparison, never replaces it.
- Verify fresh facet/vertex/corner counts, complete offsets and indices are the
  expected soup identity (`3N`, `N+1`, `3N`; checked products). For each ordered
  face/corner compare fresh position bits with the source ordered corner's actual
  position bits. Compare finite raw normal bits per ordered facet/component.
  Include signed zero. ASCII/binary conversion admits exactly equal widened values.
- Check the complete sole normal descriptor: face domain, `facet_normal` name,
  `normal` semantic, three components, float64 canonical alternative, absent set,
  offsets and presence, and empty metadata; reject missing/extra channels. Empty
  input still has this exact empty descriptor. Check complete storage validity.
- Compare actual fresh encoding/name/header with the selected explicit output
  metadata. Check actual writer facts: source encoding, metadata-policy result,
  normal selection 0, expansion acknowledgment, source counts and the complete
  corner-source map. Each entry is the source corner vertex; source soup requires
  identity. Do not treat native object padding as a descriptor value.

Set `verified` only after all checks complete. Later errors keep this owner and
all reached earlier facts. A partially compared or merely parsed document is
retained separately as `fresh_document`, with verification still unsuccessful.

## SFILE-PUBLISH-001: Neutral receipt and closed directory

The closed inventory has exactly two regular flat files: the original portable
entry basename and `stl-publication.json`. No nested directory, link, unrelated
supplement or arbitrary user path is admitted. Entry case collision is refused
before capture. A selected provider supplies an optional caller-report value
inside the mandatory module-owned receipt; it adds no inventory member.

`StlFileVerifiedContent` borrows `source: const StlFileAsset&`,
`verified: const StlVerifiedRewrite&`. `StlFileReportProvider` contains
`provide: span<const byte> (*)(const StlFileVerifiedContent&, void*)` and
`state: void*`; null functions are invalid configuration. Invoke exactly once,
after `verified` exists and before receipt serialization. Provider backing may
be prepared synchronously, then must remain readable/unchanged through complete
return and joins. Import strict UTF-8 bytes under `max_caller_report_bytes` into
charged storage. Empty is valid and differs from no provider. The provider can
retain strong verified owners; it receives no publication proof. Publisher caps
bound its import/validation/copy, not arbitrary provider computation/backing.
Provider `bad_alloc` propagates after cleanup; other external exceptions become
typed `provider_failure`. Python interpreter interruptions take the higher-priority
boundary path in SFILE-PYTHON-001 before that ordinary exception translation.
Native count, import, UTF-8 and allocation refusals keep
their own diagnostics. No callback executes after commit.

Receipt JSON is UTF-8 without BOM, duplicate keys or nonfinite numbers, with
exactly these keys (no unspecified extension fields):

| Key | Exact value/type |
|---|---|
| `schema` | `"meshvale.stl-publication/1"` |
| `profile` | `"codec-preserving"` |
| `source` | object with `encoding` string, `entry` UTF-8 basename, `bytes` nonnegative integer, `sha256` 64 lowercase hex digits |
| `payload` | same four keys for the actual staged output |
| `counts` | object with `facets`, `vertices`, `corners` nonnegative integers |
| `metadata` | object with `requested` (`retain`/`replace`), `actual` (`retained_source_metadata`/`replaced_source_metadata`), `ascii_name` string or null, `binary_header_hex` 160 lowercase hex digits or null; exactly one encoding-appropriate field nonnull |
| `units` | object with `kind` (`unknown`/`label`), `label` string or null, `rescaled` false |
| `fidelity` | object with `position_bits`, `raw_normal_bits`, `facet_order`, `corner_order`, `soup_identity`, `exact_bytes` all true |
| `verification` | `"staged-reread-and-fresh-codec-verified"` |
| `caller_report` | imported UTF-8 string, or null if no provider |

Escape caller text as one JSON string; it cannot inject keys or attestations.
Counts and payload facts come from actual verified owners. Metadata uses null for
the inactive encoding (ASCII name may be empty). The receipt deliberately records
staged verification only: it contains no success, commit, final inventory, cleanup,
delivery or self-hash claim. Its bytes cannot attest facts reached after writing it.

Serialize/admit receipt, write and check close, then reopen both files. Compare
all receipt bytes with the prepared receipt and all entry bytes with the verified
owner; compute their actual byte/hash facts. Enumerate the entire directory and
check exactly the declared names, regular file kinds, extents and owned directory
identity. Set receipt verification and closed inventory observations separately.
Prepare mandatory owner/result/receipt metadata, native terminated paths and
enumeration/cleanup storage; join all workers and close all handles before commit.

Commit once using native no-replace rename on the same filesystem: Windows
`MoveFileExW` without replacement, Linux `renameat2(RENAME_NOREPLACE)`, macOS
`renamex_np(RENAME_EXCL)`. No copy, overwrite or nonexclusive rename fallback.
An existing or racing destination is refused. Missing native capability is typed
`unsupported_profile`. Native commit is the final fallible mandatory action.
After it only inline observation updates and nonthrowing handoff of prepared
owners occur; no allocation, callback, receipt patch, Python conversion or required
physical reload follows commit. Independent tests reopen committed output.

Before successful commit, failure/stop joins every worker and attempts cleanup
without allocation or cancellation checks. Check owned stage identity before
removal; never recursively delete a replaced directory or follow a link. Remove
only owned selected members and the owned directory; foreign members prevent
complete cleanup. `cleanup` is independent `not_required`, `removed` or `failed`,
with its own nullable system error. It never overwrites the primary diagnostic.
After true commit cleanup is `not_required`; late stop cannot retract publication.
Crash durability, crash cleanup, hostile concurrent staging changes and universal
network-filesystem behavior are not promised.

## SFILE-RESULT-001: Closed reached observations

Every operation result has `status`, `phase`, `diagnostic`, `observations`,
`execution`, `requested_bytes`, `cleanup`, `cleanup_error`. Capture adds
`captured: optional<StlCapturedFile>`; read adds `captured` and
`asset: optional<StlFileAsset>`; rewrite adds `source: StlFileAsset`,
`output: optional<StlOutput>`, `fresh_document: optional<StlDocument>`,
`verified: optional<StlVerifiedRewrite>`, `bundle: optional<StlPublishedBundle>`.
Read retains capture only while decode has not succeeded. The rewrite source is
always retained even for configuration failure. Expected failures preserve every
completed owner above; unexpected exceptions propagate after joins and cleanup
and do not invent a completed typed result.

`StlFileObservations` has exactly `capture`, `read`, `write`, `staged_reread`,
`fresh_read`, `verification`, `provider`, `receipt_verification`, `inventory`,
`publication`. Each is a `StlFileObservation` with `state` from
`not_attempted`, `not_applicable`, `failed`, `canceled`, `succeeded`; nullable
`diagnostic`; nullable `facts`. Set a stage attempted before work and finish its
state on every expected return. Nonapplicable stages use no diagnostic/facts.
There is no success inferred from a later phase or nonnull hash.

Native `StlFileObservation` fields are `state: StlFileObservationState`,
`diagnostic: optional<StlFileDiagnostic>`, and `facts: StlFileFacts`.
`StlFileFacts` is the closed variant of monostate, `StlFileCaptureFacts`,
`StlFileReadFacts`, `StlFileWriteFacts`, `StlFilePayloadFacts`,
`StlFileVerificationFacts`, `StlFileInventoryFacts`, `StlFilePublicationFacts`.
These records correspond to the rows below; capture/receipt use CaptureFacts,
read/fresh_read use ReadFacts, staged_reread uses PayloadFacts. Counts/lengths
are uint64, hashes are fixed `array<byte,32>`, names are borrowed string_view
backed by a retained immutable result owner, encodings/policies are their owning
enums, and verification flags are bool. ReadFacts uses optional string_view
ascii_name and optional array<byte,80> binary_header; JSON alone uses hex.
WriteFacts contains `codec: StlWriteFacts`, bytes and hash; wire conversion flattens
codec fields. InventoryFacts contains exactly `array<StlFileCaptureFacts,2>`.
PublicationFacts contains `published: bool`, always true when present, serialized
as the row below. No dynamic untyped native dictionary or caller-supplied fact is
accepted. Fields and string views are prepared before commit and share result pins.

| Stage succeeded | Exact `facts` object; other successful stages have `facts: null` |
|---|---|
| capture | `entry: str`, `bytes: uint64`, `sha256: hex64` |
| read / fresh_read | `encoding`, `facets`, `vertices`, `corners`, `ascii_name: str|null`, `binary_header_hex: str|null` |
| write | existing `StlWriteFacts` fields as lower-snake strings/integers/bool, plus `bytes`, `sha256`; `source_encoding` nullable only in generic codec, nonnull here |
| staged_reread | `bytes`, `sha256` |
| verification | `position_bits`, `raw_normal_bits`, `facet_order`, `corner_order`, `soup_identity`, `metadata`, `exact_bytes`: all true |
| receipt_verification | `entry: "stl-publication.json"`, `bytes`, `sha256` |
| inventory | `entries`: exactly two `{entry, bytes, sha256}` objects sorted by UTF-8 basename |
| publication | `outcome: "published"` |

Failed/canceled stages have `facts: null`, diagnostic nonnull; completed prior
stages keep their facts. Provider absent is `not_applicable`; successful provider
has `facts: null`. Read-mode post-read stages are `not_applicable` from preparation.
Capture-only sets read and all following stages `not_applicable`. Native decode
starts with the supplied capture's success facts. Native rewrite starts with its
supplied asset's capture/read success facts, not a fabricated fresh source capture.
`fresh_document` can exist when verification fails; `verified` can exist when
provider/receipt/inventory/publication fails; only published has a bundle.

`StlFileStatus` ordered C++ enumerators (prefix `k`, sequential from 0) are
Success, InvalidConfiguration, InvalidRequest, UnsupportedProfile, LimitExceeded,
BudgetExceeded, IoFailure, FileChanged, StlFailure, PreservationMismatch,
ProviderFailure, DestinationExists, PublicationFailed, Canceled.
`StlFilePhase`: Configuration, PathPlanning, Capture, Read, Write, Staging,
StagedReread, FreshRead, Verification, Provider, ReceiptVerification, Inventory,
Publication, Complete. Stable wire strings are their lower-snake spellings.
`StlFileCleanupOutcome` is NotRequired, Removed, Failed;
`StlFileObservationState` is NotAttempted, NotApplicable, Failed, Canceled,
Succeeded. These use the same prefix, ordinal and wire-string convention.

`StlFileDiagnostic` fields are `status`, `phase`, `code`, nullable `file_index`
(entry 0, receipt 1), `byte_offset`, `facet`, `system_error`, `stl_status`,
`stl_diagnostic`. `system_error` is `{category: str, value: int}`; delegated STL
status/diagnostic preserves every existing field recursively, including optional
Geometry status, diagnostic and storage issue. No allocating machine-path message
is required. `StlFileDiagnosticCode` ordered enumerators are None,
RequiredFieldZero, ConfigurationExtent, Encoding, Metadata, UnitDeclaration,
CountLimit, PathUnsupported, NameUnsupported, NameCollision, SourceDestinationOverlap,
NotRegular, Open, Read, Write, Close, Changed, RequestBudget, SharedBudget,
StopRequested, DelegatedStl, ByteMismatch, ValueMismatch, NormalMismatch,
OrderMismatch, MetadataMismatch, OutputFacts, Provider, ReportUtf8, StagedInventory,
DestinationExists, Commit, CommitUnsupported. Diagnostic ordinals are not exits.

`execution` has capture, writing_files, staged_reread, byte_verification,
value_verification, receipt_verification, inventory, hashing, provider_import
(`MaterializationExecutionStats` each) and read, write, fresh_read (`StlExecution`
each). Keep planned/launched/joined/caller counters actual, including partial
launch. Serial OS/metadata actions or serial codec loops report honest zero workers.
`requested_bytes` has live file_capture, file_publication and application counters,
and nullable read/write/fresh_read `StlRequestedBytes`. A released stage's live
counter becomes zero; reached observations remain. Copies are not counted twice.

## SFILE-RESOURCES-001: Complete positive bounds and real work

All native integer fields below are uint64 unless an existing owner specifies
otherwise. Every field is required, positive, inclusive and host-representable,
including inactive ASCII/binary branches; zero does not disable a cap.
Validate all products, sums, path terminators, facet-to-3N/N+1, binary84+50N,
and buffer/ptrdiff extents before work. An empty file/document still uses positive
limits. Enum selectors, booleans and absent optional values are not positive caps.

| Type | Exact fields |
|---|---|
| `StlFileLimits` | max_entry_bytes, max_receipt_bytes, max_total_file_bytes, max_entry_name_bytes, max_native_path_code_units (including NUL), max_unit_label_bytes, max_caller_report_bytes, max_file_new_requested_bytes |
| `StlFileWorkPolicy` | io_block_bytes, parallel_min_bytes, partition_bytes, cancellation_block_bytes, max_jobs |
| `StlFileRewriteLimits` | files: StlFileLimits, write: StlWriteLimits, fresh_read: StlReadLimits, max_total_new_requested_bytes |
| `StlFileRewriteWorkPolicy` | files: StlFileWorkPolicy, write: StlWorkPolicy, fresh_read: StlWorkPolicy |
| `StlReadLimits` (existing) | max_source_bytes, max_facets, max_name_bytes, max_line_bytes, max_token_bytes, max_codec_new_requested_bytes, max_total_new_requested_bytes, materialization: MeshMaterializationLimits, inspection: MeshInspectionLimits |
| `StlWriteLimits` (existing) | max_output_bytes, max_facets, max_name_bytes, max_line_bytes, max_token_bytes, max_codec_new_requested_bytes, max_total_new_requested_bytes, inspection: MeshInspectionLimits |
| `MeshMaterializationLimits` (existing) | max_vertices, max_faces, max_corners, max_face_offsets, max_attributes, max_attribute_scalars, max_attribute_offsets, max_presence_bytes, max_metadata_entries, max_descriptor_bytes, max_new_requested_bytes |
| `MeshInspectionLimits` (existing) | max_issues, max_new_requested_bytes |
| `StlWorkPolicy` (existing) | cancellation_block_bytes, cancellation_block_tokens, cancellation_block_facets, geometry: MeshMaterializationWorkPolicy |
| `MeshMaterializationWorkPolicy` (existing) | parallel_min_bytes, partition_bytes, cancellation_block_scalars, max_jobs |

The original codec sums remain mandatory: read total covers codec + materialization
+ inspection caps; write total covers codec + inspection caps. Rewrite total covers
its file-publication cap + write total + fresh-read total using checked addition.
The path read composition declares capture-file cap + read total. App per-job
allowance covers that complete read composition + rewrite total. Capture work
already done remains live in the actual root until freed; no new ledger bypasses it.

File caps cover control/accounting owners, native/UTF path plans, hashing,
temporary I/O, retained source/receipt names, provider import, verification jobs,
inventory/cleanup storage and overlapping growth/old/new blocks; codec and Geometry
owners retain their separate caps. `max_total_file_bytes` counts entry+receipt once
for publication and entry once for capture. Read source and fresh-read source caps
also apply independently. Receipt serialization includes JSON escaping expansion.

Application `StlFileApplicationLimits` has max_jobs, max_manifest_bytes,
max_manifest_line_bytes, max_job_id_bytes, max_result_bytes, max_delivery_bytes,
max_application_new_requested_bytes, max_job_new_requested_bytes. The prepared
holder/path/result/manifest/native bridge plans and output framing use its actual
charged allocation scope; Python interpreter/object allocation and arbitrary
provider code are explicitly separate, fallible interpreter scopes. Preparation
creates Python owner shells before capture. Lazy detached exports are separate
caller-requested operations and never mandatory publication work.

Application execution fields are worker_budget, minimum_parallel_vertices,
tracked_payload_budget_bytes (existing Geometry ExecutionOptions names); this
profile requires positive explicit values, despite existing generic defaults.
Before capture require root's actual current occupancy plus application cap plus
checked sum of all admitted jobs' complete allowances to fit its payload cap.
This conservative admission is separate from live backend/ledger admission, which
still governs every allocation and can fail. The check does not reserve a second
payload lease or double-charge a planned allowance. Shared contexts can become
occupied concurrently, so later typed budget failures remain possible. A reusable
source already live in that root counts actual occupancy in addition to its
remaining fresh-work caps; never pretend it has been freed.

The [finite preset](stl-file-presets.json) fully expands every nested limit/work
field. Its `contract` value names resource baseline 0.1.0, which remains compatible
unchanged with this 0.1.1 interruption-policy clarification. Its application single
root admits one job; batch root admits up to four
ordered jobs, retaining all results. Current explicit allowance per job is
696254464 bytes: capture 100663296 + read 176160768 + publication 419430400,
where publication is file 134217728 + write 109051904 + fresh-read 176160768.
The application cap is 67108864; one job fits 1073741824 and four fit 4294967296
on an empty root. Larger jobs, hosts or already occupied roots may be refused.
These are finite application choices, not universal library maxima or memory/RSS
promises. Raising one cap requires revalidating checked sums and root occupancy.

Use actual shared leased workers for substantial file byte copy/compare/hash
partitions and independent ordered value comparisons when admitted; preserve
scalar/facet boundaries and deterministic ordering. SHA256 streaming dependencies
may remain serial with bounded checkpoints and honest counters. Caller fallback
is valid when the real ledger admits no worker lease. Codec loops retain their
existing honest serial statistics. Partition payload work, bound real relocation,
and join all started workers on every exit, including partial-launch failures.
No bookkeeping-only worker claim or unlimited serial fallback loop is admitted.
Checkpoint before/after bounded file blocks, conversions, tokens, facets, name
comparisons, receipt import/escape, inventory and the final precommit boundary.
One admitted standard-library decimal conversion is indivisible and bounded by
max_token_bytes. OS/runtime internals retain their existing exclusions.

## SFILE-PYTHON-001: Prepared, reusable operation and exact getters

The implemented development module is `meshvale_interchange.stl_files`. It uses
ordinary GIL-enabled CPython with the evaluated development dependency cohort
described in the [walkthrough](stl-file-walkthrough.md).
No `Mesh` C++ instance crosses extensions. Native owners remain the authoritative
source for rewrite; an explicitly requested `export_mesh()` returns an independent
`meshvale_geometry.Mesh` through the existing owned-record protocol. Exporting
does not replace the source or authorize edits. Python buffer/result allocations
can fail and are not represented as GIL-free work or native allocation coverage.

```python
prepare_stl_read(input, *, source_encoding, units, preset="bounded", execution=None)
prepare_stl_rewrite(input, destination, *, source_encoding, output_encoding,
                    metadata, units, acknowledge_corner_expansion,
                    preset="bounded", execution=None, report=None)
run_stl(prepared, *, cancellation=None)       # returns prepared.result itself
read_stl_file(input, *, source_encoding, units, preset="bounded",
              execution=None, cancellation=None)  # prepare + run, same operation
prepare_stl_batch(jobs, *, preset="bounded", execution=None,
                  failure_policy="continue")
run_stl_batch(prepared, *, cancellation=None) # returns prepared.result itself
```

`input`/`destination` accept `str`, `bytes`, `os.PathLike` through native filesystem
encoding, with NUL/type/extent checks before native conversion. The preparer owns
immutable native path plans; relative resolution uses one preparation working
directory. Metadata is `StlMetadata(mode: str, ascii_name: str|None,
binary_header: bytes|None)`; retain requires both None, replace requires exactly
the encoding-appropriate field. ASCII str must contain admitted printable ASCII;
binary bytes has exactly 80 bytes. Units is `StlUnits(kind: str, label: str|None)`;
unknown requires None, label requires an admitted string. Constructors freeze/copy
inputs. No dictionary with implicit defaults is accepted. Encoding and mode strings
are exactly those above; acknowledgment is built-in bool true. `execution` is the
module's `StlExecutionContext` or None; None creates the preset root, whereas a
supplied context shares its actual retained ledger. `report` is None or a callable
taking `StlVerifiedRewrite` and returning strict UTF-8 `str` or `bytes`. Other
return types become typed provider failure. Handle raised or latched
`KeyboardInterrupt` through the interruption path below before translating ordinary
provider exceptions. `MemoryError` remains a distinct unexpected allocation
exception and propagates after joins and cleanup; never translate it into provider
failure or fabricate a canceled result for it. Other provider exceptions become
typed provider failure.

`Cancellation` has the existing `request_stop() -> bool` and
`stop_requested: bool` contract. `StlExecutionContext` is a new native sharing
wrapper constructed with the three explicit execution fields; its getters are
`worker_budget`, `active_workers`, `peak_workers`, `tracked_payload_budget_bytes`,
`active_tracked_payload_bytes`, `peak_tracked_payload_bytes` (all int, readonly).
Do not pass a Geometry Python context as if it were this native ledger owner.

Prepared single and batch operations are one-shot and non-reentrant. They retain
execution and original provider backing; changing Python inputs later has no
effect. All shells/results/job slots, paths and mandatory observations are prepared
before capture. `PreparedStl.result -> StlResult` and
`PreparedStlBatch.result -> StlBatchResult` return the same stable holder each time.
No constructors permit forged captured/document/verified/published owners.
Concurrent reads of completed immutable native owners are permitted; same prepared
holder mutation/run requires external synchronization. Getters during run require
the caller's coordination and do not expose inconsistent in-progress snapshots.

The exact getters below are readonly properties unless parentheses indicate a
method. Native-owner getters return precreated Python sharing shells, not an
implicit mesh/bytes copy. Detached exports are explicitly named and may allocate.
All integer counts use Python int without narrowing. Each detached observation
or diagnostic dictionary is recursively copied; mutating it changes no native fact.

| Python type | Exact getters/methods and types |
|---|---|
| `StlCapturedFile` | `entry: str`; `byte_count: int`; `sha256: str`; `new_requested_bytes: int`; `export_bytes() -> bytes` detached copy |
| `StlDocument` | `encoding: str`; `ascii_name: str|None`; `binary_header: bytes|None` detached 80-byte copy; `facet_count: int`; `vertex_count: int`; `corner_count: int`; `new_requested_bytes: dict[str,int]` with codec/materialization/inspection/total; `export_bytes() -> bytes`; `export_mesh() -> meshvale_geometry.Mesh`, both detached copies |
| `StlFileAsset` | `document: StlDocument`; `entry: str`; `byte_count: int`; `sha256: str`; `new_requested_bytes: int`; `export_bytes() -> bytes` detached copy |
| `StlOutput` | `encoding: str`; `byte_count: int`; `sha256: str`; `facts: dict` exact write-facts schema above; `new_requested_bytes: dict[str,int]` same four codec keys; `export_bytes() -> bytes`; `export_corner_source_vertices() -> tuple[int,...]`, detached complete output-corner-to-source-vertex map |
| `StlVerifiedRewrite` | `source: StlFileAsset`; `output: StlOutput`; `document: StlDocument` fresh actual parse; `entry: str`; `byte_count: int`; `sha256: str`; `new_requested_bytes: int`; `export_bytes() -> bytes` detached copy |
| `StlPublishedBundle` | `verified: StlVerifiedRewrite`; `receipt_entry: str`; `receipt_byte_count: int`; `receipt_sha256: str`; `new_requested_bytes: int`; `export_receipt_bytes() -> bytes` detached copy |
| `StlResult` | `job_id: str`; `mode: str`; `declarations: dict`; `outcome: str`; `phase: str`; `diagnostic: dict|None`; `observations: dict`; `execution: dict`; `requested_bytes: dict`; `cleanup: str`; `cleanup_error: dict|None`; `not_attempted_reason: str|None`; `captured: StlCapturedFile|None`; `asset: StlFileAsset|None`; `output: StlOutput|None`; `fresh_document: StlDocument|None`; `verified: StlVerifiedRewrite|None`; `bundle: StlPublishedBundle|None`; `delivery: dict`; `exit_code: int`; `export_json() -> bytes|None`, detached bounded UTF-8 JSON |
| `StlBatchResult` | `jobs: tuple[StlResult,...]` fixed identity/order; `outcome: str`; `admission_diagnostic: dict|None`; `delivery: dict`; `exit_code: int`; `export_json() -> bytes|None` detached bounded UTF-8 JSON |

Native `StlFileAsset` does not have a replace/with_mesh method in this profile.
Python `asset` is the source after successful read, including later rewrite failure.
All source/metadata and binary-name getter nullability above is encoding-specific;
native empty span/string inactive getters do not imply nonnull Python metadata.
The output corner map is an owned detached tuple, never a borrowed memoryview;
no face/vertex correspondence map is invented for an unselected edit operation.

`declarations` is exactly `{source_encoding, output_encoding, metadata_mode,
units, acknowledge_corner_expansion}`. Source encoding is nonnull; output encoding
and metadata_mode are null for read. units is the exact `{kind, label, rescaled}`
object from the receipt, including false rescaled; acknowledgment is null for read
and true for rewrite. These are admitted requests retained before capture, distinct
from actual reached metadata/write facts. Metadata bytes/names are recorded by the
actual read/write facts rather than invented for an unattempted job.

Preparation representation errors raise TypeError, ValueError or OverflowError
before native capture. Expected filesystem/domain admission refusals return a
prepared holder whose admission is failed, allowing the same command schema.
Preparation allocation failure may raise MemoryError with no file side effect.
Running uses GIL-released native file/codec/worker work; Python providers/exports
hold the GIL. All successful-result owner shells and inline handoff slots are
already allocated before native commit. Lazy getters/exports after return may
raise independently without changing stored native facts or publication.

SIGINT/KeyboardInterrupt is observed at actual interpreter boundaries, including
before/after the GIL-released call and Python provider entry. A Python handler
cannot be promised to execute promptly while the main thread remains GIL-free in
native work. A different Python thread may request the shared native cancellation
token during that work. At a Python provider boundary, recognize and latch a raised
or pending `KeyboardInterrupt` while holding the GIL, request the shared native
stop, and take the interruption path before the ordinary provider-exception
translator. The provider observation is canceled, not failed with provider_failure;
preserve all completed capture/read/write/fresh-read/verification owners and facts,
join every worker and perform identity-safe precommit cleanup. The prepared run
functions catch boundary `KeyboardInterrupt`, join/unwind and return the same
holder under the cancellation/delivery precedence below, with exit 130. A provider
interruption prevents commit; no provider executes after commit. An interruption
first observed at a later interpreter boundary after true commit keeps the stored
bundle and publication succeeded, and records delivery canceled/interrupted with
exit 130. `MemoryError` follows its separate propagation rule above. Process
termination can prevent any result delivery. The convenience read cannot expose
its local holder after an
unexpected exception; use prepare/run when recovery access is required.

## SFILE-APPLICATION-001: Single and ordered batch

Console and module entry points are equivalent:
`meshvale-stl ...` and `python -m meshvale_interchange.stl_files ...`.
Select `read INPUT --source-encoding ascii|binary --units unknown|label`
or `rewrite INPUT DESTINATION --source-encoding ... --output-encoding ...
--metadata retain|replace --units unknown|label --acknowledge-corner-expansion`.
Label additionally requires `--unit-label TEXT`. Replacement ASCII requires
`--ascii-name TEXT` (empty accepted); binary requires `--binary-header-hex HEX160`.
Inapplicable metadata/label flags are errors. `--preset bounded` selects the
expanded preset; no hidden per-command limits differ from the reusable API.
`--report-text TEXT` is the command's optional UTF-8 caller-report provider.
No generic format/repair mode is accepted.

`batch MANIFEST --failure-policy continue|fail-fast --preset bounded` accepts
strict UTF-8 JSON Lines, one object per nonempty line, no comments/BOM/blank lines,
duplicate/unknown keys, NaN, infinity or trailing JSON. Manifest paths are relative
to its containing directory; single/API paths use preparation's working directory.
Read all bounded manifest bytes and validate every row and cross-job domain before
opening any source payload. Manifest itself is a protected source domain for
destination isolation. Invalid last row means zero source captures and zero stages.

`jobs` in `prepare_stl_batch` is a bounded iterable of frozen `StlReadJob` or
`StlRewriteJob`; materialize it completely in preparation. Their exact constructor
fields, also manifest keys, are `job_id: str`, `mode` (`read`/`rewrite`),
`input` path, `source_encoding`, `units: StlUnits`, and for rewrite only
`destination` path, `output_encoding`, `metadata: StlMetadata`,
`acknowledge_corner_expansion: bool`, `report_text: str|None`. Read forbids rewrite
keys. Manifest metadata is an exact object `{mode, ascii_name, binary_header_hex}`;
units is exact `{kind, label}`; inactive fields are explicit null. No callable is
accepted in a manifest; report_text becomes the same verified-stage provider.
Job IDs are unique, nonempty bounded scalar UTF-8 without controls; all paths,
counts/field types and selected options obey the same API checks. Single IDs are
`"single"`. Batch runs in input order under one root with fixed holders for all
jobs; it performs no simultaneous job execution in this version. Work within each
job still uses admitted workers.

Ordinary job failure continues by default; fail-fast leaves remaining slots
`not_attempted` with reason `fail_fast`. Cancellation prevents remaining captures
with reason `canceled`. Whole-batch admission failure leaves every slot
`not_attempted` with reason `admission_failed`. Every known valid input row retains
its slot even if a later row is invalid; malformed unidentifiable rows retain a
line-index admission diagnostic without a guessed job ID. No partial batch runs.
Reserve the max_jobs slot array before framing/materializing rows, and keep it
stable. Expected final serialization/stream failure cannot erase completed job
owners, observations or slots. The reusable caller can inspect the prepared
batch holder after the failure; command stdout alone is not an owner store.

`StlResult.outcome` is exactly `not_attempted`, `read`, `failed`, `canceled` or
`published`; top-level outcome does not erase reached stage facts.
`StlBatchResult.outcome` is `admission_failed`, `completed`, `failed` or `canceled`.
A job slot initially has configuration phase, no owners, no diagnostic, cleanup
not_required and all applicable stages not_attempted. A read success has capture/
read succeeded, asset nonnull, all rewrite stages not_applicable. A parse failure
has capture succeeded and captured nonnull, read failed, no asset/output. A capture
failure has capture failed, no captured/asset, read not_attempted. Writer failure
keeps asset/read facts, output null. Later failures follow SFILE-RESULT-001.
Unattempted slot not_attempted_reason contains its cause; no failure stage is fabricated.

| Root/job condition | Job outcome | Retained owners and observations |
|---|---|---|
| Admission fails before capture | not_attempted | no owners; applicable stages not_attempted; reason admission_failed |
| Earlier fail-fast failure | not_attempted | no owners; applicable stages not_attempted; reason fail_fast |
| Batch stops before this job | not_attempted | no owners; applicable stages not_attempted; reason canceled |
| Capture fails | failed (canceled for observed stop) | no captured/asset; capture failed/canceled, read not_attempted |
| Capture succeeds, decode fails | failed (canceled for observed stop) | captured nonnull; capture succeeded, read failed/canceled |
| Read succeeds | read | asset nonnull; captured null; capture/read succeeded |
| Rewrite later fails | failed (canceled for observed stop) | asset and every completed output/fresh/verified owner remain; bundle null |
| True native commit | published | bundle nonnull; publication succeeded regardless of later delivery |

`admission_diagnostic` is null or exactly `{code, line_index, job_index, field,
file_diagnostic}`. Indices are zero-based int or null; field is an admitted field
name string or null; file_diagnostic is the exact native diagnostic above or null.
code is one of representation, manifest_utf8, manifest_json, manifest_extent,
job_extent, duplicate_job_id, path_admission, domain_overlap, root_budget,
previously_run. A command grammar error is representation with no job/line index.
Unattempted jobs have diagnostic null; their explicit not_attempted_reason carries
the skip cause. Admission failure therefore never fabricates a failed codec stage.

Command JSON envelope has exactly `schema: "meshvale.stl-result/1"`,
`mode: "read"|"rewrite"|"batch"`, `outcome`, `admission_diagnostic`, `jobs`,
`delivery`, `exit_code`. jobs is always an ordered array, including a single slot.
Each job object has exactly job_id, mode, declarations, outcome, phase, diagnostic, observations,
execution, requested_bytes, cleanup, cleanup_error, not_attempted_reason,
owners. owners is exactly captured, asset, output, fresh_document, verified, bundle,
with boolean values saying which genuine owners are retained (never serialized
native addresses). Observation/diagnostic schemas are those above. No source or
machine paths are emitted; only admitted entry basenames and job IDs appear.
`not_attempted_reason` is null except the three reasons above. Read/write facts
do not include the potentially large corner map; the explicit getter owns export.
All JSON integers preserve uint64 values; consumers must not narrow them to float.

## SFILE-DELIVERY-001: One cancellation and exit precedence

`delivery` is exactly `{state, code, bytes_written}` with state `not_attempted`,
`succeeded`, `failed` or `canceled`, code null or `framing`, `write`, `flush`,
`interrupted`, and nonnegative bytes_written. API run returns the stable holder
with delivery not_attempted; result export is a separate bounded delivery attempt.
`export_json()` returns None on expected framing/limit failure and updates the
stable holder; allocation exceptions still propagate. A successful bytes export
marks that API framing delivery succeeded, with bytes_written equal to its byte
length; it makes no statement about a caller's later stream write. Batch job
delivery remains not_attempted because the batch envelope is the delivery unit.
The console prepares/fragments all mandatory JSON before capture/commit where
needed, fills preallocated fixed-width/native observation slots by nonthrowing
handoff, then formats/streams from the holder under max_result/max_delivery bounds.
Expected framing, broken-pipe/write and flush failure update delivery independently.
Retain all slots, owners and reached facts in memory. Native commit is not rolled
back because Python/command output failed. Unexpected exceptions retain the
exception contract; they are not converted to invented expected success/failure.

An emitted frame describes `delivery.state: "not_attempted"`, code null and
bytes_written 0: a frame cannot attest its own later successful write/flush.
The authoritative stable holder and process exit observe delivery afterward.
No trailing rewritten frame, success receipt patch or second terminal frame is
required. An optional concise stderr diagnostic follows the same stream-failure
rules and never changes an already recorded publication fact.

Select the exit once from these rules in order, shared by API `exit_code` and
commands: admission failure 2; observed operation cancellation or interpreter
interruption 130; expected delivery failure 3; any attempted job failure 1;
otherwise 0 (including API delivery not_attempted). Batch fail_fast unattempted
slots alone do not add failures; their triggering failed job does. Batch cancellation
always wins over earlier ordinary failures and delivery failure. A cancellation
request not observed before/at an actual boundary is not an invented canceled job.
Within native work the first detected error is primary; a later stop does not
replace it. A precommit observed stop prevents commit. A true commit yields
publication succeeded/published, even when a later boundary interruption makes
the application exit 130 or delivery fail. Cleanup failure remains secondary.

## SFILE-ACCEPTANCE-001: Required evidence before capability claims

Qualification requires independent literal ASCII and packed binary fixtures,
actual file close/reread/fresh codec parsing and independently reopened committed
values. Include empty/count-boundary documents, finite degenerate and repeated
facets, negative/signed-zero/subnormal/range-boundary values, zero/nonunit/opposed
normals, malformed extents, extra bytes/nonzero words, exact/inexact binary32
conversion, names/opaque headers and every same/cross-encoding metadata branch.
Verify complete normal descriptors, facet/corner order, soup IDs and the actual
corner map, plus no hidden normal generation or coordinate changes.

Exercise missing/removed/modified sources, retained owner/copy/last-release and
prepared-session destruction, Unicode names, aliases hidden by parent components,
single/API/batch logical overlap, existing/racing destinations and stage identity
replacement. Inject real I/O, short/extra read, checked close, allocation/ledger,
provider/import/UTF-8, worker/partial-launch/stop, comparison/receipt/inventory and
commit/cleanup failures. Prove reached capture-only and later-stage owner/facts,
invalid-last-row zero capture, stable all-job holders after expected delivery
framing failure, continuation/fail-fast, cancellation precedence and API/module/
console/batch parity. Include provider-boundary raised/pending KeyboardInterrupt
versus ordinary provider failure and distinct MemoryError propagation, with retained
facts, cleanup and actual late-commit observations. A boolean corruption hook
cannot replace an actual native write, close, reread, parse, comparison or
no-replace call.

Run the owning native suites, original installed walkthrough, native source archive,
relocated install, isolated Python package and command examples, supported/unsupported
allocation-profile checks, applicable sanitizers and declared hosted configurations
for their actual source/dependency identities. Then qualify actual integrated main
separately. Existing STL codec, Surface publisher or OBJ Python greens alone prove
none of this new workflow. Supported runtime floors, stable releases and package-index
publication remain separate claims requiring their own completed evidence.
