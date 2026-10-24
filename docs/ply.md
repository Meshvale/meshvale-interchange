# Typed PLY polygon interchange

| Field | Value |
|---|---|
| ID | PLY-IO-001 |
| Version | 0.1.0 |
| Status | Selected development profile; implementation and qualification pending |
| Owner | Supplied-byte PLY admission, typed schema binding and serialization |

This profile defines bounded PLY 1.0 supplied-byte reading and writing through
`meshvale/interchange/ply.h` and `meshvale::ply` in the installed
`MeshvaleInterchange` package. It requires the matching Geometry materialization
and execution producer, C++20 and the existing package dependencies. It introduces
no SDK. Header declarations and functions remain implementation work until this
development interface is available.

The [original Stanford PLY description](https://gamma-web.iacs.umd.edu/POWERPLANT/papers/ply.pdf)
defines ordered element/property records, scalar and list properties, ASCII and
two binary byte orders, with extensible element families. The Meshvale rules
below deliberately select a narrower profile. They do not admit every PLY file.
Filesystem capture/publication, generic schema authoring, richer element families,
Python, application commands and supported releases have separate gates.

## PLY-WIRE-001: Header and names

The first source bytes must be `ply` followed immediately by LF or CRLF. Reject
BOMs, prefixes, leading whitespace and a first-line suffix. The next physical
line is the single format declaration:

```text
format ascii 1.0
format binary_little_endian 1.0
format binary_big_endian 1.0
```

Exactly one alternative applies. Version and encoding words are case-sensitive;
no aliases or automatic extension/host-endian selection apply. Unsupported format
or version words produce an unsupported-profile failure. Both LF and CRLF are
accepted for header line endings, including mixed use; a bare CR is not a line
ending in this profile. Every header line, including `end_header`, requires a
line ending. Header input is printable ASCII plus space/tab separators and these
line endings. NUL, other controls and non-ASCII bytes are refused.
Missing/repeated format lines, wrong field counts and an unterminated header
are malformed; an unselected encoding/version/directive/type is unsupported.

After the first line, noncomment header lines may have leading/trailing spaces
or tabs. One or more spaces/tabs separate their fields. Every line has exactly
the fields prescribed below; an inline comment or extra token is not discarded.
Empty or whitespace-only header lines are not admitted. The format line cannot
be preceded by a comment. After it, complete comment lines may appear between
any declarations and before `end_header`.

The only remaining header directives are:

```text
element vertex COUNT
element face COUNT
property TYPE NAME
property list COUNT_TYPE ITEM_TYPE NAME
comment TEXT
end_header
```

A property belongs to the most recently declared element; a property before an
element is malformed. `end_header` has no other field. A comment can instead be
the keyword alone. Otherwise exactly its first following space/tab is the
delimiter, and every remaining byte before the line ending is comment content,
including further spaces/tabs. Content is printable ASCII or tab. Leading
indentation, the keyword, that one delimiter and the line ending do not count
toward the per-comment content cap. Comments are never recognized inside the
body. `obj_info` and every other unselected directive are explicitly unsupported.

Element/property names follow `[A-Za-z_][A-Za-z0-9_]*`, are case-sensitive and
retain their exact bytes. Property names must be unique within each element;
the same name in the two different elements is allowed. A duplicate element or
property declaration is malformed, rather than last-one-wins. Names matching a
header keyword remain legal in a property-name field. Invalid identifier bytes
are malformed. Required name and aggregate descriptor-name byte limits apply
even to zero-count elements.

Exactly one `vertex` element is required; at most one `face` element is allowed.
Their order is retained and either order is admitted. No other element is
admitted, including zero-count `edge`, `material` or user-defined elements.
Unknown elements are unsupported; their records are never silently discarded
or carried through a changed mesh. Header element counts are unsigned decimal
digits only, without a sign, prefix or exponent, checked against uint64, host
extents and configured counts. Leading zeros are allowed. A data count of zero
does not remove its property's schema declarations.

The input body starts at the byte immediately following the one LF or CRLF that
terminates `end_header`. A binary reader must not skip any subsequent byte,
including a value byte equal to space, tab, CR or LF.

## PLY-TYPES-001: Scalar and list representation

Only the following exact type words are admitted. Their binary representations
and canonical mappings are selected explicitly by this profile:

| Wire type | Binary representation | Canonical custom-property scalar |
|---|---|---|
| `char` | signed 8-bit two's complement, one byte | int32, exact sign extension |
| `uchar` | unsigned 8-bit, one byte | uint8 |
| `short` | signed 16-bit two's complement, two bytes | int32, exact sign extension |
| `ushort` | unsigned 16-bit, two bytes | uint16 |
| `int` | signed 32-bit two's complement, four bytes | int32 |
| `uint` | unsigned 32-bit, four bytes | uint32 |
| `float` | IEEE binary32, four bytes | float, exact bits |
| `double` | IEEE binary64, eight bytes | double, exact bits |

Host `char` signedness, `long` width, structure padding and alignment do not
define the wire. Binary fields are contiguous in declaration order, without
padding, and every multibyte field follows the declared byte order. Signed
fields decode through their exact width; no out-of-range signed cast supplies
the intended interpretation. Unselected spellings such as `int8`, `float32`,
64-bit integer extensions, strings and nested lists are unsupported.

List count types are any of the six admitted integer types. Floating count
declarations are unsupported. List item types may be any admitted scalar type,
subject to the structural index rule below. A count is the number of following
items, not bytes. A negative binary signed count is malformed. Every decoded
count must fit the host, per-row/aggregate bounds and available source extent
before allocation or access. Zero-length custom lists are valid authored rows.
There is no implicit missingness sentinel.

### ASCII body

Consume one row per declared element instance, in element order. Within a row,
consume properties in declaration order: one scalar or one count followed by
that many items. Space, tab, LF and CRLF separate body fields; records and lists
may wrap across physical lines. Blank body lines are allowed. Bare CR, vertical
tab, form feed, NUL and non-ASCII bytes are refused. No comments, quotes, braces
or string escapes apply in the body. The final scalar can end at EOF. After all
declared rows, only the admitted whitespace may remain; extra data or another
object is refused.

Header counts, ASCII list counts and structural vertex indices contain decimal
digits only, including when their declared wire type is signed. Negative and
plus-prefixed count/index tokens are malformed. Other signed integer property
values allow one leading `+` or `-`; unsigned property values are digits only.
Every integer must fit its declared wire range exactly. No exponent, decimal
point, hexadecimal prefix, wraparound or saturating conversion applies.

Floating tokens allow an optional leading sign, decimal digits with an optional
decimal point, and an optional `e`/`E` exponent with optional sign and required
digits. At least one significand digit is required: `1`, `1.`, `.1`, `-0` and
`+1e-3` are admitted forms. Hexadecimal forms, separators, NaN and infinity names
are not admitted. Parse directly to the declared float or double type, using
nearest conversion with ties to even. Reject conversion range failures,
nonfinite values and underflow of a nonzero significand to zero. A finite
subnormal is not a range error merely because it is subnormal. Retention applies
to the admitted declared-width value, not to an exact-real interpretation of
its source decimal spelling.

### Binary body

Consume exactly the declared scalar/count/item fields. There is no body
whitespace, separator, terminator or padding. Truncation is malformed; extra
bytes after the final field are refused. Floating fields must have finite bit
representations. Preserve both signs of zero and every finite subnormal. Do not
replace nonfinite or subnormal values by zero. Width and source-extent arithmetic
is checked before touching the field or allocating its destination.

## PLY-GEOMETRY-001: Positions, polygons and raw properties

The vertex element has exactly one scalar `x`, `y` and `z` property, at arbitrary
property positions. All three must be `float` or all three must be `double`.
Integer, mixed-width or list coordinate declarations are unsupported. Coordinates
bind to canonical positions in XYZ order, while the schema retains their
individual declaration positions and source width. Finite float positions widen
exactly through Geometry's canonical bit-preserving conversion, including signed
zero and subnormals.

If a face element is present, it has exactly one structural list named
`vertex_indices`. Its count and item types are admitted integer types. Each
index must be nonnegative, fit its declared type and be below the vertex count.
Each face has at least three corners. A scalar `vertex_indices`, missing list,
or alternate structural spelling such as `vertex_index` is unsupported. The same
name in the vertex domain is an ordinary custom property. A face element with
count zero still requires its structural list declaration. With no face element,
the mesh has no faces and canonical offsets `[0]`.

Create a charged immutable `geometry::MaterializedMesh` directly from complete
positions and `PolygonTopologyInput`, then retain a successful full storage
inspection. Keep all vertex rows, unused vertices, shared IDs, face order,
starting corners and winding. Mixed triangles/quads/n-gons, concave/nonplanar
polygons and finite repeated/coincident vertices remain when storage is valid.
There is no welding, compaction, triangulation, normal generation or geometric
repair. Inspection does not certify area, planarity, manifoldness, intersections
or a closed solid. Units remain unknown and no rescaling occurs.

Every nonstructural property becomes exactly one canonical attribute. Preserve
element order and each element's property order when forming the attribute
sequence, excluding only bound x/y/z and the structural face list. Its domain is
vertex or face; its name is the exact property name; `components` is `1`. Scalars
are dense with no offsets. Lists are ragged with row_count+1 scalar offsets,
including offsets for empty rows. Scalar alternatives follow PLY-TYPES-001.
Presence, set index and metadata are absent/empty. Semantic is empty: no normal,
color, label, UV, identity or reference meaning is inferred from a name. Separate
components are not bundled or normalized. No custom property is dropped.

Retain an immutable ordered schema with original element counts, exact names,
property kinds/widths/count types and coordinate/topology/attribute bindings.
The document also owns the complete original bytes, canonical mesh and successful
inspection. Schema provenance preserves narrow signed wire types that canonical
int32 alone cannot reconstruct. It is not another decoded value table. Do not
detour through a mutable legacy mesh or retain a permanent second decoded mesh.

Empty and point-only meshes are admitted. Actual nonempty numerical allocation
requires the compiled Geometry profile. All-empty numerical storage follows
Geometry's real empty-input admission; it is not automatically skipped or
reported as a nonempty profile refusal.

## PLY-WRITE-001: Explicit schema and complete binding

`WritePly` takes a strong supplied `MaterializedMesh`, an explicitly supplied
original `PlyDocument` as its schema source, explicit destination encoding and
canonical rewrite policy, positive limits/work policy and a fresh stop token.
Encoding and rewrite-policy defaults/foreign values are invalid configuration;
the writer does not infer a destination or derive a schema from scalar variants.
The initial interface does not construct arbitrary new schemas.

The schema source is borrowed through synchronous return and all worker joins.
Its original mesh values are not a substitute for the supplied mesh. Derive the
execution ledger from the supplied strong mesh and inspect that mesh under it.
Apply current writer descriptor/count caps to the retained schema before name
traversal/copying, even though the source document was admitted under other caps.
Schema provenance does not assert source/candidate equivalence or operation
acceptance.

Require a complete one-to-one binding of schema custom properties to supplied
attributes in their captured order. Match exact domain/name, `components` equal to `1`,
canonical scalar alternative and dense/ragged kind. Require absent presence/set
index, empty semantic and empty metadata, as captured by this profile. The
entire supplied attribute set must be bound: reject missing, extra, reordered,
renamed, differently typed or decorated channels. Even an empty added channel
is not discarded. Validate complete row shapes against current vertex/face
counts. Scalars remain scalar and lists remain lists even if every list happens
to have length1.

Encode all supplied positions and polygon loops. Recompute element counts from
the supplied mesh; preserve the schema's element/property order, names, kinds
and widths. An absent face element requires a supplied zero-face mesh. A present
zero-count source face element can encode new faces only when its existing
structural layout and all current property rows fit. All indices and list
lengths must fit their original count/item kinds. Narrow canonical int32 values
back to source char/short only after exact range validation. Reject incompatible
schema/value conversions rather than selecting a wider type or another encoding.

Output facts record source-schema encoding and original element counts,
destination encoding, canonical rewrite policy, actual supplied vertex/face/
corner counts, encoded property count, and identity correspondence at this serialization
boundary. Identity means emitted ordinal i corresponds to supplied ordinal i
in each domain; redundant identity arrays are not required. These facts never
replace the maps from an earlier edit. Returned bytes and facts retain no mesh,
source-document, borrowed options or fresh inspection owner after completion.

The required canonical rewrite policy explicitly omits header comments and
original whitespace/numeric spelling. Original bytes remain available from the
schema-source document. This is an acknowledged lexical rewrite, not bytewise
source preservation or a promise to retain comment meaning in a new file. No
`obj_info`, unknown element or unsupported property is admitted and then dropped.
Canonical output uses LF, unindented declarations, single spaces between header
fields, no comments/blank header lines, and an `end_header` line ending. It
retains declared element/property order. ASCII output has one complete element
row per LF-terminated line and single spaces between count/value fields; binary
output starts immediately after the header terminator and has no final padding.

Serialization alone performs no reload or filesystem operation. A workflow
claiming verified export must independently reload the actual encoded bytes and
check its own preservation conditions. No file replacement, atomic publication,
resource capture, receipt, batch or CLI guarantee is supplied by this codec.

## PLY-NUMERIC-001: Exact conversions and caller modes

All source and supplied floating positions/properties must be finite. Validate
raw binary representations without quieting or normalizing a rejected NaN.
Float custom properties remain float; double properties remain double. There
is no implicit conversion between canonical property alternatives.

Under a float coordinate schema, every supplied double coordinate must be an
exact finite binary32 value, including its zero sign. Otherwise return an
unsupported numeric conversion with no output. Under a double coordinate
schema, retain the actual finite double value. Binary widening/narrowing and
byte-order conversion use exact representations and are independent of caller
rounding/flush modes. No tolerance or quantization policy exists.

ASCII output uses general decimal formatting with `max_digits10` for the
declared type, retaining zero sign and sufficient digits for exact declared-width
reload. Different valid canonical spellings from conforming standard libraries
are allowed; bit fidelity, not an identical decimal string across toolchains,
is the requirement. ASCII declared-width parsing and serialization must produce
the same admitted bits under supported caller rounding and subnormal-flush
modes. Neither function changes rounding mode, exception masks, FTZ or DAZ
controls. Floating exception status flags are not a preservation guarantee and
must not be mistaken for those control modes.

Lexical scans and range checks are bounded. A standard-library conversion of
one admitted token is indivisible within `max_token_bytes`; check stop immediately
before and after it. Fixed-width binary conversion is at most one eight-byte field;
check bounded field/value work around repeated conversions. Integer formatting
also checks stop immediately before and after its bounded conversion. No
uninterruptible traversal of a long custom list is hidden behind a row boundary.

## PLY-OWNERSHIP-001: Limits and lifetime

Every configured limit and work field is required, positive, inclusive and
representable in the compiled host extents. Zero is invalid configuration rather
than unlimited/default behavior. Positive bounds still admit zero data counts.
Check sums/products before overflow, allocation, copying and indexing.

Read limits bound source bytes; header bytes/physical line count; physical
header/ASCII-body line bytes; numeric/header token bytes; per-comment content
bytes; each name and summed schema-name bytes; total property declarations;
vertices, faces, total/per-face corners; total list rows and per-row list items;
total custom-property scalar values; codec fresh requested bytes; nested Geometry
materialization/inspection counts/requests; and aggregate fresh-request allowance.
The limits apply to actual decoded extents, not a guessed triangle count.

The required read fields are `max_source_bytes`, `max_header_bytes`,
`max_header_lines`, `max_line_bytes`, `max_token_bytes`, `max_comment_bytes`,
`max_name_bytes`, `max_total_name_bytes`, `max_properties`, `max_vertices`,
`max_faces`, `max_corners`, `max_corners_per_face`, `max_list_rows`,
`max_list_items_per_row`, `max_property_values`,
`max_codec_new_requested_bytes`, `max_total_new_requested_bytes`,
`materialization` and `inspection`. The last two use Geometry's complete required
materialization and inspection limits. Counts/byte caps use uint64 and must also
fit the compiled host extents.

Definitions:

- Header bytes include all bytes from `ply` through the `end_header` terminator.
  Header line count includes format, comments and terminating header lines.
- A physical line cap excludes its LF/CRLF bytes and includes other spaces/tabs
  and any comment keyword/content. It applies to header and ASCII body, never
  to arbitrary binary payload bytes. Per-comment content is as defined above;
  aggregate comments remain bounded by header/source bytes and header lines.
- Schema-name bytes sum each element/property name once, without terminators;
  per-name caps are separate. Property count includes x/y/z and structural lists.
- A list row is one count prefix, including a zero count, across all list
  properties. Per-row item limits also apply to structural lists; per-face corner
  limits additionally apply there. Custom-property scalar values sum all
  nonstructural scalar cells and list items, excluding count prefixes, positions
  and structural indices. Nested Geometry caps separately cover full canonical
  scalar/offset/descriptor storage. Check schema-derived row/offset plans before
  allocating even when every list item count is zero.

Writer limits apply the corresponding schema/count/value bounds plus actual
header/output bytes, header lines, physical ASCII lines/tokens, codec fresh
requests, nested inspection and aggregate fresh allowance. No comment-content
limit is needed for an output with no comments. Bound current source descriptors
and complete input rows before their traversal/copy; do not treat the retained
schema's earlier admission as unlimited permission.
They have the same fields except `max_source_bytes`, `max_comment_bytes` and
`materialization`; they add `max_output_bytes`. Their `inspection` remains
required. `max_property_values` and `max_list_rows` have the same definitions
for read and write.

Reader aggregate allowance must cover the checked sum of declared codec,
materialization and inspection request caps. Writer allowance must cover codec
and inspection caps. These are bounds on concurrently live new requested blocks,
not cumulative allocation traffic or process RSS. Charge real codec/schema/
control/source/output storage, allocator overhead, decode scratch and overlapping
growth; charge Geometry materialization/inspection as disjoint live subsets.
Shared owners count once. The supplied mesh retains its original execution ledger. A separately supplied
schema-source document retains its own original ledger, which may differ.
Borrowing that document transfers neither prior charges nor source values.
Every new writer codec/inspection request and returned output derives from the
supplied mesh's ledger. Apply current writer descriptor/count/name caps before
traversing or copying the schema. Earlier retained blocks remain charged on
their owning ledgers and are excluded from this operation's fresh-request
allowance; that allowance does not combine pre-existing storage across distinct
contexts. Temporary
decode storage is released before successful return. Output retains only its
own codec blocks/context, not a new materialization or completed inspection.

The document/output owners share immutable storage. Copies are noexcept and
allocate nothing; final release frees the original blocks without allocating
or checking stop. Views need a live unreassigned owner; independent copies may
be read concurrently. Original bytes, schema, mesh/inspection and output bytes
remain usable after original caller bytes/options/context are destroyed.
Private injected allocation-backend state must outlive its last original block;
checkpoint state need only live through the synchronous call.

Borrowed spans/strings/schema/options remain readable and unchanged through
return and every joined worker. Concurrent ingress mutation or invalid native
memory is outside the contract. Use a fresh stop token for every call; an older
capture token is not retained as permanent cancellation state. Output does not
need the original caller context to remain externally alive.

## PLY-EXECUTION-001: Results, cancellation and handoff

The synchronous operation shapes are:

```text
ReadPly(bytes, limits, work, execution_context, fresh_stop)
WritePly(strong_mesh, schema_source_document, options, limits, work, fresh_stop)
```

Successful reads return one immutable document; successful writes return one
immutable output. Documents expose encoding, original bytes, ordered schema,
canonical mesh, full inspection and requested-byte subsets. A schema element
view identifies vertex/face, original count and ordered properties. Each property
view identifies its exact name, scalar/list kind, original value/item type,
list-only count type and binding role: X/Y/Z, structural vertex indices, or its
canonical attribute ordinal. Views borrow their document's immutable storage.
Output exposes bytes, complete facts and requested-byte subsets. No schema
builder or mutable view is included in this initial interface.

Configuration/cap sums are checked before allocating or traversing source
content; then a pre-requested stop returns canceled without codec allocation.
The source-byte bound precedes codec source capture. Header/schema and record
admission may use capped charged planning storage; no allocation-free parsing
claim is made. Reading establishes full typed profile validity and successful
Geometry inspection before a complete document is returned. Writer validates
its strong storage, complete binding and representability before publishing a
complete output. No failure returns a partial owner.

Expected invalid configuration, malformed source, unsupported variant/binding,
unsupported numeric conversion, limit, shared-budget refusal, cancellation,
invalid storage and delegated Geometry failures have typed result/diagnostic
forms. Keep source byte offsets and original element/property/row/item ordinals
when known; writer issues identify the actual supplied attribute/element and
schema binding. Diagnostics own fixed values rather than borrowed names. Preserve
actual Geometry stage/status/diagnostic/storage issues, including unsupported
allocation profiles. Do not synthesize a clean result from truncated inspection.

Work policy bounds byte loops, value/token loops and descriptor/element-row
loops separately, with the nested Geometry work policy. A large list is
partitioned by bounded byte/value checks even if it is one row. Numeric-token
conversion has the finite indivisible exception described above. Check stop
before work, at these bounded intervals, after delegated work, and immediately
before final owner handoff. A stop observed there refuses the owner even if all
encoding was already completed. A stop requested after return does not revoke
an existing owner. Final handoff performs no new allocation. Cleanup does not
allocate or observe cancellation.
Its required fields are `cancellation_block_bytes`,
`cancellation_block_values`, `cancellation_block_records` and `geometry`.
Values count numeric fields, including list prefixes, in either encoding; records count
schema descriptors/header lines and element rows. Header word scanning and
arbitrary content scanning also use bounded byte checks. Nested Geometry uses
its complete work policy. A fixed-width field may finish before its following
check, with an indivisible extent of at most eight bytes.

Serial codec work reports zero codec workers. Delegated Geometry jobs and
workers are real observations; all started workers join on every exit. Use the
shared execution/worker ledger rather than a competing worker pool. Unexpected
allocation-backend/runtime exceptions propagate after rollback and joins; they
are not relabeled as malformed input or simulated budget refusal. Explicit
Meshvale requests are accounted; opaque OS/runtime internals are outside that
requested-block ledger, as in Geometry's execution contract.
Execution results have codec, materialization and inspection observation subsets;
an unreached subset remains zero. Retained requested-byte results have codec,
materialization, inspection and total subsets, where total is the checked sum.
These report live attributed blocks after return, not the discarded planning
peak. The shared context owns the separate lifetime peak observation. A completed
output's materialization and inspection subsets are zero.

## PLY-PROCESSING-001: Custom properties and editing

Raw property retention is not a reference-remapping policy. Values in a custom
vertex or face property may refer to source face/corner identities or other data.
Names such as color, normal, material_index or face_indices do not automatically
establish safe transfer or an understood semantic. The codec supplies no automatic
row-local declarations and does not modify property values to make an edit pass.

A caller composing the existing charged Repair operation must satisfy its own
attribute policy, using truthful explicit domain/name row-local declarations for
these opaque properties when appropriate. When that operation reaches attribute
semantic eligibility, an opaque property without a truthful required declaration
retains its actual unsupported-attribute result. Configuration, source-storage,
target, budget, cancellation and other failure ordering remain owned by Repair.
A property-free mesh requires no declaration; empty-target processing does not
establish that a nonempty edit is safe. A list containing face IDs is not safe
merely because its vertex row remains present. Unknown
reference remapping, vertex welding and general schema migration are separate
features. An empty-target operation can preserve finite raw rows without claiming
that a nonempty edit would be safe.

An independent workflow can link `meshvale::ply` and the existing Repair target;
the codec acquires no Repair dependency or new duplicate-removal algorithm.
Codec identity facts describe its supplied mesh. Only the real operation's
complete maps provide correspondence across an accepted or unchanged edit.

## PLY-ACCEPTANCE-001: Evidence before capability claims

Independently authored ASCII, little-endian and big-endian wire/geometry/schema
oracles cover all eight primitive types, signed ranges, interleaved property
order, arbitrary core-property positions, both element orders, optional face,
empty/point-only data, mixed polygons, unused/shared vertices, winding/start
order, repeated IDs, ragged empty/nonempty lists, original narrow widths and
exact scalar alternatives/bits. Test header/binary boundary bytes that look like
whitespace, exact consumption, unsupported elements/directives/types, duplicate
names, missing bindings, nonfinite values, malformed/truncated/range controls,
explicit rewrite and incompatible conversion refusal.

Run each import encoding through all three destinations and independently reload
the actual bytes. Qualify signed zero, finite extremes and subnormals at both
floating widths; observe caller control modes before fixture restoration. Prove
physical allocation/cap-minus-one/occupied-ledger failures, exception cleanup,
mid/final cancellation, noalloc handoff/copy/free and owner use after original
context destruction through the real original-backend seam.

A separate installed operation fixture uses eight vertices including one unused
row, cyclic quads `[0,1,2,3]` and `[2,3,0,1]`, and unrelated triangle `[4,5,6]`,
with truthful custom measurement declarations. The actual accepted `keep=0, remove=1`
operation must retain all vertex rows and vertex-property rows, produce offsets `[0,4,7]`,
corners `[0,1,2,3,4,5,6]`, SourceFaces `[0,2]`, FaceMap `[0,0,1]`, SourceCorners
`[0,1,2,3,8,9,10]` and CornerMap `[0,1,2,3,2,3,0,1,4,5,6]`. Selected face scalar/
ragged rows, original wire widths and schema order survive actual write/reload.
Empty-target, missing-declaration, differing-property, reversed-winding and
coordinate-equal/different-ID controls establish the operation's real boundaries.

Require standalone installed header/codec and separate installed operation
consumers, relocated packages, source-only archives, package/notices checks,
strict normal and matching sanitizer tests, and actual applicable qualified/
nonempty-unsupported plus common-empty numerical profiles. Record real source,
compiler/runtime/dependency identities. No throughput, scene, full-PLY, stable
ABI or release coverage follows from this contract.
