#!/usr/bin/env bash
# Assert the binary actually contains the engines `wally backends` claims.
#
#   scripts/assert-binary-backends.sh <path-to-wally> <name> [<name>...]
#
# Release bottles (especially the Swift MLX host) strip global C symbols.
# Stream nm/strings into grep. `grep -q` closes the pipe early; with
# `set -o pipefail` that looks like failure (nm SIGPIPE). Disable pipefail
# for the scan.
set -euo pipefail

WALLY="${1:?usage: assert-binary-backends.sh <wally> <name>...}"
shift
if [[ $# -eq 0 ]]; then
    echo "usage: assert-binary-backends.sh <wally> <name>..." >&2
    exit 2
fi
if [[ ! -e "${WALLY}" ]]; then
    echo "not found: ${WALLY}" >&2
    exit 1
fi

has_tool=0
strings_bin=""
for cand in strings /usr/bin/strings "/c/Program Files/Git/usr/bin/strings" \
        "/usr/bin/x86_64-pc-msys-strings"; do
    if command -v "${cand}" >/dev/null 2>&1 || [[ -x "${cand}" ]]; then
        strings_bin="${cand}"
        has_tool=1
        break
    fi
done
command -v nm >/dev/null 2>&1 && has_tool=1
command -v dumpbin >/dev/null 2>&1 && has_tool=1
if [[ "${has_tool}" -eq 0 ]]; then
    echo "skip  binary-backends (no nm/dumpbin/strings)"
    exit 0
fi

need_pat() {
    case "$1" in
        mlx) echo 'raMLXRegisterRuntime|rac_mlx_set_callbacks|MLXRuntime|mlx-swift_Cmlx' ;;
        neurt) echo 'rac_plugin_entry_neurt|rac_backend_neurt|libneurt_' ;;
        qhexrt) echo 'rac_plugin_entry_qhexrt|rac_backend_qhexrt|QnnHtp' ;;
        llamacpp) echo 'rac_plugin_entry_llamacpp|rac_backend_llamacpp_register|g_llamacpp_' ;;
        sherpa) echo 'rac_plugin_entry_sherpa|rac_backend_sherpa|SherpaOnnx|g_sherpa_stt_ops' ;;
        onnx) echo 'rac_plugin_entry_onnx|rac_backend_onnx|OrtGetApiBase' ;;
        *) echo "" ;;
    esac
}

# Returns 0 if pat matches the image. Never trips pipefail on a real hit.
blob_match() {
    local pat="$1"
    set +e
    set +o pipefail
    if [[ -n "${strings_bin}" ]]; then
        "${strings_bin}" -a "${WALLY}" 2>/dev/null | grep -a -qiE "${pat}"
        if [[ $? -eq 0 ]]; then
            set -e
            set -o pipefail
            return 0
        fi
    fi
    if command -v nm >/dev/null 2>&1; then
        nm -a "${WALLY}" 2>/dev/null | grep -a -qiE "${pat}"
        if [[ $? -eq 0 ]]; then
            set -e
            set -o pipefail
            return 0
        fi
    fi
    if command -v dumpbin >/dev/null 2>&1; then
        dumpbin /ALL "${WALLY}" 2>/dev/null | grep -a -qiE "${pat}"
        if [[ $? -eq 0 ]]; then
            set -e
            set -o pipefail
            return 0
        fi
    fi
    set -e
    set -o pipefail
    return 1
}

fail=0
exe_dir="$(cd "$(dirname "${WALLY}")" && pwd)"
for name in "$@"; do
    if [[ "${name}" == mlx && -d "${exe_dir}/mlx-swift_Cmlx.bundle" ]]; then
        echo "  ok    binary has mlx (mlx-swift_Cmlx.bundle)"
        continue
    fi
    pat="$(need_pat "${name}")"
    if [[ -z "${pat}" ]]; then
        continue
    fi
    if blob_match "${pat}"; then
        echo "  ok    binary has ${name}"
    else
        echo "  FAIL  binary missing ${name} (${pat})"
        fail=1
    fi
done
exit "${fail}"
