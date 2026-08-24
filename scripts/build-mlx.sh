#!/usr/bin/env bash
# Apple shipping binary: CMake `rcli-cxx` objects + Swift MLX host → build/rcli.
#
#   scripts/build-mlx.sh [build-dir]
#
# Requires:
#   - cmake already built the rcli target (rcli-cxx + link.txt)
#   - a kit prefix on CMAKE_PREFIX_PATH / RCLI_SDK_KIT (public headers)
#   - Xcode (xcodebuild compiles MLX Metal shaders; `swift build` cannot)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-${ROOT}/build}"

if [[ ! -f "${BUILD}/CMakeFiles/rcli.dir/link.txt" ]]; then
    echo "error: ${BUILD}/CMakeFiles/rcli.dir/link.txt missing — cmake --build first" >&2
    exit 1
fi

KIT="${RCLI_SDK_KIT:-${CMAKE_PREFIX_PATH:-}}"
KIT="${KIT%%:*}"
if [[ -z "${KIT}" || ! -d "${KIT}/include" ]]; then
    echo "error: set RCLI_SDK_KIT to a staged C++ desktop kit prefix (include/rac)" >&2
    exit 1
fi

"${ROOT}/scripts/bundle-core.sh" "${BUILD}"

flags=()
while IFS= read -r entry; do
    [[ -n "${entry}" ]] || continue
    flags+=("${entry}")
done < "${BUILD}/rcli-link-flags.txt"

# The published runanywhere-swift tarball does not export RunAnywhereMLXRuntime
# (Swift MLX without a second commons archive). Apple rcli therefore needs the
# SDK source tree: nested monorepo, or RCLI_SDK_SWIFT_PATH in CI.
if [[ -z "${RCLI_SDK_SWIFT_PATH:-}" && -f "${ROOT}/../../Package.swift" ]]; then
    export RCLI_SDK_SWIFT_PATH="${ROOT}/../.."
fi
if [[ -z "${RCLI_SDK_SWIFT_PATH:-}" || ! -f "${RCLI_SDK_SWIFT_PATH}/Package.swift" ]]; then
    echo "error: Apple rcli needs the SDK Swift tree (RunAnywhereMLXRuntime)." >&2
    echo "  export RCLI_SDK_SWIFT_PATH=/path/to/runanywhere-sdks" >&2
    echo "  or build from EXTERNAL/RCLI inside that monorepo." >&2
    exit 1
fi

cd "${ROOT}/swift"
set +o pipefail
RUNANYWHERE_BUILD_MLX_DISTRIBUTION_FRAMEWORK=1 \
    xcodebuild build \
    -scheme rcli-mlx \
    -destination "platform=macOS,arch=$(uname -m)" \
    -configuration Release \
    -derivedDataPath .build/xcode \
    HEADER_SEARCH_PATHS="\$(inherited) ${KIT}/include ${ROOT}/include" \
    OTHER_LDFLAGS="${BUILD}/librcli_bundle.a ${flags[*]}" \
    | grep -E "error:|warning: .*[Mm]etal|BUILD"
xcodebuild_status=${PIPESTATUS[0]}
set -o pipefail
if [[ "${xcodebuild_status}" -ne 0 ]]; then
    echo "error: xcodebuild failed with status ${xcodebuild_status}" >&2
    exit "${xcodebuild_status}"
fi

PRODUCTS="${ROOT}/swift/.build/xcode/Build/Products/Release"
[[ -x "${PRODUCTS}/RCLIMLX" ]] || { echo "the MLX build produced no binary" >&2; exit 1; }

cp "${PRODUCTS}/RCLIMLX" "${BUILD}/rcli"
# Metal shader bundles must sit next to the executable. Copy every .bundle
# xcodebuild laid down (mlx-swift_Cmlx.bundle, mlx-swift_Cmlx.bundle, …).
shopt -s nullglob
for bundle in "${PRODUCTS}"/*.bundle; do
    dest="${BUILD}/$(basename "${bundle}")"
    rm -rf "${dest}"
    cp -R "${bundle}" "${dest}"
done
shopt -u nullglob
echo "built ${BUILD}/rcli"
