#!/usr/bin/env bash
# package-wally.sh <build-dir> <platform-tag>
#
# Relocatable wally bottle from a kit-linked CMake build. Does not compile the SDK.
#
#   platform-tag: macos-arm64 | linux-x86_64
#   version:      $WALLY_VERSION, else project(wally VERSION …)
#   macOS signing: $WALLY_CODESIGN_IDENTITY, optional $WALLY_CODESIGN_KEYCHAIN
#                  Set $WALLY_REQUIRE_DEVELOPER_ID=1 to reject ad-hoc signing.
#
# Layout:
#   wally-<platform>/bin/wally
#   wally-<platform>/lib/<onnxruntime shared lib>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD="${1:?usage: package-wally.sh <build-dir> <platform-tag>}"
PLATFORM="${2:?usage: package-wally.sh <build-dir> <platform-tag>}"
[[ "${BUILD}" = /* ]] || BUILD="${ROOT}/${BUILD}"

VERSION="${WALLY_VERSION:-}"
if [[ -z "${VERSION}" ]]; then
  VERSION="$(sed -nE 's/^project\(wally VERSION ([0-9.]+).*/\1/p' "${ROOT}/CMakeLists.txt" | head -1)"
fi
[[ -n "${VERSION}" ]] || { echo "error: cannot resolve WALLY version" >&2; exit 1; }

KIT="${WALLY_SDK_KIT:-${CMAKE_PREFIX_PATH:-}}"
KIT="${KIT%%:*}"

BIN=""
if [[ "$(uname -s)" == Darwin ]]; then
  # The macOS bottle is the Swift MLX host. Never silently ship wally-cxx.
  if [[ ! -x "${BUILD}/wally" ]]; then
    echo "error: macOS bottle requires ${BUILD}/wally (Swift MLX host)." >&2
    echo "  cmake --build with WALLY_APPLE_MLX_HOST=ON, or scripts/build-mlx.sh" >&2
    exit 1
  fi
  BIN="${BUILD}/wally"
else
  cands=("${BUILD}/wally-cxx" "${BUILD}/wally" "${BUILD}/wally.exe" "${BUILD}/Release/wally" "${BUILD}/Release/wally.exe")
  for cand in "${cands[@]}"; do
    if [[ -x "${cand}" ]]; then BIN="${cand}"; break; fi
  done
fi
[[ -n "${BIN}" ]] || { echo "error: wally binary not found under ${BUILD}" >&2; exit 1; }

DIST="${ROOT}/dist"
STAGE_ROOT="${DIST}/stage"
STAGE="${STAGE_ROOT}/wally-${PLATFORM}"
TARBALL="${DIST}/wally-${VERSION}-${PLATFORM}.tar.gz"

rm -rf "${STAGE}"
mkdir -p "${STAGE}/bin" "${STAGE}/lib"
cp "${BIN}" "${STAGE}/bin/wally"
chmod +x "${STAGE}/bin/wally"
[[ -f "${ROOT}/README.md" ]] && cp "${ROOT}/README.md" "${STAGE}/README.md"
shopt -s nullglob
for bundle in "${BUILD}"/*.bundle; do
  cp -R "${bundle}" "${STAGE}/bin/"
done
shopt -u nullglob
if [[ "$(uname -s)" == Darwin ]]; then
  if [[ ! -d "${STAGE}/bin/mlx-swift_Cmlx.bundle" ]]; then
    echo "error: macOS bottle requires mlx-swift_Cmlx.bundle next to wally (Metal shaders)." >&2
    echo "  cmake --build with WALLY_APPLE_MLX_HOST=ON, or scripts/build-mlx.sh" >&2
    exit 1
  fi
fi

copy_kit_runtime() {
  local src="$1"
  [[ -n "${src}" && -e "${src}" ]] || return 0
  cp -R "${src}" "${STAGE}/lib/"
}

if [[ -n "${KIT}" && -d "${KIT}/third_party" ]]; then
  case "${PLATFORM}" in
    macos-*)
      copy_kit_runtime "${KIT}/third_party/libonnxruntime.dylib"
      ;;
    linux-*)
      shopt -s nullglob
      for so in "${KIT}/third_party"/libonnxruntime.so*; do
        copy_kit_runtime "${so}"
      done
      shopt -u nullglob
      ;;
  esac
fi

case "${PLATFORM}" in
  macos-*)
    if [[ -d "${STAGE}/lib" ]] && compgen -G "${STAGE}/lib/*.dylib" >/dev/null; then
      install_name_tool -add_rpath "@loader_path/../lib" "${STAGE}/bin/wally" 2>/dev/null || true
      for lib in "${STAGE}/lib/"*.dylib; do
        install_name_tool -id "@rpath/$(basename "${lib}")" "${lib}" 2>/dev/null || true
      done
    fi
    sign_identity="${WALLY_CODESIGN_IDENTITY:--}"
    if [[ "${WALLY_REQUIRE_DEVELOPER_ID:-0}" == 1 && "${sign_identity}" == - ]]; then
      echo "error: production packaging requires WALLY_CODESIGN_IDENTITY" >&2
      exit 1
    fi
    sign_args=(--force --sign "${sign_identity}")
    if [[ "${sign_identity}" != - ]]; then
      sign_args+=(--options runtime --timestamp)
    fi
    if [[ -n "${WALLY_CODESIGN_KEYCHAIN:-}" ]]; then
      sign_args+=(--keychain "${WALLY_CODESIGN_KEYCHAIN}")
    fi
    shopt -s nullglob
    for lib in "${STAGE}/lib/"*.dylib; do
      codesign "${sign_args[@]}" "${lib}"
      codesign --verify --strict "${lib}"
    done
    shopt -u nullglob
    codesign "${sign_args[@]}" "${STAGE}/bin/wally"
    codesign --verify --strict "${STAGE}/bin/wally"
    ;;
  linux-*)
    if command -v patchelf >/dev/null; then
      patchelf --set-rpath "\$ORIGIN/../lib" "${STAGE}/bin/wally"
    fi
    ;;
esac

"${STAGE}/bin/wally" version >/dev/null

mkdir -p "${DIST}"
rm -f "${TARBALL}" "${TARBALL}.sha256"
# macOS tar otherwise serializes Finder metadata as `._*` AppleDouble roots,
# breaking the single-root archive contract and surprising non-macOS clients.
COPYFILE_DISABLE=1 tar -czf "${TARBALL}" -C "${STAGE_ROOT}" "wally-${PLATFORM}"
(cd "${DIST}" && shasum -a 256 "$(basename "${TARBALL}")" > "$(basename "${TARBALL}").sha256")
echo "Packaged ${TARBALL}"
tar -tzf "${TARBALL}" | head -20
