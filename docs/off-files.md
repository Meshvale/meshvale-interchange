# Retained OFF files and verified publication

| Field | Value |
|---|---|
| ID | OFF-FILES-001 |
| Version | 0.1.0 |
| Status | Development implementation; qualification pending |
| Interface | `meshvale/interchange/off_files.h` |
| Target | Optional `meshvale::off_files` in `MeshvaleInterchange` |

This interface captures one immutable OFF file and publishes one verified OFF
entry in a new directory, optionally with one computed receipt. The
[OFF codec](off.md) owns syntax, numerical admission, indexed polygon fidelity
and canonical rewriting. File publication does not select a repair operation or
prove source-to-candidate edit equivalence.

The selected build option is `MESHVALE_INTERCHANGE_OFF_FILES`, initially OFF.
Enabling it requires `meshvale::off` and the same installed Geometry execution
producer. It does not require the optional glTF modules, Repair, Python or an
additional file-format SDK. Only an enabled implementation installs its
header and exported target. Configuration alone establishes no accepted platform
or supported release.

## OFILE-CAPTURE-001: One strong source

`ReadOffFileAsset(input, limits, work, context, stop)` returns
`OffFileReadResult`: an optional immutable `OffFileAsset`, typed diagnostic and
actual execution observations. Pass the complete file to `ReadOff`; no suffix
dispatch, format guessing, resource root or external references are involved.

The copy-only asset exposes `Document()`, `EntryPath()`, `EntryBytes()` and
`NewRequestedBytes()`. Its document is the actual `OffDocument`; entry bytes
alias `Document().OriginalBytes()`, without a second permanent source copy.
The entry path is its portable UTF-8 basename. No absolute source locator or open
file handle remains in the asset. A retained asset remains usable after external
source-file editing or removal.

Borrow input/configuration through synchronous return and every worker join.
Validate configuration and path extents before traversal/allocation. Preserve
logical paths: do not canonicalize an unsupported alias into an accepted path.
Capture regular files only, rejecting symlink/reparse traversal. Windows admits
ordinary local drive paths; network/device namespaces are unsupported. POSIX
uses native regular-file handles; Linux canonical-handle lookup requires
`/proc/self/fd`. Canonical-handle comparison supplies an additional check.

Portable basenames reject dot components, separators, controls, prohibited or
reserved names, trailing dot/space and ASCII-case collisions. Unicode names are
admitted without normalization or Unicode case-equivalence guarantees.

Open first, admit observed size before payload allocation, then read bounded
blocks. Compare identity, size and available modification metadata before and
after, including short/extra-byte checks. Capture writes nothing. Failure or
cancellation yields no asset. This is no atomic-snapshot guarantee against a
concurrent editor that restores all observed metadata. Temporary IO and delegated
document bytes can overlap and remain charged until their actual frees.
Retain actual delegated OFF/Geometry statuses and diagnostics, including
unsupported numerical profiles; do not relabel them as path or parse errors.

## OFILE-PUBLISH-001: Concrete borrowed request

`PublishOffBundle(request, destination, options, limits, work, stop)` borrows a
strong source `OffFileAsset`, strong expected `MaterializedMesh` and strong
`OffOutput`. Source and expected mesh must share the actual Geometry execution
ledger, rather than merely equal execution options. `OffOutput` has no ledger
query and need not retain its input mesh; it supplies bytes/facts, not proof of
that relationship. Keep every borrowed owner alive through return and joins.

Inspect the expected mesh and refuse any attribute, including an empty channel.
Check output policies/facts and original edge-count provenance. Independently
compare actual reload against the expected counts, every binary64 position bit,
complete offsets and directed corner indices. A caller boolean, hash or copied
writer plan cannot replace these comparisons. Application code separately owns
source-to-candidate correspondence verification.

Preserve the source entry basename. The only optional additional member is one
receipt whose portable flat basename is declared in advance and differs from
the entry under ASCII-case comparison. No nested members or arbitrary static
supplements are selected.

After actual staged-entry verification, call a borrowed function-pointer/state
provider once with `OffFileVerifiedContent`, borrowing the actual staged
`ReadOff` document and exact entry bytes/name. Its return is a borrowed byte
span. Provider state, returned backing and borrowed descriptors must remain
alive, unchanged and readable through the entire publisher return, including
import joins and cleanup. State may mutate synchronously to prepare backing,
then must preserve that backing. A local temporary span is invalid. A provider
may retain a strong staged document, which proves verification only.

Admit/import provider bytes into charged owned storage before writing. Publisher
limits cover its own admission/copy/verification, not arbitrary external callback
computation or caller-created backing. The application provider must obey its
own work, allocation and cancellation contract. Provider `std::bad_alloc`
propagates after joined cleanup; other external-provider exceptions are typed
provider failure. Import/count/path errors retain their own diagnostics.
Internal allocation/native exceptions retain their propagation rules.

## OFILE-TRANSACTION-001: Verify before no-replace commit

Destination is a new directory with an existing admitted parent. An existing
file, directory, dangling link or racing destination must fail without
replacement. Preflight flat names, extents, ledger and destination profile
before creating an exclusive temporary sibling.

Stage exact output bytes and close/check writes. Capture the actual staged entry
through `ReadOff` under explicit reload limits. Verify every entry byte against
the output and every canonical row/loop against the strong expected mesh.
This staged reload is publication authority. Independently verify the complete
flat inventory, file kinds, lengths and every byte.

If requested, call the provider, stage its imported receipt, then recheck exact
entry/receipt bytes and complete inventory. Prepare immutable result/owner
metadata, join workers and close handles before one native no-replace commit.
No copy/overwrite fallback is allowed. Successful commit is the final fallible
action: no allocation, callback, report patch or mandatory reload follows it.
Tests independently reopen successful output.

Only successful commit exposes a bundle. The bundle retains source, expected
mesh, actual staged document and exact entry/receipt bytes. Entry bytes alias
that document's original storage; receipt bytes are owned imports. The borrowed
`OffOutput` is not retained. All fresh file-module requests use the common
source/expected-mesh ledger; independently supplied output remains a caller-owned
input scope.

`OffFilePublicationResult` reports failed/canceled/published outcome, actual
phase, optional bundle, primary diagnostic, cleanup not-required/removed/failed
and optional cleanup error, plus actual execution. Cleanup cannot overwrite the
primary failure, allocate, observe stop or remove an owned stage whose identity
changed. Stop before commit prevents it; stop after true commit cannot retract
publication. Unsupported native no-replace capability is a typed refusal.
Unexpected exceptions propagate after joins and best-effort owned-stage cleanup,
without an invented typed result. Crash durability/cleanup, hostile concurrent
staging protection and network-filesystem ambiguity are outside this contract.

## OFILE-RESOURCES-001: Explicit request scopes

All fields are required, positive and inclusive. Validate every nested field,
host extent, checked product and cap sum before work, even for branches not taken.

| Aggregate | Fields |
|---|---|
| `OffFileLimits` | `max_entry_bytes`, `max_receipt_bytes`, `max_total_file_bytes`, `max_entry_name_bytes`, `max_receipt_name_bytes`, `max_native_path_code_units` including terminator, `max_file_new_requested_bytes` |
| `OffFileReadLimits` | `files: OffFileLimits`, `off: OffReadLimits` |
| `OffFilePublicationLimits` | `files: OffFileLimits`, `expected_inspection: MeshInspectionLimits`, `reload: OffReadLimits` |
| `OffFileWorkPolicy` | `io_block_bytes`, `parallel_min_bytes`, `partition_bytes`, `cancellation_block_bytes`, `max_jobs` |
| `OffFileReadWorkPolicy` | `files: OffFileWorkPolicy`, `off: OffWorkPolicy` |
| `OffFilePublicationWorkPolicy` | `files: OffFileWorkPolicy`, `expected_inspection: MeshMaterializationWorkPolicy`, `reload: OffWorkPolicy` |

Total file bytes count planned entry plus receipt once; capture counts only its
entry. File fresh requests cover control/state, retained names/receipt, IO,
UTF conversion, path/handle/inventory plans, workers and simultaneous old/new
growth storage. Delegated codec/materialization/inspection requests retain their
own attributed counters and original owning sum. Report file-live counters
separately; do not infer an end-to-end sum, peak or RSS, or double-count shared
owners. Injected backend state outlives the last free.

Fixed inline diagnostics identify status/stage/code, optional entry=0/receipt=1,
byte offset, system error category/value and actual delegate diagnostics. No
allocating machine-path message is required. Distinguish configuration/request,
unsupported path/profile, changed file, IO, extent/request/shared budget,
preservation mismatch, provider, destination-exists, publication and cancellation.
An earlier detected prefix mismatch remains primary over a later stop; otherwise
actual stop returns canceled.

Substantial byte read/copy/compare work uses the shared leased workers when
admitted, with caller fallback. Bound checkpoints during payload loops and real
relocation; serial metadata actions do not justify an unbounded payload loop.
Join all workers on every exit, including partial launch. OS/runtime internals
retain their documented exclusions; Meshvale-created storage does not.
Strong copies allocate nothing and retain ledger/backend pins. Views require a
live unreassigned owner; distinct immutable copies permit concurrent reads.

## OFILE-ACCEPTANCE-001: Required development evidence

Qualification must exercise real file capture, source removal, changed and
unchanged indexed polygons, unused signed-zero vertices, complete physical
readback, optional receipt, Unicode names, existing/racing destinations and
unsupported native/numerical profiles. Independent fault fixtures cover actual
request/ledger exhaustion, cancellation during transfer/relocation, allocation
exceptions, partial launch/join, malformed staged bytes, inventory/provider/import
failure, close/commit failure, cleanup precedence and late stop after commit.
Installed consumers, source archives, sanitizer and actual hosted platform runs
remain required. This contract alone claims no qualification success.
