# std-only library: must never link Qt (see CLAUDE.md)
configure_file(src/build_info.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp @ONLY)

add_library(zub_core STATIC
    src/converter.cpp
    src/fretboard.cpp
    src/json.cpp
    src/latex.cpp
    src/library.cpp
    src/preview.cpp
    src/safety.cpp
    src/scoreregion.cpp
    src/score.cpp
    src/linediff.cpp
    src/songbook.cpp
    src/songedit.cpp
    src/songimport.cpp
    src/songoutline.cpp
    src/songsuggest.cpp
    src/songtidy.cpp
    src/songtags.cpp
    src/songtodo.cpp
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
