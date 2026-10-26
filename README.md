# meshvale-interchange

Asset import and export for polygon mesh workflows.

**Status:** Development C++20 library with OBJ/MTL text and file adapters, supplied-byte STL, indexed ASCII OFF and typed PLY under qualification, GLB containers, native glTF retention/static primitive extraction, unchanged and checked topology-selection memory output, optional native USD and FBX imports, resource snapshots, verified OBJ and glTF/GLB bundle publication and an optional OBJ Python interface. No stable release or CLI yet.

The [source-version profile](docs/versioning.md) preserves one native identity
for tagged checkouts, tagged archives and source packages named for this product.
Python's full build version must agree with the matching source identity.

## OBJ native adapter

Import/export mixed polygon faces with separate corner UVs/normals, missingness, material assignments, object/group membership and smoothing. Export reloads and verifies its result before returning text. Extra UV sets and other unsupported channels produce an explicit failure; OBJ is an initial adapter, and the wider format plan remains below.

Read the [OBJ/MTL text contract](docs/obj.md) for mesh semantics and numerical limits, and the [file/bundle contract](docs/obj-files.md) for material/texture resolution, resource boundaries and publication outcomes. Separate installed consumers demonstrate [text interchange](examples/consumer/main.cpp) and [resource-aware file publication](examples/consumer/files.cpp). Texture files retain their bytes; image decoding and shader equivalence are outside current coverage.

The [Python interface](docs/python.md) exposes the same operations through canonical Geometry snapshots, frozen asset values and owned resource bytes. It supports cooperative cancellation and synchronous publication callbacks. A verified-content callback can prepare receipts from reloaded assets and exact staged bytes for checked inclusion in the same bundle. See the [installed bundle example](examples/python/obj_bundle.py) for load → inspect → publish → reload.

## Native STL triangles

The development [STL interface](docs/stl.md) reads and writes explicitly selected
ASCII or binary supplied bytes through `meshvale::stl`. It retains facet order,
raw normals and source metadata, uses charged canonical Geometry storage, and
refuses unsupported attributes or inexact binary32 export. Import gives each
facet three distinct vertices; it does not infer shared connectivity. The
[installed consumer](examples/stl-consumer/main.cpp) demonstrates inspection and
physical reload. Qualification is in progress; filesystem publication, Python
and application entry points remain separate.

## Native indexed OFF polygons

The development [OFF interface](docs/off.md) reads and writes bounded base ASCII
OFF through `meshvale::off`. It preserves shared vertex IDs, unused vertex rows,
mixed polygon loops and exact admitted finite positions. Writing requires explicit
canonical text and unknown-edge-count policies; richer variants and attributes
are refused. The [installed codec consumer](examples/off-consumer/main.cpp)
demonstrates physical write/reload. A separate [installed Repair example](examples/off-repair-consumer/README.md)
removes a cyclic duplicate quad with the existing Repair engine, checks its full
correspondence maps, and reloads the accepted candidate. Interchange has no Repair
dependency. Qualification is in progress; Python and application commands remain
separate.

The optional [OFF file interface](docs/off-files.md) captures a retained regular
file and publishes a verified new directory with an optional computed receipt.
It reloads the staged entry and checks complete bytes before a no-replace commit.
The [installed example](examples/off-files-consumer/README.md) demonstrates source
removal, Unicode paths, receipt ownership and physical readback. This development
module is under qualification and requires no optional glTF module or Repair.

## Native typed PLY polygons

The development [PLY interface](docs/ply.md) reads and writes explicitly selected
PLY 1.0 ASCII, little-endian binary and big-endian binary through `meshvale::ply`.
It preserves indexed polygon loops, unused vertices and admitted typed vertex/face
scalar and list properties. The retained original schema records wire widths and
property order; writing checks the supplied current mesh against that schema.
Unsupported variants, incompatible bindings and inexact conversions are refused.
Custom properties remain raw values without inferred color or normal semantics.

The [installed codec example](examples/ply-consumer/README.md) demonstrates
cross-encoding physical write/reload. A separate [installed Repair example](examples/ply-repair-consumer/README.md)
uses the existing Repair engine and checks its correspondence maps and retained
properties before writing the accepted candidate. Writing requires an explicit
canonical rewrite policy.

The optional [PLY file interface](docs/ply-files.md), `meshvale::ply_files`,
captures retained regular files and publishes a verified new directory with an
optional computed receipt. Publication checks complete typed values and wire
schema before a native commit without replacement. The self-contained
[installed file example](examples/ply-files-consumer/README.md) demonstrates
unchanged and selected-face publication across all encodings, retained source
ownership and independent physical readback. Enable
`MESHVALE_INTERCHANGE_PLY_FILES=ON` to build it. Qualification is in progress;
Python and application entry points remain separate.

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
the public interface and owned lifetimes. `GltfAsset::SharesExecutionBudget`
lets callers check the retained asset's execution ledger before starting work;
the query does not allocate or change accounting. The [retained memory writer](docs/gltf_write.md)
emits exact unchanged JSON and shares full immutable resource ownership; its
[installed example](examples/consumer/gltf_write.cpp) reloads the output.

The development [direct translation extraction profile](docs/gltf-translation.md)
validates dense LINEAR/STEP node-translation tracks and retains a source-bound
proof with the extracted local triangles. The [installed extraction example](examples/consumer/gltf_translation.cpp)
demonstrates explicit limits, facts and owner lifetimes. Translation-aware checked
writing, output inspection and publication remain pending; static defaults are
unchanged, and full workflow and release qualification remain separate.

The [static raw transfer](docs/gltf-materialization.md) copies an
extracted primitive into inspected canonical Geometry storage through the
separate `meshvale::gltf_materialization` target. It retains source ownership,
channel correspondence and normalization declarations; canonical attributes
remain raw. Its [installed example](examples/consumer/gltf_materialization.cpp)
composes transfer, inspection and face selection. The transfer contract owns
numerical-profile and lifetime limits. Same-format JSON glTF/GLB filesystem publication is described below; Python glTF operations remain
forthcoming.

The [checked topology writer](docs/gltf_topology_write.md) writes an ordered
triangle subset into supplied-memory glTF JSON through `meshvale::gltf_topology_write`.
It verifies the inspected candidate and both correspondence maps, keeps original
vertex/channel accessors and resources, and appends a UINT32 index resource for
changed output. Identity output retains the exact original JSON. The
[installed example](examples/consumer/gltf_topology_write.cpp) demonstrates
selection, writing and independent reload. This native development operation
has a separate [same-format file publication contract](docs/gltf-files.md);
Python operations and a stable release remain forthcoming.

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
buffer to the canonical asset. JSON and GLB topology output can be staged, reloaded,
independently inspected and committed to a new destination without replacement.
The [publication example](examples/consumer/gltf_file_publish.cpp) selects a
triangle, removes the source files, includes static and computed receipts, and
reopens the committed bundle. Computed files can use an owning callback or a
borrowed provider backed by caller-owned storage. Both routes verify every
staged byte and file before publication. The [GLB publication example](examples/consumer/gltf_file_publish_glb.cpp)
preserves embedded BIN and ordered opaque chunks and reopens changed output;
identity output retains the exact original wire. Publication remains a development
interface under qualification. glTF Python operations remain forthcoming.

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
