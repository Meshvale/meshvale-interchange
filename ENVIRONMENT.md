# Local development environment

This file owns the boundary between portable configuration and machine settings.

Copy [environment.example.json](environment.example.json) to `.local/environment.json` and fill only the fields needed for your task. Keep local paths, credentials, source assets, and raw logs in ignored storage. The template describes configuration inputs; CMake does not read this JSON automatically.

`workspace_root` is the optional local checkout/workspace location. `vcpkg_root` is an optional existing vcpkg installation. Compiler, generator, triplet, and corpus fields remain unset until used. Relative build paths resolve from this repository. Native code needs C++20, CMake 3.24+, installed Geometry and double-precision tinyobjloader; see [the adapter contract](docs/obj.md#dependencies-and-native-consumption) and [dependency inventory](THIRD_PARTY.md).

Set `VCPKG_ROOT` to a bootstrapped vcpkg installation compatible with the pinned registry. Set `GEOMETRY_PREFIX` and `INTERCHANGE_PREFIX` to chosen absolute install prefixes, and `VCPKG_TRIPLET` to the target platform (`x64-windows-static-md`, `x64-linux`, or `arm64-osx` in the evaluated desktop lanes). In a compiler environment, use CMake manifest integration:

```sh
cmake -S . -B .local/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" -DCMAKE_PREFIX_PATH="$GEOMETRY_PREFIX"
cmake --build .local/build --config Release
ctest --test-dir .local/build -C Release --output-on-failure
cmake --install .local/build --config Release --prefix "$INTERCHANGE_PREFIX"
```

For a separate consumer, set `VCPKG_INSTALLED_DIR` to the manifest's dependency directory (by default inside `.local/build/vcpkg_installed`). This consumer has no manifest and consumes the already installed dependencies:

```sh
cmake -S examples/consumer -B .local/consumer -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" -DVCPKG_INSTALLED_DIR="$VCPKG_INSTALLED_DIR" -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_PREFIX_PATH="$INTERCHANGE_PREFIX;$GEOMETRY_PREFIX"
cmake --build .local/consumer --config Release
ctest --test-dir .local/consumer -C Release --output-on-failure
```

PowerShell callers use environment expansion such as `$env:VCPKG_ROOT` in place of shell variables. Keep actual paths in the shell or ignored configuration. CMake consumes installed dependencies, with no private repository/sibling source requirement. Native tests treat warnings as errors. Hosted checks evaluate desktop runners; they do not establish a Python wheel, stable ABI or every-architecture support matrix.

The current checks need Git and Python 3.10 or newer, with no third-party Python packages:

```sh
python scripts/check-portability.py
python scripts/check-docs.py
```

Public files use repository-relative links, public URLs, tool names, and symbolic environment values. Run these checks before committing or publishing. The portability check scans working files and staged content for machine paths and private local files; it does not scan all secrets or determine whether a document should be public. Review public content and package contents separately.
