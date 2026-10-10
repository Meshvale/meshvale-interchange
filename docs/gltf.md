# Proposed glTF and GLB asset contract

| Field | Value |
|---|---|
| ID | GLTF-ASSET-001 |
| Version | 0.1.0 draft |
| Status | Proposed; adapter API and runtime dependencies are not implemented |

This contract defines the intended native document, resource and primitive
boundaries for a glTF 2.0 adapter. The available adapters remain
[OBJ/MTL text](obj.md) and [OBJ files and bundles](obj-files.md). No glTF/GLB
header, Python interface or supported-format claim follows from this draft.
Complete core semantic admission is also unimplemented: successful parsing,
layout checks or a valid fixture do not establish every graph, accessor-role,
skin, morph or animation constraint.

## GLTF-DOCUMENT-001: Owned asset snapshots

The proposed asset is an immutable, format-owned snapshot of the complete
document and its admitted resources. It retains scenes, nodes, instances,
materials, skins, morph targets, animation references, optional opaque extension
objects and extras. It does not flatten a scene into the OBJ document or a
single Geometry mesh. Retaining those records is data preservation, not skinning,
animation, image decoding, shader evaluation or extension implementation.

The owner stores decoded UTF8 strings, exact opaque JSON numbers, original
resource spellings and bindings, and owned resource bytes. Parser pointers,
temporary buffers and writable JSON-library objects do not cross the interface.
Snapshots and decoded attributes survive destruction of parser/source objects
and do not require rereading source files for export. Public operations consume
const snapshots and construct separate candidates.

Strict JSON admission rejects decoded duplicate keys, invalid UTF8 and malformed
syntax. Decoded string values and object keys must also be valid Unicode scalar
sequences, including escaped surrogate pairs; checking raw UTF8 alone is
insufficient. Exact opaque numbers must remain tagged/lossless through serialization;
converting them to double is not preservation. Typed format fields separately
require their specified integer range or finite floating values. Introduced
nonfinite values and invalid Unicode keys/strings fail before serialization.
Counts, offsets, references and other integer metadata require exact integrality,
signedness and range checks in the retained document before passing metadata to
the parser or materializer. Parser coercion or truncation cannot admit a
fractional count as an integer.

## GLTF-SELECTION-001: Primitive and instance identity

A selection identifies its owning snapshot, mesh index and primitive index.
Handles from another snapshot or a superseded candidate fail explicitly.
Instance records separately retain scene membership, node identity and authored
transforms. Primitive extraction does not apply transforms, merge instances or
discard unused authored records implicitly.

Editing a shared mesh primitive affects every retained instance referencing it.
A processing operation must state that scope explicitly; editing one instance
requires a separate cloning/remapping policy. No implicit instance duplication,
scene flattening or graph-reference renumbering is proposed. Candidate results
retain unchanged graph identity and report any introduced records or redirected
references.

The initial extraction target is indexed/unindexed triangle primitives. Strip
or fan conversion requires an explicit request and correspondence/winding report;
line and point primitives remain retained document data until their extraction
contract exists. Polygon triangulation requires a geometric conversion policy
and one-to-many correspondence; successful index manipulation cannot establish
polygon preservation or repair quality.

## GLTF-ACCESSOR-001: Exact typed extraction

Accessor results own their values and provenance: source accessor/semantic/set,
component type, shape, normalization, count, logical layout and sparse origin.
Raw integer scalars remain exact integers, including UINT32 mesh indices beyond
float32 precision. A normalized interpretation is a separate result/view; it
does not replace raw encoding or silently renormalize authored weights.

Every UV, color, joint and weight set is enumerated and retained. Core attribute
roles constrain component types and shapes: UV/color/weight integer encodings
are normalized UBYTE/USHORT, joints are raw UBYTE/USHORT, and UINT32 is admitted
for mesh indices rather than custom vertex attributes. Morph/animation roles
have their own core rules; base-attribute admission is not their substitute.
Set continuity, attribute counts, joint/weight pairing and referenced ranges
require explicit validation before a selected processing operation.

All offset, extent, count, stride and allocation arithmetic is checked before
access. Dense base stride and packed sparse element step are distinct. Sparse
selectors must have admitted unsigned types, strict increasing order and valid
destinations. Matrix columns use required alignment; the final element extent
distinguishes optional trailing padding from packed step. Nonfinite binary
components, truncated resources and invalid alignment/ranges fail. Mesh indices
also reject primitive-restart sentinels and indices outside the selected vertex
range. Bounds metadata is verified against materialized values when required.

Dependency helpers are not a validation boundary. The evaluated cgltf float
unpack path must not be used for an interleaved base with packed sparse values:
the bounded extraction path must validate and decode that layout independently.

## GLTF-EXTENSION-001: Retention and edit restrictions

Unsupported required extensions fail admission. Optional unknown extension
objects and extras can be retained unmodified with an explicit unsupported
semantics diagnostic. Successful core validation does not validate opaque
extension meanings, embedded references or custom resources.

Opaque retention does not authorize arbitrary edits. A proposed edit must name
the core records it changes, its reference correspondence and the extensions
whose meanings could be affected. If extension dependencies cannot be established,
the edit fails; payload deletion or marking the extension optional is not a
fallback. Container conversion with optional unknown data also needs an explicit
capability decision. Unmodified retention and an admitted edit are separate
capabilities.

## GLTF-CONTAINER-001: Container bindings and provenance

The GLB reader validates magic, container and asset versions, total length,
checked chunk bounds, four-byte alignment, first/unique JSON and second/unique
BIN when present. JSON output padding is spaces; BIN output padding is zeros.
Accepting a JSON chunk's trailing whitespace does not by itself distinguish
authored JSON whitespace from container padding or validate a strict padding
policy. Reader admission and writer padding rules must be specified and tested
separately rather than inferring one from a parser's acceptance.
Logical buffer bytes are separate from container padding. Unknown trailing
chunks are ignored for core interpretation and owned byte-for-byte for retained
GLB output. Converting them to a destination without an explicit representation
fails rather than dropping them.

glTF/GLB conversion reports each changed core container binding, URI or resource
representation. Keeping buffer identities and bytes does not make the source
JSON text or container bytes identical. A GLB has no implicit memory of its
former external URI; restoring that spelling requires owned source provenance.
An inverse conversion must verify that provenance against the retained document.
Unchanged graph/string/discrete fields and opaque numeric values are compared
exactly; any permitted typed-value tolerance needs a separately documented bound.
Conversion loss or unsupported content returns an explicit failure/loss report.

## GLTF-RESOURCE-001: Resource admission and source protection

The proposed filesystem resolver admits bounded relative resources beneath an
explicit root and supported embedded forms. It has no implicit network fetch.
URI decoding, lexical traversal checks, resolved-path containment, regular-file
requirements and portable-name/alias checks precede reads. Unsupported URI/media
forms fail explicitly. Unicode URI spelling, decoded relative path and resource
identity remain distinct; spelling changes are reported.

After URI decoding, every path component is checked before reads. The proposed
portable-name policy rejects empty, `.` and `..` components, U+0000 through
U+001F, `<`, `>`, `:`, `"`, U+005C (backslash), `|`, `?`, `*`, and trailing spaces or periods;
`/` separates components and cannot be part of a component. It rejects
case-insensitive device aliases `CON`, `PRN`, `AUX`, `NUL`, `COM1` through `COM9`,
`LPT1` through `LPT9`, and their superscript-digit variants with `¹`, `²` or `³`,
including names followed by an extension. These cross-platform admission
requirements follow the [Windows file-name restrictions](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).
The implementation must also reject aliases or collisions under the destination
filesystem's rules and document its case/Unicode comparison policy; a lexical
name check alone does not establish distinct resource identities.

Snapshots retain exact external, data-URI and bufferView image/resource bytes.
Logical buffer length, external file bytes and container padding have distinct
provenance. The implementation must state whether extra external file bytes are
retained or rejected. Shared resources are not silently duplicated or omitted.
Image byte preservation does not establish decoded-image equivalence.

Reads and candidate edits never modify original files. Per-resource, aggregate
memory, document-depth/count and output limits must be selected and documented
before implementation is admitted. Experiment limits are not product defaults.
Aggregate memory is reserved and checked incrementally before resource reads,
base64 decoding and owned copies, including concurrently in-flight allocations.
A total calculated only after all resources have been loaded is not allocation
enforcement. Reservations and temporary copies count against the same operation
budget and are released on failure or cancellation.

## GLTF-PUBLISH-001: New verified bundles

Publication consumes owned snapshots and a new destination with an existing
parent. It stages resource/container output in an exclusive sibling directory,
reloads using the staged root, verifies graph/reference correspondence, exact
resource bytes and the complete file inventory, then performs a no-replace
commit. Existing or competing destinations are preserved; source files are not
reopened as publication inputs. Unsupported container/extension conversions fail
before commit. No overwrite fallback is proposed.

Results separate outcome, last phase, relative entry, changed bindings,
preserved/converted/rejected capabilities and cleanup diagnostics. Failure or
cancellation publishes no candidate; staging cleanup is attempted and cleanup
failure is reported. A successful commit remains published if cancellation is
requested afterward. Allocation failures retain the C++ exception contract.
Crash durability and hostile concurrent filesystem mutation require separate
guarantees; staging alone does not provide them.

## GLTF-EXECUTION-001: Ownership and execution

Independent accessor/resource work can partition over immutable owned snapshots
under one shared worker/memory budget. Each partition owns its output; document
and filesystem commits remain coordinated. Result ordering and diagnostics are
deterministic by source identity. Cancellation is cooperative at resource,
materialization, verification and precommit boundaries. Nested worker launches
must respect the same budget.

The first implementation must state actual serial/worker behavior, callback
thread ownership, cancellation boundaries and copy costs. No GPU, multicore
throughput, thread-safety or asynchronous API capability is established here.

## Dependency candidates and focused fixtures

[THIRD_PARTY.md](../THIRD_PARTY.md#gltf-adapter-candidates) records evaluated
reader/writer inputs and license obligations. They are not installed runtime
requirements. Dependency selection must include admitted roles, bounded decoder,
configured serializer, Unicode/duplicate/finite guards, actual notices and
installed-consumer verification.

The evaluated jsoncons configuration uses `lossless_number(true)`,
`lossless_bignum(true)`, raw bignum serialization, `allow_comments(false)` and
`allow_trailing_comma(false)`, surrounded by the duplicate/Unicode/finite guards
above. Exact parsed numeric tags remain owned and unchanged.

The original [fixture generator](../tests/gltf-fixtures/generate.py) needs only
Python 3.10+. It produces a small rich glTF/GLB scene, external/embedded/image
resources and padded matrix accessors in a new output directory:

```sh
python tests/gltf-fixtures/generate.py .local/gltf-fixtures
```

Use a fresh output directory. Generated files are test inputs, not Meshvale
adapter output or a supported capability test. Their references and bytes can
be inspected with Python and the [Khronos glTF Validator](https://github.com/KhronosGroup/glTF-Validator).
Implemented native extraction, verified publication and installed consumers
must later exercise these and malformed/cancellation cases through the actual
public interface.

Format rules come from the [pinned glTF 2.0 specification](https://github.com/KhronosGroup/glTF/blob/8e798b02d254cea97659a333cfcb20875b62bdd4/specification/2.0/Specification.adoc).
