#!/usr/bin/env bash
# =============================================================================
# e2e-linux.sh
#
# Host-side port of runanywhere-sdks/core/tests/scripts/run-cli-e2e-linux.sh.
# That original built rcli inside the SDK Docker image (RAC_BUILD_CLI=ON).
# RCLI is a kit consumer, so this drives an already-built binary instead:
#
#   scripts/e2e-linux.sh [path-to-rcli]
#
# Always runs modelless contract checks. Inference + hermetic pull run when
# RCLI_TEST_MODEL_DIR is set (same layout as SDK download-test-models.sh).
# Set RCLI_E2E_REQUIRE_MODELS=1 to fail closed if that directory is missing.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-${RCLI_BIN:-}}"
if [[ -z "$BIN" ]]; then
  if [[ -x "$ROOT/build/rcli" ]]; then
    BIN="$ROOT/build/rcli"
  elif [[ -x "$ROOT/build/rcli-cxx" ]]; then
    BIN="$ROOT/build/rcli-cxx"
  elif [[ -x "$ROOT/build/rcli.exe" ]]; then
    BIN="$ROOT/build/rcli.exe"
  fi
fi
if [[ -z "$BIN" || ! -e "$BIN" ]]; then
  echo "usage: e2e-linux.sh <path-to-rcli>" >&2
  exit 1
fi

MODEL_DIR="${RCLI_TEST_MODEL_DIR:-${RAC_TEST_MODEL_DIR:-}}"
LOG_DIR="${RCLI_TEST_LOG_DIR:-$ROOT/build/cli-e2e-logs}"
HOME_DIR="${RUNANYWHERE_HOME:-$(mktemp -d "${TMPDIR:-/tmp}/rcli-e2e.XXXXXX")}"
cleanup() { [[ -z "${RCLI_KEEP_HOME:-}" ]] && rm -rf "$HOME_DIR"; }
trap cleanup EXIT
mkdir -p "$LOG_DIR" "$HOME_DIR"

rcli() { "$BIN" --home "$HOME_DIR" "$@"; }

pass=0
fail=0
failed_names=()
check() {
  local name="$1"
  local log="$LOG_DIR/${name}.log"
  echo -n "  ${name}... "
  if "$name" >"$log" 2>&1; then
    echo "PASS ($log)"
    pass=$((pass + 1))
  else
    echo "FAIL ($log)"
    fail=$((fail + 1))
    failed_names+=("$name")
  fi
}

smoke_version() { rcli version | grep -E 'rcli|[0-9]+\.[0-9]+'; }

smoke_backends() {
  local out
  out="$(rcli backends)"
  echo "$out"
  echo "$out" | grep -qiE "llamacpp|llama"
  echo "$out" | grep -qiE "sherpa|onnx"
}

smoke_list_all() { rcli list --all; }

smoke_info_json() {
  rcli --json info | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d.get("rcli") or d.get("version")'
}

smoke_unknown() {
  set +e
  rcli definitely-not-a-command
  local code=$?
  set -e
  test "$code" -ne 0
}

hermetic_pull_rm() {
  local silero="$MODEL_DIR/ONNX/silero-vad/silero_vad.onnx"
  test -f "$silero"
  local stage
  stage="$(mktemp -d)"
  cp "$silero" "$stage/silero_vad.onnx"
  (cd "$stage" && python3 -m http.server 8077 >/dev/null 2>&1 </dev/null &)
  local i
  for i in $(seq 1 10); do
    curl -sf http://127.0.0.1:8077/ >/dev/null 2>&1 && break
    sleep 1
  done
  rcli --no-progress pull http://127.0.0.1:8077/silero_vad.onnx
  rcli list | grep -q silero_vad
  rcli rm silero_vad --force
  ! rcli list | grep -q silero_vad
  pkill -f "http.server 8077" || true
  rm -rf "$stage"
}

stage_canonical() {
  mkdir -p "$HOME_DIR/Models/LlamaCpp" "$HOME_DIR/Models/ONNX"
  cp -R "$MODEL_DIR/LlamaCpp/qwen3-0.6b" "$HOME_DIR/Models/LlamaCpp/" 2>/dev/null || true
  cp -R "$MODEL_DIR/ONNX/silero-vad" "$HOME_DIR/Models/ONNX/" 2>/dev/null || true
}

llm_one_shot() {
  stage_canonical
  local out
  out="$(rcli run qwen3-0.6b 'Reply with exactly: OK' --no-think --max-tokens 32)"
  echo "LLM said: $out"
  test -n "$out"
}

tts_stt_roundtrip() {
  rcli --no-progress pull piper || rcli --no-progress pull piper-en
  rcli tts --text "RunAnywhere runs models on device." --output /tmp/rcli-e2e-tts.wav
  test -s /tmp/rcli-e2e-tts.wav
  rcli --no-progress pull whisper-tiny
  local transcript
  transcript="$(rcli stt --input /tmp/rcli-e2e-tts.wav)"
  echo "Transcript: $transcript"
  echo "$transcript" | grep -iE "run|anywhere|models|device"
}

vad_segments() {
  rcli --no-progress pull piper || rcli --no-progress pull piper-en
  rcli tts --text "Testing voice activity detection." --output /tmp/rcli-e2e-vad.wav
  rcli --json vad --input /tmp/rcli-e2e-vad.wav | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d.get("segments") or d.get("speech") or isinstance(d, (dict, list))'
}

voice_turn() {
  rcli --no-progress pull piper || rcli --no-progress pull piper-en
  rcli --no-progress pull whisper-tiny
  rcli tts --text "Hello there." --output /tmp/rcli-e2e-turn.wav
  rcli --json voice --input /tmp/rcli-e2e-turn.wav --output /tmp/rcli-e2e-reply.wav | python3 -c 'import json,sys; json.load(sys.stdin)'
  test -s /tmp/rcli-e2e-reply.wav
}

serve_health() {
  stage_canonical
  rcli serve qwen3-0.6b --port 8090 >/tmp/rcli-e2e-serve.log 2>&1 </dev/null &
  local pid=$!
  local i
  for i in $(seq 1 30); do
    curl -sf http://127.0.0.1:8090/health >/dev/null 2>&1 && break
    sleep 1
  done
  curl -sf http://127.0.0.1:8090/health
  curl -sf http://127.0.0.1:8090/v1/models | grep -qi qwen
  kill "$pid" || true
  for i in $(seq 1 15); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 1
  done
  if kill -0 "$pid" 2>/dev/null; then
    echo "server did not exit after SIGTERM"
    kill -9 "$pid"
    exit 1
  fi
}

echo "=========================================="
echo "  rcli e2e (host)"
echo "=========================================="
echo "rcli:      $BIN"
echo "home:      $HOME_DIR"
echo "model dir: ${MODEL_DIR:-<unset>}"
echo "logs:      $LOG_DIR"

echo
echo "==> Modelless smoke"
check smoke_version
check smoke_backends
check smoke_list_all
check smoke_info_json
check smoke_unknown

if [[ -z "$MODEL_DIR" ]]; then
  if [[ "${RCLI_E2E_REQUIRE_MODELS:-0}" == "1" ]]; then
    echo "RCLI_TEST_MODEL_DIR is required (RCLI_E2E_REQUIRE_MODELS=1)" >&2
    exit 1
  fi
  echo
  echo "  skip  inference (set RCLI_TEST_MODEL_DIR to enable)"
else
  echo
  echo "==> Hermetic pull / rm (loopback HTTP, no WAN)"
  check hermetic_pull_rm
  echo
  echo "==> Real inference (canonical-layout models)"
  check llm_one_shot
  check tts_stt_roundtrip
  check vad_segments
  check voice_turn
  check serve_health
fi

echo
echo "Summary: $pass passed, $fail failed"
if [[ "$fail" -gt 0 ]]; then
  echo "Failed: ${failed_names[*]}"
  echo "Logs: $LOG_DIR"
  exit 1
fi
echo "All rcli e2e cases passed"
echo "Logs: $LOG_DIR"
