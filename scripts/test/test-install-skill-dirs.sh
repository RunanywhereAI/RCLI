#!/usr/bin/env bash
# Proves install.sh installs the RunAnywhere skill into the agent homes that
# exist: ~/.claude (Claude Code), ~/.agents (Cursor / Codex), both, or a
# ~/.claude default on a fresh machine with neither. Drives the installer's
# --print-skill-dirs seam against throwaway HOMEs; no network, no brew.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL="${SCRIPT_DIR}/../../install.sh"

fails=0
run() { HOME="$1" sh "$INSTALL" --print-skill-dirs; }
check() {
    name="$1"; expected="$2"; actual="$3"
    if [ "$expected" = "$actual" ]; then
        printf 'ok   %s\n' "$name"
    else
        printf 'FAIL %s\n  expected: %s\n  actual:   %s\n' "$name" "$expected" "$actual"
        fails=$((fails + 1))
    fi
}

t="$(mktemp -d)"; mkdir -p "$t/.claude"
check "claude-only" "$t/.claude/skills/runanywhere" "$(run "$t")"

t="$(mktemp -d)"; mkdir -p "$t/.agents"
check "agents-only" "$t/.agents/skills/runanywhere" "$(run "$t")"

t="$(mktemp -d)"; mkdir -p "$t/.claude" "$t/.agents"
check "both" "$(printf '%s\n%s' "$t/.claude/skills/runanywhere" "$t/.agents/skills/runanywhere")" "$(run "$t")"

t="$(mktemp -d)"
check "neither-defaults-claude" "$t/.claude/skills/runanywhere" "$(run "$t")"

[ "$fails" -eq 0 ] || { printf '%d test(s) failed\n' "$fails" >&2; exit 1; }
printf 'all skill-dir cases pass\n'
