# Pinned SDK kit this WALLY tree is validated against.
#
# The values are NOT written here any more. They live in `versions.toml` at the
# repo root, the single source for every version and pin, and this file reads
# them so its consumers still see the same WALLY_PINNED_* variables. Bump the
# pins in versions.toml, not here.
#
# The IDL pins mirror the kit's share/runanywhere/SCHEMA_LOCK (idl/SCHEMA_LOCK
# from the SDK). WALLY never runs protoc -- a mismatch means consume a new kit
# and update versions.toml, not regenerate headers. History on why the IDL pin
# moved from 1.1.0 to 1.1.1 across 0.20.32/0.20.33 is in versions.toml's [sdk]
# comment and this file's git log.

# Pull one `key = "value"` out of versions.toml into ${out_var}. Flat format by
# design, so a regex is enough and CMake needs no TOML parser.
function(_wally_read_version key out_var)
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../versions.toml" _line
         REGEX "^[ \t]*${key}[ \t]*=")
    if(NOT _line)
        message(FATAL_ERROR "versions.toml is missing '${key}'")
    endif()
    list(GET _line 0 _line)
    string(REGEX REPLACE "^[^\"]*\"([^\"]*)\".*$" "\\1" _value "${_line}")
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

_wally_read_version("kit_version" WALLY_PINNED_SDK_VERSION)
_wally_read_version("idl_version" WALLY_PINNED_IDL_VERSION)
_wally_read_version("idl_schema_sha256" WALLY_PINNED_IDL_SCHEMA_SHA256)
_wally_read_version("idl_protoc_version" WALLY_PINNED_IDL_PROTOC_VERSION)
_wally_read_version("kit_sha256_macos_arm64" WALLY_PINNED_KIT_SHA256_MACOS_ARM64)
_wally_read_version("kit_sha256_windows_x64" WALLY_PINNED_KIT_SHA256_WINDOWS_X64)
_wally_read_version("kit_sha256_windows_arm64" WALLY_PINNED_KIT_SHA256_WINDOWS_ARM64)
