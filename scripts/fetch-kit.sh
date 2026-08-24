#!/usr/bin/env bash
# Download and verify a pinned C++ desktop kit, then extract it to DEST.
#
#   scripts/fetch-kit.sh <macos-arm64|windows-x64> <dest-dir>
#
# Requires: gh, and either shasum or sha256sum.
# Pins live in cmake/sdk-pin.cmake. SDK_VERSION, if set, must equal
# RCLI_PINNED_SDK_VERSION — checksums are keyed to that pin, not a repo variable.
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

PINNED_SDK="$(pin_value RCLI_PINNED_SDK_VERSION)"
EXPECTED="$(pin_value "$SHA_VAR")"
if [[ -z "$PINNED_SDK" || -z "$EXPECTED" ]]; then
  echo "error: missing $SHA_VAR or RCLI_PINNED_SDK_VERSION in $PIN" >&2
  exit 1
fi
if [[ -n "${SDK_VERSION:-}" && "$SDK_VERSION" != "$PINNED_SDK" ]]; then
  echo "error: SDK_VERSION=$SDK_VERSION does not match RCLI_PINNED_SDK_VERSION=$PINNED_SDK in $PIN" >&2
  echo "  bump cmake/sdk-pin.cmake (version + SHA-256) together; do not override only SDK_VERSION" >&2
  exit 1
fi
SDK_VERSION="$PINNED_SDK"

asset="RunAnywhere-cpp-desktop-${PLATFORM}-v${SDK_VERSION}.tar.gz"
dl="$(mktemp -d)"
trap 'rm -rf "$dl"' EXIT

# Draft GitHub Releases are invisible to another repo's GITHUB_TOKEN
# (`release not found`). The pin must point at a published release
# (prerelease is fine; latest is not required).
if ! gh release download "v${SDK_VERSION}" \
  --repo RunanywhereAI/runanywhere-sdks \
  --pattern "$asset" --dir "$dl"; then
  echo "error: could not download $asset from RunanywhereAI/runanywhere-sdks@v${SDK_VERSION}" >&2
  echo "  that tag must be a published GitHub Release (drafts 404 for this token)." >&2
  gh release view "v${SDK_VERSION}" --repo RunanywhereAI/runanywhere-sdks >&2 || true
  exit 1
fi

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

# GitHub Windows runners pass GITHUB_WORKSPACE as D:\a\...; msys tar cannot -C that.
if command -v cygpath >/dev/null 2>&1; then
  DEST="$(cygpath -u "$DEST")"
fi
mkdir -p "$DEST"
tar xzf "$file" -C "$DEST" --strip-components=1
echo "kit verified and extracted to $DEST"
