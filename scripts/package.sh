#!/usr/bin/env bash
# Builds the release archive for the host platform.
#
#   scripts/package.sh [build-dir]
#
# Layout is the same shape everywhere, because Formula/rcli.rb installs all of
# libexec/ and symlinks bin/rcli at the executable regardless of platform:
#
#   libexec/rcli
#   libexec/mlx-swift_Cmlx.bundle   macOS: the Metal shaders MLX keeps outside
#                                   the binary
#   libexec/lib/*.so                Linux: whatever the binary actually links,
#                                   found with ldd and reached through an
#                                   $ORIGIN/lib rpath
#
# Windows is packaged by scripts/package-windows.ps1 instead: it produces a zip
# rather than a tarball and has DLLs to collect rather than an rpath to set.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-$PROJECT_DIR/build}"

# CMakeLists carries the release version, but a prerelease tag adds a suffix
# CMake's VERSION field cannot hold (v0.4.1-beta.1). RCLI_VERSION lets the
# caller name the archive after the tag instead, so the asset and the formula
# agree.
VERSION="${RCLI_VERSION:-$(grep 'project(rcli VERSION' "$PROJECT_DIR/CMakeLists.txt" | sed 's/.*VERSION \([0-9.]*\).*/\1/')}"

case "$(uname -s)" in
    Darwin) PLATFORM="macos-$(uname -m)" ;;
    Linux)  PLATFORM="linux-$(uname -m)" ;;
    *)      echo "ERROR: $(uname -s) is not packaged by this script" >&2; exit 1 ;;
esac

NAME="rcli-${VERSION}-${PLATFORM}"
DIST="$PROJECT_DIR/dist/$NAME"

if [ ! -x "$BUILD_DIR/rcli" ]; then
    echo "ERROR: $BUILD_DIR/rcli not found." >&2
    [ "$(uname -s)" = "Darwin" ] && echo "       Run scripts/build-mlx.sh first." >&2
    exit 1
fi

echo "Packaging rcli v${VERSION} for ${PLATFORM}"

rm -rf "$PROJECT_DIR/dist"
mkdir -p "$DIST/libexec"
cp "$BUILD_DIR/rcli" "$DIST/libexec/rcli"
echo "  + libexec/rcli"

if [ "$(uname -s)" = "Darwin" ]; then
    # Refuse to ship the CMake-only binary by mistake: it is the same
    # application but MLX cannot register in it, and the difference is invisible
    # until someone tries to run an MLX model.
    #
    # The priority column is what makes this a real check. A linked but inert
    # MLX still prints a row, so a bare '^mlx' match passes on exactly the
    # binary this is meant to reject.
    if ! "$BUILD_DIR/rcli" engines 2>/dev/null | grep -qE '^mlx[[:space:]]+[0-9]+'; then
        echo "ERROR: $BUILD_DIR/rcli has no MLX engine." >&2
        echo "       Run scripts/build-mlx.sh — cmake alone cannot link it." >&2
        exit 1
    fi

    cp -R "$BUILD_DIR/mlx-swift_Cmlx.bundle" "$DIST/libexec/"
    echo "  + libexec/mlx-swift_Cmlx.bundle"

    # Anything outside the system frameworks would have to be shipped too, and
    # nothing here should be.
    STRAY=$(otool -L "$DIST/libexec/rcli" | tail -n +2 | awk '{print $1}' |
            grep -vE '^/usr/lib/|^/System/Library/' || true)
    if [ -n "$STRAY" ]; then
        echo "ERROR: the binary depends on libraries that are not being packaged:" >&2
        echo "$STRAY" | sed 's/^/       /' >&2
        exit 1
    fi

    # Ad-hoc, which is what Gatekeeper needs for a Homebrew install; a browser
    # download would additionally need notarization with a Developer ID.
    codesign --force --sign - "$DIST/libexec/rcli"
else
    # Sherpa and onnxruntime arrive as shared objects on Linux, so whatever the
    # binary links outside the base system travels with it. Read the list from
    # ldd rather than naming files: a hardcoded list goes stale silently and the
    # failure lands on the user as a missing .so at startup.
    command -v patchelf >/dev/null 2>&1 || {
        echo "ERROR: patchelf is needed to make the Linux package relocatable" >&2
        exit 1
    }
    mkdir -p "$DIST/libexec/lib"
    # Everything glibc and the kernel provide stays out: shipping our own libc
    # against the host's loader is how a package breaks on a different distro.
    ldd "$DIST/libexec/rcli" | awk '/=>/ {print $3}' | grep -E '^/' | sort -u |
        grep -vE '/lib(c|m|dl|rt|pthread|gcc_s|stdc\+\+)\.so|ld-linux' |
    while read -r lib; do
        cp -L "$lib" "$DIST/libexec/lib/"
        echo "  + libexec/lib/$(basename "$lib")"
    done
    patchelf --set-rpath '$ORIGIN/lib' "$DIST/libexec/rcli"
    for lib in "$DIST/libexec/lib"/*.so*; do
        [ -e "$lib" ] || continue
        patchelf --set-rpath '$ORIGIN' "$lib"
    done

    # Prove the staged copy runs from its own directory rather than off the
    # build tree, which is the whole point of the rpath rewrite.
    if ! ( cd "$DIST/libexec" && ./rcli --version > /dev/null 2>&1 ); then
        echo "ERROR: the staged binary does not run; the rpath is wrong" >&2
        exit 1
    fi
fi

cd "$PROJECT_DIR/dist"
tar czf "${NAME}.tar.gz" "$NAME"
if command -v shasum > /dev/null 2>&1; then
    SHA256=$(shasum -a 256 "${NAME}.tar.gz" | awk '{print $1}')
else
    SHA256=$(sha256sum "${NAME}.tar.gz" | awk '{print $1}')
fi

echo
echo "  dist/${NAME}.tar.gz  ($(du -h "${NAME}.tar.gz" | awk '{print $1}'))"
echo "  sha256 \"$SHA256\""
