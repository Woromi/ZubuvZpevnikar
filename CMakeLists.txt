cmake_minimum_required(VERSION 3.25)

project(ZubuvZpevnikUi VERSION 0.1.0 LANGUAGES CXX)

# C++23 for std::expected (GCC 13 only provides it in C++23 mode)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ZUB_BUILD_APP "Build the Qt application (core and tests build without Qt)" ON)
option(ZUB_BUILD_TESTS "Build the core unit tests" ON)

# Warnings shared by every target of this project
add_library(zub_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zub_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor)
elseif(MSVC)
    target_compile_options(zub_warnings INTERFACE /W4 /permissive- /utf-8)
endif()

add_subdirectory(core)

if(ZUB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ZUB_BUILD_APP)
    add_subdirectory(app)
endif()
