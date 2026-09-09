#!/usr/bin/env bash
# Manual end-to-end check of the `wally codex` launch pattern, against a real
# model, off any of our infrastructure.
#
# Codex is Responses-API-only, and most OpenAI-compatible backends (Gemini here)
# speak chat/completions, so a thin LiteLLM bridge does the responses->chat
# translation — the same translation our gateway does. The point it proves: the
# throwaway CODEX_HOME + key-via-env + Responses wire that `wally codex` builds
# drives a real model to a real answer, in one command, without touching the
# user's own ~/.codex.
#
#   GEMINI_API_KEY=... scripts/dev/codex-e2e.sh
#
# Override CODEX / LITELLM if they are not on PATH. Not a hermetic test and not
# wired into CI: it needs the codex and litellm binaries and a live key.
set -euo pipefail
KEY="${GEMINI_API_KEY:?set GEMINI_API_KEY (a Gemini/generativelanguage key)}"
CODEX="${CODEX:-$(command -v codex || true)}"
LITELLM="${LITELLM:-$(command -v litellm || true)}"
MODEL="${MODEL:-gemini-flash-lite-latest}"
PORT="${PORT:-4123}"
[ -x "$CODEX" ]   || { echo "codex not found — set CODEX=/path/to/codex" >&2; exit 1; }
[ -x "$LITELLM" ] || { echo "litellm not found — set LITELLM=/path/to/litellm" >&2; exit 1; }

TMP="$(mktemp -d)"; LLPID=""
cleanup() { [ -n "$LLPID" ] && kill "$LLPID" 2>/dev/null || true; rm -rf "$TMP"; }
trap cleanup EXIT

cat > "$TMP/litellm.yaml" <<YAML
model_list:
  - model_name: bridge-model
    litellm_params:
      model: gemini/${MODEL}
      api_key: os.environ/GEMINI_API_KEY
general_settings:
  master_key: sk-local-validate
YAML

echo "== starting LiteLLM bridge (:$PORT) =="
GEMINI_API_KEY="$KEY" "$LITELLM" --config "$TMP/litellm.yaml" --port "$PORT" > "$TMP/litellm.log" 2>&1 &
LLPID=$!
ready=0
for _ in $(seq 1 60); do
  curl -sf "http://localhost:$PORT/health/liveliness" >/dev/null 2>&1 && { ready=1; break; }
  sleep 1
done
[ "$ready" = 1 ] || { echo "LiteLLM did not come up:"; tail -20 "$TMP/litellm.log"; exit 1; }

# Same ephemeral CODEX_HOME + Responses wire `wally codex` writes.
CODEX_HOME_DIR="$TMP/codex-home"; WORK="$TMP/work"; mkdir -p "$CODEX_HOME_DIR" "$WORK"
chmod 700 "$CODEX_HOME_DIR"
cat > "$CODEX_HOME_DIR/config.toml" <<TOML
model = "bridge-model"
model_provider = "bridge"

[model_providers.bridge]
name = "Local bridge"
base_url = "http://localhost:$PORT/v1"
env_key = "BRIDGE_KEY"
wire_api = "responses"
TOML

echo "== running codex (one command) =="
out="$(CODEX_HOME="$CODEX_HOME_DIR" BRIDGE_KEY="sk-local-validate" \
  "$CODEX" exec --skip-git-repo-check --sandbox read-only \
  -c 'approval_policy="never"' --cd "$WORK" "reply with exactly the token: CODEX_OK" 2>&1)" || true
echo "$out" | tail -25

echo "== verdict =="
# The prompt echoes the token once; a real model answer makes it appear again.
n="$(printf '%s' "$out" | grep -c 'CODEX_OK' || true)"
if [ "${n:-0}" -ge 2 ]; then
  echo "PASS: codex ran end to end (Responses API) and the model answered"
else
  echo "FAIL: no model answer"; echo "--- litellm log tail ---"; tail -20 "$TMP/litellm.log"; exit 1
fi
