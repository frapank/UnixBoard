cmake_minimum_required(VERSION 3.16)
project(UnixBoard CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(SLINT_VERSION 1.18.0)
set(SLINT_ARCH ${CMAKE_SYSTEM_PROCESSOR})
if(SLINT_ARCH STREQUAL "aarch64")
    set(SLINT_ARCH arm64)
endif()
FetchContent_Declare(Slint
    URL https://github.com/slint-ui/slint/releases/download/v${SLINT_VERSION}/Slint-cpp-${SLINT_VERSION}-Linux-${SLINT_ARCH}.tar.gz
    URL_HASH SHA256=8bfc1342d5c99c7a4160f9d61e969fb84b96b1df3c727fa19f3e90cb00bf402f
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(Slint)
list(PREPEND CMAKE_PREFIX_PATH ${slint_SOURCE_DIR})
find_package(Slint REQUIRED)

add_executable(unixboard
    src/main.cpp
    src/proc_exec.cpp
    src/pulse_control.cpp
    src/soundboard.cpp
)
target_include_directories(unixboard PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(unixboard PRIVATE Threads::Threads Slint::Slint)

set_property(GLOBAL PROPERTY SLINT_STYLE fluent-dark)
slint_target_sources(unixboard ui/unixboard.slint)

file(GLOB UNIXBOARD_SOURCES src/*.cpp src/*.hpp)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${UNIXBOARD_SOURCES})
    # fails the build if anything is unformatted, for CI or a pre-commit hook
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run -Werror ${UNIXBOARD_SOURCES})
endif()
