# revng C++ SDK (Experimental)

This repository contains an **experimental, embeddable C++ SDK** for driving
revng’s lifting, analysis, and decompilation workflows from your own C++ code,
without depending on the full “official tooling” UX.

The intent is: **hack directly on the real lifter + analyses + decompiler**,
and still have a reasonably sane way to build and consume those pieces as a
library.

The SDK surface in this fork lives in:

- `include/revng/SDK/Decompile.h` (public API)
- `lib/SDK/Decompile.cpp` (implementation)
- `tools/sdk/Main.cpp` (a minimal reference CLI: `sdk-decompile`)

This is intentionally **not** a narrow facade. It is a thin convenience layer
on top of revng’s real building blocks:

- the **model** (`include/revng/Model/*`, `lib/Model/*`)
- the **pipeline system** (`include/revng/Pipeline/*`, `include/revng/Pipes/*`)
- the **lifter** (TCG/QEMU based) that produces **LLVM IR** (`lib/Lift/*`)
- the analysis / transform passes (many under `lib/*`)
- the decompiler backend (PTML and plain-C outputs) (`lib/Backend/*`)

## How revng works (high level)

At a very high level, a typical workflow is:

1. **Import** a binary into a structured *model* (segments, functions, symbols,
   optional debug info).
2. **Lift** code to **LLVM IR** using QEMU’s TCG frontends (via revng’s `libtcg`
   runtime).
3. Run a **pipeline** of analyses/transforms. Pipelines and steps are described
   in YAML, and many analyses are shipped as plugin `.so` libraries.
4. Produce artifacts, such as:
   - function IR containers (`functions.bc.zstd`)
   - CFG dumps (`cfg.yml.tar.gz`)
   - per-function decompiled output (PTML archive)
   - a single-file “recompilable archive” (best-effort C)

The pipeline is configured by YAML:

- `share/revng/pipelines/revng-pipelines.yml`

## Supported inputs and architectures (practical view)

From the importer sources and the shipped pipelines:

- Binary formats: **ELF**, **PE/COFF**, **Mach-O**
- Debug info: **DWARF**, **CodeView/PDB**
- Architectures (model/runtime): `x86`, `x86_64`, `arm`, `aarch64`, `mips`,
  `mipsel`, `systemz` (s390x)

Notes:

- “Importer supports X” is not the same as “full lift + analyze + decompile
  pipeline is polished for X”. For non-ELF formats you should expect to do
  some work.
- The current SDK/CLI assumes a Linux-style deployment model:
  - `.so` plugin loading via `dlopen`
  - `libtcg-*.so` at runtime
  - `FilePath::fromLocalStorage()` is Unix-centric today

## Inventory: what you likely want to keep/build for an SDK

If your goal is “use revng as a library and hack on lifting + analyses +
decompilation”, the core pieces are:

- `lib/Model`: model types, verification, serialization, importer plumbing.
- `lib/Pipeline` + `lib/Pipes`: pipeline runner/container infrastructure.
- `lib/Lift`: lifting to LLVM IR (TCG/QEMU based).
- `lib/EarlyFunctionAnalysis`: CFG recovery and early per-function analyses.
- `lib/FunctionIsolation`: isolate functions into per-function LLVM modules and
  enforce ABI (important for function-scoped workflows).
- `lib/RemoveLiftingArtifacts`, `lib/Canonicalize`, `lib/RestructureCFG`, etc:
  IR cleanup/transforms used by the decompiler.
- `lib/Backend`: decompiler backend and output producers.
- `lib/revng/analyses/*.so`: the analysis plugins expected by the default
  pipelines and analyses lists.

Optional / often-not-needed for a lean SDK:

- `ImportFromC` / “clift” / MLIR-dependent experiments
- TypeScript and most Python packaging glue
- docs site and “full distribution” packaging targets

## Building a lean, hackable SDK

revng is LLVM-heavy. The goal here is not “make it tiny”, but “make it
reasonable to build + iterate + embed”.

This fork adds `REVNG_SDK_MODE=ON`, which defaults to disabling a bunch of
non-core targets and enables a “use prebuilt runtime resources” flow.

### Prereqs (Fedora-ish)

- C++ toolchain: `clang`, `clang++`, `cmake`, `ninja`
- LLVM development packages matching what revng expects
- Python 3 + `uv` (revng code generators run via `uv run --with` and do not
  install deps into your user/system env)
- runtime libs used by revng (zstd, libarchive, etc.)

If you see `cannot allocate memory in static TLS block` while dlopening
`libtcg-*.so`, this fork can early-link `jemalloc` to avoid the glibc static TLS
failure:

- CMake option: `REVNG_LINK_JEMALLOC_EARLY=ON` (default in this fork)
- Override path if needed: `-DREVNG_JEMALLOC_LIBRARY=/path/to/libjemalloc.so.2`

### Recommended: reuse runtime resources from the official docker image

revng’s official distribution includes a runtime prefix at `/revng/root`
(helper bitcode, support modules, prebuilt `libtcg-*.so`, headers, etc).

This fork supports using those as *prebuilt runtime resources* so you can:

- avoid rebuilding large “runtime resource” artifacts from source
- reuse the prebuilt `libtcg-*.so` that matches the pipelines

Use:

```bash
./scripts/extract-revng-runtime-from-docker.sh <image> <out_dir>
```

`<out_dir>` becomes your `REVNG_RUNTIME_PREFIX`.

Practical note: the extracted prefix is large; put it somewhere on disk
(e.g. `/var/tmp/...`) and avoid `/tmp` if it is tmpfs on your system.

### Configure (Clang recommended)

Example (tweak paths):

```bash
cmake -S . -B build-sdk-cpp-clang -GNinja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DREVNG_SDK_MODE=ON \
  -DREVNG_BUILD_TOOLS=ON \
  -DREVNG_USE_PREBUILT_RUNTIME_RESOURCES=ON \
  -DREVNG_COPY_EXTERNAL_RUNTIME_TO_BUILD_TREE=ON \
  -DREVNG_RUNTIME_PREFIX=/path/to/extracted/revng/root
```

Important knobs:

- `REVNG_SDK_MODE=ON`: defaults to disabling docs/tests/typescript/python
  packaging/import-from-c/pipeline-C/clift, and defaults to using prebuilt
  runtime resources.
- `REVNG_USE_PREBUILT_RUNTIME_RESOURCES=ON`: copies runtime resources from
  `REVNG_RUNTIME_PREFIX/share/revng/` into the build tree.
- `REVNG_COPY_EXTERNAL_RUNTIME_TO_BUILD_TREE=ON`: copies `libtcg-*.so` from
  `REVNG_RUNTIME_PREFIX/lib{,64}/` into `build/.../lib/` so running from the
  build dir works without extra setup.
- `REVNG_USE_UV=ON` (default): uses `uv run --with ...` to run code generators.

### Build

Minimal SDK deliverables:

- `revngSDK`: shared library (C++ SDK surface)
- `sdk-decompile`: reference CLI built on top of the SDK

Build the CLI:

```bash
ninja -C build-sdk-cpp-clang sdk-decompile
```

Notes:

- `sdk-decompile` loads analysis plugins (`lib/revng/analyses/*.so`) from disk.
  In this fork, `ninja sdk-decompile` also builds the `analyses` target to keep
  those plugins in sync.
- The binary ends up at `build-sdk-cpp-clang/libexec/revng/sdk-decompile`.

## Running the reference CLI: `sdk-decompile`

`sdk-decompile` is a thin wrapper around the C++ SDK:

- loads the input binary into the pipeline “input” container
- runs an initial analyses list (default: `revng-initial-auto-analysis`)
- runs one pipeline step to produce an artifact
- stores the artifact to `-o <path>`

### Basic usage

Show options:

```bash
build-sdk-cpp-clang/libexec/revng/sdk-decompile --help
```

SDK-specific options (implemented by `tools/sdk/Main.cpp`, shown under the
`SDK Options` category in `--help`):

- `--pipeline <path>`: override the pipelines YAML (default: the one under the
  discovered root).
- `--analyses-dir <dir>`: override the directory containing analysis plugins
  (`*.so`).
- `--step <name>`: pipeline step name to run to produce the requested artifact
  (default: `emit-recompilable-archive`).
- `--analysis-scope=whole|selected`: when `--function-entry` is present, control
  whether function-scoped analyses in the initial analyses list run on the
  whole program or only the selected functions.
- `--function-entry <addr>`: select one or more functions by entry (repeatable).
- `--execdir <dir>`: execution directory used to cache/resume pipeline state.
- `--resource-root <prefix>`: add extra resource roots searched for
  `share/revng/...` (repeatable).

Decompile to a “recompilable archive” (best-effort single-file C in a tar.gz):

```bash
TMPDIR=/var/tmp build-sdk-cpp-clang/libexec/revng/sdk-decompile \
  -o /var/tmp/out.tar.gz \
  /path/to/binary
```

Default step is `--step=emit-recompilable-archive`. The output archive contains:

- `decompiled/functions.c`
- `decompiled/types-and-globals.h`
- `decompiled/helpers.h`
- `decompiled/attributes.h`
- `decompiled/primitive-types.h`

Validate that the produced C at least compiles:

```bash
mkdir -p /var/tmp/revng-out
tar -xzf /var/tmp/out.tar.gz -C /var/tmp/revng-out
clang -std=c11 -ffreestanding -I/var/tmp/revng-out/decompiled \
  -c /var/tmp/revng-out/decompiled/functions.c
```

### Discovering available `--step` names

The `--step` value is a pipeline step name defined in the pipelines YAML.
Without using any additional tooling, you can discover step names by reading:

- `share/revng/pipelines/revng-pipelines.yml`

and searching for `steps:` / step `name:` entries.

### Selecting functions by entry address (`--function-entry`)

You can restrict work to one or more functions with `--function-entry`.
Each entry can be:

- a serialized MetaAddress string: `0x401000:Code_x86_64`
- or a raw PC address: `0x401000` (interpreted using the imported architecture)

Example (single function):

```bash
TMPDIR=/var/tmp build-sdk-cpp-clang/libexec/revng/sdk-decompile \
  --function-entry 0x400480 \
  -o /var/tmp/one_function.tar.gz \
  /path/to/binary
```

Behavior when `--function-entry` is present:

- For function-scoped artifacts (e.g. `--step=decompile`), only those functions
  are requested/produced.
- For `--step=emit-recompilable-archive` (default), the SDK produces a **partial**
  recompilable archive containing only the selected functions.

### PIE / base address gotcha (ELF)

On Linux, many `/usr/bin/*` are PIE (ELF type `DYN`). `readelf` shows an
entrypoint that is **relative** to the image base:

```bash
readelf -h /path/to/binary | rg 'Type:|Entry point address:'
```

revng’s importer uses a deterministic base address by default:

- `--base=0x400000` (default)

So for a PIE binary where `readelf` says `Entry point address: 0x8360`, the
function-entry you want is typically:

- `0x400000 + 0x8360 = 0x408360`

You can either:

- pass the computed address to `--function-entry`, or
- change the importer base with `--base=<addr>` and compute addresses relative
  to that base.

### Controlling analysis cost: `--analysis-scope`

Even if you select one function, the default behavior is still to run the
initial analyses list on the whole program:

- `--analysis-scope=whole` (default)

For faster iteration, you can restrict *function-scoped* analyses in the initial
analyses list to just the selected functions:

```bash
TMPDIR=/var/tmp build-sdk-cpp-clang/libexec/revng/sdk-decompile \
  --analysis-scope=selected \
  --function-entry 0x400480 \
  -o /var/tmp/one_function_fast.tar.gz \
  /path/to/binary
```

Tradeoff: some analyses (notably data-layout/type recovery) are interprocedural,
so restricting them can reduce the quality/precision of recovered types.

### Producing PTML (per-function decompiled output)

```bash
TMPDIR=/var/tmp build-sdk-cpp-clang/libexec/revng/sdk-decompile \
  --step=decompile \
  --function-entry 0x400480 \
  -o /var/tmp/decompiled_ptml.tar.gz \
  /path/to/binary
```

This produces an archive whose members are `*.c.ptml` files (one per function).

### Caching / resume: `--execdir`

If you provide `--execdir`, the pipeline state is cached and the SDK will call
`PipelineManager::store()` at the end of the run:

```bash
TMPDIR=/var/tmp build-sdk-cpp-clang/libexec/revng/sdk-decompile \
  --execdir /var/tmp/revng-execdir \
  -o /var/tmp/out.tar.gz \
  /path/to/binary
```

Useful related options:

- `--check-components-version`: invalidate cached containers when component
  hashes change (helpful while hacking).
- `--save-after-every-analysis`: store the execdir after each analysis in the
  initial analyses list (more I/O, but safer for long runs).

### Importer knobs (layout + debug info)

`sdk-decompile` also registers the global revng command-line options used by
the binary importer, which are useful for non-default layouts:

- `--base=<addr>`: base address where dynamic objects (PIE, shared libs) are
  loaded. Default is `0x400000`.
- `--debug-info=no|yes|ignore-libraries`: control how much debug info is
  imported.
- `--import-debug-info=<path>`: add extra search paths/files for debug info.

## Using the C++ SDK (`revng::sdk`)

Public entry points are declared in `include/revng/SDK/Decompile.h`.

### What you get

The SDK layer provides:

- `revng::sdk::PipelineConfig`: configuration for pipelines, plugin loading,
  selected functions, and artifact selection.
- `revng::sdk::createPipelineManager`: construct a `pipes::PipelineManager`
  with registries initialized and (optionally) analysis plugins loaded.
- `revng::sdk::produceArtifact`: convenience wrapper that:
  - loads the input binary into the “input” container
  - runs an analyses list (default: `revng-initial-auto-analysis`)
  - runs a pipeline step (default: `emit-recompilable-archive`)
  - stores the resulting artifact container to a `revng::FilePath`

### Minimal example: produce an artifact

```cpp
#include "llvm/Support/Error.h"
#include "revng/SDK/Decompile.h"
#include "revng/Storage/Path.h"

int main() {
  revng::sdk::PipelineConfig cfg;
  cfg.ArtifactStep = "emit-recompilable-archive";
  cfg.ExecutionDirectory = "/var/tmp/revng-execdir";
  cfg.FunctionEntries = { "0x400480" }; // optional
  cfg.RestrictInitialAnalysesToSelectedFunctions = true; // optional

  revng::FilePath out =
    revng::FilePath::fromLocalStorage("/var/tmp/out.tar.gz");

  if (llvm::Error err = revng::sdk::produceArtifact("/path/to/bin", out, cfg)) {
    llvm::consumeError(std::move(err));
    return 1;
  }
  return 0;
}
```

### Advanced usage: drive the pipeline yourself

If you want to use revng as an analysis engine (not just “emit an artifact”),
start from:

- `revng::sdk::createPipelineManager()`
- `revng/Pipes/PipelineManager.h`
- `revng/Pipeline/Runner.h`
- `revng/Pipeline/Target.h`

The SDK helper `createPipelineManager()` also:

- ensures runtime deps needed by dlopen’ed components are part of the initial
  load set (`revngRuntimeDeps`)
- optionally loads analysis plugins from disk
- runs all registry initialization routines (`pipeline::Registry::*`)

At that point you can:

- run pipeline steps with selected targets (per-function or whole-binary)
- access intermediate containers (LLVM modules, CFG maps, YAML globals)
- register your own pipes/analyses/containers and add them to a pipeline YAML

### Consuming from another CMake project

There are two practical ways to consume this as “an SDK”:

1. As a subproject (recommended while hacking):
   - add revng as a git submodule
   - `add_subdirectory(revng)`
2. As an installed package:
   - `cmake --install` revng into a prefix
   - `find_package(revng CONFIG REQUIRED)` in the consumer

As a subproject, you typically want to force SDK-friendly options before calling
`add_subdirectory`:

```cmake
set(REVNG_SDK_MODE ON CACHE BOOL "" FORCE)
set(REVNG_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(REVNG_USE_PREBUILT_RUNTIME_RESOURCES ON CACHE BOOL "" FORCE)
set(REVNG_COPY_EXTERNAL_RUNTIME_TO_BUILD_TREE ON CACHE BOOL "" FORCE)
set(REVNG_RUNTIME_PREFIX "/path/to/extracted/revng/root" CACHE PATH "" FORCE)

add_subdirectory(external/revng)

add_executable(mytool main.cpp)
target_link_libraries(mytool PRIVATE revngSDK)
```

As an installed package:

```cmake
find_package(revng CONFIG REQUIRED)
add_executable(mytool main.cpp)
target_link_libraries(mytool PRIVATE revngSDK)
```

Note: `revngSDK` is a convenience layer. If you want to call deeper APIs
directly (lifter, backend, model, pipeline internals), you can also link against
the internal libraries (`revngLift`, `revngBackend`, `revngModel`, ...), which
are what the SDK ultimately uses.

If installed, this fork generates a CMake package config:

- `share/revng/cmake/revngConfig.cmake` (and `revng.cmake` targets export)

It also defines convenience variables:

- `revng_ROOT`, `revng_SHARE_DIR`, `revng_PIPELINES_YAML`, `revng_ANALYSES_DIR`

## Extensibility: where to hook in

revng is modular around the pipeline system:

- pipelines are described in YAML and loaded by `pipeline::Loader`
  (`include/revng/Pipeline/Loader.h`)
- pipes and analyses register themselves into registries at startup
- many analyses are built as plugin `.so` libraries under `lib/revng/analyses/`

“Custom loader for weird memory maps/layouts” usually means:

- construct/import a `model::Binary` with the segments/functions you want, or
- add a new import/loader analysis/pipe that constructs the model the way you
  need and add it to a pipeline

## Troubleshooting notes

- If `/tmp` is tmpfs, large runs can fail due to space. Prefer:
  - `TMPDIR=/var/tmp` (or similar)
  - `--execdir /var/tmp/...`
  - output `-o /var/tmp/...`
- If analysis plugins are stale/missing, make sure the build produced them:
  - `ninja -C <build> analyses`
  - (in this fork `sdk-decompile` already depends on `analyses`)
- If `dlopen` of `libtcg` fails with static TLS errors:
  - enable `REVNG_LINK_JEMALLOC_EARLY=ON`
  - or install jemalloc and point `REVNG_JEMALLOC_LIBRARY` at it

## Known issues / TODOs

- Output C is best-effort decompiler output: expect backend corner cases on
  real binaries.
- Cross-platform work is incomplete. Plugin loading and runtime prefix layout
  are Linux-centric today.
- The SDK API is experimental and expected to evolve as we factor out a clean
  library surface (no stability guarantees yet).
