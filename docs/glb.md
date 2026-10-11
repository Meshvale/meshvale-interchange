# Supplied-byte GLB containers

| Field | Value |
|---|---|
| ID | GLB-CONTAINER-001 |
| Version | 0.1.0 |
| Status | Development candidate |
| Owner | Interchange glb.h and glb.cpp |

`ReadGlbContainer` captures a GLB 2 container from a borrowed byte span.
`WriteGlbContainer` assembles ordered supplied chunks into the same owned format.
Both require explicit nonzero `GlbLimits` and `GlbWorkPolicy`, an installed
Geometry execution context and an optional stop token. Inputs must remain alive
and unchanged until the operation returns and all workers have joined.

The [Khronos GLB specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#glb-file-format-specification)
owns the wire format. The first chunk is JSON; BIN may occur once in second
position. Other chunk types are retained in source order without interpreting
their semantics. Chunk boundaries are four-byte aligned. Assembly appends spaces
to JSON and zeros to BIN; unknown chunk payloads must already be aligned. Capture
preserves all supplied bytes, including padding and unknown chunks.

Container success establishes wire structure only. It does not establish valid
glTF JSON, asset version, resource references, logical buffer lengths, padding
relative to those lengths, image semantics or extension correctness. Feed its
JSON/BIN bytes to a separately qualified glTF asset reader. This interface has no
filesystem, publication, Geometry transfer or Repair workflow behavior.

`GlbContainer` is move-only, immutable and nullable. `Bytes()` and `Chunk()` return
borrowed spans whose owner must remain alive; moving transfers ownership without
changing the storage. `Chunk()` throws `std::out_of_range` on an invalid index.
Separate operations and read-only access to a stable owner may run concurrently;
moving or destroying the same owner needs caller synchronization.

Limits bound container bytes, chunk count and actual simultaneously requested
allocation bytes. Requested bytes include output state, descriptors, byte storage,
allocator headers/alignment and worker scratch. The shared Geometry payload cap
also applies; retained storage remains charged through owner destruction. Caller
inputs, allocator heap overhead, context configuration and thread/exception
runtime internals are excluded; this is not an RSS bound.

Structure scanning, checked header assembly and ordered error classification are
serial because they depend on preceding extents. Payload copying uses disjoint
byte partitions when the configured threshold is met and at least two shared
worker slots are available. Partition sizes are caller choices, not a throughput
promise. Small or occupied-budget work falls back to the caller. `workers_used`
reports actual launched workers; all launched workers join before any return or
exception. Partial launch failure also joins before freeing output or scratch.

Invalid options, malformed structure, unsupported container version, operation
ceilings, shared-budget exhaustion and cancellation have distinct fixed result
statuses and an applicable source byte offset. An established structural error
precedes cancellation; limits do not imply malformed glTF. Genuine allocation,
thread-launch and other runtime exceptions propagate after cleanup. Failure
returns no container and never changes input bytes. Cleanup neither allocates
nor checks cancellation.
