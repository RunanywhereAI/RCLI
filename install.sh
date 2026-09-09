#!/usr/bin/env bash
set -euo pipefail

# Installs Wally from the GitHub release tarball for this OS. No Homebrew and no
# tap: the release bottle already stages `wally` with mlx-swift_Cmlx.bundle
# beside it (Metal shaders) and its shared libraries under ../lib with an rpath
# that finds them, so a plain extract-and-symlink keeps every engine working.
REPO="RunanywhereAI/wally"
LIB_DIR="${HOME}/.local/lib/wally"
BIN_DIR="${HOME}/.local/bin"

info()  { printf "\033[1;34m==>\033[0m \033[1m%s\033[0m\n" "$*"; }
ok()    { printf "\033[1;32m==>\033[0m %s\n" "$*"; }
warn()  { printf "\033[1;33mWarning:\033[0m %s\n" "$*"; }
fail()  { printf "\033[1;31mError:\033[0m %s\n" "$*" >&2; exit 1; }

# Which agent homes get the skill. Claude Code reads ~/.claude/skills; Cursor,
# Codex and other AGENTS.md tools read ~/.agents/skills. Install into every home
# the person already has, so a Codex-only user is not handed a skill their agent
# never reads. A fresh machine with neither is a Claude-first get-started, so it
# defaults to ~/.claude. One dir per line; callers set IFS=newline to be safe
# with a $HOME that contains spaces.
skill_target_dirs() {
    targets=""
    [ -d "${HOME}/.claude" ] && targets="${targets}${HOME}/.claude/skills/runanywhere
"
    [ -d "${HOME}/.agents" ] && targets="${targets}${HOME}/.agents/skills/runanywhere
"
    [ -n "${targets}" ] || targets="${HOME}/.claude/skills/runanywhere
"
    printf '%s' "${targets}"
}

# Debug-only: print the resolved skill targets and exit before any network work.
# Exercised by scripts/test/test-install-skill-dirs.sh. Not part of the
# user-facing flow.
if [ "${1:-}" = "--print-skill-dirs" ]; then
    skill_target_dirs
    exit 0
fi

info "Checking latest Wally release..."
VERSION=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
    | grep '"tag_name"' \
    | sed 's/.*"v\([^"]*\)".*/\1/')
[[ -n "$VERSION" ]] || fail "Could not determine latest release version. Check your internet connection."
info "Latest version: v${VERSION}"

os=$(uname -s)
arch=$(uname -m)
case "${os}/${arch}" in
    Darwin/arm64)              PLATFORM="macos-arm64" ;;
    # MLX is Metal and NeuRT is the Apple Neural Engine, so an Intel Mac gets
    # neither and there is no build for it.
    Darwin/*)                  fail "Wally needs an Apple Silicon Mac. Detected: ${arch}" ;;
    Linux/x86_64 | Linux/amd64) PLATFORM="linux-x86_64" ;;
    Linux/*)                   fail "Wally has no Linux ${arch} build yet — x86_64 only. Build from source: https://github.com/${REPO}#build-from-source" ;;
    *)                         fail "Wally has no build for ${os}. On Windows, use install.ps1." ;;
esac

ASSET="wally-${VERSION}-${PLATFORM}.tar.gz"
URL="https://github.com/${REPO}/releases/download/v${VERSION}/${ASSET}"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

info "Downloading ${ASSET}..."
curl -fSL "$URL" -o "${tmp}/${ASSET}" || fail "Download failed: ${URL}"
curl -fSL "${URL}.sha256" -o "${tmp}/${ASSET}.sha256" || fail "Could not download the checksum for ${ASSET}"

info "Verifying checksum..."
# The sidecar is `<sha>  <filename>`; verify from inside tmp so the name resolves.
( cd "$tmp" && shasum -a 256 -c "${ASSET}.sha256" >/dev/null 2>&1 ) \
    || fail "Checksum verification failed for ${ASSET}. Do not use the download."

info "Extracting..."
tar -xzf "${tmp}/${ASSET}" -C "$tmp"
staged="${tmp}/wally-${PLATFORM}"
[[ -x "${staged}/bin/wally" ]] || fail "Archive did not contain bin/wally as expected."

# Replace the install tree wholesale. rm before copy is deliberate: overwriting a
# code-signed Mach-O in place while a copy may still be mapped kills it with
# SIGKILL (137). A fresh dir sidesteps that.
info "Installing to ${LIB_DIR}..."
rm -rf "$LIB_DIR"
mkdir -p "$(dirname "$LIB_DIR")" "$BIN_DIR"
cp -R "$staged" "$LIB_DIR"
ln -sfn "${LIB_DIR}/bin/wally" "${BIN_DIR}/wally"

# Make wally callable for the rest of this script even if the shell that piped us
# in never had ~/.local/bin on PATH.
export PATH="${BIN_DIR}:${PATH}"

if ! command -v wally >/dev/null 2>&1; then
    fail "Installation failed. wally not found after install."
fi

installed_version="$(wally --version 2>/dev/null \
    | sed -nE 's/^wally ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' \
    | head -1)"
if [[ "${installed_version}" != "${VERSION}" ]]; then
    fail "Installed Wally v${installed_version:-unknown}, but the latest release is v${VERSION}."
fi

ok "Wally v${VERSION} installed successfully"

# Put ~/.local/bin on PATH for future shells if it is not already there.
case ":${PATH}:" in
    *":${BIN_DIR}:"*)
        : ;;
    *)
        line='export PATH="$HOME/.local/bin:$PATH"'
        case "$(basename "${SHELL:-}")" in
            zsh)  rc="${HOME}/.zshrc" ;;
            bash) rc="${HOME}/.bashrc" ;;
            *)    rc="${HOME}/.profile" ;;
        esac
        if [ -w "$rc" ] || [ ! -e "$rc" ]; then
            printf '\n# Added by the Wally installer\n%s\n' "$line" >> "$rc"
            warn "${BIN_DIR} was not on your PATH. Added it to ${rc} — open a new shell, or run: ${line}"
        else
            warn "${BIN_DIR} is not on your PATH. Add this line to your shell profile: ${line}"
        fi
        ;;
esac

# The skill is what makes the next step self-explanatory in Claude Code: it
# teaches the assistant the commands, the harnesses, and what to do when one is
# missing. Installed unconditionally — it is a doc file, and it is the thing the
# person was promised when they copied one line off the website.
#
# Pulled from the release tag, not from main. Claude Code follows this file's
# instructions when the skill runs, so fetching it off a moving branch means a
# push to main changes what an already-installed assistant does. The tag is the
# same one the binary above came from, so the two cannot drift apart either.
SKILL_URL="https://raw.githubusercontent.com/${REPO}/v${VERSION}/skills/runanywhere/SKILL.md"
info "Installing the RunAnywhere skill for your coding agent..."
skill_installed=0
old_ifs="$IFS"
IFS='
'
for skill_dir in $(skill_target_dirs); do
    IFS="$old_ifs"
    if mkdir -p "$skill_dir" 2>/dev/null && curl -fsSL "$SKILL_URL" -o "${skill_dir}/SKILL.md"; then
        ok "Skill installed at ${skill_dir}/SKILL.md"
        skill_installed=1
    else
        warn "Could not install the skill at ${skill_dir}."
    fi
    IFS='
'
done
IFS="$old_ifs"
[ "$skill_installed" -eq 1 ] || warn "Could not install the RunAnywhere skill. Everything else still works."

# Signing in is the point of the whole flow, so it happens here rather than
# being left as an instruction the person has to notice. Already signed in is a
# no-op, and a failure is not fatal: the CLI is installed either way.
echo ""
if wally whoami >/dev/null 2>&1; then
    ok "Already signed in"
elif [[ ! -t 0 || ! -t 1 ]]; then
    # No terminal: piped into bash over SSH, or a CI step. The browser flow
    # would try to open a browser that is not there and then block until the
    # request expires, which reads as the installer hanging.
    info "Not an interactive terminal, so sign-in is left to you."
    echo "    wally login              sign in from a machine with a browser"
    echo "    wally login --no-browser print the URL and approve it elsewhere"
else
    info "Opening the console to sign in..."
    wally login || warn "Sign-in did not finish. Run \`wally login\` when you are ready."
fi

echo ""
info "Getting started:"
echo "    wally opencode --cloud -m glm-5.3   code against a hosted model"
echo "    wally usage                         credit left and what you spent"
echo "    wally pull qwen3-0.6b               download a model to this machine"
echo "    wally run qwen3-0.6b                talk to it, offline"
echo ""
echo "  Models download on demand into ~/.local/share/runanywhere"
echo "  In Claude Code, ask: \"get me started with RunAnywhere\""
