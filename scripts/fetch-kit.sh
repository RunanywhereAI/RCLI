#!/usr/bin/env bash
# Download and verify a pinned C++ desktop kit, then extract it to DEST.
#
#   scripts/fetch-kit.sh <macos-arm64|windows-x64> <dest-dir>
#
# Requires: gh, and either shasum or sha256sum.
# Pins live in cmake/sdk-pin.cmake. SDK_VERSION may override the CMake pin.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${1:?usage: fetch-kit.sh <macos-arm64|windows-x64> <dest>}"
DEST="${2:?usage: fetch-kit.sh <macos-arm64|windows-x64> <dest>}"
PIN="${ROOT}/cmake/sdk-pin.cmake"

case "$PLATFORM" in
  macos-arm64) SHA_VAR=RCLI_PINNED_KIT_SHA256_MACOS_ARM64 ;;
  windows-x64) SHA_VAR=RCLI_PINNED_KIT_SHA256_WINDOWS_X64 ;;
  *) echo "error: unknown platform '$PLATFORM'" >&2; exit 2 ;;
esac

pin_value() {
  local key="$1"
  sed -n "s/^set(${key} \"\\(.*\\)\")/\\1/p" "$PIN" | head -1
}

SDK_VERSION="${SDK_VERSION:-$(pin_value RCLI_PINNED_SDK_VERSION)}"
EXPECTED="$(pin_value "$SHA_VAR")"
if [[ -z "$SDK_VERSION" || -z "$EXPECTED" ]]; then
  echo "error: missing $SHA_VAR or RCLI_PINNED_SDK_VERSION in $PIN" >&2
  exit 1
fi

asset="RunAnywhere-cpp-desktop-${PLATFORM}-v${SDK_VERSION}.tar.gz"
dl="$(mktemp -d)"
trap 'rm -rf "$dl"' EXIT

gh release download "v${SDK_VERSION}" \
  --repo RunanywhereAI/runanywhere-sdks \
  --pattern "$asset" --dir "$dl"

file="${dl}/${asset}"
if command -v shasum >/dev/null 2>&1; then
  actual="$(shasum -a 256 "$file" | awk '{print $1}')"
else
  actual="$(sha256sum "$file" | awk '{print $1}')"
fi
if [[ "$actual" != "$EXPECTED" ]]; then
  echo "error: kit checksum mismatch for $asset" >&2
  echo "  expected $EXPECTED" >&2
  echo "  actual   $actual" >&2
  exit 1
fi

mkdir -p "$DEST"
tar xzf "$file" -C "$DEST" --strip-components=1
echo "kit verified and extracted to $DEST"
