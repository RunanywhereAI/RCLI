#!/usr/bin/env bash
# package-rcli.sh <build-dir> <platform-tag>
#
# Relocatable rcli bottle from a kit-linked CMake build. Does not compile the SDK.
#
#   platform-tag: macos-arm64 | linux-x86_64
#   version:      $RCLI_VERSION, else project(rcli VERSION …)
#   macOS signing: $RCLI_CODESIGN_IDENTITY, optional $RCLI_CODESIGN_KEYCHAIN
#                  Set $RCLI_REQUIRE_DEVELOPER_ID=1 to reject ad-hoc signing.
#
# Layout:
#   rcli-<platform>/bin/rcli
#   rcli-<platform>/lib/<onnxruntime shared lib>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD="${1:?usage: package-rcli.sh <build-dir> <platform-tag>}"
PLATFORM="${2:?usage: package-rcli.sh <build-dir> <platform-tag>}"
[[ "${BUILD}" = /* ]] || BUILD="${ROOT}/${BUILD}"

VERSION="${RCLI_VERSION:-}"
if [[ -z "${VERSION}" ]]; then
  VERSION="$(sed -nE 's/^project\(rcli VERSION ([0-9.]+).*/\1/p' "${ROOT}/CMakeLists.txt" | head -1)"
fi
[[ -n "${VERSION}" ]] || { echo "error: cannot resolve RCLI version" >&2; exit 1; }

KIT="${RCLI_SDK_KIT:-${CMAKE_PREFIX_PATH:-}}"
KIT="${KIT%%:*}"

BIN=""
if [[ "$(uname -s)" == Darwin ]]; then
  # The macOS bottle is the Swift MLX host. Never silently ship rcli-cxx.
  if [[ ! -x "${BUILD}/rcli" ]]; then
    echo "error: macOS bottle requires ${BUILD}/rcli (Swift MLX host)." >&2
    echo "  cmake --build with RCLI_APPLE_MLX_HOST=ON, or scripts/build-mlx.sh" >&2
    exit 1
  fi
  BIN="${BUILD}/rcli"
else
  cands=("${BUILD}/rcli-cxx" "${BUILD}/rcli" "${BUILD}/rcli.exe" "${BUILD}/Release/rcli" "${BUILD}/Release/rcli.exe")
  for cand in "${cands[@]}"; do
    if [[ -x "${cand}" ]]; then BIN="${cand}"; break; fi
  done
fi
[[ -n "${BIN}" ]] || { echo "error: rcli binary not found under ${BUILD}" >&2; exit 1; }

DIST="${ROOT}/dist"
STAGE_ROOT="${DIST}/stage"
STAGE="${STAGE_ROOT}/rcli-${PLATFORM}"
TARBALL="${DIST}/rcli-${VERSION}-${PLATFORM}.tar.gz"

rm -rf "${STAGE}"
mkdir -p "${STAGE}/bin" "${STAGE}/lib"
cp "${BIN}" "${STAGE}/bin/rcli"
chmod +x "${STAGE}/bin/rcli"
[[ -f "${ROOT}/README.md" ]] && cp "${ROOT}/README.md" "${STAGE}/README.md"
shopt -s nullglob
for bundle in "${BUILD}"/*.bundle; do
  cp -R "${bundle}" "${STAGE}/bin/"
done
shopt -u nullglob
if [[ "$(uname -s)" == Darwin ]]; then
  if [[ ! -d "${STAGE}/bin/mlx-swift_Cmlx.bundle" ]]; then
    echo "error: macOS bottle requires mlx-swift_Cmlx.bundle next to rcli (Metal shaders)." >&2
    echo "  cmake --build with RCLI_APPLE_MLX_HOST=ON, or scripts/build-mlx.sh" >&2
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
      install_name_tool -add_rpath "@loader_path/../lib" "${STAGE}/bin/rcli" 2>/dev/null || true
      for lib in "${STAGE}/lib/"*.dylib; do
        install_name_tool -id "@rpath/$(basename "${lib}")" "${lib}" 2>/dev/null || true
      done
    fi
    sign_identity="${RCLI_CODESIGN_IDENTITY:--}"
    if [[ "${RCLI_REQUIRE_DEVELOPER_ID:-0}" == 1 && "${sign_identity}" == - ]]; then
      echo "error: production packaging requires RCLI_CODESIGN_IDENTITY" >&2
      exit 1
    fi
    sign_args=(--force --sign "${sign_identity}")
    if [[ "${sign_identity}" != - ]]; then
      sign_args+=(--options runtime --timestamp)
    fi
    if [[ -n "${RCLI_CODESIGN_KEYCHAIN:-}" ]]; then
      sign_args+=(--keychain "${RCLI_CODESIGN_KEYCHAIN}")
    fi
    shopt -s nullglob
    for lib in "${STAGE}/lib/"*.dylib; do
      codesign "${sign_args[@]}" "${lib}"
      codesign --verify --strict "${lib}"
    done
    shopt -u nullglob
    codesign "${sign_args[@]}" "${STAGE}/bin/rcli"
    codesign --verify --strict "${STAGE}/bin/rcli"
    ;;
  linux-*)
    if command -v patchelf >/dev/null; then
      patchelf --set-rpath "\$ORIGIN/../lib" "${STAGE}/bin/rcli"
    fi
    ;;
esac

"${STAGE}/bin/rcli" version >/dev/null

mkdir -p "${DIST}"
rm -f "${TARBALL}" "${TARBALL}.sha256"
# macOS tar otherwise serializes Finder metadata as `._*` AppleDouble roots,
# breaking the single-root archive contract and surprising non-macOS clients.
COPYFILE_DISABLE=1 tar -czf "${TARBALL}" -C "${STAGE_ROOT}" "rcli-${PLATFORM}"
(cd "${DIST}" && shasum -a 256 "$(basename "${TARBALL}")" > "$(basename "${TARBALL}").sha256")
echo "Packaged ${TARBALL}"
tar -tzf "${TARBALL}" | head -20
