# Verified surface directory publication

**Status: contract draft; implementation pending.** This document selects the
intended physical publication behavior for a separate opt-in
`meshvale::surface_files` target and proposed `surface_files.h` interface.
`MESHVALE_INTERCHANGE_SURFACE_FILES` defaults to `OFF` and requires
`MESHVALE_INTERCHANGE_SURFACE_OUTPUT=ON`. These declarations, target and file
operations are not implemented by this draft. The supplied-memory
[surface contract](surface-output.md), its existing header and its qualification
remain separate. This draft and [receipt schema](surface-publication.schema.json)
require independent review before implementation.

## Scope and interface

The proposed single operation is `PublishSurfaceBundle(proof, destination,
options, limits, work, stop)`. The input is a genuine, retained
`SurfaceOutputInspection`. The operation writes its exact captured payloads;
it does not encode again or construct a source `GltfFileAsset`. It covers admitted
glTF JSON, GLB, ASCII STL and binary STL, for both strict bare and annotated
representations. A publication receipt is present in every successful physical
bundle, including strict bare output. The receipt is publication metadata, not
an additional `SurfaceMemberKind` or a member supplied to memory inspection.

The proposed names are `SurfaceFilePublicationOptions`,
`SurfaceFilePublicationLimits`, `SurfaceFilePublicationWorkPolicy`,
`SurfaceFilePublicationResult` and `SurfacePublishedBundle`. The result has an
outcome (`kFailed`, `kCanceled`, `kPublished`), fixed typed diagnostic, cleanup
observation, optional committed bundle and nullable reached-only facts. The
copy-only bundle has a private constructor and exposes only `Inspection()`
(the fresh physical proof), `Receipt()` (owned path/bytes/SHA256 view) and
`NewRequestedBytes()`. It is exposed only after actual successful commit.
No public context, transaction, overwrite switch, arbitrary supplemental-file
list or phase callback is provided. The complete typed outline below fixes the draft interface names and fields;
implementation must follow the independently reviewed contract. Enum ordinal
values have no receipt-wire meaning. This draft is not an implemented ABI.

## Configuration and limits

All scalar count/byte/work limits are positive, inclusive and checked for
uint64_t, size_t, ptrdiff_t and actual platform extents. No clamping or implicit
limit defaults. The proposed records are:

```cpp
struct SurfaceFileLimits {
  std::uint64_t max_payload_members = 0;
  std::uint64_t max_payload_member_bytes = 0;
  std::uint64_t max_total_payload_bytes = 0;
  std::uint64_t max_receipt_bytes = 0;
  std::uint64_t max_caller_report_bytes = 0;
  std::uint64_t max_path_bytes = 0;
  std::uint64_t max_total_path_bytes = 0;
  std::uint64_t max_native_path_code_units = 0;
  std::uint64_t max_inventory_entries = 0;
  std::uint64_t max_stage_name_attempts = 0;
  std::uint64_t max_new_requested_bytes = 0;
};
struct SurfaceFilePublicationLimits {
  SurfaceFileLimits files;
  SurfaceInspectLimits inspection;
  std::uint64_t max_total_requested_allowance = 0;
};
struct SurfaceFileWorkPolicy {
  std::uint64_t io_block_bytes = 0;
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct SurfaceFilePublicationWorkPolicy {
  SurfaceFileWorkPolicy files;
  SurfaceWorkPolicy inspection;
};
```

Payload caps exclude the one receipt. Path totals include every payload path and
the receipt path once; native paths include the terminator (UTF-16 code units on
Windows, char units on POSIX). `max_inventory_entries` bounds every enumeration
pass, counting observed files and directories before the next visit; the known
complete payload/receipt/parent-directory set must fit before stage creation.
Cleanup uses the same bounded, pre-admitted walk capacity and reports failure
rather than silently exceeding it. Stage attempts count actual exclusive
creation attempts, not existence probes. Receipt and report byte limits include
zero actual bytes while remaining positive configured caps.

Validate active file/fresh-inspection caps, selected nested work, checked
allowance, provider configuration, receipt path and known input/path extents in
that order before handling stop or creating a stage. Preserve the memory
inspector's first-invalid order for its own configuration. An already stopped
call cannot hide invalid configuration. This operation has no encoder-limit
object: encoding was validated when the genuine retained output/proof was
created. The earlier physical-publication paragraph's encoder-cap requirement
means validating the operations actually invoked, not inventing a new Encode
call or unused encoding configuration. The memory contract is unchanged.

## Shared ownership, admission and work

Derive the actual root ledger from
`proof.Expected().Original().Source().ShareExecutionContext()` and verify its
actual shared-budget relationship with the charged triangulation, as the memory
inspector does. No caller context or replacement ledger is accepted. A fresh
request-limited P owner charges file state/control blocks, paths, handles and
walk records, job/failure records, capture buffers, digests, receipt/report
storage, growth/rebind overlap and final owner preparation. Fresh V uses its own
`SurfaceInspectLimits` and work on that same root. Required checked admission is:

```text
publication.max_total_requested_allowance >=
  files.max_new_requested_bytes + inspection.max_total_requested_allowance
```

The nested V allowance already includes its actual read/container/extraction
requirements under the memory contract; do not add them twice. The sum is
containment validation, not a reservation, peak or success guarantee. Existing
E/input-proof occupancy, other root users and caller-owned provider storage
still consume their actual budgets. P physical capture and V's new captured
copy overlap while V runs. Release P-only temporary captures only after V owns
its independent copies. Retain the expected output and fresh proof for every
borrowed view that needs them.

All explicit allocations use those charged owners. Provider computation and
its caller-owned returned storage are outside P until imported; that exclusion
is not an unbounded module allocation allowance. No hidden default-allocator
JSON tree, path cache, emergency error allocation or post-return charging.
The bundle P getter counts P only; the fresh inspection getter counts V
only. Aliases of the same operation state are not additive. Root retained bytes,
allocator/runtime overhead and RSS are not measured by these getters.

Large copy/read/write/compare work uses disjoint bounded ranges on the existing
shared executor, with actual max-jobs/partition/stop controls, no new executor.
Dependent path planning, enumeration and ordered hash reduction may be serial.
Join all launched work before closing a shared handle, publishing facts or
propagating an exception. Reserve result/failure records before launch; reduce
failures by payload ordinal then byte offset, not thread arrival. Actual written
bytes are aggregate completed stores, not an invented global prefix. Stop is
cooperative, checked between bounded blocks; no hard wall-clock or OS-I/O latency
guarantee. Delegate profile checks and work remain the fresh inspector's actual
thread-local observations, never copies from the input proof.

## Paths and selected platform behavior

`destination` names a new directory whose parent already exists. It is borrowed
through return/all joins; relative destination resolution is captured before
work, without changing process cwd. Native Unicode destination paths are admitted
only by the selected platform path contract. The operation plans charged native
strings, admits the parent, and creates a fresh sibling stage using exclusive
creation. Only an actual name collision advances the bounded candidate sequence;
other errors fail immediately, and exhaustion is typed I/O failure.

`options.receipt_path` defaults to `publication.json`. It is a portable relative
ASCII path: slash-separated nonempty segments, each starting in `[A-Za-z0-9]`,
containing only `[A-Za-z0-9._-]`, and not ending in a dot. Reject absolute/drive/
UNC paths, backslashes, dot segments, controls, percent/colon spellings, trailing
space/dot aliases and Windows device basenames (case-insensitive, including an
extension). Reject exact or ASCII-case-folded equality with any payload and any
file/directory prefix collision; do not normalize or rename either path. Shared
parent directories are allowed when no file occupies them. Derive and admit the
complete expected directory set before creation. This ASCII choice follows the
existing [native payload names](surface-output.md#native-member-layout); it does
not exclude any genuine current Surface output. It is not universal Unicode
filesystem alias detection.

Inventory admits only the exact expected directories and regular payload files,
then the one receipt. Reject unknown/missing/duplicate members, symlinks/reparse
points and unsafe or unsupported file kinds/aliases according to the explicit
platform checks. Capture size/identity around each read; refuse observed
replacement, truncation or extension. Checks do not promise to prevent arbitrary
hostile filesystem mutation between the final check and rename.

Reuse only the existing private `CommitNoReplace` native primitive: no replacing,
copying, delete-then-rename or weaker fallback. Current primitive branches use
Windows no-replace move, Linux renameat2/RENAME_NOREPLACE, or Apple renamex_np/RENAME_EXCL; absent capability is typed
unsupported. A platform/volume's path, no-follow, identity, enumeration, close,
no-replace and cleanup routes require their own qualification. No universal
atomicity, hard-link defense, crash durability, fsync, network-filesystem or
cross-volume guarantee follows from a successful ordinary call.

The required native adapter profiles are explicit, not inferred from a compiler:

| Route | Required checks before publication |
|---|---|
| Windows | Valid native UTF-16 destination, charged absolute/canonical parent planning, handle metadata identity and regular-file checks, refusal of reparse components, exclusive new stage/files, explicit full writes/readback and close errors; native no-replace move without replacement/copy flags |
| Linux | Native path-component admission without symlink traversal, exclusive stage/files, no-follow opens and fstat-style regular-file/device/inode/size identity checks, complete bounded enumeration/readback/close; renameat2 with RENAME_NOREPLACE |
| Apple | The same admitted POSIX no-follow/identity/I/O/cleanup profile, with renamex_np and RENAME_EXCL for commit |
| Other or missing filesystem capability | Typed unsupported/failure; never silently use replacing rename or copy |

The adapter must preserve an owned-stage identity and refuse unsafe descent or
cleanup on replacement. Required native flags/identity facilities are checked by
the implementation/platform qualification; absence cannot be covered by a path
string comparison. Filesystem/volume failure remains an actual typed error even
on a listed OS. This table specifies required behavior, not current availability.

## Actual stage order

1. Validate configuration and genuine input; admit all known paths, operation
   state, inventory/walk/failure capacity, then create the owned stage. Write the
   exact input `CapturedMembers()` bytes with exclusive file creation. Join and
   explicitly close every payload handle; report real close errors.
2. Enumerate the complete payload inventory, reopen and capture every actual
   staged payload under P, then call fresh `InspectSurfaceOutput` against
   `proof.Expected()` using those captures, new V caps/work and this call's stop.
   Require its actual successful strong proof. A memory proof or matching digest
   cannot replace this native decode/compare.
3. Invoke the optional provider once, synchronously on the calling thread, using
   that actual staged proof. Import its bounded UTF-8 report, generate the native
   receipt, write/join/close it, and reopen/reread its complete bytes.
4. Finish every callback, join, fallible committed-owner preparation and
   temporary-owner release. Prepare immutable state/strong aliases and all
   buffers/records needed for the last pass before that pass begins. Retain all
   storage whose later destruction could otherwise trigger a free callback.
5. Repeat complete inventory (payloads plus receipt) and raw verification of
   every payload and receipt against retained actual expected bytes, not just
   sizes, JSON shape or cached digests. A changed payload refuses; never silently
   update the proof/receipt to it. This pass invokes no application or
   allocation/free callback. Its native read/enumeration/close operations can
   still fail. Close every handle before commit.
6. Invoke native no-replace commit as the last fallible action. Only no-throw
   field/strong-owner handoff follows successful commit. No new stop check,
   callback, allocation, temporary-owner release or free callback follows it.

The receipt cannot describe its own write/close/reread, the later final recheck,
commit or cleanup as already successful. Their actual observations belong only
to the returned typed result/facts. No callback or stop check follows successful
commit. Caller destruction of a returned owner is `noexcept` and releases its
retained charges normally; that later lifetime event is not precommit evidence.

## Receipt and caller-report seam

Always generate one module-owned receipt following
[surface-publication.schema.json](surface-publication.schema.json), with schema
ID `meshvale.surface-publication/1`. It contains encoding, representation, entry
path, UTF-8-path-sorted closed `payload_members` (kind/path/canonical-u64 byte
length/lowercase SHA256), completed payload-write and reread observations, and
actual fresh-inspection success. It includes `surface.json` when present, but
excludes the receipt itself. The existing capsule excludes its own JSON hash;
the publication receipt adds that payload edge without any self-hash cycle.
The typed returned receipt view may expose the receipt's actual digest outside
its JSON. A digest is byte integrity, not a signature or external authority.

```cpp
struct SurfaceFileVerifiedContent {
  const SurfaceOutputInspection& inspection;  // Fresh physical V only.
  std::string_view entry_path;
};
struct SurfaceReceiptProvider {
  std::span<const std::byte> (*provide)(
      const SurfaceFileVerifiedContent&, void* state) = nullptr;
  void* state = nullptr;
};
struct SurfaceFilePublicationOptions {
  std::string_view receipt_path = "publication.json";
  SurfaceReceiptProvider receipt{};
};
```

Disabled provider is the canonical null-function/null-state pair. Non-null state
with a null function is invalid configuration; an enabled function may have null
state. All options, callback/state, visible input handles and borrowed views stay
alive and unchanged through return/all joins. Callback content references borrow
the publisher only during the call. A provider may retain a strong proof copy,
not an unowned reference to the view. Its returned report bytes remain alive and
unchanged through publication return/all import joins. The module copies them
into P and never retains an unowned callback-produced view. An enabled provider
returning empty bytes produces an empty report object; disabled produces null.
No report filename or additional physical file is implied.

The optional `caller_report` is `{encoding:"utf-8", byte_length, sha256, text}`.
Receipt JSON is UTF-8 without BOM, duplicate decoded keys or nonfinite numbers.
Validate shortest valid UTF-8 caller bytes, including embedded NUL; reject malformed/overlong
UTF-8 or surrogate code points. JSON-escape reversibly, without normalization or
numeric parsing. Length and digest describe the exact original unescaped UTF-8
bytes; embedded NUL is escaped, not truncated. Validate checked worst-case
six-byte-per-input-byte JSON escaping and native extents before allocation,
then compute/check exact complete receipt size before allocating its buffer.
Configured maxima need not promise every allowed report fits the receipt cap;
an actual oversized complete receipt refuses normally, without clamping.

This text is explicitly uninterpreted caller data. The module does not certify
Geometry/Repair report semantics, parse it as a report object, expose it as an
existing standalone report artifact or infer a filename. A later adapter may
extract the exact UTF-8 bytes and invoke that report's actual validator. Caller
claims inside the string are not native publication facts. Generating the core
rather than accepting a full caller receipt avoids a second reserved-field,
duplicate-key and temporal-claim validator while retaining that later seam.

## Fresh inspection profiles

Validate and run the existing inspector under its actual numerical support and
delegate-thread rules. Do not add a blanket compiler/profile refusal. In the
unsupported materialization test profile, a genuine admitted empty input may
still produce an accepted memory proof and fresh physical inspection; preserve
that actual route. Nonempty unsupported input returns the actual reached
refusal, with no forged inspection or synthetic skipped delegate. Both qualified
and unsupported routes require tests; a profile name alone establishes neither
success nor failure. Existing numeric conversion, source maps and image bytes
are unchanged by publication.

## Typed outcomes and temporal facts

Use a fixed `SurfaceFileDiagnostic`: status/stage/code, optional member ordinal
and byte offset, optional `std::error_code`, and optional actual delegated
`SurfaceStatus`/`SurfaceDiagnostic`. No unbounded owned diagnostic string.
Primary diagnostic is never overwritten by cleanup. Proposed stage names are
Configuration, PathPlanning, Staging, Reread, Inspection, Receipt, Inventory,
Publication and Complete. The semantic mapping is:

| Actual event | Required outer status/observation |
|---|---|
| Invalid positive/extent/provider/path configuration | `kInvalidConfiguration`, before stage/stop |
| Incompatible genuine input storage | `kInvalidStorage`, with no fabricated inspection |
| Logical cap/host-size arithmetic refusal | `kLimitExceeded`; preserve actual subject |
| P request cap or shared root budget denial | `kBudgetExceeded`; do not guess which denied |
| Observed stop before commit | `kCanceled`; cleanup separately actual |
| Unsupported path/identity/no-follow/native capability or actual fresh numeric profile | `kUnsupportedProfile`, no weaker fallback |
| Actual open/enumerate/read/write/close failure | `kIoFailure`, correct stage/error/partial I/O |
| Missing/extra/replaced/unsafe-kind/mismatched inventory or bytes; failed fresh proof | `kVerificationFailed`, embedded actual V outcome when called |
| Existing/racing final destination | `kDestinationExists`; destination unchanged |
| Failed native no-replace operation | `kPublicationFailed`, actual native error |
| Non-`bad_alloc` provider exception | `kCallbackFailure` in Receipt, after joins/cleanup |
| Actual no-replace success | `kSuccess`/`kPublished`; committed owner present |

`SurfaceFileCode` uses the following complete draft vocabulary; the names and
meanings below are fixed. It is not a receipt-wire integer enumeration.
Member ordinals address the path-sorted payload list; ordinal N addresses the
separate receipt after N payloads. Unrelated path/parent errors have no member
ordinal. Byte offsets are present only when actually known.

| Code | Detection and stage |
|---|---|
| `kNone` | Actual success, Complete |
| `kRequiredFieldZero`, `kConfigurationExtent`, `kUnknownEnum` | Zero scalar, unrepresentable/overflowed configuration, unknown selector; Configuration |
| `kEncodingLimitsMismatch`, `kAllowanceTooSmall` | Invalid selected V delegates or insufficient checked P+V allowance; Configuration |
| `kProviderConfiguration` | Noncanonical disabled provider; Configuration |
| `kDestinationPath`, `kReceiptPath`, `kPathCollision` | Invalid destination syntax, portable receipt spelling, exact/case/prefix alias; Configuration/PathPlanning as actually reached |
| `kSourceContract` | Genuine retained input/ledger relationship does not satisfy this operation; PathPlanning |
| `kCountLimit`, `kRequestBudget`, `kStopRequested` | Actual logical cap, request/root budget, or observed stop; current actual stage |
| `kUnsupportedProfile` | Missing required numerical/path/file capability; actual detection stage |
| `kDestinationParent` | Parent admission fails; PathPlanning |
| `kStageCreate`, `kStageAttemptsExhausted` | Noncollision exclusive-create error or exhausted actual collision attempts; Staging |
| `kFileOpen`, `kFileRead`, `kFileWrite`, `kFileClose`, `kEnumeration` | Actual native operation fails; Staging/Reread/Receipt/Inventory as applicable |
| `kMemberInventory`, `kFileChanged`, `kFileKind` | Missing/extra member, observed identity/size/raw-byte change, unsafe nonregular file; Reread/Inventory |
| `kInspection` | Actual fresh V expected failure; Inspection with its real typed diagnostic |
| `kCallerReportEncoding`, `kReceiptSize`, `kReceiptBytes` | Invalid UTF-8, actual escaped receipt cap, receipt reread mismatch; Receipt |
| `kProvider` | Non-bad_alloc exception from the provider call only; Receipt |
| `kDestinationExists`, `kCommitFailed`, `kCommitUnsupported` | Actual no-replace result; Publication |

Configuration first-error precedence is: file scalar and file work fields in
record declaration order; fresh-inspector configuration in its existing order;
checked combined allowance; provider pair; destination/receipt syntax and alias
rules; known payload/path/complete-walk extents. Only then handle stop, actual
parent admission and stage creation. A data-dependent later I/O/inspection
failure does not retroactively replace an earlier completed failure. Normal
parallel reductions preserve earliest member then byte offset.

Malformed caller UTF-8 is `kInvalidStorage/kCallerReportEncoding` in Receipt;
escaped receipt-size refusal is `kLimitExceeded/kReceiptSize`; receipt reread
mismatch is `kVerificationFailed/kReceiptBytes`. Unsupported capability is
distinct from an actually observed substituted symlink/reparse/unsafe member,
which is `kVerificationFailed/kFileKind`. Configuration arithmetic overflow is
`kInvalidConfiguration/kConfigurationExtent`; data-dependent extent refusal is
`kLimitExceeded/kCountLimit`. Attribute budget denial only from actual allocator
budget provenance, preserving arithmetic failures; never guess request versus
root pressure.

The optional V diagnostic preserves real call status; map its cap, cancellation
and unsupported results to the matching outer categories, rather than flattening
them into verification failure. A fresh V call observation exists only if that call was entered; retain its actual
returned status even when its facts are absent. No inner delegate call fact exists
when admission/profile checks prevented that inner invocation. Engine/thread/unexpected runtime exceptions and
`std::bad_alloc` propagate after joins and cleanup. Catch only the actual provider
invocation for provider-exception mapping; never normalize an engine exception
as callback failure. Unknown enum/configuration values refuse rather than being silently
interpreted. The implementation header must use the complete outline below.

Nullable phase observations become present only when the phase is entered;
partial byte/count/work values describe actual progress, not a completed call or
one global prefix. Fresh V facts are from this call only, never copied input E/V
facts. Receipt write/reread/final inventory and commit observations become present
after their actual events. Reserve/freeze facts without emergency allocation.
A bundle is absent on every expected failure/cancel. Cleanup is `kNotRequired`,
`kRemoved` or `kFailed`, with its separate actual error. Remove only a stage whose
owned root identity is still proven; do not delete a substituted tree. Cleanup
failure may leave stage residue and cannot convert the primary failure to success.
Cleanup does not skip its bounded identity-safe attempt merely because stop is
already requested. Its optional error is the real native error, or an explicitly
classified generic error for local identity/cap refusal, not a fabricated OS
errno. The committed owner does not retain a destination path.
Unexpected exceptions have no fabricated normal result or guaranteed typed
cleanup return; their scope guard still performs bounded best-effort cleanup.

## Complete typed outline

This is a contract outline, not an available header. Required standard includes
are those for the shown fixed records, filesystem path, optional, spans, stop
and error types, plus the existing `surface_write.h` types. All names are in
`meshvale::interchange`.

```cpp
enum class SurfaceFilePublicationOutcome { kFailed, kCanceled, kPublished };
enum class SurfaceFileCleanupOutcome { kNotRequired, kRemoved, kFailed };
enum class SurfaceFileStatus {
  kSuccess, kInvalidConfiguration, kInvalidStorage, kLimitExceeded,
  kBudgetExceeded, kCanceled, kUnsupportedProfile, kIoFailure,
  kVerificationFailed, kDestinationExists, kPublicationFailed, kCallbackFailure
};
enum class SurfaceFileStage {
  kConfiguration, kPathPlanning, kStaging, kReread, kInspection,
  kReceipt, kInventory, kPublication, kComplete
};
enum class SurfaceFileCode {
  kNone, kRequiredFieldZero, kConfigurationExtent, kUnknownEnum,
  kEncodingLimitsMismatch, kAllowanceTooSmall, kProviderConfiguration,
  kDestinationPath, kReceiptPath, kPathCollision, kSourceContract,
  kCountLimit, kRequestBudget, kStopRequested, kUnsupportedProfile,
  kDestinationParent, kStageCreate, kStageAttemptsExhausted,
  kFileOpen, kFileRead, kFileWrite, kFileClose, kEnumeration,
  kMemberInventory, kFileChanged, kFileKind, kInspection,
  kCallerReportEncoding, kReceiptSize, kReceiptBytes, kProvider,
  kDestinationExists, kCommitFailed, kCommitUnsupported
};
struct SurfaceFileDiagnostic {
  SurfaceFileStatus status = SurfaceFileStatus::kInvalidConfiguration;
  SurfaceFileStage stage = SurfaceFileStage::kConfiguration;
  SurfaceFileCode code = SurfaceFileCode::kNone;
  std::optional<std::uint64_t> member_ordinal, byte_offset;
  std::optional<std::error_code> system_error;
  std::optional<SurfaceStatus> inspection_status;
  std::optional<SurfaceDiagnostic> inspection_diagnostic;
};
struct SurfaceFileCleanupObservation {
  SurfaceFileCleanupOutcome outcome = SurfaceFileCleanupOutcome::kNotRequired;
  std::optional<std::error_code> error;
};
enum class SurfaceFilePhaseOutcome { kCompleted, kFailed, kCanceled };
struct SurfaceFileExecutionStats {
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t caller_jobs = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t completed_jobs = 0;
};
struct SurfaceFileIoObservation {
  SurfaceFilePhaseOutcome outcome = SurfaceFilePhaseOutcome::kFailed;
  std::uint64_t bytes_read = 0, bytes_written = 0, bytes_copied = 0;
  std::uint64_t bytes_compared = 0, bytes_hashed = 0;
  std::uint64_t completed_files = 0, closed_files = 0;
  SurfaceFileExecutionStats execution;
};
struct SurfaceFileInventoryObservation {
  SurfaceFilePhaseOutcome outcome = SurfaceFilePhaseOutcome::kFailed;
  std::uint64_t observed_files = 0, observed_directories = 0;
};
struct SurfaceFileInspectionObservation {
  SurfaceStatus status = SurfaceStatus::kInvalidConfiguration;
  SurfaceDiagnostic diagnostic;
  std::optional<SurfaceFacts> facts;
};
struct SurfaceFileProviderObservation {
  SurfaceFilePhaseOutcome outcome = SurfaceFilePhaseOutcome::kFailed;
  std::optional<std::uint64_t> returned_bytes;
};
enum class SurfaceFileCommitOutcome {
  kCommitted, kDestinationExists, kFailed, kUnsupported
};
struct SurfaceFileCommitObservation {
  SurfaceFileCommitOutcome outcome = SurfaceFileCommitOutcome::kFailed;
  std::optional<std::error_code> error;
};
struct SurfaceFileFacts {
  std::optional<SurfaceFileIoObservation> payload_write;
  std::optional<SurfaceFileInventoryObservation> payload_inventory;
  std::optional<SurfaceFileIoObservation> payload_reread;
  std::optional<SurfaceFileInspectionObservation> inspection;
  std::optional<SurfaceFileProviderObservation> provider;
  std::optional<SurfaceFileIoObservation> report_import;
  std::optional<SurfaceFileIoObservation> receipt_write;
  std::optional<SurfaceFileIoObservation> receipt_reread;
  std::optional<SurfaceFileInventoryObservation> final_inventory;
  std::optional<SurfaceFileIoObservation> final_payload_verification;
  std::optional<SurfaceFileIoObservation> final_receipt_verification;
  std::optional<SurfaceFileCommitObservation> commit;
};
struct SurfaceFileReceiptView {
  std::string_view path;
  std::span<const std::byte> bytes;
  SurfaceDigest sha256;
};
class SurfacePublishedBundle {
 public:
  SurfacePublishedBundle(const SurfacePublishedBundle&) noexcept;
  SurfacePublishedBundle& operator=(const SurfacePublishedBundle&) noexcept;
  ~SurfacePublishedBundle() noexcept;
  const SurfaceOutputInspection& Inspection() const noexcept;
  SurfaceFileReceiptView Receipt() const noexcept;
  std::uint64_t NewRequestedBytes() const noexcept;
 private:
  // Private charged state and constructor; no public default/move-empty owner.
};
struct SurfaceFilePublicationResult {
  SurfaceFilePublicationOutcome outcome = SurfaceFilePublicationOutcome::kFailed;
  SurfaceFileDiagnostic diagnostic;
  SurfaceFileCleanupObservation cleanup;
  std::optional<SurfacePublishedBundle> bundle;
  SurfaceFileFacts facts;
};
[[nodiscard]] SurfaceFilePublicationResult PublishSurfaceBundle(
    const SurfaceOutputInspection& inspected,
    const std::filesystem::path& destination,
    const SurfaceFilePublicationOptions& options,
    const SurfaceFilePublicationLimits& limits,
    const SurfaceFilePublicationWorkPolicy& work,
    std::stop_token stop = {});
```

Facts are a fixed, default-empty record; optionals become present only on actual
entry after their storage is admitted. Config rejection can return all absent.
An entered phase starts with actual zero counters, not an inferred native call.
Provider is absent when disabled; a throwing provider has no `returned_bytes`.
Inspection is present only for an actual fresh call; its returned status remains
present even if the inspector returns no facts. No old E/V facts are copied.
Commit becomes present only after the actual native call returns. A final
inventory `kCompleted` means the complete expected set was actually matched.

I/O counters count actual successful transfers, copies, byte-pair comparisons
(one compared pair counts one), and hash input bytes respectively, separately;
never sum them as one byte stream. Short transfers count only bytes really
transferred. A completed file passed all of that phase's operations and identity
checks; `closed_files` counts successful explicit closes only. Report import has
no native files/closes. Native-runtime buffering and V's separate work are not
P I/O counters. Job counters are actual planned range records, successfully
launched threads, caller-executed jobs, joined threads and completely finished
jobs. Canceled/partial jobs are not completed jobs; no global prefix follows.
Inventory counts exclude the stage root and dot/dot-dot enumeration sentinels.
All counters are checked before incrementing and never wrap.

Receipt observations in JSON are derived only from completed payload_write,
payload_reread and the successful inspection record. Their byte/member totals
must equal the actual payload inventory. Receipt/file operation facts not listed
there remain typed-return observations only. All fixed failure/facts slots are
prepared before their associated call; no emergency allocation on a normal
failure path. Facts may retain fresh `SurfaceFacts` and its existing charges,
but the fixed P counters do not own another copy of P buffers.

Each returned receipt span/path or inspection reference borrows a live unchanged
committed owner. Strong copies allocate nothing and share immutable storage.
Distinct live handles permit concurrent reads; assignment/destruction of the
same visible handle cannot race its getters or borrowed views. No JSON parsing,
caller boolean or fabricated maps can construct this owner. No destination-path
getter or implicit filesystem reread is added.

## Required qualification before availability

Test genuine empty and rich admitted inputs across all four encodings and both
representations, preserving existing typed refusals. Exercise exact configured
cap and cap-minus-one admission, shared-ledger pressure, block/worker stop after
actual progress, provider disabled/empty/NUL/invalid UTF-8/throw/oversize/lifetime,
write/read/enumeration/close errors, allocation failure and partial launch/join.
Use a minimal private operation/hook table for fault points and actual progress;
no public stage-control or generic filesystem framework. Test mutation after the
provider, changed/missing/extra/symlink members, actual exclusive-stage collisions,
name-attempt exhaustion, racing destination and identity-safe failed cleanup.
Callbacks that deliberately mutate staging must precede final raw verification.
No arbitrary hook may run after it and falsify the ordering being tested.

Prove no allocation/free callback or fallible owner preparation after successful
commit, no worker/handle escape and actual ledger release after the last strong
alias. Required source/header/package/installed/archive/relocation/sanitizer and
declared platform routes must execute the new target; existing memory-only or
base compatibility greens do not establish this module. This draft grants no
filesystem implementation availability, stable release, universal platform/ABI,
scene/corpus fidelity, Repair/Python/single/batch integration or MVP completion.
