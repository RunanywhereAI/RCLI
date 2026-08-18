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

include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v7.0.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

add_executable(rcli src/main.cpp)
target_link_libraries(rcli PRIVATE ftxui::screen ftxui::dom ftxui::component)
target_compile_definitions(rcli PRIVATE RCLI_VERSION="${PROJECT_VERSION}")
target_compile_options(rcli PRIVATE -Wall -Wextra)

# scripts/package.sh copies build/rcli into the tarball's bin/, which is what
# Formula/rcli.rb installs. Emitting the binary anywhere else silently breaks
# the release without breaking the build.
set_target_properties(rcli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
