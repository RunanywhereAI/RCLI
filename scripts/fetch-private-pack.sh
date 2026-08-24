#!/usr/bin/env bash
# Overlay a private NeuRT / QHexRT pack onto an extracted public C++ kit prefix.
#
#   scripts/fetch-private-pack.sh <macos-arm64|windows-arm64> <kit-prefix>
#
# Resolution order:
#   1. RCLI_PRIVATE_OVERLAY  — local tarball
#   2. GitHub Actions artifact download (same-repo CI)
#   3. skip (OSS bottle) unless RCLI_REQUIRE_PRIVATE=1
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${1:?usage: fetch-private-pack.sh <macos-arm64|windows-arm64> <kit-prefix>}"
DEST="${2:?usage: fetch-private-pack.sh <macos-arm64|windows-arm64> <kit-prefix>}"

case "$PLATFORM" in
  macos-arm64) ENGINE=neurt ;;
  windows-arm64) ENGINE=qhexrt ;;
  windows-x64)
    echo "QHexRT is Windows ARM64 (Snapdragon NPU) only; no overlay on windows-x64."
    exit 0
    ;;
  *)
    echo "error: unknown platform '$PLATFORM'" >&2
    exit 2
    ;;
esac

apply_tar() {
  local tar="$1"
  if command -v cygpath >/dev/null 2>&1; then
    DEST="$(cygpath -u "$DEST")"
  fi
  tar xzf "$tar" -C "$DEST"
  echo "private overlay applied: $tar -> $DEST"
}

if [[ -n "${RCLI_PRIVATE_OVERLAY:-}" ]]; then
  [[ -f "$RCLI_PRIVATE_OVERLAY" ]] || {
    echo "error: RCLI_PRIVATE_OVERLAY is not a file: $RCLI_PRIVATE_OVERLAY" >&2
    exit 1
  }
  apply_tar "$RCLI_PRIVATE_OVERLAY"
  exit 0
fi

# Prefer a tarball already sitting next to the kit (CI drops artifacts here).
# Check this BEFORE any token gate — a staged overlay must apply without
# NEURUN_TOKEN / GH_TOKEN (those are unused; there is no remote fetch path).
shopt -s nullglob
local_hits=("$DEST"/../RunAnywhere-cpp-desktop-"${PLATFORM}"-"${ENGINE}"-private-v*.tar.gz)
if [[ ${#local_hits[@]} -gt 0 ]]; then
  apply_tar "${local_hits[0]}"
  exit 0
fi
shopt -u nullglob

if [[ "${RCLI_REQUIRE_PRIVATE:-}" == "1" ]]; then
  echo "error: no private ${ENGINE} overlay found for $PLATFORM" >&2
  echo "  set RCLI_PRIVATE_OVERLAY or drop the tarball next to the kit prefix" >&2
  exit 1
fi
echo "skip: no private ${ENGINE} overlay for $PLATFORM (OSS kit only)"
exit 0
