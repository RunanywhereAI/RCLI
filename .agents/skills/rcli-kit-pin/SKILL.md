---
name: rcli-kit-pin
description: Bump RCLI's pinned C++ desktop kit and IDL schema lock. Use when consuming a new RunAnywhere kit, sdk-pin.cmake is stale, SCHEMA_LOCK mismatches, or the user mentions find_package(RunAnywhere), fetch-kit, or pin bump.
---

# RCLI kit pin

RCLI is a kit consumer. It never builds the SDK and never runs `protoc`.

## Pin file

`cmake/sdk-pin.cmake`:

- `RCLI_PINNED_SDK_VERSION`
- `RCLI_PINNED_IDL_VERSION`
- `RCLI_PINNED_IDL_SCHEMA_SHA256`  (kit `share/runanywhere/SCHEMA_LOCK`)
- `RCLI_PINNED_IDL_PROTOC_VERSION`
- `RCLI_PINNED_KIT_SHA256_MACOS_ARM64`
- `RCLI_PINNED_KIT_SHA256_WINDOWS_X64`

`scripts/fetch-kit.sh` refuses `SDK_VERSION` that does not equal the pin.
`cmake/RunAnywhereSDK.cmake` configure-fails on IDL mismatch.

## Bump procedure

1. Confirm the SDK GitHub Release `v<ver>` has:
   - `RunAnywhere-cpp-desktop-macos-arm64-v<ver>.tar.gz`
   - `RunAnywhere-cpp-desktop-windows-x64-v<ver>.tar.gz`
2. Download both; record SHA-256. On Windows, use the **x64** kit (not arm64).
3. Extract; read `share/runanywhere/SCHEMA_LOCK` and `VERSION`.
4. Update every `RCLI_PINNED_*` value in `cmake/sdk-pin.cmake` in one commit.
5. Configure against the new prefix:

   ```bash
   cmake -B build -DCMAKE_PREFIX_PATH=/path/to/kit -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   bash scripts/e2e.sh ./build/rcli-cxx
   ```

6. Do **not** run `protoc` or copy headers out of SDK source to "fix" a lock
   mismatch — that is the pin telling you to consume a different kit.

## Windows link

Until a later kit bakes these into `SYSTEM_LIBS`, `cmake/RunAnywhereSDK.cmake`
strips `.dll` from `INTERFACE_LINK_LIBRARIES` and appends `iphlpapi`, `xmllite`,
`ole32`. Keep that. Do not force `/MT`.
