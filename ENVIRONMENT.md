# Local development environment

This file owns the boundary between portable configuration and machine settings.

Copy [environment.example.json](environment.example.json) to `.local/environment.json` and fill only the fields needed for your task. Keep local paths, credentials, source assets, and raw logs in ignored storage. The template describes configuration inputs; CMake does not read this JSON automatically.

`workspace_root` is the optional local checkout/workspace location. `vcpkg_root` is an optional existing vcpkg installation. Compiler, generator, triplet, and corpus fields remain unset until used. Relative build paths resolve from this repository. Native code needs C++20, CMake 3.24+, installed Geometry and double-precision tinyobjloader; see [the adapter contract](docs/obj.md#dependencies-and-native-consumption) and [dependency inventory](THIRD_PARTY.md).

The private [glTF parser foundation](docs/gltf-foundation.md) additionally acquires
SHA256-verified public cgltf/jsoncons archives during configuration. The installed
Geometry package must provide `meshvale::editing` execution leases. For offline
configuration, supply `MESHVALE_CGLTF_ARCHIVE` and `MESHVALE_JSONCONS_ARCHIVE` as
absolute archive paths; their hashes must match [GltfDependencies.cmake](cmake/GltfDependencies.cmake).
Arbitrary FetchContent source-directory overrides are rejected. This changes build
inputs for the [native glTF reader/extractor](docs/gltf-read.md); parser dependency
headers remain private and are not installed.

The cgltf translation unit propagates allocation/admission exceptions from its
C++ callbacks and owns cleanup of partial parser blocks. MSVC builds compile
that unit with `/EHsc-`, explicitly disabling the extern-C no-throw assumption
from a surrounding `/EHsc` setting while retaining synchronous exception
unwinding. The source is compiled as C++; this requirement does not modify the
pinned parser archive. A build that reinstates that assumption can skip owning
cleanup on callback failure. Native allocation-fault tests exercise this path.

Set `VCPKG_ROOT` to a bootstrapped vcpkg installation compatible with the pinned registry. Set `GEOMETRY_PREFIX` and `INTERCHANGE_PREFIX` to chosen absolute install prefixes, and `VCPKG_TRIPLET` to the target platform (`x64-windows-static-md`, `x64-linux`, or `arm64-osx` in the evaluated desktop lanes). In a compiler environment, use CMake manifest integration:

```sh
cmake -S . -B .local/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" -DCMAKE_PREFIX_PATH="$GEOMETRY_PREFIX"
cmake --build .local/build --config Release
ctest --test-dir .local/build -C Release --output-on-failure
cmake --install .local/build --config Release --prefix "$INTERCHANGE_PREFIX"
```

For a separate consumer, set `VCPKG_INSTALLED_DIR` to the manifest's dependency directory (by default inside `.local/build/vcpkg_installed`). This consumer has no manifest and consumes the already installed dependencies:

```sh
cmake -S examples/consumer -B .local/consumer -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" -DVCPKG_INSTALLED_DIR="$VCPKG_INSTALLED_DIR" -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_PREFIX_PATH="$INTERCHANGE_PREFIX;$GEOMETRY_PREFIX"
cmake --build .local/consumer --config Release
ctest --test-dir .local/consumer -C Release --output-on-failure
```

The consumer must match the producer's build configuration and C++ runtime. These commands use Release throughout; MSVC Debug and Release are incompatible when exchanging standard-library objects through this interface. PowerShell callers use environment expansion such as `$env:VCPKG_ROOT` in place of shell variables. Keep actual paths in the shell or ignored configuration. CMake consumes installed dependencies, with no private repository/sibling source requirement. Native tests treat warnings as errors. Hosted checks evaluate desktop runners; they do not establish a Python wheel, stable ABI or every-architecture support matrix.

The optional [Python build](docs/python.md) additionally needs the pinned build tools and installed Geometry/tinyobjloader prefixes. Wheel builds disable native testing and install only the Python component; native-only builds do not acquire a Python dependency. Candidate wheels are tested in fresh environments outside the checkout. This is development packaging evidence, not a stable release matrix.

The documentation/portability checks need Git and Python 3.10 or newer, with no third-party Python packages:

```sh
python scripts/check-portability.py
python scripts/check-docs.py
```

Public files use repository-relative links, public URLs, tool names, and symbolic environment values. Run these checks before committing or publishing. The portability check scans working files and staged content for machine paths and private local files; it does not scan all secrets or determine whether a document should be public. Review public content and package contents separately.

## C++ formatting

Use the [shared C++20 protocol](https://github.com/Meshvale/.github/blob/542b03450a3e4f376ddbbaea39f1b17badd0a6f6/docs/cpp-development.md) and the repository `.clang-format`. Install `clang-format==23.1.3` in an isolated tooling environment, then run:

```sh
python scripts/check-cpp-format.py --formatter clang-format
```

The checker selects tracked and nonignored untracked `.h`, `.hpp` and `.cpp` files from Git and requires formatter version `23.1.3`; pass a tool executable through `--formatter` when it is outside the shell path. Formatting verifies layout and include ordering, not API naming, ownership or algorithm correctness. Existing public snake-case names retain their contract; a separate verified migration must address naming.

## Optional PLY files

Enable `MESHVALE_INTERCHANGE_PLY_FILES=ON` to build and install
`meshvale::ply_files`. The [file contract](docs/ply-files.md) owns retained capture,
current typed/schema bounds, verified receipt publication and native commit
semantics. The [standalone example](examples/ply-files-consumer/README.md) links
only installed public targets, exercises all three encodings and preserves typed
properties after explicit Geometry face selection. Use the same compiler, runtime
and materialization profile for producer and consumer.

The native inventory checker accepts `--ply-files ON|OFF`, defaults to the native
cache selection and requires the same value in an installed consumer cache.
Enabled profiles require three actual native suites and one installed suite;
unsupported profiles execute genuine empty publication and nonempty/point refusal.
Native and sanitizer routes enable this target alongside the other optional file
modules. Binary fixture source bytes are published as exact hexadecimal text.

## Optional OFF files

Set `MESHVALE_INTERCHANGE_OFF_FILES=ON` to build and install `meshvale::off_files`
and its public header. The option defaults to OFF and requires no optional glTF
module, Repair or Python. The [file contract](docs/off-files.md) owns capture,
verified publication, limits and cancellation. The
[standalone installed example](examples/off-files-consumer/README.md) exercises
the public target with matching installed dependencies.

Enabling this option adds three exact native entries (`off_files_semantics`,
`off_files_requests`, `off_files_consumer`) and one installed entry
(`installed_off_files_consumer`) in both qualified and unsupported profiles.
`scripts/check-native-tests.py` reads the configured option or accepts explicit
`--off-files ON|OFF` and rejects a mismatched inventory. Repository native rows
enable it alongside the optional glTF cohort; the all-optional-OFF row keeps it
disabled. Both Linux sanitizer lanes enable it and require these additional
entries. Configured checks establish no completed platform qualification.

## Optional glTF and GLB files

Set `MESHVALE_INTERCHANGE_GLTF_FILES=ON` to build and install `meshvale::gltf_files`
and its public header. The option defaults to OFF and requires topology writing
and workflow support ON. It uses the existing Geometry/tinyobjloader/parser
dependencies; Python wheels explicitly keep it OFF. The
[file contract](docs/gltf-files.md) owns supported JSON/GLB capture, embedded BIN
binding and same-format JSON/GLB verified publication.

With the option ON, the inventory checker additionally requires `gltf_files_link`
and `gltf_files`, `gltf_files_glb` and `gltf_files_glb_requests` in both numerical
profiles, plus `installed_gltf_files_link` and `installed_gltf_files_consumer`.
Qualified builds additionally require `gltf_file_publish_semantics`,
`gltf_file_publish_requests`, `gltf_file_verified_provider_semantics`,
`gltf_file_verified_provider_requests`, `gltf_file_publish_glb_semantics` and
`gltf_file_publish_glb_requests`, plus `installed_gltf_file_publish_consumer` and
`installed_gltf_file_publish_glb_consumer`;
unsupported builds require `unsupported_gltf_file_publication_dependency`,
`unsupported_gltf_file_verified_provider_dependency` and
`unsupported_gltf_glb_publication_dependency`, which exercise actual
numerical-profile refusal before publication. The base `gltf_execution_budget`
fixture also runs with optional modules OFF. JSON/GLB capture and
extraction do not require numerical materialization admission. Installed examples
default the option to ON only when the exported target exists. Pass
`--gltf-files ON` or `OFF` to the inventory checker for an explicit selection; the
default reads the configured native option and checks both caches against it.
These counts describe configured requirements; completed runs own acceptance.

## Optional USD native module

Add `-DMESHVALE_INTERCHANGE_USD=ON` to the native build, supply the exact
OpenUSD26.08 package and compatible dependencies, then consume the separately
installed `MeshvaleInterchangeUsd` package. The [USD contract](docs/usd.md)
owns supported inputs, loading limits, SDK ownership, runtime/plugin layout and
notice-root configuration. Default native builds and current Python wheels
leave USD disabled. See the [SDK guide](docs/format-sdks.md) for preparation.

## Optional FBX native module

Add `-DMESHVALE_INTERCHANGE_FBX=ON -DFBXSDK_ROOT="$FBXSDK_ROOT"` to a Windows
x64 MSVC Release native build using dynamic CRT. The SDK must be the locally
licensed Autodesk2020.3.11 VS2022 shared configuration. Supply the matching
runtime DLL directory on PATH and consume `MeshvaleInterchangeFbx` separately.
The [FBX contract](docs/fbx.md) owns exact compiler/configuration admission,
static-import coverage, SDK cleanup and notice requirements. Default native
builds and current Python wheels leave FBX disabled.

## Optional supplied surface output

Set `MESHVALE_INTERCHANGE_SURFACE_OUTPUT=ON` to build and install
`meshvale::surface_output` and `meshvale/interchange/surface_write.h`. The option
defaults to `OFF`; it adds no Python interface. The
[surface contract](docs/surface-output.md) owns the four supplied-memory encodings,
strict/annotated representation, explicit finite numeric policy, caps, lifetimes
and actual-byte inspection. Build it with matching installed Geometry
`meshvale::charged_triangulation` and materialization targets. The ON-only
configuration probe checks the actual charged overloads and strong-owner symbols;
the shared development version is insufficient to identify that interface.

Use the existing producer/consumer commands with the additional
`-DMESHVALE_INTERCHANGE_SURFACE_OUTPUT=ON` on **both** configurations. The
[installed example](examples/consumer/surface_output.cpp) links the public target
and uses only installed public headers. An ON-only compile check includes the
new header twice; it is not an extra CTest entry.

The inventory checker accepts `--surface-output ON|OFF`, defaults to the native
cache selection and requires the same installed selection. ON adds exactly
`surface_output`, `surface_numeric`, `surface_capsule` and the installed
`installed_surface_output_consumer` in both declared profiles. Unsupported rows
must observe the real nonempty Geometry construction refusal before testing a
genuine admitted empty route; they cannot fabricate a surface owner or a decode
success. A reached unqueryable parser profile must retain the actual refusal and
absence of a native read call. OFF preserves the prior inventories.

These are source/configuration gates. No completed native, installed, archive,
sanitizer or platform qualification is asserted for this new module.

## Explicit native materialization profiles

The [repository workflow](.github/workflows/repository-checks.yml) selects explicit
Geometry and Interchange materialization test profiles. Every native build passes
`MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE=qualified` or `unsupported`;
the installed examples use the same value. The
[inventory checker](scripts/check-native-tests.py) verifies that the declared
profile matches both CMake caches, the actual configured compiler identity/version,
build configuration and complete native/installed test sets. No profile selects
an empty suite or silently skips unsupported numerical capture.

Surface-enabled native and sanitizer rows pin Geometry to
`b0935bc912dbc6b5c338652bf2991a2a721d6d94`, independently of its development
package version. The all-optional-OFF control retains the original
`48298b24be7c132816fb5580d22d58bde61558d0` producer and disables surface output.
The newer [compiled allocation-profile contract](https://github.com/Meshvale/meshvale-geometry/blob/b0935bc912dbc6b5c338652bf2991a2a721d6d94/docs/materialization.md#compiled-allocation-profiles)
owns the numerical admission tuple. Geometry is configured as a standalone
producer without the Interchange/vcpkg toolchain. Native lanes disable system
Eigen discovery so Geometry acquires its pinned official source dependency.

| Native row | Configuration and declared profile | Required inventory |
|---|---|---|
| Windows x64 MSVC | Release, qualified; files/surface ON | Geometry 19; Interchange 49; installed 25 |
| Windows x64 MSVC AddressSanitizer | Matching Release, qualified; files/surface ON | Geometry 19; Interchange 49; installed 25 |
| Linux x64 Clang 18.1.3 on Ubuntu 24.04 | Debug, qualified; files/surface ON | Geometry 19; Interchange 49; installed 25 |
| Linux GNU | Release, unsupported; files/surface ON | Geometry 14; Interchange 40; installed 19 |
| macOS AppleClang | Release, unsupported; files/surface ON | Geometry 14; Interchange 40; installed 19 |
| Windows x64 MSVC optional modules OFF | Release, qualified; writer/support/files/surface OFF | Geometry 16; Interchange 23; installed 12 |

These inventories include STL, OFF and PLY codec/request tests and their installed
consumers. Each row additionally builds the separate
[OFF Repair example](examples/off-repair-consumer/README.md) and
[PLY Repair example](examples/ply-repair-consumer/README.md), each with one required
CTest entry. CI pins public Repair
`99c2fb742dd16a7d147d7994a8db80088289505d`, builds only its native base and charged
duplicate targets with matching compiler/configuration/instrumentation, and
disables its glTF workflows and Python. Each example requires actual empty
read/Repair/write/reload in every profile; unsupported rows additionally require
the actual nonempty Geometry refusal. This dependency belongs to example
qualification, not the Interchange package.

Qualified rows include the focused materialization and independent semantic
suites. Unsupported rows exercise actual nonempty delegated unsupported-profile
outcomes, with an installed public fixture. Existing eleven native suites, six
installed consumers and lexical/accessor/Read/Write oracles remain required in
both profiles. The additional materialization oracle runs in qualified rows.
These are configured gates; only completed actual runs establish acceptance of
the candidate's implementation. The [draft transfer contract](docs/gltf-materialization.md)
owns interface availability and scope.

Native builds enable `MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE` and
`MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT` by default. The
[topology output contract](docs/gltf_topology_write.md#gtw-build-001-native-target-and-dependency)
owns its compiled Geometry sharing requirement and installed target. Configuration
checks callable `MaterializedMesh::ShareExecutionContext()` and
`SharesExecutionBudget()` symbols. Explicitly disabling both options keeps the
base capped reader/extractor and STL/OFF/PLY available, with qualified 23/installed 12
or unsupported 22/installed 12 tests. It installs neither optional target nor its
header. Workflow support requires the topology writer when enabled.

With the writer ON, qualified rows also require its focused request/worker/fault
suite, independent preservation/reload suite, public UINT32 index above2^24 case,
link consumer and installed operation consumer. The large-index test is required
and declares a 2048 MiB resource budget; CTest schedules it alone while its operation
still uses admitted workers. Unsupported rows add the real dependency link check
without pretending that nonempty numerical materialization is admitted. These
inventory definitions are configured requirements, not completed hosted proof.

With workflow support ON, qualified rows additionally require independent
six-phase output inspection, actual UINT32 inspection above2^24, an installed
link consumer and a public operation/lifetime consumer. Both large-index tests
use a 2048 MiB resource budget and run separately. Unsupported rows retain the
optional target link checks without claiming numerical-profile admission. The
[workflow support contract](docs/gltf-workflow-support.md) owns capped read and
extraction accounting, retained live counters and independent inspection scope.

Native rows use Ninja and record their compile-command inventories and test
results. Windows loads the installed Visual Studio x64 compiler environment.
Its AddressSanitizer row applies `/fsanitize=address /Zi` to Geometry, Interchange
and all installed consumers. The separate
[Windows sanitizer triplet](cmake/sanitizer-triplets/x64-windows-static-md-asan.cmake)
builds only the matching Release tinyobjloader library with dynamic CRT and static
library linkage; it does not qualify a Debug dependency or mixed CRT consumer.
Binary-cache reuse is disabled, and build/dependency concurrency is bounded to
two. This lane is separate from Linux ASan/UBSan and TSan.

The six ordinary Python rows retain the Release OBJ interface and matching
Geometry wheel/native prefix (`48298b24be7c132816fb5580d22d58bde61558d0`).
Python packaging explicitly disables the topology writer and surface output. These rows do not qualify a glTF Python operation,
charged transfer or Linux Release materialization profile. The separate portable
OBJ candidate workflow retains its own package/dependency preparation checks.

## Linux native sanitizer evaluation

The workflow defines separate Ubuntu 24.04 Linux x64 Clang 18.1.3 lanes for
AddressSanitizer plus UndefinedBehaviorSanitizer and for ThreadSanitizer. A
completed run is required to establish execution evidence; configuration alone
does not establish sanitizer coverage or platform support. The
[runner](scripts/run-native-sanitizers.py) requires clean exact Geometry, Repair
and vcpkg revisions matching the ordinary native job, without changing the Python
cohort. The pinned Repair revision is the example producer described above.

Each lane uses matching Debug compilation and sanitizer link/runtime flags for
Geometry, Interchange, the standalone Repair producer, all twenty-five installed
consumers and the separate OFF and PLY Repair examples. Geometry's owning
allocation-profile contract admits only its observed compiler, libc/libstdc++,
Eigen/alignment/SIMD and instrumentation tuple; a changed runner environment must
fail the qualified suite rather than relabel it unsupported. The ordinary native
and ASan/UBSan Geometry owners remain unoptimized. TSan uses exactly
`-fsanitize=thread -O1 -g -fno-omit-frame-pointer` with matching thread-sanitizer
link inputs. That optimization does not qualify optimized native or ASan builds.

The manifest selects double-precision tinyobjloader. The
[custom triplets](cmake/sanitizer-triplets/x64-linux-asan-ubsan.cmake) rebuild its
Debug and Release configurations with matching instrumentation; consumers select
Debug. Tracked environment flags participate in vcpkg's port ABI; binary-cache
reuse is disabled. Build concurrency is bounded to two. Actual compiled-command
inventories verify compiler, instrumentation and Meshvale warnings as errors.

Both lanes enable JSON/GLB, OFF and PLY files plus supplied surface output and
require Interchange's qualified 49 suites, twenty-five installed test entries and
the separate OFF and PLY Repair tests. Geometry requires 19 suites with allocation
probes enabled under ASan/UBSan and 16 with them disabled under TSan. TSan retains the actual
materialization worker/cancellation/partial-launch/shared-budget tests; an
unsupported fixture or serial allocation observation cannot substitute for them.
The runner executes independent lexical, accessor, actual Read/extraction,
Write/reload and materialization oracles. Failures propagate; there are no
sanitizer suppressions, retry-to-success or reduced oracle substitutions.

To run on the matching Linux x64 environment, prepare the workflow's exact public
Geometry/Repair/vcpkg checkouts and supply their locations:

```sh
python scripts/run-native-sanitizers.py --profile asan-ubsan --geometry-source "$GEOMETRY_SOURCE" --repair-source "$REPAIR_SOURCE" --vcpkg-root "$VCPKG_ROOT"
python scripts/run-native-sanitizers.py --profile tsan --geometry-source "$GEOMETRY_SOURCE" --repair-source "$REPAIR_SOURCE" --vcpkg-root "$VCPKG_ROOT"
```

Each profile requires a fresh `.local/native-sanitizers/<profile>` directory.
Commands, identities, compiler flags, test inventories, oracle observations and
CTest output remain ignored; hosted jobs upload separate artifacts, including
partial failure evidence. A completion marker is written only after every actual
command passes. Parser headers and Eigen code compile into instrumented Meshvale
targets. System libraries, thread/runtime internals and dependency tooling are
not comprehensively instrumented. Optional FBX/USD and Python bindings remain
disabled; these lanes do not qualify those SDKs, complete glTF semantics, original
allocation-request planning or an unexecuted architecture.
