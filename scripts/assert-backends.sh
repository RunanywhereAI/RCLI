#!/usr/bin/env bash
# Assert `wally backends` lists the given engine names.
#
#   scripts/assert-backends.sh <path-to-wally> <name> [<name>...]
set -euo pipefail

WALLY="${1:?usage: assert-backends.sh <wally> <name>...}"
shift
if [[ $# -eq 0 ]]; then
    echo "usage: assert-backends.sh <wally> <name>..." >&2
    exit 2
fi
if [[ ! -e "${WALLY}" ]]; then
    echo "not found: ${WALLY}" >&2
    exit 1
fi

out="$(mktemp)"
trap 'rm -f "${out}"' EXIT
json=""
if "${WALLY}" --json backends >"${out}" 2>/dev/null; then
    json="$(cat "${out}")"
elif "${WALLY}" backends --json >"${out}" 2>/dev/null; then
    json="$(cat "${out}")"
else
    json="$("${WALLY}" backends 2>/dev/null || true)"
fi

if [[ -z "${json}" ]]; then
    echo "FAIL  backends produced no output" >&2
    exit 1
fi

echo "backends:"
printf '%s\n' "${json}" | sed 's/^/  /'

fail=0
for name in "$@"; do
    safe_name="$(printf '%s' "${name}" | sed 's/[][\\.^$*+?(){}|]/\\&/g')"
    if printf '%s\n' "${json}" | grep -qiE "\"name\"[[:space:]]*:[[:space:]]*\"${safe_name}\"|[[:space:]]${safe_name}([,[:space:]]|$)"; then
        echo "  ok    ${name}"
    else
        echo "  FAIL  ${name} not registered"
        fail=1
    fi
done
exit "${fail}"
