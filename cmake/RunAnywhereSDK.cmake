# Consumes a packaged C++ desktop kit via find_package(RunAnywhere).
# The SDK is never compiled as a subdirectory or FetchContent'd.
#
#   CMAKE_PREFIX_PATH=<kit prefix>     # dist/cpp-desktop-<os>-<arch>
#   or RUNANYWHERE_ROOT=<kit prefix>
#   or RCLI_SDK_KIT=<kit prefix>
#
# RCLI_SDK_DIR is accepted only as an alias for a *kit* prefix (must contain
# lib/cmake/RunAnywhere/RunAnywhereConfig.cmake). Pointing it at the monorepo
# source tree is a hard error.

set(RCLI_SDK_KIT "" CACHE PATH "Path to a staged RunAnywhere C++ desktop kit")
set(RCLI_SDK_DIR "" CACHE PATH "Deprecated alias for RCLI_SDK_KIT (kit prefix, not source)")
set(RCLI_SDK_VERSION "" CACHE STRING "EXACT find_package version; default from cmake/sdk-pin.cmake")

if(RCLI_SDK_DIR AND NOT RCLI_SDK_KIT)
    set(RCLI_SDK_KIT "${RCLI_SDK_DIR}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/sdk-pin.cmake")
if(RCLI_SDK_VERSION AND NOT RCLI_SDK_VERSION STREQUAL RCLI_PINNED_SDK_VERSION)
    message(FATAL_ERROR
        "RCLI_SDK_VERSION=${RCLI_SDK_VERSION} does not match "
        "RCLI_PINNED_SDK_VERSION=${RCLI_PINNED_SDK_VERSION} in cmake/sdk-pin.cmake. "
        "Bump the pin (and kit checksums) instead of overriding the version.")
endif()
if(NOT RCLI_SDK_VERSION)
    set(RCLI_SDK_VERSION "${RCLI_PINNED_SDK_VERSION}")
endif()

if(RCLI_SDK_KIT)
    if(EXISTS "${RCLI_SDK_KIT}/CMakeLists.txt" AND NOT EXISTS
       "${RCLI_SDK_KIT}/lib/cmake/RunAnywhere/RunAnywhereConfig.cmake")
        message(FATAL_ERROR
            "RCLI_SDK_KIT/RCLI_SDK_DIR points at SDK source (${RCLI_SDK_KIT}). "
            "Build the C++ desktop kit first:\n"
            "  cmake --preset cpp-desktop-macos-arm64 && "
            "cmake --build --preset cpp-desktop-macos-arm64 --target package-cpp-desktop\n"
            "then pass -DRCLI_SDK_KIT=<sdks>/dist/cpp-desktop-<os>-<arch>")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${RCLI_SDK_KIT}")
endif()
if(DEFINED ENV{RUNANYWHERE_ROOT} AND NOT RCLI_SDK_KIT)
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{RUNANYWHERE_ROOT}")
endif()

find_package(RunAnywhere ${RCLI_SDK_VERSION} EXACT REQUIRED CONFIG)

# Proto is the SOT across the two repos. The kit stamps SCHEMA_LOCK into
# find_package vars; this pin must match. Do not run protoc in RCLI to "fix"
# a mismatch — bump the pin after consuming a new kit.
if(NOT RunAnywhere_IDL_SCHEMA_SHA256)
    message(FATAL_ERROR
        "RunAnywhere kit is missing IDL schema metadata "
        "(RunAnywhere_IDL_SCHEMA_SHA256). Rebuild with package-cpp-desktop.")
endif()
if(NOT RunAnywhere_IDL_SCHEMA_SHA256 STREQUAL RCLI_PINNED_IDL_SCHEMA_SHA256
   OR NOT RunAnywhere_IDL_VERSION STREQUAL RCLI_PINNED_IDL_VERSION
   OR NOT RunAnywhere_IDL_PROTOC_VERSION STREQUAL RCLI_PINNED_IDL_PROTOC_VERSION)
    message(FATAL_ERROR
        "RunAnywhere kit IDL does not match cmake/sdk-pin.cmake.\n"
        "  kit:  ${RunAnywhere_IDL_VERSION} / ${RunAnywhere_IDL_SCHEMA_SHA256} / protoc ${RunAnywhere_IDL_PROTOC_VERSION}\n"
        "  pin:  ${RCLI_PINNED_IDL_VERSION} / ${RCLI_PINNED_IDL_SCHEMA_SHA256} / protoc ${RCLI_PINNED_IDL_PROTOC_VERSION}\n"
        "Consume the kit that matches this pin, or update the pin after a schema bump. "
        "RCLI must not run protoc.")
endif()

set(RCLI_SDK_ROOT "${RunAnywhere_INCLUDE_DIR}/.." CACHE INTERNAL "")
set(RCLI_IDL_DIR "${RunAnywhere_IDL_DIR}" CACHE INTERNAL "")
set(RCLI_PROTO_INCLUDE_DIR "${RunAnywhere_PROTO_INCLUDE_DIR}" CACHE INTERNAL "")

if(NOT TARGET RunAnywhere::commons)
    message(FATAL_ERROR "find_package(RunAnywhere) did not import RunAnywhere::commons")
endif()

# MSVC link.exe cannot consume a DLL (LNK1107). Older kits globbed
# third_party/onnxruntime.dll into INTERFACE_LINK_LIBRARIES. Drop any
# .dll from the imported target and, when present, link the import lib.
if(WIN32)
    get_target_property(_rcli_ra_ifaces RunAnywhere::commons INTERFACE_LINK_LIBRARIES)
    if(_rcli_ra_ifaces)
        set(_rcli_ra_kept "")
        foreach(_lib IN LISTS _rcli_ra_ifaces)
            if(_lib MATCHES "\\.[Dd][Ll][Ll]$")
                continue()
            endif()
            list(APPEND _rcli_ra_kept "${_lib}")
        endforeach()
        set_property(TARGET RunAnywhere::commons PROPERTY INTERFACE_LINK_LIBRARIES "${_rcli_ra_kept}")
    endif()
    if(DEFINED RunAnywhere_LIBRARY_DIR AND EXISTS "${RunAnywhere_LIBRARY_DIR}/onnxruntime.lib")
        get_target_property(_rcli_ra_ifaces RunAnywhere::commons INTERFACE_LINK_LIBRARIES)
        set(_rcli_ra_ifaces "${_rcli_ra_ifaces}")
        if(NOT _rcli_ra_ifaces MATCHES "onnxruntime\\.lib")
            set_property(TARGET RunAnywhere::commons APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES "${RunAnywhere_LIBRARY_DIR}/onnxruntime.lib")
        endif()
    endif()
    if(DEFINED RunAnywhere_LIBRARY_DIR AND EXISTS "${RunAnywhere_LIBRARY_DIR}/libcurl.lib")
        get_target_property(_rcli_ra_ifaces RunAnywhere::commons INTERFACE_LINK_LIBRARIES)
        set(_rcli_ra_ifaces "${_rcli_ra_ifaces}")
        if(NOT _rcli_ra_ifaces MATCHES "libcurl\\.lib")
            set_property(TARGET RunAnywhere::commons APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES "${RunAnywhere_LIBRARY_DIR}/libcurl.lib")
        endif()
    endif()
    # Static libcurl/libarchive in 0.20.26 kits omit these Windows imports.
    # Keep them here until a later kit bakes them into SYSTEM_LIBS.
    foreach(_sys IN ITEMS iphlpapi xmllite ole32)
        get_target_property(_rcli_ra_ifaces RunAnywhere::commons INTERFACE_LINK_LIBRARIES)
        set(_rcli_ra_ifaces "${_rcli_ra_ifaces}")
        if(NOT _rcli_ra_ifaces MATCHES "${_sys}")
            set_property(TARGET RunAnywhere::commons APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES "${_sys}")
        endif()
    endforeach()
endif()

# Back-compat name used by the rest of this CMakeLists.
if(NOT TARGET rac_commons)
    add_library(rac_commons ALIAS RunAnywhere::commons)
endif()

function(rcli_define_engine_macros target)
    if(RunAnywhere_HAS_LLAMACPP)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_LLAMACPP=1)
    endif()
    if(RunAnywhere_HAS_ONNX)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_ONNX=1)
    endif()
    if(RunAnywhere_HAS_SHERPA)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_SHERPA=1)
    endif()
    if(RunAnywhere_HAS_MLX)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_MLX=1)
    endif()
    if(RunAnywhere_HAS_CLOUD)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_CLOUD=1)
    endif()
    if(TARGET RunAnywhere::neurt)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_NEURT=1)
        target_link_libraries(${target} PRIVATE RunAnywhere::neurt)
    endif()
    if(TARGET RunAnywhere::qhexrt)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_QHEXRT=1)
        target_link_libraries(${target} PRIVATE RunAnywhere::qhexrt)
    endif()
    if(RunAnywhere_HAS_RAG)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_RAG=1)
    endif()
    if(RunAnywhere_HAS_SERVER)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_SERVER=1)
    endif()
endfunction()

# Win32 LoadLibrary searches the exe directory then PATH. Kit third_party
# (onnxruntime.dll, sherpa-onnx-c-api.dll, …) must sit next to every binary
# that links rcli_core — rcli.exe and the unit-test exes. Linking the import
# lib is not enough; 0xc0000135 is a missing DLL at process start.
function(rcli_stage_windows_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()
    if(DEFINED RunAnywhere_THIRD_PARTY_DIR AND EXISTS "${RunAnywhere_THIRD_PARTY_DIR}")
        file(GLOB _rcli_tp_dlls "${RunAnywhere_THIRD_PARTY_DIR}/*.dll")
        foreach(_dll IN LISTS _rcli_tp_dlls)
            get_filename_component(_dll_name "${_dll}" NAME)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}"
                    "$<TARGET_FILE_DIR:${target}>/${_dll_name}"
                COMMENT "Stage ${_dll_name} next to $<TARGET_FILE_NAME:${target}>"
                VERBATIM)
        endforeach()
    endif()
    if(DEFINED RunAnywhere_LIBRARY_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DSRC_DIR=${RunAnywhere_LIBRARY_DIR}/../bin"
                "-DDST_DIR=$<TARGET_FILE_DIR:${target}>"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy-overlay-dlls.cmake"
            COMMENT "Stage overlay DLLs next to $<TARGET_FILE_NAME:${target}>"
            VERBATIM)
    endif()
endfunction()
