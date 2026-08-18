cmake_minimum_required(VERSION 3.24)
project(rcli VERSION 0.3.7 LANGUAGES CXX)

# Version stays at the last published release until the rework ships; the
# Homebrew formula and scripts/package.sh both read it, and moving it early
# would advertise a build users cannot install yet.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RunAnywhereSDK)

include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v7.0.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

add_executable(rcli
    src/main.cpp
    src/catalog/catalog.cpp
    src/chat/complete.cpp
    src/chat/transcript.cpp
    src/settings/settings.cpp
    src/screens/chat.cpp
    src/screens/models.cpp
    src/screens/screens.cpp
    src/sdk/install.cpp
    src/sdk/llm.cpp
    src/sdk/session.cpp
    src/theme/theme.cpp
    src/tools/shell.cpp
    src/ui/list.cpp
    src/ui/shell.cpp
)
target_include_directories(rcli PRIVATE src)
# ftxui::component already pulls dom and screen; naming all three makes the
# linker warn about duplicate archives.
target_link_libraries(rcli PRIVATE ftxui::component rac_commons)
target_compile_definitions(rcli PRIVATE RCLI_VERSION="${PROJECT_VERSION}")
foreach(_backend llamacpp onnx sherpa mlx neurt cloud)
    if(TARGET rac_backend_${_backend})
        string(TOUPPER ${_backend} _upper)
        target_compile_definitions(rcli PRIVATE RCLI_HAS_${_upper}=1)
        target_link_libraries(rcli PRIVATE rac_backend_${_backend})
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
target_compile_definitions(rcli PRIVATE RAC_HAVE_PROTOBUF=1)
if(RAC_PROTOBUF_NAMESPACE_ISOLATED)
    # The bundled runtime rewrites the google namespace; generated messages
    # compiled here must agree or the two see different types.
    target_compile_definitions(rcli PRIVATE google=runanywhere_internal)
endif()
target_include_directories(rcli PRIVATE
    "${_RAC_CORE}/src/generated/proto"
    "${_RAC_CORE}/src/generated"
    ${Protobuf_INCLUDE_DIRS}
)
if(TARGET protobuf::libprotobuf)
    target_link_libraries(rcli PRIVATE protobuf::libprotobuf)
elseif(Protobuf_LIBRARIES)
    target_link_libraries(rcli PRIVATE ${Protobuf_LIBRARIES})
endif()
if(RAC_ABSL_LIBS)
    target_link_libraries(rcli PRIVATE ${RAC_ABSL_LIBS})
endif()
if(RAC_PROTOBUF_COMPILE_OPTIONS)
    target_compile_options(rcli PRIVATE ${RAC_PROTOBUF_COMPILE_OPTIONS})
endif()

target_compile_options(rcli PRIVATE -Wall -Wextra)

# scripts/package.sh copies build/rcli into the tarball's bin/, which is what
# Formula/rcli.rb installs. Emitting the binary anywhere else silently breaks
# the release without breaking the build.
set_target_properties(rcli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Isolation probe, not shipped: reproduces an SDK call path with no UI so a
# crash can be attributed to the SDK or to the app.
add_executable(probe tools/probe.cpp src/catalog/catalog.cpp src/sdk/install.cpp src/sdk/session.cpp)
target_include_directories(probe PRIVATE src "${_RAC_CORE}/src/generated/proto" "${_RAC_CORE}/src/generated" ${Protobuf_INCLUDE_DIRS})
target_compile_definitions(probe PRIVATE RAC_HAVE_PROTOBUF=1)
if(RAC_PROTOBUF_NAMESPACE_ISOLATED)
    target_compile_definitions(probe PRIVATE google=runanywhere_internal)
endif()
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
if(TARGET rac_backend_llamacpp)
    target_link_libraries(probe PRIVATE rac_backend_llamacpp)
endif()
set_target_properties(probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
