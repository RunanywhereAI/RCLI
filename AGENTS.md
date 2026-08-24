# AGENTS.md — RCLI

> **`AGENTS.md` is the real file.** `CLAUDE.md` is a committed symlink to it.
> Editing either name edits the same bytes. `bash scripts/ci/check-agents-sync.sh --fix`
> recreates the link and mirrors `.claude/skills/` → `.agents/skills/`. CI fails
> if the pair drifts.

This repository is the official `rcli` product CLI. It consumes a **packaged
C++ desktop kit** via `find_package(RunAnywhere)`. It does not `add_subdirectory`
or FetchContent the SDK, and it does not compile llama.cpp / Sherpa / ONNX / MLX
from source.

Pin: `cmake/sdk-pin.cmake`. Prefix: `-DCMAKE_PREFIX_PATH=` or `-DRCLI_SDK_KIT=`.
Pointing `RCLI_SDK_DIR` at SDK **source** is a configure error.

`BEST_PRACTISES.md` / `BEST_PRACTICES.md` (if present) is a local playbook and
must stay gitignored. The rules below are the subset that applies to this CLI.

## Ownership model

```text
argv / flags / env
  -> src/commands/cmd_*.cpp     thin: parse → bootstrap() → one rac_* → render
  -> C++ desktop kit            catalog, download, lifecycle, generate, serve
  -> engines (in the kit)       llama.cpp, Sherpa, ONNX, MLX (Apple host)
```

The kit owns truth: models, backends, proto contracts, download, inference.
The CLI renders and interacts. If a command is composing a multi-step bootstrap,
hardcoding an engine name, or post-processing model output, that is a bug in the
SDK — fix it there, then consume a new kit.

## Layering

- Command TUs stay thin. Business rules do not live in CLI11 callbacks, Swift,
  or the REPL.
- **Proto is the SOT.** Include kit `include/runanywhere/proto/*.pb.h` (same
  protoc that built commons). Do not run `protoc` here. Do not compile `*.pb.cc`
  (those objects are already inside `librac_commons.a`). Parse `rac_*` byte
  buffers with `src/io/proto.h` into `runanywhere::v1::*`.
- Typed contracts at every boundary: CLI11 → proto messages → `rac_*` → stdout.
  No parallel hand-written enums for values that exist in `idl/*.proto`.
- Structured errors. Machine-readable codes from the ABI; human text on stderr.
  Results on stdout. `--json` prints exactly one document on stdout.
- Never log API keys, tokens, or Authorization headers. Status/progress go to
  stderr.

Consistency with the SDK is `idl/SCHEMA_LOCK`, copied into the kit as
`share/runanywhere/SCHEMA_LOCK` and pinned here as `RCLI_PINNED_IDL_SCHEMA_SHA256`.
Configure fails if the kit's lock does not match (`cmake/RunAnywhereSDK.cmake`).
When the schema changes, consume a new kit and bump the pin — never regenerate
headers locally.

`RunAnywhere::commons` applies `google=runanywhere_internal` when the kit was
built with namespace isolation. Never `find_package(Protobuf)` against Homebrew.

## Command surface

Dual grammar: spec namespaces (`llm generate`, `models download`) plus terminal
aliases (`run`, `pull`, `stt`). One `configure_*` wires both. See
`src/commands/commands.h`.

Do not reintroduce FetchContent of the SDK, a second inference backend tree,
or a retired MetalRT / hardcoded catalog.

## Configuration and secrets

- Read environment in one place (`GlobalOptions` / `bootstrap()`).
- Safe local default: `--environment development` (keyless). Production requires
  an API key + https. Tests must not need real keys or network.
- Secrets stay in GitHub Actions secrets / the user's env, never in source,
  formula files, or skill docs.

## Tests

Hermetic unit tests (`tests/test_rcli_unit.cpp`): no models, no network, no real
keys. `ctest` is the default CI bar.

Smoke / e2e (`scripts/smoke.sh`, `scripts/e2e.sh`) prove the product promise
against a **pinned kit**, not SDK source. Optional `RCLI_E2E_MODEL` covers
download + generate. Apple MLX shipping binary: `scripts/build-mlx.sh` +
`scripts/smoke-mlx.sh`.

## CI

Minimum: fetch the pinned kit (`scripts/fetch-kit.sh`) → configure → build →
ctest → smoke. Fail on pin / schema mismatch. `scripts/ci/check-agents-sync.sh`
fails the PR when `CLAUDE.md` is not a symlink to `AGENTS.md`, or when
`.claude/skills` and `.agents/skills` differ.

Linux bottles are not a v1 merge blocker. Windows x64 and macOS arm64 are.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/cpp-desktop-<os>-<arch>
cmake --build build -j "$(sysctl -n hw.logicalcpu)"
ctest --test-dir build --output-on-failure
bash scripts/e2e.sh ./build/rcli-cxx   # Windows: ./build/rcli.exe
```

Apple shipping binary is the Swift MLX host (`scripts/build-mlx.sh`) wrapping
`rcli_run_main`. CMake output on Apple is `rcli-cxx` so the two cannot clobber.

NeuRT and QHexRT are optional private packs (`NEURUN_TOKEN`), never required to
configure or link the public bottle. QHexRT is not shipped for Windows x64.

## Skills

Canonical tree: `.claude/skills/` (Claude Code). Mirror: `.agents/skills/`
(Cursor / Codex). Edit the canonical tree, then
`bash scripts/ci/check-agents-sync.sh --fix`. Never hand-edit the mirror.

| Skill | When |
|---|---|
| `rcli-architecture` | New commands, layering, proto, "where does this logic go?" |
| `rcli-kit-pin` | Bump `cmake/sdk-pin.cmake`, consume a new kit |
| `rcli-e2e` | macOS / Windows verification against a kit |
| `rcli-release` | Cut a product release (independent of SDK version) |

## Definition of done

- Command is parse → `bootstrap()` → one `rac_*` → render.
- Proto types from the kit; pin matches `SCHEMA_LOCK`.
- Errors structured; no secrets on the log line.
- Unit tests hermetic; smoke/e2e green on the platforms you touched.
- `check-agents-sync.sh` passes.
- Docs name any mock, scaffold, or deferred work honestly (Linux bottle, NeuRT).
