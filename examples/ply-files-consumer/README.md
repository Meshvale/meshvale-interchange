# Retained typed PLY file publication

This standalone C++20 example links the installed `meshvale::ply_files` target.
It captures original typed PLY inputs, explicitly selects face rows for changed
chains, writes each destination encoding, and publishes a new flat directory
with a verified receipt. It independently checks all eight scalar wire types,
dense/list values, signed-zero and subnormal bits, schema order, physical reload
and complete inventory. The captured owner remains usable after source removal.

Configure with `cmake -S examples/ply-files-consumer -B build/ply-files-example`
and your installed package prefixes in `CMAKE_PREFIX_PATH`. Build and run CTest
with the same compiler, runtime and materialization profile as the producer.
The producer must enable `MESHVALE_INTERCHANGE_PLY_FILES=ON`. Select the documented
qualified profile only for an actually qualified Geometry producer.

Running the example normally uses a temporary directory. Pass
`--output-dir NEW_DIRECTORY` to keep the original demonstration outputs for
inspection. The directory must be new. `--unsupported-profile` runs genuine empty
chains and checks nonempty and point-only refusal on the common profile.
