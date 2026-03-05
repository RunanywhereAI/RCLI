#!/usr/bin/env bash
set -euo pipefail

VERSION="0.1.4"
REPO="RunanywhereAI/RCLI"
TAP="RunanywhereAI/rcli"
FORMULA="rcli"

info()  { printf "\033[1;34m==>\033[0m \033[1m%s\033[0m\n" "$*"; }
ok()    { printf "\033[1;32m==>\033[0m %s\n" "$*"; }
warn()  { printf "\033[1;33mWarning:\033[0m %s\n" "$*"; }
fail()  { printf "\033[1;31mError:\033[0m %s\n" "$*" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || fail "RCLI requires macOS. Detected: $(uname -s)"

arch=$(uname -m)
[[ "$arch" == "arm64" ]] || fail "RCLI requires Apple Silicon (M1+). Detected: $arch"

if ! command -v brew &>/dev/null; then
    info "Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    eval "$(/opt/homebrew/bin/brew shellenv)"
fi

info "Tapping $TAP..."
brew tap "$TAP" "https://github.com/$REPO.git" 2>/dev/null || true

info "Installing RCLI v${VERSION}..."
if brew install "$FORMULA" 2>/dev/null; then
    ok "Installed via Homebrew"
else
    warn "brew install failed (likely macOS 26 CLT issue). Installing manually..."

    brew fetch "$FORMULA" 2>/dev/null || true

    CACHED="$(brew --cache "$FORMULA" 2>/dev/null || true)"

    if [[ -z "$CACHED" || ! -f "$CACHED" ]]; then
        info "Downloading tarball directly..."
        CACHED="/tmp/rcli-${VERSION}.tar.gz"
        curl -fsSL -o "$CACHED" \
            "https://github.com/$REPO/releases/download/v${VERSION}/rcli-${VERSION}-Darwin-arm64.tar.gz"
    fi

    WORKDIR=$(mktemp -d)
    tar xzf "$CACHED" -C "$WORKDIR"

    CELLAR="/opt/homebrew/Cellar/$FORMULA/$VERSION"
    rm -rf "$CELLAR" 2>/dev/null || sudo rm -rf "$CELLAR"
    mkdir -p "$CELLAR/bin" "$CELLAR/lib" 2>/dev/null || sudo mkdir -p "$CELLAR/bin" "$CELLAR/lib"
    cp "$WORKDIR"/rcli-*/bin/rcli "$CELLAR/bin/" 2>/dev/null || sudo cp "$WORKDIR"/rcli-*/bin/rcli "$CELLAR/bin/"
    cp "$WORKDIR"/rcli-*/lib/*.dylib "$CELLAR/lib/" 2>/dev/null || sudo cp "$WORKDIR"/rcli-*/lib/*.dylib "$CELLAR/lib/"

    brew link --overwrite "$FORMULA" 2>/dev/null || sudo brew link --overwrite "$FORMULA"

    rm -rf "$WORKDIR"
    ok "Installed manually into Homebrew Cellar"
fi

if ! command -v rcli &>/dev/null; then
    fail "Installation failed. rcli not found in PATH."
fi

ok "RCLI v${VERSION} installed successfully"
echo ""
info "Downloading AI models (~1GB, one-time)..."
rcli setup
