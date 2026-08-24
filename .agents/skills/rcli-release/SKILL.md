---
name: rcli-release
description: Cut an RCLI product release independent of the SDK kit version. Downloads the pinned C++ desktop kit, builds bottles, signs, stamps Formula/rcli.rb, and publishes a GitHub Release. Use when tagging rcli, shipping bottles, or updating the Homebrew formula.
---

# RCLI release

Repo: `RunanywhereAI/RCLI`. Product version is `CMakeLists.txt` /
`project(rcli VERSION …)`. SDK kit pin is `cmake/sdk-pin.cmake`.

## Preconditions

1. `cmake/sdk-pin.cmake` lists `RCLI_PINNED_SDK_VERSION` and SHA-256 for each kit.
2. That SDK tag exists on `RunanywhereAI/runanywhere-sdks` with:
   - `RunAnywhere-cpp-desktop-macos-arm64-v<ver>.tar.gz`
   - `RunAnywhere-cpp-desktop-windows-x64-v<ver>.tar.gz`
3. `rcli --version` after link prints both versions and must not silently
   disagree with `rac_get_version()`.

## Steps

1. Confirm pin: `grep RCLI_PINNED cmake/sdk-pin.cmake`
2. Download kits (never clone the SDK): `bash scripts/fetch-kit.sh <platform> <dest>`
3. macOS: configure + build against the kit. `cmake --build` produces `build/rcli`
   (Swift MLX host). Do not package `rcli-cxx`.
4. Windows x64: MSVC after `vcvarsall x64`, same kit, `/MD`. No QHexRT on x64.
5. Sign: Developer ID + notary (macOS), Authenticode (Windows). Secrets live
   in **this** repo (`docs/RELEASING.md`).
6. Package (`scripts/package-rcli.sh` / `scripts/package-rcli-windows.ps1`).
7. Stamp `Formula/rcli.rb` via `scripts/stamp-formula.py`. Commit the stamp —
   the release job may stamp on the runner without pushing.
8. Tag `v<product>` (not the SDK version). Publish the GitHub Release with bottles.

NeuRT / QHexRT are optional packs. Public `brew install rcli` must not require
`NEURUN_TOKEN`. Linux bottles are not a v1 blocker.

Do not attach `rcli-*` bottles to an SDK GitHub Release. Kits ship from
`runanywhere-sdks`; bottles ship from here.
