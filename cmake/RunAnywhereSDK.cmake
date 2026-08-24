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
    if(RunAnywhere_HAS_NEURT AND TARGET RunAnywhere::neurt)
        target_compile_definitions(${target} PRIVATE RCLI_HAS_NEURT=1)
        target_link_libraries(${target} PRIVATE RunAnywhere::neurt)
    endif()
    if(RunAnywhere_HAS_QHEXRT AND TARGET RunAnywhere::qhexrt)
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
