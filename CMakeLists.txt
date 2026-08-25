cmake_minimum_required(VERSION 3.24)
project(rcli VERSION 0.5.0 LANGUAGES C CXX)

# Product version is independent of the SDK kit pin in cmake/sdk-pin.cmake.
# scripts/package-rcli.sh and the release workflow stamp this into Formula/rcli.rb.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RunAnywhereSDK)

# In-tree rcli sources (copied from runanywhere-sdks/rcli). Command files are
# thin: parse → bootstrap() → one rac_* call → render.
set(RCLI_SOURCES
    src/app.cpp
    src/bootstrap.cpp
    src/catalog/catalog.cpp
    src/catalog/model_ref.cpp
    src/commands/cmd_version.cpp
    src/commands/cmd_info.cpp
    src/commands/cmd_auth.cpp
    src/commands/cmd_backends.cpp
    src/commands/cmd_list.cpp
    src/commands/cmd_lora.cpp
    src/commands/cmd_models.cpp
    src/commands/cmd_pull.cpp
    src/commands/cmd_rerank.cpp
    src/commands/cmd_rm.cpp
    src/commands/cmd_run.cpp
    src/commands/cmd_tool.cpp
    src/commands/cmd_image.cpp
    src/commands/cmd_segment.cpp
    src/commands/cmd_serve.cpp
    src/commands/cmd_show.cpp
    src/commands/cmd_stt.cpp
    src/commands/cmd_diarize.cpp
    src/commands/cmd_embed.cpp
    src/commands/cmd_telemetry.cpp
    src/commands/cmd_tts.cpp
    src/commands/cmd_vad.cpp
    src/commands/cmd_voice.cpp
    src/commands/cmd_rag.cpp
    src/commands/cmd_bench.cpp
    src/commands/engine_options.cpp
    src/commands/model_setup.cpp
    src/config/cli_paths.cpp
    src/device_info.cpp
    src/net/control_plane.cpp
    src/io/wav_io.cpp
    src/io/image_io.cpp
    src/io/output.cpp
    src/progress/progress_bar.cpp
    src/repl/repl.cpp
    src/util/term.cpp
)

add_library(rcli_core STATIC ${RCLI_SOURCES})

if(WIN32)
    target_compile_definitions(rcli_core PUBLIC RCLI_NO_LINENOISE=1)
    if(MSVC)
        target_compile_options(rcli_core PUBLIC
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/windows_proto_compat.h")
    else()
        target_compile_options(rcli_core PUBLIC
            -include "${CMAKE_CURRENT_SOURCE_DIR}/src/windows_proto_compat.h")
    endif()
else()
    target_sources(rcli_core PRIVATE third_party/linenoise/linenoise.c)
    set_source_files_properties(third_party/linenoise/linenoise.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_include_directories(rcli_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CLI11"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linenoise"
)

find_package(Threads REQUIRED)
target_link_libraries(rcli_core PUBLIC rac_commons Threads::Threads)
target_compile_definitions(rcli_core PUBLIC
    RCLI_VERSION="${PROJECT_VERSION}"
    RCLI_PINNED_SDK_VERSION="${RCLI_PINNED_SDK_VERSION}"
    RAC_HAVE_PROTOBUF=1
    CLI11_HAS_CODECVT=0)
rcli_define_engine_macros(rcli_core)
if(RunAnywhere_HAS_RAG)
    target_compile_definitions(rcli_core PUBLIC RAC_HAVE_RAG=1)
endif()
if(RunAnywhere_HAS_SERVER)
    target_compile_definitions(rcli_core PUBLIC RCLI_HAS_SERVER=1)
endif()

# Proto types are the SOT. The kit ships the *.pb.h that commons was compiled
# with (same protoc, same isolated runtime). RCLI never runs protoc and never
# compiles *.pb.cc — those objects already live in librac_commons.a.
if(NOT RunAnywhere_PROTO_INCLUDE_DIR
   OR NOT EXISTS "${RunAnywhere_PROTO_INCLUDE_DIR}/model_types.pb.h"
   OR NOT EXISTS "${RunAnywhere_PROTO_INCLUDE_DIR}/schema_lock.h")
    message(FATAL_ERROR
        "RunAnywhere kit is missing generated proto headers "
        "(RunAnywhere_PROTO_INCLUDE_DIR=${RunAnywhere_PROTO_INCLUDE_DIR}). "
        "Rebuild the kit with package-cpp-desktop.")
endif()
# Include path + google=runanywhere_internal come from RunAnywhere::commons.

if(APPLE)
    target_link_libraries(rcli_core PUBLIC "-framework IOKit" "-framework CoreFoundation")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(rcli_core PRIVATE -Wall -Wextra)
endif()

add_executable(rcli src/main.cpp)
target_link_libraries(rcli PRIVATE rcli_core)

if(MINGW)
    target_link_options(rcli PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

if(APPLE)
    # Shipped `rcli` is scripts/build-mlx.sh + Swift MLX host. This binary is
    # rcli-cxx so cmake --build cannot clobber the fat Apple binary.
    set_target_properties(rcli PROPERTIES OUTPUT_NAME "rcli-cxx")
endif()

set_target_properties(rcli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(APPLE)
    set_target_properties(rcli PROPERTIES
        BUILD_RPATH "${RunAnywhere_THIRD_PARTY_DIR}"
        INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
    set_target_properties(rcli PROPERTIES
        BUILD_RPATH "${RunAnywhere_THIRD_PARTY_DIR}"
        INSTALL_RPATH "\$ORIGIN/../lib")
endif()
rcli_stage_windows_runtime_dlls(rcli)

install(TARGETS rcli RUNTIME DESTINATION bin)

# Apple product binary is the Swift MLX host (`build/rcli`), not `rcli-cxx`.
# Users still type `rcli`; llama.cpp / ONNX / Sherpa register from C++ bootstrap
# and MLX from the Swift entry. Turn off only for a C++-only compile loop.
option(RCLI_APPLE_MLX_HOST
    "On Apple, link the Swift MLX host as build/rcli (the product CLI)" ON)
if(APPLE AND RCLI_APPLE_MLX_HOST)
    if(NOT RCLI_SDK_ROOT)
        message(FATAL_ERROR "RCLI_SDK_ROOT is empty; cannot build the MLX host")
    endif()
    add_custom_target(rcli-mlx ALL
        COMMAND "${CMAKE_COMMAND}" -E env
            "RCLI_SDK_KIT=${RCLI_SDK_ROOT}"
            bash "${CMAKE_SOURCE_DIR}/scripts/build-mlx.sh" "${CMAKE_BINARY_DIR}"
        DEPENDS rcli
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Swift MLX host → ${CMAKE_BINARY_DIR}/rcli"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
