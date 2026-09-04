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

## Signing in to a console

`rcli login` is a device flow, the same shape `gh auth login` uses. The terminal
asks for a code, a browser the person already trusts approves it, and the
terminal collects a key. No password ever reaches the CLI.

The four endpoints it calls are **not ours to rename**: an installed binary
talks to whatever the console deploys, so a field or path change breaks every
copy in the wild. They are `POST /auth/cli/start`, `/auth/cli/poll`,
`/auth/cli/refresh`, and `GET /v1/me`. The console side has a test that reads
`src/account/console.cpp` directly and fails if the two drift.

Two secrets do different jobs. `request_code` is public and names the attempt;
`poll_secret` proves the process collecting the grant is the one that started
it. The console stores only a hash of the second.

`RCLI_CONSOLE_URL` points at the console; it defaults to `http://localhost:8080`,
which is nothing, so a local run needs it set. `RCLI_PROFILE_DIR` moves the
credential file, which is what lets several accounts share one machine.

The credential is a normal API key with the customer's credit behind it. Treat
it as one: it goes in the profile file at `0600` and nowhere else, and it is
never logged.

## Building against the SDK

`RCLI_SDK_KIT` points at a built kit, not at SDK source. `cmake/sdk-pin.cmake`
pins the IDL version and its hash; a mismatch is a hard error and the fix is to
consume a matching kit or bump the pin, **never to run protoc**.

Two binaries come out of a build. `rcli-cxx` is the CLI. `rcli` is the same
thing plus the MLX backend, and it only builds when `RCLI_SDK_SWIFT_PATH` names
an SDK checkout with the Swift tree. Ship `rcli`.

MLX resolves its Metal shaders from `mlx-swift_Cmlx.bundle` beside the
executable. Copy the binary somewhere on its own and MLX silently fails to
register, so an install puts both together and points a wrapper at them.

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
against a **pinned kit**, not SDK source. `scripts/e2e-modalities.sh` (called
from `e2e.sh`) runs optional round-trips **by modality** — LLM, STT, TTS, VLM,
embed, image, VAD, rerank, segment — and never requires `--engine`. Discover
models via `RCLI_E2E_<MOD>` (path or catalog id), `RCLI_E2E_MODEL_ROOTS`, or
`RCLI_E2E_AUTO=1`. Legacy `RCLI_E2E_MODEL` / `RCLI_E2E_MLX_MODEL` /
`RCLI_E2E_NEURT_MODEL` / `RCLI_E2E_QHEXRT_MODEL` still map onto those
primitives. Public CI leaves every knob unset (skip). There is one CLI named
`rcli`. On Apple, `cmake --build` links the Swift MLX host as `build/rcli`
(llama.cpp + ONNX + Sherpa + MLX). Windows is `build/rcli.exe` (no MLX).
`rcli-cxx` is an Apple compile artifact, not the product. Full MLX model
smoke: `scripts/smoke-mlx.sh`.

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
# Apple: ./build/rcli   Windows: ./build/rcli.exe
bash scripts/e2e.sh ./build/rcli
```

On Apple Silicon, `cmake --build` produces `build/rcli` (Swift host wrapping
`rcli_run_main`). Users never run `rcli-cxx`; that name exists only so CMake
cannot overwrite the product binary. Independent clones set
`RCLI_SDK_SWIFT_PATH` to a runanywhere-sdks checkout (CI does this). Nested
`EXTERNAL/RCLI` finds `../../Package.swift` automatically. Disable the host
with `-DRCLI_APPLE_MLX_HOST=OFF` only for a C++-only compile loop.

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
