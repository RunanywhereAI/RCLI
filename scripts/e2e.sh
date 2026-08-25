#!/usr/bin/env bash
# Kit-consumer e2e for a built rcli binary. Does not compile the SDK.
#
#   scripts/e2e.sh <path-to-rcli>
#
# Always runs scripts/smoke.sh (no model download). Set RCLI_E2E_MODEL to also
# pull a catalog model and run one generation — that needs network + disk.
# Overlay backends: RCLI_E2E_MLX_MODEL, RCLI_E2E_NEURT_MODEL, RCLI_E2E_QHEXRT_MODEL.
# CMAKE_PREFIX_PATH is optional; RCLI_SDK_KIT is preferred. Unset is fine.
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
kit_root="${RCLI_SDK_KIT:-${CMAKE_PREFIX_PATH-}}"
kit_root="${kit_root%%:*}"
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

expected_backends=()
kit_flag_true() {
    local key="$1"
    [[ -n "${kit_cfg}" ]] || return 0
    grep -Eq "set\\(${key}[[:space:]]+(TRUE|ON|1)\\)" "${kit_cfg}"
}
# When the kit Config is present, honor HAS_* (Windows ARM64 public kits have
# no llama.cpp/ONNX/Sherpa). Without a kit, assume the public OSS bottle.
if [[ -z "${kit_cfg}" ]]; then
    expected_backends=(llamacpp onnx sherpa)
else
    if kit_flag_true RunAnywhere_HAS_LLAMACPP; then
        expected_backends+=(llamacpp)
    fi
    if kit_flag_true RunAnywhere_HAS_ONNX; then
        expected_backends+=(onnx)
    fi
    if kit_flag_true RunAnywhere_HAS_SHERPA; then
        expected_backends+=(sherpa)
    fi
fi
base="$(basename "${RCLI}")"
base="${base%.exe}"
if [[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 && "${base}" == "rcli" ]]; then
    expected_backends+=(mlx)
fi
# Overlay archives flip HAS_NEURT/HAS_QHEXRT at find_package time, but the
# packaged Config.cmake still says FALSE. Presence of the backend library is
# the source of truth. Only RCLI_SDK_KIT counts — ambient CMAKE_PREFIX_PATH
# from an overlay rebuild must not fail a public-bottle e2e.
overlay_root="${RCLI_SDK_KIT-}"
if [[ -n "${overlay_root}" ]] && command -v cygpath >/dev/null 2>&1; then
    overlay_root="$(cygpath -u "${overlay_root}")"
fi
if [[ -n "${overlay_root}" ]]; then
    if [[ -f "${overlay_root}/lib/librac_backend_neurt.a" || -f "${overlay_root}/lib/rac_backend_neurt.lib" ]]; then
        expected_backends+=(neurt)
    fi
    if [[ -f "${overlay_root}/lib/librac_backend_qhexrt.a" || -f "${overlay_root}/lib/rac_backend_qhexrt.lib" ]]; then
        expected_backends+=(qhexrt)
    fi
fi
if [[ ${#expected_backends[@]} -eq 0 ]]; then
    echo "  skip  backends (kit has no engines)"
elif bash "${ROOT}/scripts/assert-backends.sh" "${RCLI}" "${expected_backends[@]}"; then
    echo "  ok    backends ${expected_backends[*]}"
else
    echo "  FAIL  backends ${expected_backends[*]}"
    fail=1
fi
if [[ ${#expected_backends[@]} -gt 0 ]]; then
    if bash "${ROOT}/scripts/assert-binary-backends.sh" "${RCLI}" "${expected_backends[@]}"; then
        echo "  ok    binary ${expected_backends[*]}"
    else
        echo "  FAIL  binary ${expected_backends[*]}"
        fail=1
    fi
fi
if "${RCLI}" models list --help >/dev/null 2>&1; then
    check "models list" "${RCLI}" models list
fi

# Per-backend help surfaces that only compile in when the engine is linked.
for name in "${expected_backends[@]}"; do
    case "${name}" in
        neurt)
            check "image generate --help" "${RCLI}" image generate --help
            ;;
        mlx|qhexrt)
            check "run --help" "${RCLI}" run --help
            ;;
    esac
done

roundtrip() {
    local label="$1"
    local model="$2"
    local kind="${3:-llm}"
    echo "e2e (${label}): ${model}"
    export RUNANYWHERE_HOME="${RUNANYWHERE_HOME:-$(mktemp -d)}"
    if [[ -e "${model}" ]]; then
        echo "  ok    local path ${model}"
    elif "${RCLI}" models download "${model}"; then
        echo "  ok    models download ${model}"
    elif "${RCLI}" pull "${model}"; then
        echo "  ok    pull ${model}"
    else
        echo "  FAIL  download ${model}"
        return 1
    fi
    case "${kind}" in
        image)
            local out
            out="$(mktemp -t rcli-e2e-img).png"
            if "${RCLI}" image generate --model "${model}" --prompt "a red square" --out "${out}" --steps 4 >/dev/null 2>&1 \
                && [[ -s "${out}" ]]; then
                echo "  ok    image generate"
                rm -f "${out}"
                return 0
            fi
            echo "  FAIL  image generate"
            return 1
            ;;
        *)
            if "${RCLI}" llm generate --model "${model}" "Reply with exactly: ok" --max-output-tokens 8 >/dev/null 2>&1 \
                || "${RCLI}" run "${model}" "Reply with exactly: ok" >/dev/null 2>&1; then
                echo "  ok    generate"
                return 0
            fi
            echo "  FAIL  generate"
            return 1
            ;;
    esac
}

# Default one-model path (any backend). Specific engines below can also be set.
if [[ -n "${RCLI_E2E_MODEL:-}" ]]; then
    roundtrip "model" "${RCLI_E2E_MODEL}" llm || fail=1
else
    echo "  skip  model round-trip (set RCLI_E2E_MODEL to enable)"
fi

# Newly added backends: run a real model when the tester asks, never in
# default CI (downloads are large; NeuRT/QHexRT overlays are private).
if [[ -n "${RCLI_E2E_MLX_MODEL:-}" ]]; then
    roundtrip "mlx" "${RCLI_E2E_MLX_MODEL}" llm || fail=1
elif printf '%s\n' "${expected_backends[@]}" | grep -qx mlx; then
    echo "  skip  mlx round-trip (set RCLI_E2E_MLX_MODEL, e.g. mlx-qwen3)"
fi
if [[ -n "${RCLI_E2E_NEURT_MODEL:-}" ]]; then
    roundtrip "neurt" "${RCLI_E2E_NEURT_MODEL}" image || fail=1
elif printf '%s\n' "${expected_backends[@]}" | grep -qx neurt; then
    echo "  skip  neurt round-trip (set RCLI_E2E_NEURT_MODEL, e.g. sd15)"
fi
if [[ -n "${RCLI_E2E_QHEXRT_MODEL:-}" ]]; then
    roundtrip "qhexrt" "${RCLI_E2E_QHEXRT_MODEL}" llm || fail=1
elif printf '%s\n' "${expected_backends[@]}" | grep -qx qhexrt; then
    echo "  skip  qhexrt round-trip (set RCLI_E2E_QHEXRT_MODEL)"
fi

exit "${fail}"
