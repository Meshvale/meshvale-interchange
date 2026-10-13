# Native glTF retention and primitive extraction

The installed [gltf.h](../include/meshvale/interchange/gltf.h) interface reads
supplied glTF 2 JSON bytes and resource bindings, then extracts a selected static
primitive into owned typed channels. Link `meshvale::interchange`; no parser SDK
header or JSON DOM is exposed. The [installed consumer](../examples/consumer/gltf.cpp)
demonstrates ownership, fresh cancellation tokens, sparse channels and source
corner maps. The broader [asset proposal](gltf.md) includes future Geometry
transfer, editing and publication. The [retained memory writer](gltf_write.md)
preserves an unchanged admitted asset. Python remains OBJ-only.

## Read and ownership

`ReadGltfAsset` requires explicit `GltfReadLimits`, `GltfWorkPolicy`, a shared
Geometry execution context and a stop token. Required bounds and partition
thresholds default to zero, which is invalid configuration. Inputs remain alive
and unchanged until the synchronous call returns. An accepted `GltfAsset` owns
the original JSON bytes, decoded strings, exact numeric lexemes and resource
bytes. Copies and primitive/instance handles retain a const shared owner.
Borrowed spans require their corresponding owner to remain alive. Selection
rejects handles belonging to another asset.

`GltfAsset::SharesExecutionBudget(context)` performs an allocation-free,
nonthrowing query of the asset's retained Geometry worker/payload ledger. It
returns true for context copies sharing that ledger and false for independently
created contexts, even with equal numeric limits. A moved-from supplied context
returns false. The query exposes no context, allocator or raw identity, takes no
lock, performs no checkpoint or reservation and changes no counters. Distinct
fresh operation caps do not affect this ledger comparison or become
interchangeable allocators. The asset and context remain alive during the query;
concurrent const queries through immutable copies are allowed, while reassignment,
destruction or moving the same object concurrently requires caller synchronization.
File callers can query `source.Asset()` through this same interface without another
parse or extraction. The caller owns false-result diagnostics and validation order.

`RetainedCoreMetadataV1` checks strict JSON and selected core metadata, references
and graph declarations. It rejects decoded duplicate keys, invalid Unicode scalar
sequences, missing required fields, fractional integer metadata, invalid core
ranges and malformed references. Exact integers and binary32 values are checked
before a charged core projection reaches the parser. Binary32 ingress and parser
conversion preserve signed zero and finite subnormals independently of numeric
locale and caller rounding mode. Legal values exceeding supported representations
or configured bounds produce `kUnsupportedProfile`.

Extras, unknown properties and optional extension payloads retain their original
source; the parser projection excludes their contents. Required extensions are
unsupported. A primitive has a conservative unsupported capability when any
declared optional extension may affect its interpretation. Retention does not
evaluate extensions, materials, images, skinning or animation and does not certify
complete Khronos semantic conformance.

Nodes retain authored local transform bits without applying them. Scene instances
remain distinct, and unused primitives remain selectable. Node forests reject
cycles, multiple parents and inconsistent scene roots. Selected declaration checks
cover skin joint ancestry and scene membership, inverse-bind accessor shape/rows,
morph target/base row counts and weight counts, and duplicate animation node/path
destinations or animation targets with authored matrices. General matrices and
zero scales are retained. Full transform, animation-time and output evaluation
remain outside this processing subset.

## Resource bindings and embedded data

Every buffer with an external URI or no URI, and every image with an external URI,
needs one caller binding by resource kind and table index, including unused
records. Data-URI resources and buffer-view images reject caller bindings.
An image with neither URI nor bufferView is malformed. Missing, duplicate, extra and too-short
bindings report `kResourceBindingMismatch`. Full supplied buffer spans are owned,
including trailing bytes beyond declared logical length. Buffer-view images alias
the owned buffer range without a duplicate copy. Filesystem and network resolution
are caller responsibilities.

Embedded decoding supports these exact lowercase media prefixes with canonical
standard base64:

- `data:application/octet-stream;base64,`
- `data:application/gltf-buffer;base64,`
- `data:image/png;base64,`
- `data:image/jpeg;base64,`

Alternative media parameters/case, percent encoding, whitespace, URL-safe symbols,
omitted padding and nonzero unused padding bits are unsupported representations.
Provably impossible grouping/padding, illegal canonical alphabet characters and a
missing required comma are malformed. Media/role mismatches and conflicting image
MIME declarations are malformed. Known PNG/JPEG MIME types require their initial
signature bytes; this is a necessary check, not complete image decoding.

## Typed primitive output

`ExtractGltfPrimitive` takes a fresh stop token and required extraction limits.
`kRequireTriangles` accepts TRIANGLES; `kConvertStripOrFan` accepts only
TRIANGLE_STRIP or TRIANGLE_FAN and returns triangle indices plus source stream
corner/accessor-row maps. Legal degenerate triangles remain present. Extraction
does not transform or merge scene instances.

Supported POSITION, NORMAL, TANGENT, TEXCOORD, COLOR and authored custom scalar/
vector channels retain component type, shape, normalization declaration, exact
typed raw values/bounds and dense/sparse provenance. JOINTS/WEIGHTS, morph and
animation effects, custom vertex matrices and non-triangle modes report their
unsupported processing capability. Sparse selectors are validated and applied to
base or implicit-zero rows. Cross-document accessor/view roles are checked before
selected decoding; extraction preserves source index encodings alongside converted
triangle indices.

`kRawAndNormalized` also returns normalized integer views rounded to nearest-even
binary64 independently of caller/worker rounding modes. Raw bytes remain unchanged.
The direction profile accepts a squared-length error of at most `2^-16` for
normals and tangent XYZ; larger finite errors are unsupported. Tangent W must be
exactly +1 or -1, and float colors must lie in [0,1]. No direction renormalization
or color clamping occurs. Typed vectors here are binary interchange transport;
Geometry transfer into canonical scalar/vector buffers remains a separate API.

## Execution and failure

Owned source, parser temporaries, projections, resource bytes, typed outputs,
worker arrays and per-job diagnostics share Geometry's checked allocation ledger.
Actual requested blocks include ownership headers, control blocks, alignment and
overlapping replacements. Extraction's operation cap also charges its accounting
state. Input memory, ledger/stop-state construction and runtime exception/thread
storage are outside this requested-block accounting; it is not an RSS limit.

Eligible independent resource copies and accessor rows reserve workers from the
shared context. An occupied budget falls back to the caller. Every started worker
is joined before releasing reservations, including partial launch failures.
Ordered JSON capture and graph construction run on the caller because they mutate
source-ordered traversal state. No parallel throughput guarantee is made.

Expected malformed/unsupported/configuration/binding/budget/cancellation failures
return fixed diagnostics and no partial result. Actual allocation/runtime exceptions
propagate after cleanup. An accepted asset's capture token does not become the
cancellation token of a later extraction. Native and installed tests cover those
lifetime and worker boundaries; full edited asset writing, Geometry transfer and
filesystem publication require their own interfaces and qualification.
