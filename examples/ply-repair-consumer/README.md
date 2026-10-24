# Installed PLY and Repair consumer

This standalone C++20 example runs eighteen import -> actual duplicate-face processing -> export -> physical reload chains across ASCII, little-endian and big-endian PLY. It keeps polygon loops, all eight primitive widths, ordered custom scalar/list properties and an unused vertex. Its literal byte, schema and value checks are independent of the reader/writer. The real existing Repair operation supplies every edit map. Explicit row-local declarations are truthful only for these authored measurements; arbitrary imported properties need their own safety decision. Missing declarations, differing property values, reversed winding and coordinate-equal different vertex IDs are actual rejection controls.

Configure this directory with CMake; provide matching installed Geometry, Interchange and Repair prefixes:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/geometry;/path/to/interchange;/path/to/repair"
cmake --build build
ctest --test-dir build --output-on-failure
```

Run `meshvale_ply_repair_consumer --output NEW_DIRECTORY` to retain three original files and eighteen verified outputs. The directory must not already exist. Running without arguments uses a temporary directory and removes only that owned directory. These demonstration writes are not an atomic file publisher. All limits are explicit and bounded. The source-selected unsupported numerical profile executes real nonempty refusals and common empty roundtrips instead of claiming nonempty conversion support.
