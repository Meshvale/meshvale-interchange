# Retained glTF memory output

| Field | Value |
|---|---|
| ID | GLTF-WRITE-001 |
| Version | 0.1.0 |
| Status | Selected interface; implementation pending |
| Interface | [gltf_write.h](../include/meshvale/interchange/gltf_write.h) |
| Related | [Asset contract](gltf.md), [build/install](../ENVIRONMENT.md) |

## GLTF-WRITE-PRESERVE-001: Unchanged emission

`WriteRetainedGltfAsset` emits an unchanged admitted asset as owned memory.
`GltfWriteBundle::Json()` contains a newly allocated exact copy of the original
JSON bytes, including member order, whitespace, escaped spelling, absent and
explicit defaults, opaque members and exact number lexemes. It uses the retained
original document; parser projection and floating conversion are not writer
inputs. It does not construct a writable JSON DOM or reopen source files.

`Resources()` shares the immutable retained resource owner. Kind/table index,
origin, authored URI, buffer/view provenance, logical byte count and full captured
bytes remain unchanged. External trailing bytes, data-URI decoded bytes and
bufferView image aliases remain available. Sharing retains the source asset's
existing charged storage until the final output/source owner is destroyed;
resource payloads are not copied or rebound by this operation.

Scene, node, mesh, primitive, material, skin, morph, animation and resource
identities/references remain unchanged in JSON. Optional opaque extensions and
records unsupported for primitive processing remain preservable as admitted
source data. Emission does not establish their semantic validity, processing
legality, shader equivalence or image decoding. Required unsupported extensions
cannot enter this operation without an admitted source asset.

## GLTF-WRITE-OWNER-001: Lifetime and concurrency

The visible immutable bundle owns its JSON and shares ownership of the retained
resource inventory. JSON/resource/URI spans survive destruction of caller input,
the source asset wrapper and the caller's execution-context wrapper. A span still
requires at least one corresponding bundle copy to remain alive. Copies retain
one output owner and do not duplicate payloads.

Independent bundle copies and const reads may run concurrently. Assigning or
destroying the same wrapper concurrently requires caller synchronization. The
operation is synchronous; the caller keeps its source wrapper alive until return.
Cancellation of the original read token cannot cancel a new writer operation.

## GLTF-WRITE-LIMIT-001: Admission and charges

All five write limits and both work-policy fields are mandatory positive
host-representable values, with no product defaults. Configuration rejection
precedes cancellation and output allocation.

`max_json_bytes`, `max_resources`, `max_resource_bytes` and
`max_total_resource_bytes` bound the exposed output inventory. Resource totals
sum full bytes for every exposed resource, including aliased bufferView images;
they describe inventory and are not an allocation measurement. Checked arithmetic
rejects an unrepresentable total before allocation.

`max_output_requested_bytes` caps new operation requests before construction,
including charged accounting state, rebound shared control/output blocks,
allocator headers/alignment padding, JSON storage and concurrent worker scratch.
The retained source's existing charges are excluded from this operation cap and
remain live on the same Geometry execution ledger. Source/output coexistence and
other operations can exhaust that shared ledger independently of this cap.

`NewRequestedBytes()` reports current retained new-operation requests after the
call has joined workers and released scratch. It is not peak process memory or
the source's retained charge. Standard thread/runtime exception machinery is
outside the owned-container accounting contract. No total-memory or throughput
claim is made.

## GLTF-WRITE-EXECUTION-001: Copy work and rollback

Only JSON bytes need new payload storage. Resource ownership sharing and ordered
limit checks run on the caller. JSON partitions write disjoint preallocated byte
ranges. A copy meeting `parallel_min_json_bytes` with at least two partitions
tries nonwaiting worker reservations on the source's shared execution ledger;
fewer than two available slots falls back to the caller. The caller-selected
`json_partition_bytes` determines ranges; it is not a library default.

Cancellation is cooperative before allocation, between bounded copy blocks and
before returning success. Every started worker joins before its lease/output is
released, including partial launch failure. Actual worker/runtime exceptions take
precedence over cooperative cancellation; source-order job slots select among
multiple actual failures. No partial bundle escapes a failed operation and source
bytes remain unchanged. Expected configuration, operation-limit, shared-budget
and cancellation failures return a fixed diagnostic. Actual allocation/runtime
exceptions propagate after rollback and joins. Destruction does not allocate or
consult cancellation.

## Selected scope

This interface supplies memory output for unchanged retained glTF assets.
Primitive replacement, Geometry transfer, resource/container conversion, GLB
assembly, filesystem staging/publication, Python bindings and processing workflows
require their separate contracts and acceptance evidence.
