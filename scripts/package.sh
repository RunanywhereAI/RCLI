#!/usr/bin/env bash
# Builds the release tarball: one rcli binary, plus the Metal shaders MLX keeps
# in a resource bundle rather than inside the executable.
#
# Nothing else travels with it. The binary links commons, llama.cpp, sherpa,
# ONNX and NeuRT statically, so the dylib collection and rpath surgery this
# script used to do is gone along with the engine it did it for.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-$PROJECT_DIR/build}"

# CMakeLists carries the release version, but a prerelease tag adds a suffix
# CMake's VERSION field cannot hold (v0.4.1-beta.1). RCLI_VERSION lets the
# caller name the tarball after the tag instead, so the asset and the formula
# agree.
VERSION="${RCLI_VERSION:-$(grep 'project(rcli VERSION' "$PROJECT_DIR/CMakeLists.txt" | sed 's/.*VERSION \([0-9.]*\).*/\1/')}"
ARCH=$(uname -m)
NAME="rcli-${VERSION}-Darwin-${ARCH}"
DIST="$PROJECT_DIR/dist/$NAME"

if [ ! -x "$BUILD_DIR/rcli" ]; then
    echo "ERROR: $BUILD_DIR/rcli not found. Run scripts/build-mlx.sh first." >&2
    exit 1
fi

# Refuse to ship the CMake-only binary by mistake: it is the same application
# but MLX cannot register in it, and the difference is invisible until someone
# tries to run an MLX model.
#
# The priority column is what makes this a real check. A linked but inert MLX
# still prints a row, so a bare '^mlx' match passes on exactly the binary this
# is meant to reject.
if ! "$BUILD_DIR/rcli" engines 2>/dev/null | grep -qE '^mlx[[:space:]]+[0-9]+'; then
    echo "ERROR: $BUILD_DIR/rcli has no MLX engine." >&2
    echo "       Run scripts/build-mlx.sh — cmake alone cannot link it." >&2
    exit 1
fi

echo "Packaging rcli v${VERSION} for Darwin-${ARCH}"

rm -rf "$PROJECT_DIR/dist"
mkdir -p "$DIST/libexec"

cp "$BUILD_DIR/rcli" "$DIST/libexec/rcli"
cp -R "$BUILD_DIR/mlx-swift_Cmlx.bundle" "$DIST/libexec/"
echo "  + libexec/rcli"
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

cd "$PROJECT_DIR/dist"
tar czf "${NAME}.tar.gz" "$NAME"
SHA256=$(shasum -a 256 "${NAME}.tar.gz" | awk '{print $1}')

echo
echo "  dist/${NAME}.tar.gz  ($(du -h "${NAME}.tar.gz" | awk '{print $1}'))"
echo "  sha256 \"$SHA256\""
