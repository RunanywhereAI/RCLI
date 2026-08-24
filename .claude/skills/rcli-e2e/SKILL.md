---
name: rcli-e2e
description: Run RCLI smoke and e2e against a pinned C++ desktop kit on macOS and Windows. Use when verifying a kit pin, a Windows link fix, MLX host, or when the user asks to e2e-test rcli.
---

# RCLI e2e

Always test the **product binary against a released or local kit**, never by
compiling SDK source into this tree.

## macOS arm64

```bash
bash scripts/fetch-kit.sh macos-arm64 /tmp/kit
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/kit
cmake --build build -j "$(sysctl -n hw.logicalcpu)"
ctest --test-dir build --output-on-failure
bash scripts/e2e.sh ./build/rcli
```

`cmake --build` on Apple links the Swift MLX host as `build/rcli` (the only
binary users run). e2e asserts `llamacpp`, `onnx`, `sherpa`, and `mlx`.

Optional real generate: `RCLI_E2E_MODEL=qwen3-0.6b bash scripts/e2e.sh ./build/rcli`.
Full MLX LLM/TTS/STT/VLM: `bash scripts/smoke-mlx.sh` (downloads models).

Independent clones: `export RCLI_SDK_SWIFT_PATH=/path/to/runanywhere-sdks`.
Nested `EXTERNAL/RCLI` finds `../../Package.swift`. C++-only loop:
`-DRCLI_APPLE_MLX_HOST=OFF` then e2e `./build/rcli-cxx` (no MLX).

## Windows x64

Host cmake/ninja on PATH may be Python ARM64 — use VS **x64** after `vcvarsall x64`.
Kit is `/MD`. Do not force `/MT`.

```bat
vcvarsall.bat x64
bash scripts/fetch-kit.sh windows-x64 C:\kit
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/kit
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
bash scripts/e2e.sh ./build/rcli.exe
```

Copy `onnxruntime.dll` from the kit `third_party/` next to `rcli.exe` if smoke
cannot load ORT. Do not tarball `swift/` (multi-GB).

## Bar

CI builds the Apple product `rcli` (MLX host) and Windows `rcli.exe`, then
runs unit + modelless e2e (backends include MLX on macOS arm64). A pin bump
is not done until those are green. Linux e2e is optional (`scripts/e2e-linux.sh`).
