---
name: wally-architecture
description: Where Wally logic belongs — command layering, proto as SOT, kit vs CLI ownership, Apple MLX host vs wally-cxx. Use when adding a command, moving inference logic, or deciding whether a bug is SDK or CLI.
---

# Wally architecture

Repo: `RunanywhereAI/RCLI`. Product CLI named `wally`. It consumes a **packaged
C++ desktop kit** via `find_package(RunAnywhere)`. It does not
`add_subdirectory` or FetchContent the SDK, and it does not compile llama.cpp /
Sherpa / ONNX / MLX from source.

Product version (`project(wally VERSION …)` in `CMakeLists.txt`) is independent
of the SDK kit pin in `cmake/sdk-pin.cmake`.

## Ownership

```text
argv / flags / env
  -> src/commands/cmd_*.cpp     thin: parse → bootstrap() → one rac_* → render
  -> C++ desktop kit            catalog, download, lifecycle, generate, serve
  -> engines (in the kit)       llama.cpp, Sherpa, ONNX, MLX (Apple host);
                                NeuRT / QHexRT only when the private overlay
                                was applied at configure time
```

The kit owns truth: models, backends, proto contracts, download, inference.
The CLI renders and interacts. If a command is composing a multi-step bootstrap,
hardcoding an engine name, or post-processing model output, that is a bug in the
SDK — fix it there, then consume a new kit (**wally-kit-pin**).

## Layering rules

- Command TUs stay thin. Business rules do not live in CLI11 callbacks, Swift,
  or the REPL.
- **Proto is the SOT.** Include kit `include/runanywhere/proto/*.pb.h` (same
  protoc that built commons). Do not run `protoc` here. Do not compile `*.pb.cc`
  (those objects are already inside `librac_commons.a`). Parse `rac_*` byte
  buffers with `src/io/proto.h` into `runanywhere::v1::*`.
- No parallel hand-written enums for values that exist in `idl/*.proto`.
- Structured errors. Machine-readable codes from the ABI; human text on stderr.
  Results on stdout. `--json` prints exactly one document on stdout.
- Never log API keys, tokens, or Authorization headers.

`RunAnywhere::commons` applies `google=runanywhere_internal` when the kit was
built with namespace isolation. Never `find_package(Protobuf)` against Homebrew.

## Command surface

Dual grammar: spec namespaces (`llm generate`, `models download`) plus terminal
aliases (`run`, `pull`, `stt`). One `configure_*` wires both
(`src/commands/commands.h`).

Do not reintroduce FetchContent of the SDK, a second inference backend tree, or
a retired MetalRT / hardcoded catalog.

## Apple MLX host

On Apple Silicon, `cmake --build` produces `build/wally` (Swift host wrapping
`wally_run_main`). Users never run `wally-cxx`; that name exists only so CMake
cannot overwrite the product binary. Independent clones set `WALLY_SDK_SWIFT_PATH`
to a runanywhere-sdks checkout (CI does this). Nested `EXTERNAL/Wally` finds
`../../Package.swift` automatically. Disable with `-DWALLY_APPLE_MLX_HOST=OFF`
only for a C++-only compile loop.

NeuRT image gen is `#if WALLY_HAS_NEURT` in `src/commands/cmd_image.cpp`, which
is true only when the NeuRT overlay is applied. Public bottles stay OSS.
`--engine qhexrt` / `qnn` / `npu` / `hexagon` map to
`INFERENCE_FRAMEWORK_QHEXRT`. Local HNPU trees are inferred from `v75`/`v79`/
`v81`, `context.bin`, or `*_HNPU` directory names.

## Skills trees

Canonical: `.claude/skills/`. Mirror: `.agents/skills/`. Edit canonical, then
`bash scripts/ci/check-agents-sync.sh --fix`. Never hand-edit the mirror.
`CLAUDE.md` is a symlink to `AGENTS.md`.
