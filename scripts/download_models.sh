#!/bin/bash
#
# download_models.sh
# RCLI
#
# Downloads all required models for the RCLI engine.
# Models are placed in ~/Library/RCLI/models/
#
# Usage: ./download_models.sh [target_dir]
#

set -euo pipefail

MODELS_DIR="${1:-$HOME/Library/RCLI/models}"
mkdir -p "$MODELS_DIR"

echo "======================================"
echo "  RCLI Model Downloader"
echo "======================================"
echo "Target: $MODELS_DIR"
echo ""

# --- LLM: Liquid LFM2 1.2B Tool Q4_K_M ---
LLM_FILE="$MODELS_DIR/lfm2-1.2b-tool-q4_k_m.gguf"
if [ -f "$LLM_FILE" ]; then
    echo "[ok] LLM model already exists"
else
    echo "Downloading: Downloading Liquid LFM2 1.2B Tool Q4_K_M (~731MB)..."
    curl -L -o "$LLM_FILE" \
        "https://huggingface.co/LiquidAI/LFM2-1.2B-Tool-GGUF/resolve/main/LFM2-1.2B-Tool-Q4_K_M.gguf"
    echo "[ok] LLM model downloaded"
fi

# --- STT: Whisper base.en (sherpa-onnx format) ---
WHISPER_DIR="$MODELS_DIR/whisper-base.en"
if [ -d "$WHISPER_DIR" ] && [ -f "$WHISPER_DIR/base.en-encoder.int8.onnx" ]; then
    echo "[ok] Whisper model already exists"
else
    echo "Downloading: Downloading Whisper base.en (~40MB)..."
    mkdir -p "$WHISPER_DIR"
    WHISPER_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-base.en.tar.bz2"
    curl -L "$WHISPER_URL" | tar xj -C "$MODELS_DIR/"
    # The archive extracts to sherpa-onnx-whisper-base.en/
    if [ -d "$MODELS_DIR/sherpa-onnx-whisper-base.en" ]; then
        mv "$MODELS_DIR/sherpa-onnx-whisper-base.en/"* "$WHISPER_DIR/" 2>/dev/null || true
        rm -rf "$MODELS_DIR/sherpa-onnx-whisper-base.en"
    fi
    echo "[ok] Whisper model downloaded"
fi

# --- STT: Zipformer streaming (sherpa-onnx format) ---
ZIPFORMER_DIR="$MODELS_DIR/zipformer"
if [ -d "$ZIPFORMER_DIR" ] && [ -f "$ZIPFORMER_DIR/encoder-epoch-99-avg-1.int8.onnx" ]; then
    echo "[ok] Zipformer model already exists"
else
    echo "Downloading: Downloading Zipformer streaming (~15MB)..."
    mkdir -p "$ZIPFORMER_DIR"
    ZIP_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-en-20M-2023-02-17.tar.bz2"
    curl -L "$ZIP_URL" | tar xj -C "$MODELS_DIR/"
    EXTRACTED="$MODELS_DIR/sherpa-onnx-streaming-zipformer-en-20M-2023-02-17"
    if [ -d "$EXTRACTED" ]; then
        mv "$EXTRACTED/"* "$ZIPFORMER_DIR/" 2>/dev/null || true
        rm -rf "$EXTRACTED"
    fi
    echo "[ok] Zipformer model downloaded"
fi

# --- TTS: Piper Amy medium ---
PIPER_DIR="$MODELS_DIR/piper-voice"
if [ -d "$PIPER_DIR" ] && [ -f "$PIPER_DIR/en_US-lessac-medium.onnx" ]; then
    echo "[ok] Piper TTS model already exists"
else
    echo "Downloading: Downloading Piper TTS lessac-medium (~60MB)..."
    mkdir -p "$PIPER_DIR"
    PIPER_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-en_US-lessac-medium.tar.bz2"
    curl -L "$PIPER_URL" | tar xj -C "$MODELS_DIR/"
    EXTRACTED="$MODELS_DIR/vits-piper-en_US-lessac-medium"
    if [ -d "$EXTRACTED" ]; then
        mv "$EXTRACTED/"* "$PIPER_DIR/" 2>/dev/null || true
        rm -rf "$EXTRACTED"
    fi
    echo "[ok] Piper TTS model downloaded"
fi

# --- VAD: Silero VAD ---
VAD_FILE="$MODELS_DIR/silero_vad.onnx"
if [ -f "$VAD_FILE" ]; then
    echo "[ok] VAD model already exists"
else
    echo "Downloading: Downloading Silero VAD v5 (~2MB)..."
    curl -L -o "$VAD_FILE" \
        "https://github.com/snakers4/silero-vad/raw/v5.1/src/silero_vad/data/silero_vad.onnx"
    echo "[ok] VAD model downloaded"
fi

# --- espeak-ng data (for Piper TTS) ---
ESPEAK_DIR="$MODELS_DIR/espeak-ng-data"
if [ -d "$ESPEAK_DIR" ]; then
    echo "[ok] espeak-ng-data already exists"
else
    echo "Downloading: Downloading espeak-ng-data (~4MB)..."
    ESPEAK_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/espeak-ng-data.tar.bz2"
    curl -L "$ESPEAK_URL" | tar xj -C "$MODELS_DIR/"
    echo "[ok] espeak-ng-data downloaded"
fi

echo ""
echo "======================================"
echo "All models downloaded to: $MODELS_DIR"
echo ""
echo "Contents:"
du -sh "$MODELS_DIR"/* 2>/dev/null | sed 's|.*/||' | while read line; do echo "  $line"; done
echo "======================================"
