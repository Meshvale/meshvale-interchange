# Native glTF parser foundation

This implementation is a private foundation for the [proposed glTF asset
contract](gltf.md). It adds no installed glTF header, asset reader, extraction,
writer or Python operation. See the [implemented adapters](../README.md) for
current format coverage.

The native library compiles pinned cgltf and explicitly configured jsoncons
through private implementation files. jsoncons uses lossless number/bignum
options, strict comments/trailing-comma handling and an explicit nesting limit;
its syntax event counter does not retain a document. A separate private capture
seam now owns a flat immutable JSON snapshot. The cgltf seam accepts a
small core projection and copies summary values before freeing its parser owner.
Successful parsing is not metadata, graph, resource or accessor-role admission.
The capture seam retains original bytes, exact numeric lexemes, decoded scalar-
valid UTF8 strings and keys, source order and parent/child/sibling relationships.
It rejects malformed syntax, invalid UTF8, lone escaped surrogates and duplicate
decoded keys. Number-looking strings remain strings; huge numbers and nonzero
tiny exponents remain their authored lexemes, with no double conversion.

Capture is JSON retention only. It accepts scalar/array roots and arbitrary JSON
fields; it does not admit glTF asset versions, integer metadata, enums, required
extensions or core field/graph/resource/accessor semantics. No captured document
is passed to cgltf before those later guards exist. There is no installed DOM,
asset reader or selection handle yet.

## Immutable capture ownership

The private capture requires nonzero source-byte, node-count, decoded-byte and
nesting-depth limits; zero is an invalid caller configuration. Well-formed JSON
exceeding these limits returns a size-limit failure. Source bytes, flat nodes,
decoded text, raw-number ranges, parser temporaries, traversal stack and shared
owner/control block use the checked allocator. Copies share a const owner and
remain charged until the final copy dies. Borrowed text views require a visible
snapshot owner. Input bytes remain unchanged and alive until capture returns;
the successful snapshot survives their destruction and the caller context's
destruction. Moved-from wrappers reject access.

Expected syntax, duplicate-key, size-limit, payload-budget and cancellation
failures return no partial snapshot; actual allocation/runtime exceptions
propagate. Capture copies source bytes in bounded chunks and checks cancellation
during scanning, Unicode/duplicate checks and parser events. This ordered parse
runs on its calling thread: SAX events update a shared nesting stack and
source-ordered sibling links, while raw-number correspondence follows the same
lexical order. Splitting these stateful passes requires a separate qualified
algorithm. The source-copy chunks currently provide cancellation checkpoints,
not parallel partitions; this bounded foundation has no public work policy.
Substantial resource copies and accessor rows require later shared-budget worker
admission and measured thresholds; this stage makes no parallel throughput claim.

## Allocation and execution ownership

The private allocator owns a copy of Geometry's execution context and a stop
token. Standard allocator rebinds share that ledger; copy/move assignment and
swap propagate the allocator. Each allocation reserves its complete requested
block before calling the backend: payload, ownership header and alignment
padding. Count multiplication and block-size addition reject overflow before
allocation. A growing container charges both its old and replacement blocks
until the old storage is actually freed.

Each block retains its release backend and a payload lease. Destruction releases
storage before its charge, without allocating or checking cancellation. A block
and its rebound shared owner can survive destruction of the caller's context or
stop source. Expected size/budget/cancellation failures have fixed private codes;
unexpected allocation exceptions propagate after parser cleanup. The cgltf C
callbacks contain exceptions and report them after releasing the parser.

This is requested-block accounting, not RSS or heap-overhead measurement. The
caller-owned input, execution ledger/stop-state creation, runtime exception
storage and unrelated Geometry result lifetimes are outside this allocator's
charge. It does not establish a total process allocation cap. Future owned
projections, resources and decoded outputs must use the same
checked seam before a reader can claim complete requested-payload coverage.
There is no new product budget default or public read-limit policy in this stage.

The tests exercise exact caps including headers, alignment, overflow, overlapping
container growth, partial failure/cancellation cleanup, stateful rebind ownership,
concurrent operations and mixed Geometry/Interchange reservations. Tiny parser
and allocator checks run serially to inspect deterministic ownership and fault
boundaries; the concurrent tests use two actual threads. This stage performs no
resource/channel work and makes no parallel throughput claim. Worker leases are
held through actual worker joins; later resource/channel partitions must use the
same context rather than launch an independent worker budget.

The native `gltf_json_snapshot` test checks owned lifetimes, exact caps, malformed
and duplicate input, overlapping retained charges and every observed allocation
boundary for failure and cancellation. An independent standard-library Python
oracle compares original fixtures against the compiled capture executable:

```sh
python tests/gltf_json_oracle.py "$GLTF_JSON_TEST" .local/json-oracle
```

Set `GLTF_JSON_TEST` to the built `meshvale_gltf_json_tests` executable, including
the configuration directory and platform suffix where required. Use the same
compiler/runtime environment as the native build, including the sanitizer
runtime when evaluating an instrumented executable. This tests lexical JSON
retention, decoded Unicode and exact Decimal values, not glTF schema legality.

## Build inputs and consumption

[THIRD_PARTY.md](../THIRD_PARTY.md#gltf-parser-foundation) owns the exact
cgltf/jsoncons pins and notices. [CMake acquisition](../cmake/GltfDependencies.cmake)
checks archive SHA256 and keeps upstream headers private. Native installations
include their notices; Python license metadata includes them even though Python
exposes no glTF API. No parser SDK or private header is installed for consumers.

The installed Geometry package must supply `meshvale::editing` and its public
execution leases. The evaluated producer source is revision
`920be542502652b1d16c5f90414cec6495ff62b4`; native package version remains `0.0.0`.
The [installed execution consumer](../examples/consumer/execution.cpp) links only
`meshvale::interchange`, verifies the public shared ledger/lease dependency and
reads an ordinary OBJ triangle. It does not exercise an unimplemented glTF API.

Use [ENVIRONMENT.md](../ENVIRONMENT.md) for configuration and checks. The native
`gltf_allocation` test uses only the selected private implementation and installed
Geometry targets. Retained JSON/resources, all core admission guards, enumerable
owner-bound handles, typed extraction and verified publication are separate
implementation gates, with skin/morph/animation support still outstanding.

## Exact metadata preparation

Private metadata helpers compare authored decimal numbers without allocation or
locale-dependent conversion. Unsigned/signed integer ingress preserves exact
64-bit boundaries and rejects fractional values before narrowing; binary32
conversion preserves signed zero and admits finite subnormals. Legal values
outside a native representation return an unsupported outcome. These helpers
and private immutable asset ownership prepare metadata admission; they do not
yet provide a callable installed asset reader or extraction interface.
