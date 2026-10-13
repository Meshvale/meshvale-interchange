# meshvale-interchange

Asset import and export for polygon mesh workflows.

**Status:** Development C++20 library with OBJ/MTL text and file adapters, supplied-byte GLB containers, native glTF retention/static primitive extraction, unchanged and checked topology-selection memory output, optional native USD and FBX imports, resource snapshots, verified OBJ bundle publication and an optional OBJ Python interface. No stable release or CLI yet.

## OBJ native adapter

Import/export mixed polygon faces with separate corner UVs/normals, missingness, material assignments, object/group membership and smoothing. Export reloads and verifies its result before returning text. Extra UV sets and other unsupported channels produce an explicit failure; OBJ is an initial adapter, and the wider format plan remains below.

Read the [OBJ/MTL text contract](docs/obj.md) for mesh semantics and numerical limits, and the [file/bundle contract](docs/obj-files.md) for material/texture resolution, resource boundaries and publication outcomes. Separate installed consumers demonstrate [text interchange](examples/consumer/main.cpp) and [resource-aware file publication](examples/consumer/files.cpp). Texture files retain their bytes; image decoding and shader equivalence are outside current coverage.

The [Python interface](docs/python.md) exposes the same operations through canonical Geometry snapshots, frozen asset values and owned resource bytes. It supports cooperative cancellation and synchronous publication callbacks. A verified-content callback can prepare receipts from reloaded assets and exact staged bytes for checked inclusion in the same bundle. See the [installed bundle example](examples/python/obj_bundle.py) for load → inspect → publish → reload.

## Native GLB containers

Read or assemble a supplied GLB 2 byte container with owned JSON, optional BIN and
opaque chunks, explicit limits and shared-budget copy workers. The
[container contract](docs/glb.md) separates wire validation from glTF asset
semantics; a successful container read does not validate meshes or resources.
The [installed example](examples/consumer/glb.cpp) uses only public headers and
targets. Wire-container success is separate from glTF asset admission.

## Native glTF retention and extraction

Read supplied JSON/resources into an immutable asset and extract selected static
triangle primitives with typed raw/normalized channels, sparse provenance and
strip/fan source corner maps. The [native reader contract](docs/gltf-read.md)
documents its finite metadata/graph/resource profile and explicit unsupported
capabilities. The [installed consumer](examples/consumer/gltf.cpp) demonstrates
the public interface and owned lifetimes. The [retained memory writer](docs/gltf_write.md)
emits exact unchanged JSON and shares full immutable resource ownership; its
[installed example](examples/consumer/gltf_write.cpp) reloads the output.

The [static raw transfer](docs/gltf-materialization.md) copies an
extracted primitive into inspected canonical Geometry storage through the
separate `meshvale::gltf_materialization` target. It retains source ownership,
channel correspondence and normalization declarations; canonical attributes
remain raw. Its [installed example](examples/consumer/gltf_materialization.cpp)
composes transfer, inspection and face selection. The transfer contract owns
numerical-profile and lifetime limits. JSON filesystem publication is described below; Python glTF operations remain
forthcoming.

The [checked topology writer](docs/gltf_topology_write.md) writes an ordered
triangle subset into supplied-memory glTF JSON through `meshvale::gltf_topology_write`.
It verifies the inspected candidate and both correspondence maps, keeps original
vertex/channel accessors and resources, and appends a UINT32 index resource for
changed output. Identity output retains the exact original JSON. The
[installed example](examples/consumer/gltf_topology_write.cpp) demonstrates
selection, writing and independent reload. This native development operation
does not establish edited GLB publication, Python operations or a stable release.

The additive [retained workflow support](docs/gltf-workflow-support.md) provides
fully capped read/extraction entrypoints and live attributable owner counters.
Its optional `meshvale::gltf_workflow_support` target independently inspects
supplied topology output against the retained source, candidate and actual
reloaded owners across six comparison phases. The installed
[capped reader example](examples/consumer/gltf_workflow_read.cpp) and
[inspection example](examples/consumer/gltf_workflow_support.cpp) demonstrate
public ownership and cleanup. This development interface remains under
qualification; file publication has its own optional module and release support
remains pending.

## Optional native glTF and GLB files

The development [file interface](docs/gltf-files.md) captures a JSON or GLB entry and its
external resources into immutable owners through the opt-in `meshvale::gltf_files`
target. It retains complete physical bytes, including resource tails, sorted
unique relative paths and the capped canonical asset. The
[installed example](examples/consumer/gltf_files.cpp) reads real files and uses
their retained owners after removing the source files. Limits, strict path
admission and current qualification scope remain with the contract. GLB capture
retains complete JSON, BIN and opaque chunks and binds a supported embedded
buffer to the canonical asset. JSON topology output can be staged, reloaded,
independently inspected and committed to a new destination without replacement.
The [publication example](examples/consumer/gltf_file_publish.cpp) selects a
triangle, removes the source files, includes static and computed receipts, and
reopens the committed bundle. JSON publication remains under qualification;
GLB publication and glTF Python operations remain forthcoming.

## Optional native USD extraction

The opt-in [USD module](docs/usd.md) extracts one selected static polygon cage
from USDA or USDC, with owned normal/UV channels and original primvar indexing.
It uses a separate installed `MeshvaleInterchangeUsd` package. Scene composition,
skinning, animation, materials, Python bindings and writing remain outside this
subset. Default OBJ builds need no OpenUSD.

## Optional native FBX import

The opt-in [FBX module](docs/fbx.md) imports the documented static polygon subset,
with owned normal/UV channels and original layer mapping metadata. Its separate
`MeshvaleInterchangeFbx` package requires a locally licensed Autodesk2020.3.11 SDK
on the evaluated Windows x64 Release configuration. Transformed/animated/skinned
and material-bearing assets are rejected explicitly. No FBX writer or Python
binding is provided; default OBJ builds need no FBX SDK.

## Planned capabilities

- Polygon face and attribute preservation where a format supports it.
- Scene, material, instance, and resource handling with stated coverage.
- Explicit reports for triangulation, unsupported features, and conversion loss.
- Candidate formats: OBJ/MTL, PLY, OFF, STL, and glTF/GLB.
- Broader FBX/USD scene coverage and 3MF integration remain under evaluation.

The [proposed glTF/GLB asset contract](docs/gltf.md) defines document/resource
ownership, primitive selection and preservation gates for the next adapter.
The supplied-memory reader and extraction subset is described above; the broader
proposal remains partly unimplemented. The [native parser foundation](docs/gltf-foundation.md)
supplies immutable JSON capture, checked allocation and pinned parser inputs;
see the implemented native adapters above for current format coverage.

Supported formats, operation guarantees, and platform compatibility will be documented and tested with each implementation and release.

The [format SDK setup guide](docs/format-sdks.md) prepares optional native
development dependencies for STL, FBX and USD work, with current capability and
installation requirements kept separate.

## Development

Read [ENVIRONMENT.md](ENVIRONMENT.md) for portable build/install instructions and checks, [THIRD_PARTY.md](THIRD_PARTY.md) for dependencies, and [CHANGELOG.md](CHANGELOG.md) for consumer-facing changes. [AGENTS.md](AGENTS.md) provides focused contributor instructions. The installed CMake target is `meshvale::interchange`.

## Contributing

Use this repository's issues for reproducible problems and feature requests. Follow the public [contribution guide](https://github.com/Meshvale/.github/blob/main/CONTRIBUTING.md) and include how your change was validated. Share only assets you have permission to redistribute.

## License

Original material is licensed under [Apache-2.0](LICENSE). See [NOTICE](NOTICE) for attribution; third-party material retains its own terms.
