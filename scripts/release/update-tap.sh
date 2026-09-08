#!/usr/bin/env bash
# =============================================================================
# update-tap.sh <version>
#
# Stamps Formula/wally.rb from a PUBLISHED GitHub Release (reads .sha256
# sidecars) and pushes Formula/wally.rb to the Homebrew tap.
#
#   ./scripts/release/update-tap.sh 0.5.0
#
# Environment:
#   WALLY_TAP_REPO   Tap git remote to update (required unless DRY_RUN=1)
#   WALLY_TAP_DIR    Existing tap checkout to reuse (default: fresh temp clone)
#   DRY_RUN=1        Render + print, do not commit/push
# =============================================================================

set -euo pipefail

VERSION="${1:?usage: update-tap.sh <version>}"
VERSION="${VERSION#v}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
FORMULA="${CLI_ROOT}/Formula/wally.rb"
RELEASE_BASE="https://github.com/RunanywhereAI/wally/releases/download/v${VERSION}"
# The tap is the wally repo itself: install.sh taps `RunanywhereAI/wally` at this
# repo's git URL, and the formula lives in-repo at Formula/wally.rb, so the
# stamped formula is pushed back here. Override with WALLY_TAP_REPO only to
# publish to a separate homebrew-tap repo.
TAP_REPO="${WALLY_TAP_REPO:-git@github.com:RunanywhereAI/wally.git}"

fetch_sha() {
    local asset="$1"
    local line
    line="$(curl -fsSL "${RELEASE_BASE}/${asset}.sha256")" ||
        { echo "ERROR: missing release asset ${asset}.sha256 — is v${VERSION} published?" >&2; exit 1; }
    echo "${line}" | awk '{print $1}'
}

echo "Fetching release checksums for v${VERSION}..."
SHA_MAC_ARM="$(fetch_sha "wally-${VERSION}-macos-arm64.tar.gz")"

if [[ -f "${SCRIPT_DIR}/stamp-formula.py" ]]; then
    python3 "${SCRIPT_DIR}/stamp-formula.py" "${VERSION}" \
        "macos-arm64=${SHA_MAC_ARM}"
else
    echo "ERROR: scripts/release/stamp-formula.py missing" >&2
    exit 1
fi

echo "Stamped formula:"
echo "----------------------------------------"
cat "${FORMULA}"
echo "----------------------------------------"

if [[ "${DRY_RUN:-0}" == "1" ]]; then
    echo "DRY_RUN=1 — not pushing to the tap."
    exit 0
fi

if [[ -z "${TAP_REPO}" ]]; then
    echo "ERROR: WALLY_TAP_REPO resolved empty." >&2
    exit 1
fi

TAP_DIR="${WALLY_TAP_DIR:-}"
if [[ -z "${TAP_DIR}" ]]; then
    TAP_DIR="$(mktemp -d)/homebrew-tap"
    git clone --depth 1 "${TAP_REPO}" "${TAP_DIR}"
fi

mkdir -p "${TAP_DIR}/Formula"
cp "${FORMULA}" "${TAP_DIR}/Formula/wally.rb"
git -C "${TAP_DIR}" add Formula/wally.rb
git -C "${TAP_DIR}" commit -m "wally ${VERSION}"
git -C "${TAP_DIR}" push

echo "Tap formula updated in ${TAP_REPO}"
