#!/usr/bin/env bash
# Kit-consumer e2e for a built rcli binary. Does not compile the SDK.
#
#   scripts/e2e.sh <path-to-rcli>
#
# Always runs scripts/smoke.sh (no model download). Set RCLI_E2E_MODEL to also
# pull a catalog model and run one generation — that needs network + disk.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RCLI="${1:?usage: e2e.sh <path-to-rcli>}"
if [[ ! -e "${RCLI}" ]]; then
    echo "not found: ${RCLI}" >&2
    exit 1
fi
if [[ ! -x "${RCLI}" && "${RCLI}" != *.exe && "${RCLI}" != *.EXE ]]; then
    echo "not executable: ${RCLI}" >&2
    exit 1
fi

bash "${ROOT}/scripts/smoke.sh" "${RCLI}"

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

echo "e2e (modelless extras): ${RCLI}"
check "info" "${RCLI}" info
if "${RCLI}" models list --help >/dev/null 2>&1; then
    check "models list" "${RCLI}" models list
fi

if [[ -n "${RCLI_E2E_MODEL:-}" ]]; then
    echo "e2e (model): ${RCLI_E2E_MODEL}"
    export RUNANYWHERE_HOME="${RUNANYWHERE_HOME:-$(mktemp -d)}"
    if "${RCLI}" models download "${RCLI_E2E_MODEL}"; then
        echo "  ok    models download ${RCLI_E2E_MODEL}"
    elif "${RCLI}" pull "${RCLI_E2E_MODEL}"; then
        echo "  ok    pull ${RCLI_E2E_MODEL}"
    else
        echo "  FAIL  download ${RCLI_E2E_MODEL}"
        fail=1
    fi
    if "${RCLI}" llm generate --model "${RCLI_E2E_MODEL}" "Reply with exactly: ok" --max-output-tokens 8 >/dev/null 2>&1 \
        || "${RCLI}" run "${RCLI_E2E_MODEL}" "Reply with exactly: ok" >/dev/null 2>&1; then
        echo "  ok    generate"
    else
        echo "  FAIL  generate"
        fail=1
    fi
else
    echo "  skip  model round-trip (set RCLI_E2E_MODEL to enable)"
fi

exit "${fail}"
