#!/usr/bin/env bash
# OSS keyless gate: kit-linked wally → development → public staging backend blast.
# No API key. Asserts exit 0 and all 12 modalities stored ≥ 1.
#
# Requires a staging backend origin via env (never hardcode private infra hosts):
#   STAGING_BASE_URL or RA_OSS_BASE_URL
#
#   STAGING_BASE_URL=https://staging.example.com ./scripts/ci/oss_keyless_telemetry_blast.sh
#   RA_SKIP_BUILD=1 STAGING_BASE_URL=... ./scripts/ci/oss_keyless_telemetry_blast.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

OSS_URL="${RA_OSS_BASE_URL:-${STAGING_BASE_URL:-}}"
if [[ -z "$OSS_URL" ]]; then
  echo "Set STAGING_BASE_URL or RA_OSS_BASE_URL to the public staging backend origin." >&2
  exit 1
fi
export STAGING_BASE_URL="${STAGING_BASE_URL:-$OSS_URL}"

case "$(uname -s)" in
  Darwin) PLATFORM=macos-arm64; JOBS="$(sysctl -n hw.logicalcpu)" ;;
  *)
    echo "oss-keyless blast v1 is Darwin (kit pin is macos-arm64 / windows-x64)." >&2
    echo "unsupported host OS '$(uname -s)'" >&2
    exit 1
    ;;
esac

KIT="${WALLY_SDK_KIT:-$ROOT/kit}"
WALLY="${RA_WALLY_BIN:-$ROOT/build/wally-cxx}"
if [[ ! -x "$WALLY" ]]; then
  WALLY="${RA_WALLY_BIN:-$ROOT/build/wally}"
fi

if [[ "${RA_SKIP_BUILD:-0}" != "1" ]]; then
  if [[ ! -f "$KIT/include/runanywhere/proto/model_types.pb.h" ]]; then
    bash "$ROOT/scripts/fetch-kit.sh" "$PLATFORM" "$KIT"
  fi
  cmake -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$KIT"
  cmake --build "$ROOT/build" -j "$JOBS"
fi

[[ -x "$WALLY" ]] || {
  echo "wally not executable: $WALLY" >&2
  exit 1
}

SESSION="${RA_OSS_SESSION_ID:-oss-ci-$(date +%s)-$RANDOM}"
TMP_HOME="$(mktemp -d "${TMPDIR:-/tmp}/oss-keyless.XXXXXX")"
cleanup() { rm -rf "$TMP_HOME"; }
trap cleanup EXIT

# Ambient shell keys must not force JWT on the OSS path.
unset RUNANYWHERE_API_KEY RUNANYWHERE_API_URL RUNANYWHERE_ENVIRONMENT RUNANYWHERE_BASE_URL || true

export XDG_CONFIG_HOME="$TMP_HOME/config"
export XDG_DATA_HOME="$TMP_HOME/data"
export XDG_STATE_HOME="$TMP_HOME/state"
export RUNANYWHERE_HOME="$TMP_HOME/home"

echo "[oss-keyless] wally=$WALLY"
echo "[oss-keyless] base_url=$OSS_URL"
echo "[oss-keyless] session_id=$SESSION"

set +e
OUT="$("$WALLY" --environment development \
  --base-url "$OSS_URL" \
  telemetry blast \
  --processing-ms 42.5 \
  --session-id "$SESSION" \
  --input-tokens 128 \
  --output-tokens 256 2>&1)"
RC=$?
set -e
printf '%s\n' "$OUT"

if [[ "$RC" -ne 0 ]]; then
  echo "[oss-keyless] FAIL: wally exited $RC" >&2
  exit "$RC"
fi

MODALITIES=(llm stt tts vlm rag imagegen embeddings vad voice lora model system)
missing=0
for m in "${MODALITIES[@]}"; do
  if ! printf '%s\n' "$OUT" | awk -v mod="$m" '
    $1 == mod && $2 == "ok" {
      for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+$/) nums[++n] = $i
      if (n >= 3 && nums[n-1] + 0 >= 1) { found = 1; exit }
    }
    END { exit found ? 0 : 1 }
  '; then
    echo "[oss-keyless] FAIL: modality '$m' not ok / stored < 1" >&2
    missing=1
  fi
done

if [[ "$missing" -ne 0 ]]; then
  exit 1
fi

echo "[oss-keyless] OK — 12/12 modalities stored (session_id=$SESSION)"
