#!/usr/bin/env bash
# package-rcli.sh <build-dir> <platform-tag>
#
# Relocatable rcli bottle from a kit-linked CMake build. Does not compile the SDK.
#
#   platform-tag: macos-arm64 | linux-x86_64
#   version:      $RCLI_VERSION, else project(rcli VERSION …)
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
  cands=("${BUILD}/rcli" "${BUILD}/rcli-cxx" "${BUILD}/Release/rcli" "${BUILD}/Release/rcli.exe")
else
  cands=("${BUILD}/rcli-cxx" "${BUILD}/rcli" "${BUILD}/Release/rcli" "${BUILD}/Release/rcli.exe")
fi
for cand in "${cands[@]}"; do
  if [[ -x "${cand}" ]]; then BIN="${cand}"; break; fi
done
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
if [[ -d "${BUILD}/mlx-swift_Cmlx.bundle" ]]; then
  cp -R "${BUILD}/mlx-swift_Cmlx.bundle" "${STAGE}/bin/"
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
    codesign --force -s - "${STAGE}/bin/rcli" 2>/dev/null || true
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
tar -czf "${TARBALL}" -C "${STAGE_ROOT}" "rcli-${PLATFORM}"
(cd "${DIST}" && shasum -a 256 "$(basename "${TARBALL}")" > "$(basename "${TARBALL}").sha256")
echo "Packaged ${TARBALL}"
tar -tzf "${TARBALL}" | head -20
