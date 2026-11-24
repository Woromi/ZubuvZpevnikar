cmake_minimum_required(VERSION 3.25)

project(ZubuvZpevnikar VERSION 0.1.0 LANGUAGES CXX)

# C++23 for std::expected (GCC 13 only provides it in C++23 mode)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ZUB_BUILD_APP "Build the Qt application (core and tests build without Qt)" ON)
option(ZUB_BUILD_TESTS "Build the core unit tests" ON)
option(ZUB_BUILD_CONVERTER "Build chords_converter (external/chords_converter) with cargo, next to the app" ON)
option(ZUB_FETCH_SUBMODULES "Check out empty submodules (external/*) at configure time" ON)

# A fresh clone has the submodule folders empty: check out the commits this repo pins (never a
# newer one; bumping a pin stays a deliberate commit: git submodule update --remote). Needs git
# and, the first time, the network; skipped without either, the build then says what is missing.
if(ZUB_FETCH_SUBMODULES AND EXISTS ${PROJECT_SOURCE_DIR}/.git)
    find_package(Git QUIET)
    foreach(submodule IN ITEMS external/chords_converter external/zubuvzpevnik-package)
        file(GLOB contents ${PROJECT_SOURCE_DIR}/${submodule}/*)
        if(GIT_FOUND AND NOT contents)
            message(STATUS "${submodule}: checking out the pinned commit")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} submodule update --init -- ${submodule}
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                RESULT_VARIABLE fetched)
            if(NOT fetched EQUAL 0)
                message(WARNING "${submodule}: git submodule update --init failed (offline?)")
            endif()
        endif()
    endforeach()
endif()

# Warnings shared by every target of this project
add_library(zub_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zub_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor)
elseif(MSVC)
    # /Zc:preprocessor: the standard preprocessor (the old one breaks raw strings in macro
    # arguments, e.g. CHECK(parse(R"("\u0041")))).
    target_compile_options(zub_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:preprocessor)
endif()

add_subdirectory(core)

if(ZUB_BUILD_TESTS)
    enable_testing()
endif()

# The app before the tests: tests/app links its library (zpevnikar_app).
if(ZUB_BUILD_APP)
    add_subdirectory(app)
endif()

if(ZUB_BUILD_TESTS)
    add_subdirectory(tests)
endif()
