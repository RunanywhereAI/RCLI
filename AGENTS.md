# AGENTS.md — RCLI (product CLI)

This repository is the official `rcli`. It consumes a **packaged C++ desktop
kit** via `find_package(RunAnywhere)`. It does not `add_subdirectory` or
FetchContent the SDK, and it does not compile llama.cpp / Sherpa / ONNX / MLX
from source.

Pin: `cmake/sdk-pin.cmake`. Prefix: `-DCMAKE_PREFIX_PATH=` or `-DRCLI_SDK_KIT=`.

Pointing `RCLI_SDK_DIR` at SDK **source** is a configure error.

## Layering

Command files (`src/commands/cmd_*.cpp`) stay thin: parse → `bootstrap()` →
**one** `rac_*` call → render. Catalog, download, lifecycle, and generate live
in commons. If the CLI is composing a multi-step bootstrap or hardcoding an
engine name, that is a bug in the SDK — fix it there, then consume a new kit.

Apple shipping binary is the Swift MLX host (`scripts/build-mlx.sh`) wrapping
`rcli_run_main`. CMake output on Apple is `rcli-cxx`.

Public headers: kit `include/rac/**`. **Proto types are the SOT** — include
kit `include/runanywhere/proto/*.pb.h` (generated with the same protoc that
built commons). Do not run `protoc` in this repo and do not compile `*.pb.cc`
(those objects are already inside `librac_commons.a`). Command files parse
`rac_*` byte buffers into `runanywhere::v1::*` messages via `io/proto.h`.

Consistency with the SDK is `idl/SCHEMA_LOCK`, copied into the kit and pinned
here as `RCLI_PINNED_IDL_SCHEMA_SHA256` in `cmake/sdk-pin.cmake`. Configure
fails if the kit's lock does not match. When the schema changes, consume a
new kit and bump the pin — never regenerate headers locally.

The kit's `RunAnywhere::commons` INTERFACE applies `google=runanywhere_internal`
so those headers match the isolated runtime linked from the archive. Never
`find_package(Protobuf)` against Homebrew for this binary.

## Command surface

Dual grammar: spec namespaces (`llm generate`, `models download`) plus
terminal aliases (`run`, `pull`, `stt`). One `configure_*` wires both.
See `src/commands/commands.h`.

Retired MetalRT / Mac-only tool-calling / hardcoded 486-entry catalog from the
pre-kit RCLI must stay deleted. Do not reintroduce FetchContent of the SDK,
`src/tools/shell`, or a second inference backend tree.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/cpp-desktop-<os>-<arch>
cmake --build build -j "$(sysctl -n hw.logicalcpu)"
```

NeuRT and QHexRT are optional private packs (`NEURUN_TOKEN`), never required
to configure or link the public bottle. QHexRT is not shipped for Windows x64.
