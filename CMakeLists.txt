cmake_minimum_required(VERSION 3.24)
project(rcli VERSION 0.4.0 LANGUAGES C CXX)

# 0.3.7 was the last MetalRT release and its tag already carries that tarball,
# so the rework starts at 0.4.0 rather than reusing it. scripts/package.sh reads
# this to name the tarball, and the release workflow stamps the same number into
# Formula/rcli.rb.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RunAnywhereSDK)

# CLI11 and linenoise are vendored rather than fetched: they are the same two
# the SDK's own rcli vendors, both are a single translation unit, and a release
# build should not depend on the network reaching two more repositories.
# Everything except the entry point, so there can be more than one entry point.
# The Apple build adds a Swift one that registers the MLX callbacks first; both
# call rcli_run(), so the app itself exists once.
add_library(rcli_core STATIC
    src/audio/audio.cpp
    src/catalog/catalog.cpp
    src/chat/complete.cpp
    src/cli/app.cpp
    src/cli/cmd_bench.cpp
    src/cli/cmd_chat.cpp
    src/cli/cmd_engines.cpp
    src/cli/cmd_image.cpp
    src/cli/cmd_models.cpp
    src/cli/cmd_speech.cpp
    src/cli/output.cpp
    src/cli/preview.cpp
    src/media/png.cpp
    src/repl/repl.cpp
    src/repl/session.cpp
    src/sdk/download.cpp
    src/sdk/imagine.cpp
    src/sdk/install.cpp
    src/sdk/llm.cpp
    src/sdk/session.cpp
    src/sdk/speech.cpp
    src/settings/settings.cpp
    src/tools/shell.cpp
)

# linenoise drives the terminal through termios, which Windows does not have.
# The console does its own line editing there, and repl.cpp falls back to
# getline, so the commands are the same and only completion is lost.
if(WIN32)
    target_compile_definitions(rcli_core PUBLIC RCLI_NO_LINENOISE=1)
else()
    target_sources(rcli_core PRIVATE third_party/linenoise/linenoise.c)
endif()
target_include_directories(rcli_core PUBLIC
    src
    third_party/CLI11
    third_party/linenoise
)
# zlib for PNG encode and decode; it is already a transitive dependency of the
# SDK, naming it here is what makes that explicit rather than accidental.
find_package(ZLIB REQUIRED)
target_link_libraries(rcli_core PRIVATE rac_commons ZLIB::ZLIB)
target_compile_definitions(rcli_core PRIVATE RCLI_VERSION="${PROJECT_VERSION}")
foreach(_backend llamacpp onnx sherpa mlx neurt cloud)
    if(TARGET rac_backend_${_backend})
        string(TOUPPER ${_backend} _upper)
        target_compile_definitions(rcli_core PRIVATE RCLI_HAS_${_upper}=1)
        target_link_libraries(rcli_core PRIVATE rac_backend_${_backend})
    endif()
endforeach()


# Proto-byte ABI access. Downloads, the model registry and the lifecycle are
# protobuf-only in commons — there is no plain-C equivalent — so a consumer that
# wants them has to re-attach what rac_commons keeps PRIVATE. This mirrors the
# SDK's own rcli target; the difference is RCLI_SDK_ROOT, because the SDK is not
# the top-level project here.
set(_RAC_CORE "${RCLI_SDK_ROOT}/core")
if(NOT EXISTS "${_RAC_CORE}/src/generated/proto/model_types.pb.cc")
    message(FATAL_ERROR
        "the SDK's generated protos are missing — configure the SDK once so it "
        "generates them, or check RCLI_SDK_DIR")
endif()
target_compile_definitions(rcli_core PRIVATE RAC_HAVE_PROTOBUF=1)
if(RAC_PROTOBUF_NAMESPACE_ISOLATED)
    # The bundled runtime rewrites the google namespace; generated messages
    # compiled here must agree or the two see different types.
    target_compile_definitions(rcli_core PRIVATE google=runanywhere_internal)
endif()
target_include_directories(rcli_core PRIVATE
    "${_RAC_CORE}/src/generated/proto"
    "${_RAC_CORE}/src/generated"
    ${Protobuf_INCLUDE_DIRS}
)
if(TARGET protobuf::libprotobuf)
    target_link_libraries(rcli_core PRIVATE protobuf::libprotobuf)
elseif(Protobuf_LIBRARIES)
    target_link_libraries(rcli_core PRIVATE ${Protobuf_LIBRARIES})
endif()
if(RAC_ABSL_LIBS)
    target_link_libraries(rcli_core PRIVATE ${RAC_ABSL_LIBS})
endif()
if(RAC_PROTOBUF_COMPILE_OPTIONS)
    target_compile_options(rcli_core PRIVATE ${RAC_PROTOBUF_COMPILE_OPTIONS})
endif()

if(APPLE)
    # AudioQueue, for microphone capture and playback.
    target_link_libraries(rcli_core PRIVATE "-framework AudioToolbox")
endif()

target_compile_options(rcli_core PRIVATE -Wall -Wextra)

add_executable(rcli src/main.cpp)
target_link_libraries(rcli PRIVATE rcli_core)

if(APPLE)
    # On Apple the shipped `rcli` is the one scripts/build-mlx.sh produces: MLX
    # inference is Swift, so the binary that can use every engine has to be
    # linked by xcodebuild. This one is an intermediate — it exists so CMake
    # writes the link line that build-mlx.sh harvests, and so there is a way to
    # build without Xcode. Naming it apart is what stops a later
    # `cmake --build` from quietly replacing the full binary with this one.
    set_target_properties(rcli PROPERTIES OUTPUT_NAME "rcli-cxx")
endif()

# scripts/package.sh copies build/rcli into the tarball's libexec/, which is
# what Formula/rcli.rb installs before symlinking bin/rcli at it. Emitting the
# binary anywhere else silently breaks the release without breaking the build.
set_target_properties(rcli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Isolation probe, not shipped: reproduces an SDK call path with no UI so a
# crash can be attributed to the SDK or to the app.
add_executable(probe tools/probe.cpp src/audio/audio.cpp src/catalog/catalog.cpp
    src/sdk/download.cpp src/sdk/install.cpp src/sdk/llm.cpp src/sdk/session.cpp
    src/sdk/speech.cpp src/settings/settings.cpp)
target_include_directories(probe PRIVATE src "${_RAC_CORE}/src/generated/proto" "${_RAC_CORE}/src/generated" ${Protobuf_INCLUDE_DIRS})
target_compile_definitions(probe PRIVATE RAC_HAVE_PROTOBUF=1)
if(RAC_PROTOBUF_NAMESPACE_ISOLATED)
    target_compile_definitions(probe PRIVATE google=runanywhere_internal)
endif()
# ftxui::screen only: the settings registry carries the theme setting, whose
# value type is an ftxui colour mode.
target_link_libraries(probe PRIVATE rac_commons)
if(TARGET protobuf::libprotobuf)
    target_link_libraries(probe PRIVATE protobuf::libprotobuf)
endif()
if(RAC_ABSL_LIBS)
    target_link_libraries(probe PRIVATE ${RAC_ABSL_LIBS})
endif()
foreach(_b llamacpp onnx sherpa mlx neurt cloud)
    if(TARGET rac_backend_${_b})
        string(TOUPPER ${_b} _u)
        target_compile_definitions(probe PRIVATE RCLI_HAS_${_u}=1)
        target_link_libraries(probe PRIVATE rac_backend_${_b})
    endif()
endforeach()
if(APPLE)
    target_link_libraries(probe PRIVATE "-framework AudioToolbox")
endif()
set_target_properties(probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
