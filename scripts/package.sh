#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

VERSION=$(grep 'project(rcli VERSION' "$PROJECT_DIR/CMakeLists.txt" | sed 's/.*VERSION \([0-9.]*\).*/\1/')
ARCH=$(uname -m)
TARBALL_NAME="rcli-${VERSION}-Darwin-${ARCH}"
DIST_DIR="$PROJECT_DIR/dist/$TARBALL_NAME"

if [ ! -f "$BUILD_DIR/rcli" ]; then
    echo "ERROR: build/rcli not found. Run cmake --build first."
    exit 1
fi

echo "Packaging RCLI v${VERSION} for Darwin-${ARCH}"
echo ""

rm -rf "$PROJECT_DIR/dist"
mkdir -p "$DIST_DIR/bin" "$DIST_DIR/lib"

# --- Collect binary ---
cp "$BUILD_DIR/rcli" "$DIST_DIR/bin/rcli"

# --- Collect dylibs ---
DYLIBS=(
    "$BUILD_DIR/bin/libllama.0.dylib"
    "$BUILD_DIR/bin/libggml.0.dylib"
    "$BUILD_DIR/bin/libggml-base.0.dylib"
    "$BUILD_DIR/bin/libggml-cpu.0.dylib"
    "$BUILD_DIR/bin/libggml-blas.0.dylib"
    "$BUILD_DIR/bin/libggml-metal.0.dylib"
    "$BUILD_DIR/lib/libsherpa-onnx-c-api.dylib"
)

ONNX_DYLIB=$(find "$BUILD_DIR/_deps/onnxruntime-src/lib" -name "libonnxruntime.*.*.dylib" 2>/dev/null | head -1)
if [ -z "$ONNX_DYLIB" ]; then
    ONNX_DYLIB=$(find "$BUILD_DIR/_deps/onnxruntime-src/lib" -name "libonnxruntime.*.dylib" ! -name "libonnxruntime.dylib" 2>/dev/null | head -1)
fi
if [ -z "$ONNX_DYLIB" ]; then
    echo "  WARNING: Could not find versioned libonnxruntime dylib"
fi
DYLIBS+=("$ONNX_DYLIB")

for lib in "${DYLIBS[@]}"; do
    if [ -f "$lib" ]; then
        cp "$lib" "$DIST_DIR/lib/"
        echo "  + lib/$(basename "$lib")"
    else
        echo "  WARNING: $lib not found, skipping"
    fi
done

# Also copy the unversioned onnxruntime symlink if the binary references it
ONNX_BASENAME=$(basename "$ONNX_DYLIB")
if [ ! -f "$DIST_DIR/lib/libonnxruntime.dylib" ] && [ -f "$DIST_DIR/lib/$ONNX_BASENAME" ]; then
    (cd "$DIST_DIR/lib" && ln -sf "$ONNX_BASENAME" libonnxruntime.dylib)
fi

echo ""
echo "Fixing rpaths..."

# --- Fix the main binary ---
BINARY="$DIST_DIR/bin/rcli"

# Remove all existing rpaths from binary
RPATHS=$(otool -l "$BINARY" | grep -A2 LC_RPATH | grep "path " | awk '{print $2}' || true)
for rpath in $RPATHS; do
    install_name_tool -delete_rpath "$rpath" "$BINARY" 2>/dev/null || true
done

# Add the single correct rpath
install_name_tool -add_rpath "@executable_path/../lib" "$BINARY" 2>/dev/null || true
echo "  fixed: rcli binary"

# --- Fix dylib cross-references ---
# Each dylib uses @rpath/libXXX to reference others.
# We need dylibs to find each other in the same lib/ directory.
for lib in "$DIST_DIR/lib/"*.dylib; do
    [ -L "$lib" ] && continue  # skip symlinks
    libname=$(basename "$lib")

    # Remove existing absolute rpaths
    LIB_RPATHS=$(otool -l "$lib" | grep -A2 LC_RPATH | grep "path " | awk '{print $2}' || true)
    for rpath in $LIB_RPATHS; do
        install_name_tool -delete_rpath "$rpath" "$lib" 2>/dev/null || true
    done

    # Add @loader_path so dylibs find each other in the same directory
    install_name_tool -add_rpath "@loader_path" "$lib" 2>/dev/null || true

    # Fix the install name to use @rpath (most already do, but ensure consistency)
    current_id=$(otool -D "$lib" | tail -1)
    if [[ "$current_id" != "@rpath/"* ]]; then
        install_name_tool -id "@rpath/$libname" "$lib" 2>/dev/null || true
    fi

    echo "  fixed: $libname"
done

# --- Ad-hoc codesign everything (required for macOS Gatekeeper) ---
echo ""
echo "Codesigning..."
codesign --force --sign - "$BINARY"
for lib in "$DIST_DIR/lib/"*.dylib; do
    codesign --force --sign - "$lib"
done

# --- Create tarball ---
echo ""
echo "Creating tarball..."
cd "$PROJECT_DIR/dist"
tar czf "${TARBALL_NAME}.tar.gz" "$TARBALL_NAME"

SHA256=$(shasum -a 256 "${TARBALL_NAME}.tar.gz" | awk '{print $1}')
SIZE=$(du -h "${TARBALL_NAME}.tar.gz" | awk '{print $1}')

echo ""
echo "========================================"
echo "  Package: dist/${TARBALL_NAME}.tar.gz"
echo "  Size:    $SIZE"
echo "  SHA256:  $SHA256"
echo "========================================"
echo ""
echo "Tarball contents:"
tar tzf "${TARBALL_NAME}.tar.gz" | head -20
echo ""
echo "Update Formula/rcli.rb with:"
echo "  sha256 \"$SHA256\""
