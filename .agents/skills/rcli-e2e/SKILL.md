---
name: rcli-e2e
description: Verify a built rcli binary against a pinned C++ desktop kit on macOS and Windows. Use when CI smoke/e2e is red, backends are missing, DLLs fail to load, or the Apple MLX host fails to link.
---

# RCLI e2e

Entry: `scripts/e2e.sh <path-to-rcli>`. Always runs `scripts/smoke.sh`. Set
`RCLI_E2E_MODEL` for download + generate (needs network + disk).

Pass `RCLI_SDK_KIT` (or `CMAKE_PREFIX_PATH`) so the script can read
`RunAnywhereConfig.cmake` `HAS_*` flags and stage Windows DLLs.

## What "green" means

`scripts/assert-backends.sh` requires every engine the kit actually ships:

| Condition | Required `rcli --json backends` name |
|---|---|
| always | `llamacpp` |
| `RunAnywhere_HAS_ONNX` TRUE (or no kit cfg) | `onnx` |
| `RunAnywhere_HAS_SHERPA` TRUE (or no kit cfg) | `sherpa` |
| Darwin arm64 product binary `rcli` (not `rcli-cxx`) | `mlx` |

Do **not** drop sherpa from the expected list to make 0.20.26 Windows green
while `HAS_SHERPA` is TRUE. That kit compiled sherpa with speech ops off
(`RAC_SHERPA_ROUTABLE=0`): `rac_backend_sherpa_register()` returned SUCCESS,
`capability_check` returned `BACKEND_UNAVAILABLE`, the registry refused the
plugin. The fix is pin a routable kit (0.20.28+), not weaken the assertion.

## `backends` must walk every live primitive

`src/commands/cmd_backends.cpp` iterates `1 .. RAC_PRIMITIVE_COUNT-1`, skipping
retired wire value 6. ONNX without RAG only advertises SEGMENT / DIARIZE. A
hardcoded GENERATE_TEXT / TRANSCRIBE / EMBED list made onnx invisible even when
the plugin was registered. Do not reintroduce a primitive allow-list.

## Windows DLLs

Win32 `LoadLibrary` searches the exe directory, then PATH. `e2e.sh` copies
`third_party` / `bin` / `lib` `*.dll` next to `rcli.exe` and prepends those
dirs to PATH **before** smoke. Skipping that produces "llamacpp only" even when
the kit contains `rac_backend_onnx.lib` + `onnxruntime.dll`.

Never pass `onnxruntime.dll` to `link.exe` (LNK1107) — link the import lib;
stage the DLL at runtime.

GitHub Windows: `GITHUB_WORKSPACE` is `D:\a\...`; msys `tar -C` needs
`cygpath -u` (`fetch-kit.sh` already does).

## Apple MLX host link (Ninja)

`scripts/bundle-core.sh` merges everything `rcli` links into `librcli_bundle.a`
for SwiftPM.

- Ninja **never** writes `CMakeFiles/rcli.dir/link.txt` (Makefiles only).
- Harvest with `ninja -t commands rcli | tail -1`. The **last** command is the
  link. Grepping for `rcli` hits compile lines (`CMakeFiles/rcli.dir/…`).
- Apple ld emits one token `-Wl,-force_load,/abs/path/lib.a`. That ends in
  `.a` but is not a path. Prefixing `BUILD/` produces
  `build/-Wl,-force_load,…`. Strip `-Wl,-force_load,` first.
- Ninja lists archives twice; `libtool -static` then fails on duplicate
  members unless you dedupe.

CI macOS runner is **macos-26** (Xcode 26 / Swift 6.2). The MLX host resolves
`RunanywhereAI/runanywhere-sdks` `Package.swift`, which is
`swift-tools-version: 6.2`. macos-15 is Xcode 16.4 / Swift 6.1 and fails after
a successful libtool merge with `package 'runanywhere-sdks' is using Swift
tools version 6.2.0 but the installed version is 6.1.0`. macos-14 is Swift
5.10. `release.yml` must use the same runner as `ci.yml`.

## Linux

Linux bottles are not a v1 merge blocker. Windows x64 and macOS arm64 are.
`scripts/e2e-linux.sh` exists for later.

## Private engines

NeuRT / QHexRT only appear in `backends` when the overlay was applied
(`RCLI_HAS_NEURT` / QHexRT). Public CI must pass without overlays. Image gen
(`cmd_image.cpp`) is compiled out unless NeuRT is present.
