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

## Explicit native materialization profiles

The [repository workflow](.github/workflows/repository-checks.yml) selects explicit
Geometry and Interchange materialization test profiles. Every native build passes
`MESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE=qualified` or `unsupported`;
the installed examples use the same value. The
[inventory checker](scripts/check-native-tests.py) verifies that the declared
profile matches both CMake caches, the actual configured compiler identity/version,
build configuration and complete native/installed test sets. No profile selects
an empty suite or silently skips unsupported numerical capture.

Native and sanitizer rows pin Geometry to
`48298b24be7c132816fb5580d22d58bde61558d0`, independently of its development
package version. Its [compiled allocation-profile contract](https://github.com/Meshvale/meshvale-geometry/blob/48298b24be7c132816fb5580d22d58bde61558d0/docs/materialization.md#compiled-allocation-profiles)
owns the numerical admission tuple. Geometry is configured as a standalone
producer without the Interchange/vcpkg toolchain. Native lanes disable system
Eigen discovery so Geometry acquires its pinned official source dependency.

| Native row | Configuration and declared profile | Required inventory |
|---|---|---|
| Windows x64 MSVC | Release, qualified; file module ON | Geometry 16; Interchange 33; installed 16 |
| Windows x64 MSVC AddressSanitizer | Matching Release, qualified; file module ON | Geometry 16; Interchange 33; installed 16 |
| Linux x64 Clang 18.1.3 on Ubuntu 24.04 | Debug, qualified; file module ON | Geometry 16; Interchange 33; installed 16 |
| Linux GNU | Release, unsupported; file module ON | Geometry 13; Interchange 24; installed 12 |
| macOS AppleClang | Release, unsupported; file module ON | Geometry 13; Interchange 24; installed 12 |
| Windows x64 MSVC optional modules OFF | Release, qualified; writer/support/files OFF | Geometry 16; Interchange 16; installed 8 |

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
base capped reader/extractor available, with qualified 16/installed 8 or
unsupported 15/installed 8 tests. It installs neither optional target nor its
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
Geometry wheel/native prefix (`48bb348112f49a07aec03bc69b9875ea6b62128a`).
Python packaging explicitly disables the topology writer. These rows do not qualify a glTF Python operation,
charged transfer or Linux Release materialization profile. The separate portable
OBJ candidate workflow retains its own package/dependency preparation checks.

## Linux native sanitizer evaluation

The workflow defines separate Ubuntu 24.04 Linux x64 Clang 18.1.3 lanes for
AddressSanitizer plus UndefinedBehaviorSanitizer and for ThreadSanitizer. A
completed run is required to establish execution evidence; configuration alone
does not establish sanitizer coverage or platform support. The
[runner](scripts/run-native-sanitizers.py) requires clean exact Geometry and vcpkg
revisions matching the ordinary native job, without changing the Python cohort.

Each lane uses matching Debug compilation and sanitizer link/runtime flags for
Geometry, Interchange and all sixteen installed consumers. Geometry's owning
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

Both lanes enable JSON/GLB files and require Interchange's qualified 33 suites and sixteen installed test
entries. Geometry requires 16 suites with allocation probes enabled under
ASan/UBSan and 13 suites with them disabled under TSan. TSan retains the actual
materialization worker/cancellation/partial-launch/shared-budget tests; an
unsupported fixture or serial allocation observation cannot substitute for them.
The runner executes independent lexical, accessor, actual Read/extraction,
Write/reload and materialization oracles. Failures propagate; there are no
sanitizer suppressions, retry-to-success or reduced oracle substitutions.

To run on the matching Linux x64 environment, prepare the workflow's exact public
Geometry/vcpkg checkouts and supply their locations:

```sh
python scripts/run-native-sanitizers.py --profile asan-ubsan --geometry-source "$GEOMETRY_SOURCE" --vcpkg-root "$VCPKG_ROOT"
python scripts/run-native-sanitizers.py --profile tsan --geometry-source "$GEOMETRY_SOURCE" --vcpkg-root "$VCPKG_ROOT"
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
