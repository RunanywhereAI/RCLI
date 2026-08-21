#!/usr/bin/env bash
# Builds the Apple flavour of rcli: the same application, entered through Swift
# so MLX's runtime callbacks are registered before anything runs.
#
#   scripts/build-mlx.sh [build-dir]
#
# CMake must have built the rcli target first; this merges what it produced and
# hands it to xcodebuild, which owns the final link.
#
# xcodebuild rather than `swift build`, and this is not a preference: mlx-swift
# says it outright in its own README — SwiftPM on the command line cannot
# compile Metal shaders, xcodebuild can. Without them MLX links, registers
# nothing, and reports itself unavailable at runtime.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-${ROOT}/build}"

"${ROOT}/scripts/bundle-core.sh" "${BUILD}"

flags=()
while IFS= read -r entry; do
    [[ -n "${entry}" ]] || continue
    flags+=("${entry}")
done < "${BUILD}/rcli-link-flags.txt"

SDK="${RCLI_SDK_DIR:-$(cd "${ROOT}/../runanywhere-sdks" && pwd)}"

cd "${ROOT}/swift"
# RUNANYWHERE_BUILD_MLX_DISTRIBUTION_FRAMEWORK is what makes MLXRuntime
# reference the commons ABI by header and link no second copy of it — without
# it the published XCFrameworks come along and the process ends up with two
# plugin registries.
#
# HEADER_SEARCH_PATHS is needed because the -Xcc include the SDK sets for that
# lane does not reach xcodebuild's module dependency scanner, which then fails
# to build MLXBackend.
RUNANYWHERE_BUILD_MLX_DISTRIBUTION_FRAMEWORK=1 \
    xcodebuild build \
    -scheme rcli-mlx \
    -destination "platform=macOS,arch=$(uname -m)" \
    -configuration Release \
    -derivedDataPath .build/xcode \
    HEADER_SEARCH_PATHS="\$(inherited) ${SDK}/core/include" \
    OTHER_LDFLAGS="${BUILD}/librcli_bundle.a ${flags[*]}" \
    | grep -E "error:|warning: .*[Mm]etal|BUILD" || true

PRODUCTS="${ROOT}/swift/.build/xcode/Build/Products/Release"
[[ -x "${PRODUCTS}/RCLIMLX" ]] || { echo "the MLX build produced no binary" >&2; exit 1; }

# This is the rcli that ships: one executable with every engine. mlx-swift keeps
# its Metal shaders in a resource bundle rather than inside the binary, so that
# one directory has to sit beside it — without it MLX reports itself
# unavailable and the other five engines carry on.
cp "${PRODUCTS}/RCLIMLX" "${BUILD}/rcli"
rm -rf "${BUILD}/mlx-swift_Cmlx.bundle"
cp -R "${PRODUCTS}/mlx-swift_Cmlx.bundle" "${BUILD}/"
echo "built ${BUILD}/rcli"
