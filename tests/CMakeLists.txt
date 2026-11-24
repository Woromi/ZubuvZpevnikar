# Catch2 3 from the system (Linux packages), else downloaded once at configure time (Windows).
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    message(STATUS "Catch2 3 not installed: fetching v3.7.1")
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
include(Catch)

add_executable(zub_core_tests
    test_audio.cpp
    test_bookfile.cpp
    test_booktranspose.cpp
    test_fretboard.cpp
    test_json.cpp
    test_latex.cpp
    test_library.cpp
    test_wishlist.cpp
    test_linediff.cpp
    test_midi.cpp
    test_midibend.cpp
    test_package.cpp
    test_preview.cpp
    test_safety.cpp
    test_scoremidi.cpp
    test_scorestaves.cpp
    test_scoreregion.cpp
    test_scoretiming.cpp
    test_score.cpp
    test_song.cpp
    test_songbook.cpp
    test_songcheck.cpp
    test_songedit.cpp
    test_songimport.cpp
    test_songoutline.cpp
    test_songsuggest.cpp
    test_songtidy.cpp
    test_songheader.cpp
    test_songtags.cpp
    test_songdates.cpp
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

# The app's widgets, headless. Needs the Qt application (ZUB_BUILD_APP).
if(TARGET zpevnikar_app)
    add_subdirectory(app)
endif()
