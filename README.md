# meshvale-interchange

Asset import and export for polygon mesh workflows.

**Status:** Development C++20 library with OBJ/MTL text and file adapters, resource snapshots, verified bundle publication and an optional Python interface. No stable release or CLI yet.

## Available native adapter

Import/export mixed polygon faces with separate corner UVs/normals, missingness, material assignments, object/group membership and smoothing. Export reloads and verifies its result before returning text. Extra UV sets and other unsupported channels produce an explicit failure; OBJ is an initial adapter, and the wider format plan remains below.

Read the [OBJ/MTL text contract](docs/obj.md) for mesh semantics and numerical limits, and the [file/bundle contract](docs/obj-files.md) for material/texture resolution, resource boundaries and publication outcomes. Separate installed consumers demonstrate [text interchange](examples/consumer/main.cpp) and [resource-aware file publication](examples/consumer/files.cpp). Texture files retain their bytes; image decoding and shader equivalence are outside current coverage.

The [Python interface](docs/python.md) exposes the same operations through canonical Geometry snapshots, frozen asset values and owned resource bytes. It supports cooperative cancellation and synchronous publication callbacks. A verified-content callback can prepare receipts from reloaded assets and exact staged bytes for checked inclusion in the same bundle. See the [installed bundle example](examples/python/obj_bundle.py) for load → inspect → publish → reload.

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
Its API and runtime dependencies are not implemented; see the implemented
native adapters above for current format coverage.

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
