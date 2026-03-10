cmake_minimum_required(VERSION 3.15)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

project(rcli VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    add_compile_options(-mcpu=native)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    add_compile_options(-mavx2 -mfma -mf16c)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED)
    if(LTO_SUPPORTED)
        message(STATUS "LTO enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# --- Install rpath (must be set before defining targets) ---
set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_RPATH FALSE)

# --- llama.cpp ---
set(GGML_METAL ON CACHE BOOL "Enable Metal GPU acceleration" FORCE)
set(GGML_ACCELERATE ON CACHE BOOL "Enable Apple Accelerate framework" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/llama.cpp ${CMAKE_BINARY_DIR}/llama.cpp EXCLUDE_FROM_ALL)

# --- sherpa-onnx (STT + TTS + VAD) ---
set(SHERPA_ONNX_ENABLE_C_API ON CACHE BOOL "Enable C API" FORCE)
set(SHERPA_ONNX_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx/cmake/Modules")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx/cmake")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx)
add_subdirectory(deps/sherpa-onnx ${CMAKE_BINARY_DIR}/sherpa-onnx EXCLUDE_FROM_ALL)

# --- USearch v2.16.5 (header-only HNSW vector index for RAG) ---
include(FetchContent)
FetchContent_Declare(
    usearch
    GIT_REPOSITORY https://github.com/unum-cloud/usearch.git
    GIT_TAG        v2.16.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(usearch)

# --- FTXUI v5.0.0 (Terminal UI library for interactive dashboard) ---
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v5.0.0
    GIT_SHALLOW    TRUE
)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS_SAVE ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(ftxui)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVE})

# =============================================================================
# librcli — static library: voice pipeline + RAG + macOS Actions
# =============================================================================

add_library(rcli STATIC
    # --- Voice pipeline engines ---
    src/engines/stt_engine.cpp
    src/engines/llm_engine.cpp
    src/engines/model_profile.cpp
    src/engines/tts_engine.cpp
    src/engines/vad_engine.cpp
    src/engines/metalrt_loader.cpp
    src/engines/metalrt_engine.cpp
    src/engines/metalrt_stt_engine.cpp
    src/engines/metalrt_tts_engine.cpp
    src/audio/audio_io.cpp
    src/audio/mic_permission.mm
    src/pipeline/orchestrator.cpp
    src/pipeline/sentence_detector.cpp
    src/tools/tool_engine.cpp
    src/bench/benchmark.cpp

    # --- RAG retrieval engine ---
    src/engines/embedding_engine.cpp
    src/rag/vector_index.cpp
    src/rag/bm25_index.cpp
    src/rag/hybrid_retriever.cpp
    src/rag/document_processor.cpp
    src/rag/index_builder.cpp

    # --- RCLI action system + API ---
    src/actions/action_registry.cpp
    src/actions/applescript_executor.cpp
    src/actions/register_all.cpp
    src/actions/notes_actions.cpp
    src/actions/reminders_actions.cpp
    src/actions/messages_actions.cpp
    src/actions/app_control_actions.cpp
    src/actions/window_actions.cpp
    src/actions/files_actions.cpp
    src/actions/clipboard_actions.cpp
    src/actions/system_actions.cpp
    src/actions/media_actions.cpp
    src/actions/web_actions.cpp
    src/actions/navigation_actions.cpp
    src/actions/communication_actions.cpp
    src/actions/browser_actions.cpp
    src/api/rcli_api.cpp
)

set_source_files_properties(src/audio/mic_permission.mm
    PROPERTIES LANGUAGE CXX)

target_include_directories(rcli PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/llama.cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/llama.cpp/ggml/include
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx/sherpa-onnx/c-api
    ${usearch_SOURCE_DIR}/include
)

target_link_libraries(rcli PUBLIC
    llama
    ggml
    sherpa-onnx-c-api
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework AudioUnit"
    "-framework Foundation"
    "-framework AVFoundation"
    "-framework IOKit"
)

if(GGML_METAL)
    target_link_libraries(rcli PUBLIC "-framework Metal" "-framework MetalKit")
endif()

target_compile_options(rcli PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-missing-field-initializers
)

# =============================================================================
# rcli CLI executable
# =============================================================================
add_executable(rcli_cli
    src/cli/main.cpp
)

set_target_properties(rcli_cli PROPERTIES OUTPUT_NAME "rcli")

target_link_libraries(rcli_cli PRIVATE rcli ftxui::screen ftxui::dom ftxui::component)

target_compile_options(rcli_cli PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

target_compile_definitions(rcli_cli PRIVATE
    RCLI_VERSION="${PROJECT_VERSION}"
)

# =============================================================================
# rcli_test — test executable
# =============================================================================
add_executable(rcli_test
    src/test/test_pipeline.cpp
)

set_target_properties(rcli_test PROPERTIES OUTPUT_NAME "rcli_test")

target_link_libraries(rcli_test PRIVATE rcli)

target_compile_options(rcli_test PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# =============================================================================
# Install targets (for Homebrew / distribution)
# =============================================================================
include(GNUInstallDirs)

install(TARGETS rcli_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/api/rcli_api.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rcli)

# Install shared libraries that rcli depends on (for CPack)
install(DIRECTORY ${CMAKE_BINARY_DIR}/bin/ DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILES_MATCHING PATTERN "*.dylib")
install(DIRECTORY ${CMAKE_BINARY_DIR}/lib/ DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILES_MATCHING PATTERN "*.dylib")

# =============================================================================
# CPack (produces rcli-<version>-Darwin.tar.gz)
# =============================================================================
set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "rcli")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "On-device voice AI for macOS")
set(CPACK_PACKAGE_VENDOR "RunanywhereAI")
include(CPack)

# Strip in release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(rcli_cli PRIVATE -Wl,-dead_strip)
    target_link_options(rcli_test PRIVATE -Wl,-dead_strip)
endif()
