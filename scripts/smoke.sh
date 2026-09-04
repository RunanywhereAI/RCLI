#!/usr/bin/env bash
# Command-surface smoke: no model download.
#
#   scripts/smoke.sh <path-to-wally>
set -euo pipefail

WALLY="${1:?usage: smoke.sh <path-to-wally>}"
if [[ ! -e "${WALLY}" ]]; then
    echo "not found: ${WALLY}" >&2
    exit 1
fi
if [[ ! -x "${WALLY}" && "${WALLY}" != *.exe && "${WALLY}" != *.EXE ]]; then
    echo "not executable: ${WALLY}" >&2
    exit 1
fi

fail=0
check() {
    local label="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "  ok    ${label}"
    else
        echo "  FAIL  ${label}"
        fail=1
    fi
}

echo "smoke: ${WALLY}"
check "--help"     "${WALLY}" --help
check "version"    "${WALLY}" version
check "help"       "${WALLY}" --help
# backends is the spec name; some builds still accept engines as an alias.
if "${WALLY}" backends --help >/dev/null 2>&1; then
    check "backends" "${WALLY}" backends
elif "${WALLY}" engines --help >/dev/null 2>&1; then
    check "engines" "${WALLY}" engines
else
    echo "  FAIL  backends/engines"
    fail=1
fi

if "${WALLY}" definitely-not-a-command >/dev/null 2>&1; then
    echo "  FAIL  unknown command was accepted"
    fail=1
else
    echo "  ok    unknown command rejected"
fi

exit "${fail}"
