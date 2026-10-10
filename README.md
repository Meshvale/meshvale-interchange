# meshvale-interchange

Asset import and export for polygon mesh workflows.

**Status:** Development native library with OBJ/MTL text and file adapters, resource snapshots and verified bundle publication. No stable release, CLI or Python package yet.

## Available native adapter

Import/export mixed polygon faces with separate corner UVs/normals, missingness, material assignments, object/group membership and smoothing. Export reloads and verifies its result before returning text. Extra UV sets and other unsupported channels produce an explicit failure; OBJ is an initial adapter, and the wider format plan remains below.

Read the [OBJ/MTL text contract](docs/obj.md) for mesh semantics and numerical limits, and the [file/bundle contract](docs/obj-files.md) for material/texture resolution, resource boundaries and publication outcomes. Separate installed consumers demonstrate [text interchange](examples/consumer/main.cpp) and [resource-aware file publication](examples/consumer/files.cpp). Texture files retain their bytes; image decoding and shader equivalence are outside current coverage.

## Planned capabilities

- Polygon face and attribute preservation where a format supports it.
- Scene, material, instance, and resource handling with stated coverage.
- Explicit reports for triangulation, unsupported features, and conversion loss.
- Candidate formats: OBJ/MTL, PLY, OFF, STL, and glTF/GLB.
- FBX, 3MF, and USD integrations remain under evaluation.

Supported formats, operation guarantees, and platform compatibility will be documented and tested with each implementation and release.

## Development

Read [ENVIRONMENT.md](ENVIRONMENT.md) for portable build/install instructions and checks, [THIRD_PARTY.md](THIRD_PARTY.md) for dependencies, and [CHANGELOG.md](CHANGELOG.md) for consumer-facing changes. [AGENTS.md](AGENTS.md) provides focused contributor instructions. The installed CMake target is `meshvale::interchange`.

## Contributing

Use this repository's issues for reproducible problems and feature requests. Follow the public [contribution guide](https://github.com/Meshvale/.github/blob/main/CONTRIBUTING.md) and include how your change was validated. Share only assets you have permission to redistribute.

## License

Original material is licensed under [Apache-2.0](LICENSE). See [NOTICE](NOTICE) for attribution; third-party material retains its own terms.
