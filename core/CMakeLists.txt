# std-only library: must never link Qt (see CLAUDE.md)
configure_file(src/build_info.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp @ONLY)

add_library(zub_core STATIC
    src/latex.cpp
    src/library.cpp
    src/song.cpp
    src/textfile.cpp
    src/version.cpp
)
add_library(Zub::core ALIAS zub_core)

target_include_directories(zub_core
    PUBLIC include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(zub_core PRIVATE zub_warnings)
