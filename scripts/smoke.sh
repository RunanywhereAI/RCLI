#!/usr/bin/env bash
# Exercises the command surface without downloading a model.
#
#   smoke.sh <path-to-rcli>
#
# No model is fetched because the smallest is hundreds of megabytes and this is
# not testing generation. What it does test is that every command parses,
# reaches the SDK, and exits cleanly, which is where a broken build or a missing
# engine actually shows up. Generation is covered separately before a release.
#
# Runs on macOS, Linux and Windows (through Git Bash), so nothing here may
# assume a POSIX-only tool or a tty.
set -euo pipefail

RCLI="${1:?usage: smoke.sh <path-to-rcli>}"
[ -x "${RCLI}" ] || { echo "not executable: ${RCLI}" >&2; exit 1; }

fail=0
check() {
    local label="$1"; shift
    if "$@" > /dev/null 2>&1; then
        echo "  ok    ${label}"
    else
        echo "  FAIL  ${label}"
        fail=1
    fi
}

echo "smoke: ${RCLI}"
check "--version"        "${RCLI}" --version
check "--help"           "${RCLI}" --help
check "list"             "${RCLI}" list
check "list --all"       "${RCLI}" list --all
check "search"           "${RCLI}" search qwen
check "engines"          "${RCLI}" engines
check "where"            "${RCLI}" where
check "config"           "${RCLI}" config

# The catalog is compiled in, so a model that is not installed still resolves.
check "show"             "${RCLI}" show qwen3-0.6b

# stdin is not a tty here, so the REPL takes the getline path and /bye exits it.
# This is the one check that would catch the REPL failing to start at all.
if printf '/bye\n' | "${RCLI}" run > /dev/null 2>&1; then
    echo "  ok    run (repl exits on /bye)"
else
    echo "  FAIL  run (repl exits on /bye)"
    fail=1
fi

# An unknown command must be rejected rather than silently accepted.
if "${RCLI}" definitely-not-a-command > /dev/null 2>&1; then
    echo "  FAIL  unknown command was accepted"
    fail=1
else
    echo "  ok    unknown command rejected"
fi

exit "${fail}"
