#!/usr/bin/env bash
# =============================================================================
# update-tap.sh <version>
#
# Stamps Formula/rcli.rb from a PUBLISHED GitHub Release (reads .sha256
# sidecars) and pushes Formula/rcli.rb to the Homebrew tap.
#
#   ./scripts/update-tap.sh 0.5.0
#
# Environment:
#   RCLI_TAP_REPO   Tap git remote to update (required unless DRY_RUN=1)
#   RCLI_TAP_DIR    Existing tap checkout to reuse (default: fresh temp clone)
#   DRY_RUN=1       Render + print, do not commit/push
# =============================================================================

set -euo pipefail

VERSION="${1:?usage: update-tap.sh <version>}"
VERSION="${VERSION#v}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
FORMULA="${CLI_ROOT}/Formula/rcli.rb"
RELEASE_BASE="https://github.com/RunanywhereAI/RCLI/releases/download/v${VERSION}"
TAP_REPO="${RCLI_TAP_REPO:-}"

fetch_sha() {
    local asset="$1"
    local line
    line="$(curl -fsSL "${RELEASE_BASE}/${asset}.sha256")" ||
        { echo "ERROR: missing release asset ${asset}.sha256 — is v${VERSION} published?" >&2; exit 1; }
    echo "${line}" | awk '{print $1}'
}

echo "Fetching release checksums for v${VERSION}..."
SHA_MAC_ARM="$(fetch_sha "rcli-${VERSION}-macos-arm64.tar.gz")"

if [[ -f "${CLI_ROOT}/scripts/stamp-formula.py" ]]; then
    python3 "${CLI_ROOT}/scripts/stamp-formula.py" "${VERSION}" \
        "macos-arm64=${SHA_MAC_ARM}"
else
    echo "ERROR: scripts/stamp-formula.py missing" >&2
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
    echo "ERROR: set RCLI_TAP_REPO explicitly; the canonical RCLI vs homebrew-tap repository has not been decided." >&2
    exit 1
fi

TAP_DIR="${RCLI_TAP_DIR:-}"
if [[ -z "${TAP_DIR}" ]]; then
    TAP_DIR="$(mktemp -d)/homebrew-tap"
    git clone --depth 1 "${TAP_REPO}" "${TAP_DIR}"
fi

mkdir -p "${TAP_DIR}/Formula"
cp "${FORMULA}" "${TAP_DIR}/Formula/rcli.rb"
git -C "${TAP_DIR}" add Formula/rcli.rb
git -C "${TAP_DIR}" commit -m "rcli ${VERSION}"
git -C "${TAP_DIR}" push

echo "Tap formula updated in ${TAP_REPO}"
