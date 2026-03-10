#!/bin/bash
# =============================================================================
# RCLI MetalRT — Full End-to-End Setup
# =============================================================================
#
# One script to go from a fresh clone to a working RCLI with MetalRT GPU
# acceleration (LLM + Whisper STT + Kokoro TTS — all on Apple Silicon GPU).
#
# Prerequisites:
#   - macOS 14+ on Apple Silicon (M1/M2/M3/M4)
#   - Xcode 15+ with command-line tools (xcode-select --install)
#   - CMake 3.15+ (brew install cmake)
#   - Git, curl, python3 (pre-installed on macOS)
#
# Expected repo layout (parent directory):
#   parent/
#   ├── RCLI-MetalRT/      ← This repo (CLI app)
#   ├── metalrt-binaries/  ← Pre-built release artifacts (required, public)
#   └── MetalRT/           ← Metal GPU inference engine (optional, for development)
#
# All models are fetched from https://huggingface.co/runanywhere
#
# Usage:
#   cd RCLI-MetalRT
#   bash scripts/setup_metalrt.sh
#
# =============================================================================
set -euo pipefail

TMPFILES=()
cleanup() { for f in "${TMPFILES[@]}"; do rm -rf "$f" 2>/dev/null; done; }
trap cleanup EXIT

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

info()  { echo -e "  ${CYAN}▸${RESET} $*"; }
ok()    { echo -e "  ${GREEN}✓${RESET} $*"; }
warn()  { echo -e "  ${YELLOW}⚠${RESET} $*"; }
fail()  { echo -e "  ${RED}✗${RESET} $*"; exit 1; }
step()  { echo -e "\n${BOLD}[$1/$TOTAL_STEPS] $2${RESET}"; }

TOTAL_STEPS=6

# ── Paths ────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RCLI_DIR="$(dirname "$SCRIPT_DIR")"
PARENT_DIR="$(dirname "$RCLI_DIR")"
METALRT_SRC="$PARENT_DIR/MetalRT"
METALRT_BIN="$PARENT_DIR/metalrt-binaries"
DEPS_DIR="$RCLI_DIR/deps"
BUILD_DIR="$RCLI_DIR/build"
ENGINES_DIR="$HOME/Library/RCLI/engines"
MODELS_DIR="$HOME/Library/RCLI/models"
METALRT_MODELS="$MODELS_DIR/metalrt"

echo ""
echo -e "${BOLD}${CYAN}"
echo "  ██████╗  ██████╗██╗     ██╗       MetalRT"
echo "  ██╔══██╗██╔════╝██║     ██║       GPU Setup"
echo "  ██████╔╝██║     ██║     ██║"
echo "  ██╔══██╗██║     ██║     ██║       Apple Silicon"
echo "  ██║  ██║╚██████╗███████╗██║       Full Setup"
echo "  ╚═╝  ╚═╝ ╚═════╝╚══════╝╚═╝"
echo -e "${RESET}"
echo -e "  ${DIM}Powered by RunAnywhere${RESET}"
echo ""

# ── Preflight checks ────────────────────────────────────────────────────────
info "Checking prerequisites..."

[[ "$(uname -m)" == "arm64" ]] || fail "Apple Silicon (arm64) required. Got: $(uname -m)"
command -v cmake >/dev/null 2>&1 || fail "CMake not found. Install: brew install cmake"
command -v git   >/dev/null 2>&1 || fail "Git not found. Install: xcode-select --install"
command -v curl  >/dev/null 2>&1 || fail "curl not found."

ok "macOS $(sw_vers -productVersion) on $(uname -m)"
ok "CMake $(cmake --version | head -1 | awk '{print $3}')"

# Verify metalrt-binaries repo exists (required); MetalRT source is optional (closed source)
[[ -d "$METALRT_BIN" ]] || fail "metalrt-binaries not found at $METALRT_BIN\n       Clone it: git clone https://github.com/RunanywhereAI/metalrt-binaries.git $METALRT_BIN"

ok "metalrt-binaries: $METALRT_BIN"
if [[ -d "$METALRT_SRC" ]]; then
    ok "MetalRT source: $METALRT_SRC (optional, for development)"
else
    info "MetalRT source not found (optional — using pre-built binaries)"
fi

# HuggingFace authentication (needed for private runanywhere LLM repos)
HF_TOKEN="${HF_TOKEN:-}"
if [ -z "$HF_TOKEN" ] && [ -f "$HOME/.cache/huggingface/token" ]; then
    HF_TOKEN=$(cat "$HOME/.cache/huggingface/token" | tr -d '\n\r')
fi

if [ -z "$HF_TOKEN" ]; then
    warn "HuggingFace token not found — LLM model downloads may fail for private repos."
    info "Set HF_TOKEN env var or run: huggingface-cli login"
fi

hf_curl() {
    if [ -n "$HF_TOKEN" ]; then
        curl -H "Authorization: Bearer $HF_TOKEN" "$@"
    else
        curl "$@"
    fi
}

# ═════════════════════════════════════════════════════════════════════════════
# STEP 1: Clone build dependencies (llama.cpp + sherpa-onnx)
# ═════════════════════════════════════════════════════════════════════════════
step 1 "Cloning build dependencies"

mkdir -p "$DEPS_DIR"

if [ -d "$DEPS_DIR/llama.cpp" ]; then
    ok "llama.cpp already cloned"
else
    info "Cloning llama.cpp (LLM inference with Metal GPU)..."
    git clone --depth 1 https://github.com/ggml-org/llama.cpp "$DEPS_DIR/llama.cpp"
    ok "llama.cpp cloned"
fi

if [ -d "$DEPS_DIR/sherpa-onnx" ]; then
    ok "sherpa-onnx already cloned"
else
    info "Cloning sherpa-onnx (CPU STT/TTS/VAD fallback)..."
    git clone --depth 1 https://github.com/k2-fsa/sherpa-onnx "$DEPS_DIR/sherpa-onnx"
    ok "sherpa-onnx cloned"
fi

# ═════════════════════════════════════════════════════════════════════════════
# STEP 2: Build RCLI
# ═════════════════════════════════════════════════════════════════════════════
step 2 "Building RCLI"

NCPU=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

mkdir -p "$BUILD_DIR"
info "Configuring CMake (Release, Metal GPU enabled)..."

# Apple Silicon has NEON/dotprod but NOT SVE/SME. The ggml CMake uses
# check_cxx_source_runs() which compiles+runs a test binary — these SVE/SME
# binaries can hang indefinitely on M-series chips instead of cleanly failing.
# Pre-seed the cache with correct results to skip the try_run entirely.
if [[ "$(uname -m)" == "arm64" ]]; then
    CACHE_INIT="$BUILD_DIR/apple_silicon_cache.cmake"
    cat > "$CACHE_INIT" << 'CACHE_EOF'
set(GGML_MACHINE_SUPPORTS_dotprod TRUE CACHE INTERNAL "")
set(GGML_MACHINE_SUPPORTS_i8mm "" CACHE INTERNAL "")
set(GGML_MACHINE_SUPPORTS_sve "" CACHE INTERNAL "")
set(GGML_MACHINE_SUPPORTS_sme "" CACHE INTERNAL "")
set(GGML_MACHINE_SUPPORTS_nosve TRUE CACHE INTERNAL "")
set(GGML_MACHINE_SUPPORTS_nosme TRUE CACHE INTERNAL "")
set(GGML_MACHINE_SUPPORTS_noi8mm TRUE CACHE INTERNAL "")
CACHE_EOF
    CMAKE_INIT_FLAG="-C $CACHE_INIT"
else
    CMAKE_INIT_FLAG=""
fi

cmake -S "$RCLI_DIR" -B "$BUILD_DIR" \
    $CMAKE_INIT_FLAG \
    -DCMAKE_BUILD_TYPE=Release \
    -DGGML_METAL=ON \
    -DGGML_ACCELERATE=ON 2>&1 | tail -5

info "Compiling with $NCPU threads (this takes 2-5 minutes)..."
cmake --build "$BUILD_DIR" -j"$NCPU" 2>&1 | tail -3

[[ -f "$BUILD_DIR/rcli" ]] || fail "Build failed — rcli binary not produced"
ok "Built: $BUILD_DIR/rcli"

# ═════════════════════════════════════════════════════════════════════════════
# STEP 3: Install MetalRT engine (libmetalrt.dylib + default.metallib)
# ═════════════════════════════════════════════════════════════════════════════
step 3 "Installing MetalRT engine binary"

mkdir -p "$ENGINES_DIR"

# Find libmetalrt.dylib — search in metalrt-binaries repo first, then MetalRT build
DYLIB_SRC=""
METALLIB_SRC=""

for search_dir in \
    "$METALRT_BIN/build" \
    "$METALRT_BIN/release" \
    "$METALRT_BIN" \
    "$METALRT_SRC/build" \
    "$METALRT_SRC"; do
    if [ -f "$search_dir/libmetalrt.dylib" ]; then
        DYLIB_SRC="$search_dir/libmetalrt.dylib"
        break
    fi
done

# Also check inside release tarballs
if [ -z "$DYLIB_SRC" ]; then
    LATEST_VER=$(cat "$METALRT_BIN/LATEST_VERSION" 2>/dev/null | tr -d '\n\r' || echo "")
    if [ -n "$LATEST_VER" ]; then
        TARBALL="$METALRT_BIN/releases/$LATEST_VER/metalrt-${LATEST_VER}-macos-arm64.tar.gz"
        if [ -f "$TARBALL" ]; then
            info "Extracting from release tarball: $TARBALL"
            TMPDIR_EXTRACT=$(mktemp -d)
            TMPFILES+=("$TMPDIR_EXTRACT")
            tar xzf "$TARBALL" -C "$TMPDIR_EXTRACT"
            EXTRACTED_DIR=$(find "$TMPDIR_EXTRACT" -name "libmetalrt.dylib" -exec dirname {} \; | head -1)
            if [ -n "$EXTRACTED_DIR" ]; then
                DYLIB_SRC="$EXTRACTED_DIR/libmetalrt.dylib"
                [ -f "$EXTRACTED_DIR/default.metallib" ] && METALLIB_SRC="$EXTRACTED_DIR/default.metallib"
            fi
        fi
    fi
fi

[ -z "$DYLIB_SRC" ] && fail "libmetalrt.dylib not found in metalrt-binaries or MetalRT build.\n       Build MetalRT first, or place the dylib in $METALRT_BIN/build/"

info "Source: $DYLIB_SRC"
cp "$DYLIB_SRC" "$ENGINES_DIR/libmetalrt.dylib"
ok "Copied libmetalrt.dylib → $ENGINES_DIR/"

# Copy default.metallib
if [ -z "$METALLIB_SRC" ]; then
    METALLIB_DIR=$(dirname "$DYLIB_SRC")
    [ -f "$METALLIB_DIR/default.metallib" ] && METALLIB_SRC="$METALLIB_DIR/default.metallib"
fi
if [ -z "$METALLIB_SRC" ]; then
    for search_dir in "$METALRT_SRC/build" "$METALRT_SRC" "$METALRT_BIN/build" "$METALRT_BIN"; do
        [ -f "$search_dir/default.metallib" ] && METALLIB_SRC="$search_dir/default.metallib" && break
    done
fi
if [ -n "$METALLIB_SRC" ]; then
    cp "$METALLIB_SRC" "$ENGINES_DIR/default.metallib"
    ok "Copied default.metallib → $ENGINES_DIR/"
else
    warn "default.metallib not found — MetalRT may not work without it"
fi

# Re-sign after copy
codesign --force --sign - "$ENGINES_DIR/libmetalrt.dylib" 2>/dev/null
ok "Code signature updated"

LATEST_VER=$(cat "$METALRT_BIN/LATEST_VERSION" 2>/dev/null | tr -d '\n\r' || echo "local")
echo "${LATEST_VER}-local" > "$ENGINES_DIR/VERSION"
info "Version: ${LATEST_VER}-local"

# ═════════════════════════════════════════════════════════════════════════════
# STEP 4: Download MetalRT LLM models from runanywhere HF org
# ═════════════════════════════════════════════════════════════════════════════
step 4 "Downloading MetalRT LLM models"

mkdir -p "$METALRT_MODELS"

download_metalrt_llm() {
    local name="$1" dir="$2" hf_repo="$3" hf_subdir="$4"
    local target="$METALRT_MODELS/$dir"
    local hf_base="https://huggingface.co/$hf_repo/resolve/main"
    [ -n "$hf_subdir" ] && hf_base="$hf_base/$hf_subdir"

    if [ -f "$target/model.safetensors" ] && [ -f "$target/tokenizer.json" ] && [ -f "$target/config.json" ]; then
        ok "$name already downloaded"
        return
    fi

    info "Downloading $name from $hf_repo..."
    mkdir -p "$target"
    hf_curl -fL -# -o "$target/model.safetensors" "$hf_base/model.safetensors"
    hf_curl -fL -# -o "$target/tokenizer.json"     "$hf_base/tokenizer.json"
    hf_curl -fL -# -o "$target/config.json"         "$hf_base/config.json"
    ok "$name downloaded"
}

download_metalrt_llm \
    "Liquid LFM2.5 1.2B" \
    "LFM2.5-1.2B-MLX-4bit" \
    "LiquidAI/LFM2.5-1.2B-Instruct-MLX-4bit" \
    ""

download_metalrt_llm \
    "Qwen3 0.6B" \
    "Qwen3-0.6B-MLX-4bit" \
    "runanywhere/qwen3_0.6B_MLX_4bit" \
    "Qwen3-0.6B-MLX-4bit"

download_metalrt_llm \
    "Qwen3 4B" \
    "Qwen3-4B-MLX-4bit" \
    "runanywhere/qwen3_4B_mlx_4bit" \
    "Qwen3-4B-MLX-4bit"

download_metalrt_llm \
    "Llama 3.2 3B Instruct" \
    "Llama-3.2-3B-Instruct-MLX-4bit" \
    "runanywhere/Llama_32_3B_4bit" \
    "Llama-3.2-3B-Instruct-4bit"

# ═════════════════════════════════════════════════════════════════════════════
# STEP 5: Download MetalRT STT + TTS models from runanywhere HF org
# ═════════════════════════════════════════════════════════════════════════════
step 5 "Downloading MetalRT STT + TTS models"

download_whisper() {
    local name="$1" dir="$2" hf_repo="$3" hf_subdir="$4"
    local target="$METALRT_MODELS/$dir"
    local hf_base="https://huggingface.co/$hf_repo/resolve/main"
    [ -n "$hf_subdir" ] && hf_base="$hf_base/$hf_subdir"

    if [ -f "$target/model.safetensors" ] && [ -f "$target/tokenizer.json" ]; then
        ok "$name already downloaded"
        return
    fi

    info "Downloading $name from $hf_repo..."
    mkdir -p "$target"
    curl -fL -# -o "$target/config.json"        "$hf_base/config.json"
    curl -fL -# -o "$target/model.safetensors"   "$hf_base/model.safetensors"
    curl -fL -# -o "$target/tokenizer.json"      "$hf_base/tokenizer.json"
    ok "$name downloaded"
}

download_whisper "Whisper Tiny MLX 4-bit"   "whisper-tiny-mlx-4bit"   "runanywhere/whisper_tiny_4bit"   "whisper-tiny-mlx-4bit"
download_whisper "Whisper Small MLX 4-bit"  "whisper-small-mlx-4bit"  "runanywhere/whisper_small_4bit"  "whisper-small-mlx-4bit"
download_whisper "Whisper Medium MLX 4-bit" "whisper-medium-mlx-4bit" "runanywhere/whisper_medium_4bit" "whisper-medium-mlx-4bit"

# --- Kokoro 82M bf16 (TTS) ---
KOKORO_DIR="$METALRT_MODELS/Kokoro-82M-bf16"
KOKORO_HF="runanywhere/kokoro_bf16"
KOKORO_SUBDIR="Kokoro-82M-bf16"
KOKORO_HF_BASE="https://huggingface.co/$KOKORO_HF/resolve/main/$KOKORO_SUBDIR"

VOICE_LIST="af_heart af_alloy af_aoede af_bella af_jessica af_kore af_nicole af_nova af_river af_sarah af_sky am_adam am_echo am_eric am_fenrir am_liam am_michael am_onyx am_puck am_santa bf_alice bf_emma bf_isabella bf_lily bm_daniel bm_fable bm_george bm_lewis"

if [ -f "$KOKORO_DIR/kokoro-v1_0.safetensors" ] && [ -f "$KOKORO_DIR/voices/af_heart.safetensors" ]; then
    ok "Kokoro 82M bf16 already downloaded"
else
    info "Downloading Kokoro 82M bf16 TTS from $KOKORO_HF..."
    mkdir -p "$KOKORO_DIR/voices"
    curl -fL -# -o "$KOKORO_DIR/config.json" "$KOKORO_HF_BASE/config.json"
    curl -fL -# -o "$KOKORO_DIR/kokoro-v1_0.safetensors" "$KOKORO_HF_BASE/kokoro-v1_0.safetensors"

    info "  Downloading voice embeddings..."
    for v in $VOICE_LIST; do
        curl -fL -s -o "$KOKORO_DIR/voices/${v}.safetensors" "$KOKORO_HF_BASE/voices/${v}.safetensors"
    done
    VOICE_COUNT=$(ls "$KOKORO_DIR/voices/"*.safetensors 2>/dev/null | wc -l | tr -d ' ')
    ok "Kokoro 82M downloaded ($VOICE_COUNT voices)"
fi

# ═════════════════════════════════════════════════════════════════════════════
# STEP 6: Install binary + write config
# ═════════════════════════════════════════════════════════════════════════════
step 6 "Final installation"

if [ -d "/opt/homebrew/bin" ] && [ -w "/opt/homebrew/bin" ]; then
    INSTALL_DIR="/opt/homebrew/bin"
elif [ -d "/usr/local/bin" ] && [ -w "/usr/local/bin" ]; then
    INSTALL_DIR="/usr/local/bin"
else
    INSTALL_DIR="$HOME/.local/bin"
    mkdir -p "$INSTALL_DIR"
    if ! echo "$PATH" | tr ':' '\n' | grep -qx "$INSTALL_DIR"; then
        warn "$INSTALL_DIR is not in PATH — add it to your shell profile"
    fi
fi

cp "$BUILD_DIR/rcli" "$INSTALL_DIR/rcli"
codesign --force --sign - "$INSTALL_DIR/rcli" 2>/dev/null
ok "Installed rcli → $INSTALL_DIR/rcli"

CONFIG_DIR="$HOME/Library/RCLI"
mkdir -p "$CONFIG_DIR"
CONFIG_FILE="$CONFIG_DIR/config"

if [ -f "$CONFIG_FILE" ]; then
    grep -v '^engine=' "$CONFIG_FILE" | grep -v '^model=' > "${CONFIG_FILE}.tmp" || true
    mv "${CONFIG_FILE}.tmp" "$CONFIG_FILE"
fi
echo "engine=metalrt" >> "$CONFIG_FILE"
echo "model=lfm2.5-1.2b" >> "$CONFIG_FILE"
ok "Config: engine=metalrt, model=lfm2.5-1.2b"

# ═════════════════════════════════════════════════════════════════════════════
# Summary
# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo -e "${BOLD}${GREEN}  ═══════════════════════════════════════════════════${RESET}"
echo -e "${BOLD}${GREEN}  Setup complete! All MetalRT components installed.${RESET}"
echo -e "${BOLD}${GREEN}  ═══════════════════════════════════════════════════${RESET}"
echo ""
echo -e "  ${BOLD}Engine:${RESET}  $ENGINES_DIR/libmetalrt.dylib ($(du -h "$ENGINES_DIR/libmetalrt.dylib" | awk '{print $1}'))"
echo -e "  ${BOLD}Version:${RESET} $(cat "$ENGINES_DIR/VERSION")"
echo -e "  ${BOLD}Models:${RESET}  All from https://huggingface.co/runanywhere"
echo ""
echo -e "  ${BOLD}MetalRT Models:${RESET}"
for d in "$METALRT_MODELS"/*/; do
    [ -d "$d" ] || continue
    dname=$(basename "$d")
    size=$(du -sh "$d" 2>/dev/null | awk '{print $1}')
    echo -e "    ${DIM}$dname${RESET} ($size)"
done
echo ""
echo -e "  ${BOLD}Get started:${RESET}"
echo "    rcli                    # interactive text mode"
echo "    rcli listen             # voice mode (press Space to talk)"
echo "    rcli metalrt status     # check MetalRT installation"
echo "    rcli bench              # run benchmarks"
echo ""
