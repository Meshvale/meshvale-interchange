# Indexed OFF through the existing Repair engine

This standalone installed C++ example reads an original mixed quad/triangle OFF
fixture, removes its cyclic duplicate quad with the existing charged Repair
operation, writes the real accepted candidate, and independently reloads physical
output. It also proves an unchanged request, complete source face/corner maps,
unused vertex and signed-zero preservation, and refusal of reversed-winding or
coordinate-equal/different-index targets. It introduces no repair algorithm.

Install matching Geometry, native Interchange with `meshvale::off`, and Repair
with `meshvale::materialized_duplicates`. Match their compiler configuration and
C++ runtime. Interchange itself has no Repair dependency. Configure this separate
example with their installed prefixes and existing parser dependency prefix:

```sh
cmake -S examples/off-repair-consumer -B .local/off-repair-consumer -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$INTERCHANGE_PREFIX;$REPAIR_PREFIX;$GEOMETRY_PREFIX;$PARSER_PREFIX"
cmake --build .local/off-repair-consumer --config Release
ctest --test-dir .local/off-repair-consumer -C Release --output-on-failure
```

PowerShell uses `$env:INTERCHANGE_PREFIX` and corresponding environment expansions.
The numerical profile helper selects the expected actual producer outcome;
unsupported profiles must show the real nonempty Geometry refusal. Qualified
profiles exercise actual Repair. This is development evidence, not a stable
release or every-platform support claim.

By default the executable uses and removes its own new temporary directory. Pass
`--output NEW_DIRECTORY` to retain `source.off`, `changed.off` and `unchanged.off`.
An existing output directory is refused. These file operations are caller-owned
demonstration I/O, with no atomic verified publisher claim. Canonical output
rewrites comments/spacing and emits zero for unknown edge count under the explicit
[OFF policies](../../docs/off.md). Codec identity refers to the supplied candidate;
the separately checked Repair maps describe the original edit.
