cmake_minimum_required(VERSION 3.16)
project(UnixBoard CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ftxui)

add_executable(unixboard
    src/main.cpp
    src/proc_exec.cpp
    src/pulse_control.cpp
    src/soundboard.cpp
)
target_include_directories(unixboard PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(unixboard PRIVATE Threads::Threads
    ftxui::screen ftxui::dom ftxui::component)

file(GLOB UNIXBOARD_SOURCES src/*.cpp src/*.hpp)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${UNIXBOARD_SOURCES})
    # fails the build if anything is unformatted, for CI or a pre-commit hook
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run -Werror ${UNIXBOARD_SOURCES})
endif()
