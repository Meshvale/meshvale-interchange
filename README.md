# meshvale-interchange

Asset import and export for polygon mesh workflows.

**Status:** Repository initialized. Geometry algorithms, executable tools, bindings, format adapters, and release packages are forthcoming; the items below describe planned capabilities.

## Planned capabilities

- Polygon face and attribute preservation where a format supports it.
- Scene, material, instance, and resource handling with stated coverage.
- Explicit reports for triangulation, unsupported features, and conversion loss.
- Candidate formats: OBJ/MTL, PLY, OFF, STL, and glTF/GLB.
- FBX, 3MF, and USD integrations remain under evaluation.

Supported formats, operation guarantees, and platform compatibility will be documented and tested with each implementation and release.

## Development

Read [ENVIRONMENT.md](ENVIRONMENT.md) for portable configuration and the current checks. There is no native build or installable package yet. [AGENTS.md](AGENTS.md) provides focused instructions for work in this repository.

## Contributing

Use this repository's issues for reproducible problems and feature requests. Follow the public [contribution guide](https://github.com/Meshvale/.github/blob/main/CONTRIBUTING.md) and include how your change was validated. Share only assets you have permission to redistribute.

## License

Original material is licensed under [Apache-2.0](LICENSE). See [NOTICE](NOTICE) for attribution; third-party material retains its own terms.
