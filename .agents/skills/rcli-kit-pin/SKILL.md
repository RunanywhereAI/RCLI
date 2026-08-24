---
name: rcli-kit-pin
description: Bump cmake/sdk-pin.cmake to a new published SDK C++ desktop kit (version + SHA-256 + IDL lock). Use after an SDK GitHub Release is published (not draft), when fetch-kit.sh 404s, or when SCHEMA_LOCK mismatches.
---

# RCLI kit pin

Pin file: `cmake/sdk-pin.cmake`. Fetcher: `scripts/fetch-kit.sh`. Overlay:
`scripts/fetch-private-pack.sh`. CMake wrapper: `cmake/RunAnywhereSDK.cmake`.

Companion: **cpp-desktop-kit** in runanywhere-sdks (what the tarball contains).

## Preconditions

1. SDK GitHub Release `v$SDK` is **published** (`isDraft=false`). Drafts 404
   for this repo's `GITHUB_TOKEN`. Prerelease is fine; `latest` is not required.
2. Public assets exist:
   `RunAnywhere-cpp-desktop-{macos-arm64,windows-x64}-v$SDK.tar.gz`
   (+ windows-arm64 once that kit is on the release).
3. Do **not** point `RCLI_SDK_DIR` at SDK **source** — configure error.
   Prefix is `-DCMAKE_PREFIX_PATH=` or `-DRCLI_SDK_KIT=`.

## Bump together (never version-only)

```bash
# After SDK v$SDK is published:
gh release download "v$SDK" --repo RunanywhereAI/runanywhere-sdks \
  --pattern 'RunAnywhere-cpp-desktop-*-v'"$SDK"'.tar.gz*' -D /tmp/kits
shasum -a 256 /tmp/kits/RunAnywhere-cpp-desktop-*.tar.gz
```

Update in `cmake/sdk-pin.cmake`:

- `RCLI_PINNED_SDK_VERSION`
- `RCLI_PINNED_KIT_SHA256_MACOS_ARM64`
- `RCLI_PINNED_KIT_SHA256_WINDOWS_X64`
- `RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64` (uncomment once the asset exists)
- IDL triple copied from the kit's `share/runanywhere/SCHEMA_LOCK`:
  `RCLI_PINNED_IDL_VERSION`, `RCLI_PINNED_IDL_SCHEMA_SHA256`,
  `RCLI_PINNED_IDL_PROTOC_VERSION`

`fetch-kit.sh` refuses `SDK_VERSION != RCLI_PINNED_SDK_VERSION`. Do not override
only an env var to "try" a newer kit — checksums are keyed to the pin.

Configure fails if the extracted kit's `SCHEMA_LOCK` does not match the IDL
pin. When the schema changes, consume a new kit and bump the pin — never
regenerate headers locally. RCLI never runs `protoc`.

## CI Swift tree

`.github/workflows/ci.yml` and `release.yml` check out
`RunanywhereAI/runanywhere-sdks` at `ref: v$SDK` into `.deps/runanywhere-sdks`
for the Apple MLX host (`RCLI_SDK_SWIFT_PATH`). Bump that ref in the **same
commit** as the pin. macos-26 (Xcode 26 / Swift 6.2 — matches SDK
`swift-tools-version: 6.2`); macos-15 is Swift 6.1; macos-14 is Swift 5.10.

## Private overlays

Not part of the pin. Applied after extract:

1. `RCLI_PRIVATE_OVERLAY` (local tarball)
2. tarball next to the kit prefix named
   `RunAnywhere-cpp-desktop-{macos-arm64-neurt,windows-arm64-qhexrt}-private-v*.tar.gz`
3. skip (OSS) unless `RCLI_REQUIRE_PRIVATE=1`

QHexRT is Windows ARM64 only. Public bottles must stay OSS — never bake overlay
bytes into a Homebrew bottle or a public GitHub Release asset.

## Verify

```bash
bash scripts/fetch-kit.sh macos-arm64 /tmp/kit
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/kit
cmake --build build -j "$(sysctl -n hw.logicalcpu)"
RCLI_SDK_KIT=/tmp/kit bash scripts/e2e.sh ./build/rcli
```
