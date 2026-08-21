# Brings in the RunAnywhere SDK, which is where every model, engine and
# inference call comes from. RCLI supplies the terminal; the SDK supplies
# everything behind it.
#
# Two sources, because the two situations are genuinely different:
#
#   RCLI_SDK_DIR   an existing checkout, used as-is. This is the development
#                  path: the SDK is a large tree with a long first build, and
#                  pointing at a sibling clone reuses whatever is already built
#                  there instead of fetching and rebuilding a second copy.
#   otherwise      FetchContent at RCLI_SDK_TAG. This is what CI and a fresh
#                  clone get, and it is pinned rather than tracking a branch so
#                  a release is reproducible.

set(RCLI_SDK_DIR "" CACHE PATH "Path to an existing runanywhere-sdks checkout")
set(RCLI_SDK_TAG "v0.20.24" CACHE STRING "runanywhere-sdks tag to fetch when RCLI_SDK_DIR is unset")

# The SDK reads these before its own defaults apply, so they have to be set
# before the tree is added. Desktop adapter: file I/O, secure storage and the
# curl HTTP transport the download orchestrator needs. Without it rac_init()
# refuses to start.
set(RAC_DESKTOP_ADAPTER ON CACHE BOOL "" FORCE)
set(RAC_BUILD_BACKENDS ON CACHE BOOL "" FORCE)
set(RAC_ENABLE_PROTOBUF ON CACHE BOOL "" FORCE)
set(RAC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# rcli lives in this repo now; building the SDK's own copy would produce a
# second binary with the same name.
set(RAC_BUILD_CLI OFF CACHE BOOL "" FORCE)
# RAC_STATIC_PLUGINS is deliberately left at the SDK's own default. Forcing it
# on folds the cloud engine into rac_commons, which stops it being a
# rac_backend_cloud target; the loop below then never defines RCLI_HAS_CLOUD and
# the engine disappears from the binary with nothing to say so.

if(NOT APPLE)
    # MLX is Metal, and NeuRT is the Apple Neural Engine. Neither has a
    # non-Apple implementation to link, so asking for them off Apple fails the
    # configure rather than producing a smaller binary. Linux and Windows get
    # llama.cpp, sherpa and ONNX; the engine list simply reads shorter there.
    set(RAC_BACKEND_MLX OFF CACHE BOOL "" FORCE)
    set(RAC_BACKEND_NEURT OFF CACHE BOOL "" FORCE)
    # Apple Foundation Models, System TTS and CoreML diffusion.
    set(RAC_BUILD_PLATFORM OFF CACHE BOOL "" FORCE)
endif()

if(RCLI_SDK_DIR)
    if(NOT EXISTS "${RCLI_SDK_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "RCLI_SDK_DIR=${RCLI_SDK_DIR} has no CMakeLists.txt")
    endif()
    set(RCLI_SDK_ROOT "${RCLI_SDK_DIR}" CACHE INTERNAL "")
    message(STATUS "RunAnywhere SDK: local checkout at ${RCLI_SDK_DIR}")
    add_subdirectory("${RCLI_SDK_DIR}" "${CMAKE_BINARY_DIR}/runanywhere-sdks" EXCLUDE_FROM_ALL)
else()
    message(STATUS "RunAnywhere SDK: fetching ${RCLI_SDK_TAG}")
    include(FetchContent)
    FetchContent_Declare(runanywhere_sdks
        GIT_REPOSITORY https://github.com/RunanywhereAI/runanywhere-sdks.git
        GIT_TAG ${RCLI_SDK_TAG}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(runanywhere_sdks)
    set(RCLI_SDK_ROOT "${runanywhere_sdks_SOURCE_DIR}" CACHE INTERNAL "")
endif()

if(NOT TARGET rac_commons)
    message(FATAL_ERROR "the SDK did not define rac_commons — check RCLI_SDK_DIR or the tag")
endif()
