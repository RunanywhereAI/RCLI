---
name: rcli-architecture
description: RCLI layering and command-authoring rules. Use when adding or changing CLI commands, deciding where logic belongs, touching proto/ABI boundaries, or when the user mentions bootstrap, rac_*, thin commands, or kit vs CLI.
---

# RCLI architecture

Repo: `RunanywhereAI/RCLI`. Product CLI over a packaged C++ desktop kit.

## Where logic goes

| Layer | Owns | Does not own |
|---|---|---|
| `src/commands/cmd_*.cpp` | argv, flags, one `rac_*`, render | catalog, download, generate, engine pick |
| `src/bootstrap.*` | one-time SDK bring-up | per-command inference |
| `src/io/proto.h` | `rac_proto_buffer_t` ↔ `runanywhere::v1::*` | a second proto compile |
| C++ desktop kit | models, lifecycle, inference, serve | terminal UX |

If a command needs a sequence the public `rac_*` ABI does not offer, the fix
belongs in the SDK, then a new kit — not a workaround here.

## Adding a command

1. Add `src/commands/cmd_<name>.cpp` and declare it in `src/commands/commands.h`.
2. Register from `src/app.cpp`. Dual grammar: spec namespace + terminal alias
   share one `configure_*`.
3. Add the TU to `CMakeLists.txt`.
4. Keep the TU thin: parse → `bootstrap()` → **one** `rac_*` → render.
5. Use kit proto types. Parse with `src/io/proto.h`. Do not run `protoc`.
6. Results on stdout; logs/progress on stderr. `--json` = one document.
7. Hermetic unit coverage for parse/render helpers. No real keys in unit tests.

## Anti-patterns

- `add_subdirectory` / FetchContent of the SDK, or `RCLI_SDK_DIR` at source.
- Compiling `*.pb.cc` or `find_package(Protobuf)` (Homebrew).
- Hardcoded engine names, path patterns, or a local model catalog.
- Business rules in Swift (`swift/`) or the REPL. Swift is the Apple **entry**
  (register MLX callbacks, then `rcli_run_main`). It is not a second CLI.
- Logging API keys / tokens.
- Forcing MSVC `/MT` — the kit is `/MD`.
