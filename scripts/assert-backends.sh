#!/usr/bin/env bash
# Assert `rcli backends` lists the given engine names.
#
#   scripts/assert-backends.sh <path-to-rcli> <name> [<name>...]
set -euo pipefail

RCLI="${1:?usage: assert-backends.sh <rcli> <name>...}"
shift
if [[ $# -eq 0 ]]; then
    echo "usage: assert-backends.sh <rcli> <name>..." >&2
    exit 2
fi
if [[ ! -e "${RCLI}" ]]; then
    echo "not found: ${RCLI}" >&2
    exit 1
fi

out="$(mktemp)"
trap 'rm -f "${out}"' EXIT
json=""
if "${RCLI}" --json backends >"${out}" 2>/dev/null; then
    json="$(cat "${out}")"
elif "${RCLI}" backends --json >"${out}" 2>/dev/null; then
    json="$(cat "${out}")"
else
    json="$("${RCLI}" backends 2>/dev/null || true)"
fi

if [[ -z "${json}" ]]; then
    echo "FAIL  backends produced no output" >&2
    exit 1
fi

echo "backends:"
printf '%s\n' "${json}" | sed 's/^/  /'

fail=0
for name in "$@"; do
    if printf '%s\n' "${json}" | grep -qiE "\"name\"[[:space:]]*:[[:space:]]*\"${name}\"|[[:space:]]${name}([,[:space:]]|$)"; then
        echo "  ok    ${name}"
    else
        echo "  FAIL  ${name} not registered"
        fail=1
    fi
done
exit "${fail}"
