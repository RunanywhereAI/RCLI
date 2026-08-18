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
    src/screens/chat.cpp
    src/screens/models.cpp
    src/screens/screens.cpp
    src/sdk/llm.cpp
    src/sdk/session.cpp
    src/theme/theme.cpp
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

target_compile_options(rcli PRIVATE -Wall -Wextra)

# scripts/package.sh copies build/rcli into the tarball's bin/, which is what
# Formula/rcli.rb installs. Emitting the binary anywhere else silently breaks
# the release without breaking the build.
set_target_properties(rcli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Isolation probe, not shipped: reproduces an SDK call path with no UI so a
# crash can be attributed to the SDK or to the app.
add_executable(probe tools/probe.cpp)
target_link_libraries(probe PRIVATE rac_commons)
if(TARGET rac_backend_llamacpp)
    target_link_libraries(probe PRIVATE rac_backend_llamacpp)
endif()
set_target_properties(probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
