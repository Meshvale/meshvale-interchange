# Installed PLY consumer

This standalone C++20 example runs nine import -> export -> physical reload chains across ASCII, little-endian and big-endian PLY. It keeps polygon loops, all eight primitive widths, ordered custom scalar/list properties and an unused vertex. Its literal byte, schema and value checks are independent of the reader/writer. The codec has no Repair dependency and performs no welding or triangulation.

Configure this directory with CMake; provide matching installed Geometry and Interchange prefixes:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/geometry;/path/to/interchange"
cmake --build build
ctest --test-dir build --output-on-failure
```

Run `meshvale_ply_consumer --output NEW_DIRECTORY` to retain three original files and nine verified outputs. The directory must not already exist. Running without arguments uses a temporary directory and removes only that owned directory. These demonstration writes are not an atomic file publisher. All limits are explicit and bounded. The source-selected unsupported numerical profile executes real nonempty refusals and common empty roundtrips instead of claiming nonempty conversion support.
