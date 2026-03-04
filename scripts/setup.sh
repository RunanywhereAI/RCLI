#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DEPS_DIR="$PROJECT_DIR/deps"

echo ""
echo "  ██████╗  ██████╗██╗     ██╗"
echo "  ██╔══██╗██╔════╝██║     ██║"
echo "  ██████╔╝██║     ██║     ██║"
echo "  ██╔══██╗██║     ██║     ██║"
echo "  ██║  ██║╚██████╗███████╗██║"
echo "  ╚═╝  ╚═╝ ╚═════╝╚══════╝╚═╝"
echo "  Setup  •  Powered by RunAnywhere"
echo ""

mkdir -p "$DEPS_DIR"

# --- 1. Clone llama.cpp ---
if [ ! -d "$DEPS_DIR/llama.cpp" ]; then
    echo "[1/2] Cloning llama.cpp (LLM + embedding inference)..."
    git clone --depth 1 https://github.com/ggml-org/llama.cpp "$DEPS_DIR/llama.cpp"
    echo "  -> Done"
else
    echo "[1/2] llama.cpp already present"
fi

# --- 2. Clone sherpa-onnx ---
if [ ! -d "$DEPS_DIR/sherpa-onnx" ]; then
    echo "[2/2] Cloning sherpa-onnx (STT + TTS + VAD)..."
    git clone --depth 1 https://github.com/k2-fsa/sherpa-onnx "$DEPS_DIR/sherpa-onnx"
    echo "  -> Done"
else
    echo "[2/2] sherpa-onnx already present"
fi

echo ""
echo "=== Dependencies ready ==="
echo ""
echo "Next steps:"
echo "  1. Download models:  bash scripts/download_models.sh"
echo "  2. Build:            mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j\$(sysctl -n hw.ncpu)"
echo "  3. Run:              ./build/rcli"
echo ""
