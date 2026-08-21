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
    # get neither and there is no build for it. Linux is x86-64 only for now.
    Darwin/arm64)  ;;
    Linux/x86_64)  ;;
    Darwin/*)      fail "RCLI needs an Apple Silicon Mac. Detected: ${arch}" ;;
    Linux/*)       fail "RCLI on Linux is x86-64 only. Detected: ${arch}" ;;
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

ok "RCLI v${VERSION} installed successfully"
echo ""
info "Getting started:"
echo "    rcli list --all              every model in the catalog"
echo "    rcli pull qwen3-0.6b         download one"
echo "    rcli run qwen3-0.6b          talk to it, /? for commands"
echo "    rcli engines                 which backends are available here"
echo ""
echo "  Models download on demand into ~/.local/share/runanywhere"
