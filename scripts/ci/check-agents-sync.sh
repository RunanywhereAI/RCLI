#!/usr/bin/env bash
#
# check-agents-sync.sh — keep AGENTS.md/CLAUDE.md and .claude/.agents skills identical.
#
# CLAUDE.md is a symlink to the sibling AGENTS.md (same pattern as the SDK
# repo). .claude/skills/ is canonical; .agents/skills/ is a generated copy
# (not a directory symlink) so Windows checkouts without core.symlinks still
# work. CI fails if either pair drifts.
#
# Usage:
#   scripts/ci/check-agents-sync.sh          check (CI); exit 1 on drift
#   scripts/ci/check-agents-sync.sh --fix    recreate the symlink and mirror skills
#   scripts/ci/check-agents-sync.sh --help
#
# Exit codes: 0 ok/fixed | 1 drift | 2 tooling error
set -euo pipefail

usage() {
  cat <<'EOF'
check-agents-sync.sh — AGENTS.md ↔ CLAUDE.md and .claude/skills ↔ .agents/skills.

  scripts/ci/check-agents-sync.sh          check (CI / pre-commit)
  scripts/ci/check-agents-sync.sh --fix    repair symlink + regenerate the skills mirror
  scripts/ci/check-agents-sync.sh --help

Exit codes: 0 ok/fixed | 1 drift | 2 tooling error
EOF
}

MODE=check
case "${1:-}" in
  ""|--check) MODE=check ;;
  --fix)      MODE=fix ;;
  -h|--help)  usage; exit 0 ;;
  *) printf 'error: unknown argument: %s\n' "$1" >&2; exit 2 ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

command -v git >/dev/null 2>&1 || { printf 'error: git not found on PATH\n' >&2; exit 2; }
git rev-parse --is-inside-work-tree >/dev/null 2>&1 \
  || { printf 'error: %s is not a git work tree\n' "${ROOT}" >&2; exit 2; }

status=0

is_tracked() { git ls-files --error-unmatch -- "$1" >/dev/null 2>&1; }

# --- AGENTS.md / CLAUDE.md ---------------------------------------------------
if [ ! -f AGENTS.md ]; then
  printf 'error: AGENTS.md is missing\n' >&2
  exit 1
fi

claude_ok=0
if [ -L CLAUDE.md ] && [ "$(readlink CLAUDE.md)" = "AGENTS.md" ]; then
  claude_ok=1
fi

if [ "${MODE}" = fix ]; then
  rm -f CLAUDE.md
  ln -s AGENTS.md CLAUDE.md
  printf 'linked CLAUDE.md -> AGENTS.md\n'
else
  if [ "${claude_ok}" -ne 1 ]; then
    if [ ! -e CLAUDE.md ]; then
      printf 'error: missing CLAUDE.md (should be a symlink -> AGENTS.md; run --fix)\n' >&2
    elif [ ! -L CLAUDE.md ]; then
      printf 'error: CLAUDE.md is a regular file, not a symlink -> AGENTS.md (run --fix)\n' >&2
    else
      printf 'error: CLAUDE.md points to %s, expected AGENTS.md (run --fix)\n' "$(readlink CLAUDE.md)" >&2
    fi
    status=1
  elif ! is_tracked CLAUDE.md; then
    printf 'error: CLAUDE.md is a correct symlink but untracked — git add CLAUDE.md\n' >&2
    status=1
  fi
fi

# --- skills trees ------------------------------------------------------------
SRC=".claude/skills"
DST=".agents/skills"

if [ "${MODE}" = fix ]; then
  if [ ! -d "${SRC}" ]; then
    printf 'error: %s is missing; create canonical skills before --fix\n' "${SRC}" >&2
    exit 1
  fi
  rm -rf "${DST}"
  mkdir -p "$(dirname "${DST}")"
  cp -R "${SRC}" "${DST}"
  printf 'mirrored %s -> %s\n' "${SRC}" "${DST}"
  exit "${status}"
fi

if [ ! -d "${SRC}" ]; then
  printf 'error: missing canonical skills tree %s\n' "${SRC}" >&2
  exit 1
fi
if [ ! -d "${DST}" ]; then
  printf 'error: missing skills mirror %s (run --fix)\n' "${DST}" >&2
  exit 1
fi
if ! is_tracked "${SRC}/rcli-architecture/SKILL.md" || ! is_tracked "${DST}/rcli-architecture/SKILL.md"; then
  printf 'error: skill trees must be git-tracked (got ignored? check .gitignore)\n' >&2
  status=1
fi

if ! diff -rq "${SRC}" "${DST}" >/dev/null 2>&1; then
  printf 'error: %s and %s differ. Edit .claude/skills, then run --fix.\n' "${SRC}" "${DST}" >&2
  diff -rq "${SRC}" "${DST}" >&2 || true
  status=1
fi

if [ "${status}" -ne 0 ]; then
  printf '\nFix locally with:\n  bash scripts/ci/check-agents-sync.sh --fix\n  git add AGENTS.md CLAUDE.md .claude/skills .agents/skills\n' >&2
  exit 1
fi

printf 'AGENTS.md/CLAUDE.md symlink ok; .claude/skills and .agents/skills match.\n'
