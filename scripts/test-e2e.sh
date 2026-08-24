#!/usr/bin/env bash
# Kit-consumer rewrite of runanywhere-sdks/rcli/scripts/test-e2e.sh
#
# Original configured the SDK with RAC_BUILD_CLI=ON. This repo consumes a
# published C++ desktop kit:
#
#   1. Configure + build against CMAKE_PREFIX_PATH / RCLI_SDK_KIT
#   2. Offline unit/segment tests (ctest)
#   3. CLI contract smoke (scripts/smoke.sh + scripts/e2e.sh)
#   4. Optional model round-trip when RCLI_E2E_MODEL is set
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

KIT="${RCLI_SDK_KIT:-${CMAKE_PREFIX_PATH:-$ROOT/kit}}"
BUILD="${RCLI_BUILD_DIR:-$ROOT/build}"
JOBS="${RCLI_JOBS:-}"
if [[ -z "$JOBS" ]]; then
  case "$(uname -s)" in
    Darwin) JOBS="$(sysctl -n hw.logicalcpu)" ;;
    Linux)  JOBS="$(nproc)" ;;
    *)      JOBS=2 ;;
  esac
fi

pass=0
fail=0
ok()  { printf '  PASS %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL %s\n' "$1"; fail=$((fail + 1)); }

echo "==> Kit prefix: $KIT"
if [[ ! -d "$KIT/include" ]]; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) bash "$ROOT/scripts/fetch-kit.sh" macos-arm64 "$KIT" ;;
    *)
      echo "error: no kit at $KIT — set RCLI_SDK_KIT or CMAKE_PREFIX_PATH" >&2
      exit 1
      ;;
  esac
fi

if [[ "${RCLI_E2E_KEEP_BUILD:-0}" != "1" || ! -f "$BUILD/CMakeCache.txt" ]]; then
  echo "==> Configuring"
  cmake -B "$BUILD" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
    -DCMAKE_PREFIX_PATH="$KIT"
fi

echo "==> Building"
cmake --build "$BUILD" -j "$JOBS"

BIN=""
for cand in "$BUILD/rcli" "$BUILD/rcli-cxx" "$BUILD/rcli.exe" "$BUILD/Release/rcli.exe"; do
  if [[ -e "$cand" ]]; then BIN="$cand"; break; fi
done
[[ -n "$BIN" ]] || { echo "rcli binary not found under $BUILD" >&2; exit 1; }
echo "rcli: $BIN"

echo "==> Offline tests (ctest)"
if ctest --test-dir "$BUILD" --output-on-failure ${CMAKE_BUILD_TYPE:+-C "$CMAKE_BUILD_TYPE"}; then
  ok "ctest"
else
  bad "ctest"
fi

echo "==> CLI contract"
if bash "$ROOT/scripts/smoke.sh" "$BIN"; then ok "smoke.sh"; else bad "smoke.sh"; fi
if bash "$ROOT/scripts/e2e.sh" "$BIN"; then ok "e2e.sh"; else bad "e2e.sh"; fi

echo "Summary: $pass passed, $fail failed"
[[ "$fail" -eq 0 ]]
