find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(zub_core_tests
    test_json.cpp
    test_latex.cpp
    test_library.cpp
    test_linediff.cpp
    test_preview.cpp
    test_safety.cpp
    test_song.cpp
    test_songbook.cpp
    test_songedit.cpp
    test_songimport.cpp
    test_songoutline.cpp
    test_songsuggest.cpp
    test_songtidy.cpp
    test_songtodo.cpp
    test_textfile.cpp
    test_version.cpp
)
target_link_libraries(zub_core_tests PRIVATE Zub::core zub_warnings Catch2::Catch2WithMain)
target_compile_definitions(zub_core_tests PRIVATE
    ZUB_EXPECTED_VERSION="${PROJECT_VERSION}"
    ZUB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ZUB_REAL_SONGBOOK_DIR="${PROJECT_SOURCE_DIR}/../ZubuvZpevnik"
)

catch_discover_tests(zub_core_tests DISCOVERY_MODE PRE_TEST)
