#!/usr/bin/env bash
set -euo pipefail

REPO="RunanywhereAI/RCLI"
TAP="RunanywhereAI/rcli"
# Fully qualified on purpose. runanywhereai/tap also provides a formula called
# rcli, and a bare `brew install rcli` on a machine with both taps fails with
# "Formulae found in multiple taps" rather than picking one.
FORMULA="runanywhereai/rcli/rcli"

info()  { printf "\033[1;34m==>\033[0m \033[1m%s\033[0m\n" "$*"; }
ok()    { printf "\033[1;32m==>\033[0m %s\n" "$*"; }
warn()  { printf "\033[1;33mWarning:\033[0m %s\n" "$*"; }
fail()  { printf "\033[1;31mError:\033[0m %s\n" "$*" >&2; exit 1; }

info "Checking latest RCLI release..."
VERSION=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
    | grep '"tag_name"' \
    | sed 's/.*"v\([^"]*\)".*/\1/')
[[ -n "$VERSION" ]] || fail "Could not determine latest release version. Check your internet connection."
info "Latest version: v${VERSION}"

os=$(uname -s)
arch=$(uname -m)
case "${os}/${arch}" in
    # MLX is Metal and NeuRT is the Apple Neural Engine, so an Intel Mac would
    # get neither and there is no build for it. No Linux release is published.
    Darwin/arm64)  ;;
    Darwin/*)      fail "RCLI needs an Apple Silicon Mac. Detected: ${arch}" ;;
    Linux/*)       fail "RCLI does not currently publish a Linux binary. Build from source: https://github.com/${REPO}#build-from-source" ;;
    *)             fail "RCLI has no build for ${os}. On Windows, use install.ps1." ;;
esac

if ! command -v brew &>/dev/null; then
    info "Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    # Homebrew installs to a different prefix on each platform and does not put
    # itself on PATH, so the shellenv has to come from wherever it landed.
    for prefix in /opt/homebrew /home/linuxbrew/.linuxbrew /usr/local; do
        if [ -x "${prefix}/bin/brew" ]; then
            eval "$("${prefix}/bin/brew" shellenv)"
            break
        fi
    done
    command -v brew &>/dev/null || fail "Homebrew installed but is not on PATH. Open a new shell and run this again."
fi

info "Tapping $TAP..."
brew tap "$TAP" "https://github.com/$REPO.git" 2>/dev/null || true

# Force-update the tap so Homebrew sees the latest formula
brew update --force 2>/dev/null || true

info "Installing RCLI v${VERSION}..."
if brew upgrade "$FORMULA" 2>/dev/null || brew install "$FORMULA" 2>/dev/null; then
    ok "Installed via Homebrew"
else
    # There used to be a fallback here that unpacked the tarball into the Cellar
    # by hand. Homebrew is the only supported path now: the formula puts rcli and
    # mlx-swift_Cmlx.bundle in libexec and symlinks bin/rcli at the binary, and
    # if the bundle does not end up beside the executable MLX quietly reports
    # itself unavailable while the other five engines carry on. That is not a
    # failure worth risking in an installer that cannot test for it.
    warn "brew install failed. Re-running it with the output shown:"
    brew install "$FORMULA" || true
    echo ""
    echo "  Fix what Homebrew reported above, then run:"
    echo "    brew tap $TAP https://github.com/$REPO.git"
    echo "    brew install $FORMULA"
    echo ""
    echo ""
    fail "Could not install $FORMULA. If it still fails, open an issue at https://github.com/$REPO/issues with the output above."
fi

if ! command -v rcli &>/dev/null; then
    fail "Installation failed. rcli not found in PATH."
fi

installed_version="$(rcli --version 2>/dev/null \
    | sed -nE 's/^rcli ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' \
    | head -1)"
if [[ "${installed_version}" != "${VERSION}" ]]; then
    fail "Homebrew installed RCLI v${installed_version:-unknown}, but GitHub's latest release is v${VERSION}. The tap formula must be updated before this installer can claim success."
fi

ok "RCLI v${VERSION} installed successfully"

# The skill is what makes the next step self-explanatory in Claude Code: it
# teaches the assistant the commands, the harnesses, and what to do when one is
# missing. Installed unconditionally — it is a doc file, and it is the thing the
# person was promised when they copied one line off the website.
SKILL_DIR="${HOME}/.claude/skills/runanywhere"
info "Installing the RunAnywhere skill for Claude Code..."
if mkdir -p "$SKILL_DIR" 2>/dev/null &&
   curl -fsSL "https://raw.githubusercontent.com/${REPO}/main/skills/runanywhere/SKILL.md" \
        -o "${SKILL_DIR}/SKILL.md"; then
    ok "Skill installed at ${SKILL_DIR}/SKILL.md"
else
    warn "Could not install the Claude skill. Everything else still works."
fi

# Signing in is the point of the whole flow, so it happens here rather than
# being left as an instruction the person has to notice. Already signed in is a
# no-op, and a failure is not fatal: the CLI is installed either way.
echo ""
if rcli whoami >/dev/null 2>&1; then
    ok "Already signed in"
else
    info "Opening the console to sign in..."
    rcli login || warn "Sign-in did not finish. Run \`rcli login\` when you are ready."
fi

echo ""
info "Getting started:"
echo "    rcli opencode --cloud -m glm-5.3   code against a hosted model"
echo "    rcli usage                         credit left and what you spent"
echo "    rcli pull qwen3-0.6b               download a model to this machine"
echo "    rcli run qwen3-0.6b                talk to it, offline"
echo ""
echo "  Models download on demand into ~/.local/share/runanywhere"
echo "  In Claude Code, ask: \"get me started with RunAnywhere\""
