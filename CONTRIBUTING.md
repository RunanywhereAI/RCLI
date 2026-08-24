# Contributing to rcli

`rcli` is the desktop CLI for RunAnywhere. Inference, catalog, download, and
lifecycle live in the **C++ desktop kit** (`find_package(RunAnywhere)`). This
repo is argv, terminal I/O, and the Apple MLX host.

Do not point CMake at SDK source. Do not FetchContent or `add_subdirectory` the
SDK. If a command needs a sequence the public `rac_*` ABI does not offer, the
fix belongs in the SDK, then a new kit — not a workaround here.

## Prerequisites

- CMake 3.24+, a C++20 compiler
- A staged kit matching `cmake/sdk-pin.cmake` (`RCLI_PINNED_SDK_VERSION`)
- Xcode 26+ only if you are linking the shipping Apple binary (`scripts/build-mlx.sh`)

Build a kit from a runanywhere-sdks checkout:

```bash
cmake --preset cpp-desktop-macos-arm64
cmake --build --preset cpp-desktop-macos-arm64 --target package-cpp-desktop \
  -j "$(sysctl -n hw.logicalcpu)"
```

The prefix is `dist/cpp-desktop-macos-arm64` in that checkout.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/cpp-desktop-macos-arm64
cmake --build build -j "$(sysctl -n hw.logicalcpu)"
./build/rcli-cxx version    # Apple: cmake output is rcli-cxx
./build/rcli-cxx backends
```

`RCLI_SDK_DIR` / `RCLI_SDK_KIT` is an alias for that **kit prefix**. Pointing it
at a source tree is a configure error.

On Apple, the binary that ships is the Swift MLX host:

```bash
scripts/build-mlx.sh
```

That produces `build/rcli` and must not be replaced by a later `cmake --build`
(`rcli-cxx` is the CMake name so the two cannot clobber each other).

## Tests

```bash
cmake --build build --target test_rcli_unit
ctest --test-dir build -R rcli --output-on-failure
bash scripts/smoke.sh ./build/rcli-cxx
```

## Layout

```
src/commands/     one file per command; parse → bootstrap() → one rac_* call
src/catalog/      thin helpers over commons catalog APIs
src/repl/         linenoise REPL
include/          rcli_run_main for the Swift host
cmake/sdk-pin.cmake   EXACT kit version (+ sha256 once kits are published)
third_party/      CLI11 + linenoise, vendored
swift/            Apple entry: register MLX, then rcli_run_main
```

## Adding a command

1. Add `src/commands/cmd_yours.cpp` and declare it in `src/commands/commands.h`.
2. Register it from `src/app.cpp`.
3. Add the file to `CMakeLists.txt`.
4. Keep the TU thin. No engine names, path patterns, or multi-step orchestration.

## Pull requests

Branch off `main`. `cmake --build` must stay green. On Apple, if you touch the
host or link line, run `scripts/build-mlx.sh` as well.
