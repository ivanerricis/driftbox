# Development guide

## Setup

Windows: run `scripts/dev/setup-windows.ps1` to check tools (Git, CMake, Visual Studio, Ninja, clang-format) and set `CPM_SOURCE_CACHE`. On macOS/Linux install CMake, Ninja and a C++20 compiler.

Keep the checkout path short and share dependencies via `CPM_SOURCE_CACHE` (for example `C:\dev\.cpm`). With git worktrees each worktree has its own `build/` but reuses the cache.

## Build and test

```
cmake --preset win-vs            # or win-ninja, mac-universal, linux
cmake --build --preset win-vs-release
ctest --preset win-vs-release
```

Debug build: `cmake --build --preset win-vs-debug`. Presets set `CMAKE_POLICY_VERSION_MINIMUM=3.5` so newer CMake versions accept older dependencies.

## Validating the plugin

`scripts/dev/run-pluginval.ps1` downloads pluginval (once) and validates a built VST3 at strictness 10:

```
powershell -File scripts/dev/run-pluginval.ps1 -Vst3 path\to\Driftbox.vst3
```

On macOS also run `auval -v aufx Drbx Ivnr` (the AU must be installed in `~/Library/Audio/Plug-Ins/Components`); for CLAP use clap-validator. CI runs all of these.

## Formatting and lint

`clang-format` (v18 in CI) with `.clang-format`. See `CONTRIBUTING.md`.

`clang-tidy` (job `clang-tidy`) is enforced: any warning in project code (`src/`) fails the job (`--warnings-as-errors='*'`). The checks are listed in `.clang-tidy`; `performance-enum-size` is disabled on purpose because several enums are frozen contracts. It needs a compile database plus the generated binary-data headers (`DriftboxUiData.h`, `DriftboxPresetData.h`). Build the targets `DriftboxUiAssets` and `DriftboxPresetData` (`juce_add_binary_data` targets, not part of any preset) before running it, as the CI job does:

```
cmake --preset ci-linux -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DDRIFTBOX_BUILD_TESTS=OFF
cmake --build build/ci-linux --target DriftboxUiAssets DriftboxPresetData
git ls-files 'src/*.cpp' | xargs clang-tidy-18 -p build/ci-linux --quiet --warnings-as-errors='*' --header-filter="^$PWD/src/.*"
```

## Sanitizers

The `ci-linux-asan` preset builds a Debug configuration with AddressSanitizer and UndefinedBehaviorSanitizer (JUCE `jassert`s stay active). The CI job `asan-ubsan` runs the test binary under `xvfb-run` with `~[allocation]~[perf]`: the allocation tests replace the global `operator new`/`delete` and conflict with ASan, and the `[perf]` CPU-budget test measures wall-clock time, which the sanitizers inflate (it still runs in the Release builds). ctest cannot filter on Catch2 tags, so the binary is started directly. The job is enforced (required check): findings halt the run (`halt_on_error=1`) and fail the job.

```
cmake --preset ci-linux-asan
cmake --build --preset ci-linux-asan --parallel
build/ci-linux-asan/<path>/DriftboxTests '~[allocation]~[perf]'
```

## macOS in CI

macOS is built only on pushes to `main`, manual runs, or pull requests labelled `ci:macos` (macOS runners are slow and queue-limited). Add the label when a change touches platform-specific code.

## Packaging

`scripts/ci/package.sh <windows|macos|linux> <build_dir> <out_dir>` produces `Driftbox-v<version>-windows.zip`, `-macos.zip` (VST3, CLAP and AU, ad-hoc signed) or `-linux.tar.gz` (VST3 and CLAP). There is no Standalone. For example, after a `win-vs` build: `bash scripts/ci/package.sh windows build/win-vs dist` (`dist/` is git-ignored). CI uploads each package as the artifact `driftbox-<platform>`.

## Architecture

`docs/ARCHITECTURE.md` describes the DSP chain, the parameters, state versioning, undo and the frozen C++ contracts. `docs/PLAN.md` holds the work plan. Parameter IDs are immutable (see `CONTRIBUTING.md`).
