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

# Local monorepo: the SDK Package.swift lives at the workspace root. Independent
# clones leave RCLI_SDK_SWIFT_PATH unset and pin runanywhere-swift by version.
if [[ -z "${RCLI_SDK_SWIFT_PATH:-}" && -f "${ROOT}/../../Package.swift" ]]; then
    export RCLI_SDK_SWIFT_PATH="${ROOT}/../.."
fi

cd "${ROOT}/swift"
RUNANYWHERE_BUILD_MLX_DISTRIBUTION_FRAMEWORK=1 \
    xcodebuild build \
    -scheme rcli-mlx \
    -destination "platform=macOS,arch=$(uname -m)" \
    -configuration Release \
    -derivedDataPath .build/xcode \
    HEADER_SEARCH_PATHS="\$(inherited) ${KIT}/include ${ROOT}/include" \
    OTHER_LDFLAGS="${BUILD}/librcli_bundle.a ${flags[*]}" \
    | grep -E "error:|warning: .*[Mm]etal|BUILD" || true

PRODUCTS="${ROOT}/swift/.build/xcode/Build/Products/Release"
[[ -x "${PRODUCTS}/RCLIMLX" ]] || { echo "the MLX build produced no binary" >&2; exit 1; }

cp "${PRODUCTS}/RCLIMLX" "${BUILD}/rcli"
if [[ -d "${PRODUCTS}/mlx-swift_Cmlx.bundle" ]]; then
    rm -rf "${BUILD}/mlx-swift_Cmlx.bundle"
    cp -R "${PRODUCTS}/mlx-swift_Cmlx.bundle" "${BUILD}/"
fi
echo "built ${BUILD}/rcli"
