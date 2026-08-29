#!/usr/bin/env bash
# Command-surface smoke: no model download.
#
#   scripts/smoke.sh <path-to-rcli>
set -euo pipefail

RCLI="${1:?usage: smoke.sh <path-to-rcli>}"
if [[ ! -e "${RCLI}" ]]; then
    echo "not found: ${RCLI}" >&2
    exit 1
fi
if [[ ! -x "${RCLI}" && "${RCLI}" != *.exe && "${RCLI}" != *.EXE ]]; then
    echo "not executable: ${RCLI}" >&2
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

echo "smoke: ${RCLI}"
check "--help"     "${RCLI}" --help
check "version"    "${RCLI}" version
check "help"       "${RCLI}" --help
# backends is the spec name; some builds still accept engines as an alias.
if "${RCLI}" backends --help >/dev/null 2>&1; then
    check "backends" "${RCLI}" backends
elif "${RCLI}" engines --help >/dev/null 2>&1; then
    check "engines" "${RCLI}" engines
else
    echo "  FAIL  backends/engines"
    fail=1
fi

if "${RCLI}" definitely-not-a-command >/dev/null 2>&1; then
    echo "  FAIL  unknown command was accepted"
    fail=1
else
    echo "  ok    unknown command rejected"
fi

exit "${fail}"
