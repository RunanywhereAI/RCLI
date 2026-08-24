#!/usr/bin/env bash
# Kit-consumer e2e for a built rcli binary. Does not compile the SDK.
#
#   scripts/e2e.sh <path-to-rcli>
#
# Always runs scripts/smoke.sh (no model download). Set RCLI_E2E_MODEL to also
# pull a catalog model and run one generation — that needs network + disk.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RCLI="${1:?usage: e2e.sh <path-to-rcli>}"
if [[ ! -e "${RCLI}" ]]; then
    echo "not found: ${RCLI}" >&2
    exit 1
fi
if [[ ! -x "${RCLI}" && "${RCLI}" != *.exe && "${RCLI}" != *.EXE ]]; then
    echo "not executable: ${RCLI}" >&2
    exit 1
fi

kit_cfg=""
kit_root="${RCLI_SDK_KIT:-${CMAKE_PREFIX_PATH%%:*}}"
if [[ -n "${kit_root}" ]] && command -v cygpath >/dev/null 2>&1; then
    kit_root="$(cygpath -u "${kit_root}")"
fi
if [[ -n "${kit_root}" ]]; then
    for cand in \
        "${kit_root}/lib/cmake/RunAnywhere/RunAnywhereConfig.cmake" \
        "${kit_root}/lib/cmake/runanywhere/RunAnywhereConfig.cmake"; do
        if [[ -f "${cand}" ]]; then
            kit_cfg="${cand}"
            break
        fi
    done
fi

# Win32 LoadLibrary searches the exe directory, then PATH. Stage kit DLLs
# next to rcli.exe and put third_party on PATH so onnx/sherpa can register
# even if CMake's configure-time GLOB missed a file.
if [[ "${RCLI}" == *.exe || "${RCLI}" == *.EXE ]] && [[ -n "${kit_root}" ]]; then
    exe_dir="$(cd "$(dirname "${RCLI}")" && pwd)"
    for d in "${kit_root}/third_party" "${kit_root}/bin" "${kit_root}/lib"; do
        [[ -d "${d}" ]] || continue
        if command -v cygpath >/dev/null 2>&1; then
            PATH="$(cygpath -u "${d}"):${PATH}"
        else
            PATH="${d}:${PATH}"
        fi
        find "${d}" -maxdepth 1 \( -iname '*.dll' \) -exec cp -n {} "${exe_dir}/" \; 2>/dev/null || true
    done
    export PATH
fi

bash "${ROOT}/scripts/smoke.sh" "${RCLI}"

fail=0
check() {
    local label="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "  ok    ${label}"
    else
        echo "  FAIL  ${label}"
        fail=1
    fi
}

echo "e2e (modelless extras): ${RCLI}"
check "info" "${RCLI}" info

# Assert the engines this binary's kit actually ships. A 0.20.26 Windows kit
# may only advertise llamacpp; 0.20.28+ kits advertise onnx + sherpa too.
# Product `rcli` on Apple Silicon also includes MLX. Intermediate `rcli-cxx` does not.

expected_backends=(llamacpp)
kit_flag_true() {
    local key="$1"
    [[ -n "${kit_cfg}" ]] || return 0
    grep -Eq "set\\(${key}[[:space:]]+(TRUE|ON|1)\\)" "${kit_cfg}"
}
if [[ -z "${kit_cfg}" ]] || kit_flag_true RunAnywhere_HAS_ONNX; then
    expected_backends+=(onnx)
fi
if [[ -z "${kit_cfg}" ]] || kit_flag_true RunAnywhere_HAS_SHERPA; then
    expected_backends+=(sherpa)
fi
base="$(basename "${RCLI}")"
base="${base%.exe}"
if [[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 && "${base}" == "rcli" ]]; then
    expected_backends+=(mlx)
fi
if bash "${ROOT}/scripts/assert-backends.sh" "${RCLI}" "${expected_backends[@]}"; then
    echo "  ok    backends ${expected_backends[*]}"
else
    echo "  FAIL  backends ${expected_backends[*]}"
    fail=1
fi
if "${RCLI}" models list --help >/dev/null 2>&1; then
    check "models list" "${RCLI}" models list
fi

if [[ -n "${RCLI_E2E_MODEL:-}" ]]; then
    echo "e2e (model): ${RCLI_E2E_MODEL}"
    export RUNANYWHERE_HOME="${RUNANYWHERE_HOME:-$(mktemp -d)}"
    if "${RCLI}" models download "${RCLI_E2E_MODEL}"; then
        echo "  ok    models download ${RCLI_E2E_MODEL}"
    elif "${RCLI}" pull "${RCLI_E2E_MODEL}"; then
        echo "  ok    pull ${RCLI_E2E_MODEL}"
    else
        echo "  FAIL  download ${RCLI_E2E_MODEL}"
        fail=1
    fi
    if "${RCLI}" llm generate --model "${RCLI_E2E_MODEL}" "Reply with exactly: ok" --max-output-tokens 8 >/dev/null 2>&1 \
        || "${RCLI}" run "${RCLI_E2E_MODEL}" "Reply with exactly: ok" >/dev/null 2>&1; then
        echo "  ok    generate"
    else
        echo "  FAIL  generate"
        fail=1
    fi
else
    echo "  skip  model round-trip (set RCLI_E2E_MODEL to enable)"
fi

exit "${fail}"
