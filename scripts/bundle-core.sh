#!/usr/bin/env bash
# Merges everything the rcli executable links into one static archive, and
# writes out the system flags that are left over.
#
# The Swift build needs both: SwiftPM owns the final link for the MLX binary
# (it has to, so it can lay out the MLX Metal resource bundles), and it cannot
# be handed a hundred-odd CMake-internal archives one at a time. Rather than
# maintaining a second copy of the dependency list that would drift the first
# time a target is added, this reads the link line CMake already generated.
set -euo pipefail

BUILD="${1:-build}"
LINK_TXT="${BUILD}/CMakeFiles/rcli.dir/link.txt"
OUT_LIB="${BUILD}/librcli_bundle.a"
OUT_FLAGS="${BUILD}/rcli-link-flags.txt"

# Ninja never writes CMakeFiles/<tgt>.dir/link.txt (Makefiles only). The last
# command ninja runs for `rcli` is the link; compile lines also contain
# `CMakeFiles/rcli.dir/` so grepping for "rcli" picks compiles.
if [[ ! -s "${LINK_TXT}" && -f "${BUILD}/build.ninja" ]]; then
    mkdir -p "${BUILD}/CMakeFiles/rcli.dir"
    ninja_bin="$(command -v ninja || command -v ninja-build || true)"
    if [[ -n "${ninja_bin}" ]]; then
        "${ninja_bin}" -C "${BUILD}" -t commands rcli | tail -1 > "${LINK_TXT}" || true
    fi
fi

if [[ ! -s "${LINK_TXT}" ]]; then
    echo "no ${LINK_TXT} — build the rcli target first (Ninja or Makefiles)" >&2
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

archives=()
while IFS= read -r token; do
    case "${token}" in
        *.a)
            if [[ "${token}" = /* ]]; then
                archives+=("${token}")
            else
                archives+=("${BUILD}/${token}")
            fi
            ;;
    esac
done < <(expand_link_tokens ${line})

if [[ ${#archives[@]} -eq 0 ]]; then
    echo "found no static archives in ${LINK_TXT}" >&2
    echo "link line: ${line}" >&2
    exit 1
fi

# libtool rather than ar: it merges archives rather than nesting them, and it
# is what ships with the toolchain that produced them.
libtool -static -o "${OUT_LIB}" "${archives[@]}"

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
contains() {
    local needle="$1"
    shift
    local item
    for item in "$@"; do
        [[ "${item}" == "${needle}" ]] && return 0
    done
    return 1
}

for token in ${line}; do
    if [[ "${previous}" == "-framework" ]]; then
        contains "${token}" "${frameworks[@]:-}" || frameworks+=("${token}")
        previous=""
        continue
    fi
    case "${token}" in
        -framework) previous="-framework" ;;
        -l*)
            contains "${token}" "${libraries[@]:-}" || libraries+=("${token}")
            ;;
        *.tbd|*.dylib)
            # As a bare path these are silently ignored by swiftc, which then
            # fails on the symbols they would have provided. The -l spelling is
            # what it understands, and the SDK resolves it to the same stub.
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

echo "merged ${#archives[@]} archives into ${OUT_LIB}"
