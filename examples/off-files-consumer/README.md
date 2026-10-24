# Installed OFF file consumer

This example exercises the development
[OFF file contract](../../docs/off-files.md); qualification is pending.
It captures a real Unicode-named OFF file, retains its immutable bytes after
the original file is removed, writes canonical OFF and publishes a new flat
directory with an optional verified receipt. Independent physical reopening
checks indexed polygons, unused signed-zero vertices, exact bytes and inventory.
An existing destination must refuse replacement. Empty input is exercised in
every profile; `--unsupported-profile` requires the actual nonempty Geometry
refusal rather than skipping that boundary.

Install the matching native producer with `MESHVALE_INTERCHANGE_OFF_FILES=ON`,
then configure this directory with `CMAKE_PREFIX_PATH` pointing to its installed
Interchange, Geometry and dependency prefixes. Select the same documented
materialization profile, compiler, configuration, CRT and instrumentation as the
producer. Build and run CTest. This example requires no Repair or Python package.
It is development evidence when actually executed, not a supported release.
