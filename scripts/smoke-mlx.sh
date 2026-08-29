#!/usr/bin/env bash
# MLX smoke — port of runanywhere-sdks/rcli/scripts/smoke-mlx-cli.sh
#
# Builds the Swift MLX host, then exercises backends, LLM, TTS, STT, and VLM
# against the product `rcli` binary (not the SDK DevTools playground).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${RCLI_BIN:-$ROOT/build/rcli}"
HOME_DIR="${RCLI_HOME:-${RUNANYWHERE_MLX_SMOKE_HOME:-/tmp/rcli-mlx-smoke}}"
PULL="${RCLI_SMOKE_PULL:-${RUNANYWHERE_MLX_SMOKE_PULL:-1}}"

LLM_MODEL="${RCLI_SMOKE_LLM:-${RUNANYWHERE_MLX_SMOKE_LLM:-mlx-qwen3-0.6b-4bit}}"
VLM_MODEL="${RCLI_SMOKE_VLM:-${RUNANYWHERE_MLX_SMOKE_VLM:-mlx-fastvlm-0.5b-bf16}}"
STT_MODEL="${RCLI_SMOKE_STT:-${RUNANYWHERE_MLX_SMOKE_STT:-mlx-qwen3-asr-0.6b-8bit}}"
TTS_MODEL="${RCLI_SMOKE_TTS:-${RUNANYWHERE_MLX_SMOKE_TTS:-mlx-soprano-1.1-80m-5bit}}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "smoke-mlx: Darwin only" >&2
  exit 0
fi
if [[ "$(uname -m)" != "arm64" ]]; then
  echo "smoke-mlx: Apple Silicon only" >&2
  exit 0
fi

bash "$ROOT/scripts/build-mlx.sh"
if [[ ! -x "$BIN" ]]; then
  echo "error: missing $BIN (scripts/build-mlx.sh should install it as build/rcli)" >&2
  exit 1
fi

mkdir -p "$HOME_DIR"
rcli() { "$BIN" --home "$HOME_DIR" "$@"; }

pull_if_enabled() {
  local model="$1"
  if [[ "$PULL" == "1" ]]; then
    rcli pull "$model"
  fi
}

require_file() {
  local path="$1"
  if [[ ! -s "$path" ]]; then
    echo "expected non-empty file: $path" >&2
    exit 1
  fi
}

require_text() {
  local label="$1"
  local value="$2"
  if [[ -z "${value//[[:space:]]/}" ]]; then
    echo "$label produced empty output" >&2
    exit 1
  fi
}

make_default_image() {
  local out="$1"
  base64 --decode >"$out" <<'PNG'
iVBORw0KGgoAAAANSUhEUgAAAGQAAABkCAIAAAD/gAIDAAAAiklEQVR4nO3QQQ3AIADAQMDwMPrvQJkQxZbB7iTn9cw7wF2mB6wH7AbYDbAbYDfAbgDdALsBdgPsBtgNsBtgN8BugN0AuwF2A+wG2A2wG2A3wG6A3QC7AXYD7AbYDbAbYDfAboDdALsBdgPsBtgNsBtgN8BugN0AuwF2A+wG2A2wG2A3wG6A3QC7AXYDXBuUBgGkrp6WAAAAAElFTkSuQmCC
PNG
}

echo "MLX backend smoke"
rcli --json backends | grep -q '"name":"mlx"'
rcli --json backends | grep -q '"name":"llamacpp"'

echo "LLM: $LLM_MODEL"
pull_if_enabled "$LLM_MODEL"
llm_out="$(rcli run "$LLM_MODEL" "Say OK in one short sentence." --max-tokens 16 --temp 0.1)"
require_text "LLM" "$llm_out"
printf '%s\n' "$llm_out"

echo "TTS: $TTS_MODEL"
pull_if_enabled "$TTS_MODEL"
tts_wav="$HOME_DIR/mlx-smoke-tts.wav"
rm -f "$tts_wav"
rcli tts --model "$TTS_MODEL" --text "Hello from MLX text to speech." --output "$tts_wav"
require_file "$tts_wav"

echo "STT: $STT_MODEL"
pull_if_enabled "$STT_MODEL"
stt_out="$(rcli stt --model "$STT_MODEL" --input "$tts_wav")"
require_text "STT" "$stt_out"
printf '%s\n' "$stt_out"

echo "VLM: $VLM_MODEL"
pull_if_enabled "$VLM_MODEL"
image_path="${RCLI_SMOKE_IMAGE:-${RUNANYWHERE_MLX_SMOKE_IMAGE:-$HOME_DIR/mlx-smoke-image.png}}"
if [[ -z "${RCLI_SMOKE_IMAGE:-${RUNANYWHERE_MLX_SMOKE_IMAGE:-}}" ]]; then
  make_default_image "$image_path"
fi
vlm_out="$(rcli run "$VLM_MODEL" --image "$image_path" \
  "Describe the image in one short sentence." --max-tokens 32 --temp 0.1)"
require_text "VLM" "$vlm_out"
printf '%s\n' "$vlm_out"

echo "smoke-mlx: ok"
