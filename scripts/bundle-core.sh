#!/usr/bin/env bash
# Merges everything the wally executable links into one static archive, and
# writes out the system flags that are left over.
#
# The Swift build needs both: SwiftPM owns the final link for the MLX binary
# (it has to, so it can lay out the MLX Metal resource bundles), and it cannot
# be handed a hundred-odd CMake-internal archives one at a time. Rather than
# maintaining a second copy of the dependency list that would drift the first
# time a target is added, this reads the link line CMake already generated.
set -euo pipefail

BUILD="${1:-build}"
LINK_TXT="${BUILD}/CMakeFiles/wally.dir/link.txt"
OUT_LIB="${BUILD}/libwally_bundle.a"
OUT_FLAGS="${BUILD}/wally-link-flags.txt"

# Ninja never writes CMakeFiles/<tgt>.dir/link.txt (Makefiles only). The last
# command ninja runs for `wally` is the link; compile lines also contain
# `CMakeFiles/wally.dir/` so grepping for "wally" picks compiles.
if [[ ! -s "${LINK_TXT}" && -f "${BUILD}/build.ninja" ]]; then
    mkdir -p "${BUILD}/CMakeFiles/wally.dir"
    ninja_bin="$(command -v ninja || command -v ninja-build || true)"
    if [[ -n "${ninja_bin}" ]]; then
        "${ninja_bin}" -C "${BUILD}" -t commands wally | tail -1 > "${LINK_TXT}" || true
    fi
fi

if [[ ! -s "${LINK_TXT}" ]]; then
    echo "no ${LINK_TXT} — build the wally target first (Ninja or Makefiles)" >&2
    exit 1
fi

line="$(cat "${LINK_TXT}")"

# Expand CMake @response files so archives listed only there are visible.
expand_link_tokens() {
    local token rsp
    for token in "$@"; do
        case "${token}" in
            @*)
                rsp="${token#@}"
                if [[ ! -f "${rsp}" && -f "${BUILD}/${rsp}" ]]; then
                    rsp="${BUILD}/${rsp}"
                fi
                if [[ -f "${rsp}" ]]; then
                    # shellcheck disable=SC2046
                    expand_link_tokens $(cat "${rsp}")
                fi
                ;;
            *)
                printf '%s\n' "${token}"
                ;;
        esac
    done
}

contains() {
    local needle="$1"
    shift
    local item
    for item in "$@"; do
        [[ "${item}" == "${needle}" ]] && return 0
    done
    return 1
}

# Plugin backends are -force_load on wally-cxx so their static registrars run.
# llama-common is a regular archive: force-loading it pulls download.cpp.o,
# which references cpp-httplib methods that the kit never emitted as objects
# (wally-cxx never pulled that TU). Split so Swift can force_load plugins
# without dragging those undefineds in.
plugins=()
regular=()
previous=""
while IFS= read -r token; do
    archive=""
    kind="regular"
    if [[ "${previous}" == "-force_load" ]]; then
        archive="${token}"
        kind="plugin"
        previous=""
    else
        case "${token}" in
            -Wl,-force_load,*)
                archive="${token#-Wl,-force_load,}"
                kind="plugin"
                ;;
            -Wl,--whole-archive|--whole-archive|-Wl,--no-whole-archive|--no-whole-archive)
                continue
                ;;
            -force_load)
                previous="-force_load"
                continue
                ;;
            -*)
                continue
                ;;
            *.a|*.o)
                archive="${token}"
                ;;
        esac
    fi
    [[ -n "${archive}" ]] || continue
    if [[ "${archive}" != /* ]]; then
        archive="${BUILD}/${archive}"
    fi
    [[ -f "${archive}" ]] || continue
    if [[ "${kind}" == "plugin" ]]; then
        contains "${archive}" "${plugins[@]:-}" || plugins+=("${archive}")
    else
        contains "${archive}" "${regular[@]:-}" || regular+=("${archive}")
    fi
done < <(expand_link_tokens ${line})

# A backend listed both ways (force_load + again as a plain .a) stays a plugin.
regular_only=()
for a in "${regular[@]:-}"; do
    contains "${a}" "${plugins[@]:-}" && continue
    regular_only+=("${a}")
done
regular=("${regular_only[@]:-}")

if [[ ${#regular[@]} -eq 0 ]]; then
    echo "found no static archives in ${LINK_TXT}" >&2
    echo "link line: ${line}" >&2
    exit 1
fi

OUT_PLUGINS="${BUILD}/libwally_plugins.a"
# libtool rather than ar: it merges archives rather than nesting them, and it
# is what ships with the toolchain that produced them.
libtool -static -o "${OUT_LIB}" "${regular[@]}"
if [[ ${#plugins[@]} -gt 0 ]]; then
    libtool -static -o "${OUT_PLUGINS}" "${plugins[@]}"
fi

# What is left is frameworks and system libraries, which the Swift link needs
# verbatim. .tbd and .dylib entries matter as much as -l flags: zlib, bz2 and
# curl all arrive as absolute SDK paths, and dropping them costs a link error
# a long way from here.
#
# Deduplicated on whole entries rather than on lines, because `-framework` and
# the name after it are one entry: dropping the repeated name on its own leaves
# a dangling flag that consumes whatever follows it.
frameworks=()
libraries=()
previous=""

for token in ${line}; do
    if [[ "${previous}" == "-framework" ]]; then
        contains "${token}" "${frameworks[@]:-}" || frameworks+=("${token}")
        previous=""
        continue
    fi
    case "${token}" in
        -framework) previous="-framework" ;;
        -Wl,-rpath,*)
            contains "${token}" "${libraries[@]:-}" || libraries+=("${token}")
            ;;
        -l*)
            contains "${token}" "${libraries[@]:-}" || libraries+=("${token}")
            ;;
        *.tbd|*.dylib)
            # xcodebuild/clang needs a -L or it fails with "library not found
            # for -lonnxruntime". Bare paths are ignored by `swift build`.
            dir="$(cd "$(dirname "${token}")" && pwd)"
            lflag="-L${dir}"
            contains "${lflag}" "${libraries[@]:-}" || libraries+=("${lflag}")
            name="$(basename "${token}")"
            name="${name%.tbd}"
            name="${name%.dylib}"
            name="-l${name#lib}"
            contains "${name}" "${libraries[@]:-}" || libraries+=("${name}")
            ;;
    esac
done

: > "${OUT_FLAGS}"
for name in "${frameworks[@]:-}"; do
    [[ -n "${name}" ]] || continue
    printf -- '-framework\n%s\n' "${name}" >> "${OUT_FLAGS}"
done
for name in "${libraries[@]:-}"; do
    [[ -n "${name}" ]] || continue
    printf -- '%s\n' "${name}" >> "${OUT_FLAGS}"
done

echo "merged ${#regular[@]} archives into ${OUT_LIB} (${#plugins[@]} plugins force-loaded separately)"
