# Native glTF parser and allocation foundation

The [native reader contract](gltf-read.md) describes the installed asset and
primitive operations. This document describes their private implementation
seams and dependency inputs. [gltf.h](../include/meshvale/interchange/gltf.h)
owns public declarations; private headers, parser pointers and JSON-library
objects are not installed consumer interfaces.

## Capture and core projection

A flat immutable snapshot owns original source, decoded Unicode scalar-valid
UTF8 strings/keys, numeric token ranges and source-ordered node relationships.
Capture rejects malformed JSON, decoded duplicate keys and invalid Unicode.
Numbers retain their original spelling without binary-double conversion. Copies
retain a const owner, and their views require a live owner.

Capture alone does not admit glTF metadata. The reader separately traverses a
static vocabulary of 227 core paths to check types, exact integer/range/enum
constraints, required fields, array bounds and selected dependencies. Extras,
unknown properties and optional extension contents stay uninterpreted. Required
extensions are unsupported. A charged core projection includes admitted core
values and excludes opaque contents before entering cgltf. Parser success is a
prerequisite, not complete graph, accessor-role or format semantic validation.

Integer ingress compares bounded authored decimal values before narrowing.
Binary32 ingress preserves signed zero and finite subnormals; legal unsupported
representations report an explicit outcome. cgltf's configured numeric hook
parses projected values directly to binary32 using locale-independent conversion,
without changing the caller's rounding state. Same-library observer qualification
copies converted fields before freeing the parser owner; production retains no
borrowed parser data.

## Allocation and execution

The checked allocator owns a copy of Geometry's execution context and a stop
token. Rebound standard containers share the ledger; copy/move assignment and
swap propagate allocator ownership. Every actual block reserves payload,
ownership header and alignment padding before calling its backend. Multiplication
and addition reject overflow before allocation, and growing containers charge old
and replacement blocks until the old storage is freed.

Each block retains its release backend and payload lease. Cleanup frees storage
before releasing its charge and neither allocates nor checks cancellation. Shared
owner/control blocks, source snapshots, parser temporaries, traversal stacks,
projections, resources, decoded outputs and worker arrays use this seam. The
per-extraction operation cap also charges its own shared accounting state and
includes replacement overlap. Operation allocators are distinct even when their
root ledger is shared.

This accounts requested blocks, not RSS or allocator heap overhead. Caller input,
execution ledger/stop-state construction and runtime exception/thread storage are
outside the accounting seam. Expected limit, payload-budget and cancellation
failures use fixed diagnostics. Actual allocation/runtime exceptions propagate
after cleanup; parser C callbacks contain exceptions until parser ownership is
released.

Ordered capture and graph traversal run on the caller because they update ordered
state. Independent resource copies and accessor rows use Geometry worker leases
where eligible and fall back when the shared budget is occupied. Worker leases
remain held until every started thread is joined, including partial launch and
worker exceptions. Private qualification hooks and statistics are synchronous
implementation test seams; no public callback or throughput claim follows.

## Build inputs and checks

[THIRD_PARTY.md](../THIRD_PARTY.md#gltf-parser-foundation) owns cgltf/jsoncons pins
and notices. [GltfDependencies.cmake](../cmake/GltfDependencies.cmake) verifies
archive SHA256 values and keeps upstream headers private. Native installs include
the notices; Python license metadata retains them although Python remains
OBJ-only. The installed Geometry package supplies `meshvale::editing` and public
execution leases. See [ENVIRONMENT.md](../ENVIRONMENT.md) for portable build and
installed-consumer commands.

Native tests exercise exact requested-block caps, replacement overlap, aligned
backend failure, parser/capture lifetimes, resource inventories and aliases,
worker fallback/joins, metadata bounds, sparse decoding and typed provenance.
The [installed glTF consumer](../examples/consumer/gltf.cpp) uses public headers
and targets for actual read/extract ownership and cancellation boundaries.
The independent standard-library oracles can be run with the corresponding
built executables:

```sh
python tests/gltf_json_oracle.py "$GLTF_JSON_TEST" .local/json-oracle
python tests/gltf_accessor_oracle.py "$GLTF_EXTRACT_TEST" .local/accessor-oracle
python tests/gltf_accessor_oracle.py "$GLTF_EXTRACT_TEST" .local/read-extract-oracle read
```

Set those executable paths for the selected build/configuration and use its
compiler/runtime environment, including sanitizer runtime when instrumented.
The lexical oracle tests exact JSON/Unicode retention; the accessor oracle
checks independent component bytes, normalized values, sparse provenance and
triangle corner maps. These checks do not certify complete materials, images,
transforms, skinning, morph evaluation or animation semantics.
