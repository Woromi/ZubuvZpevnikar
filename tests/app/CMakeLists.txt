# App tests: the Qt widgets driven headless (offscreen platform) through the same library the app
# executable links (zpevnikar_app). Internals are reached through `struct AppTestAccess`
# (AppTestAccess.hpp), a friend of the classes tested, never by redefining `private`.
# Cases tagged [.tools] need lualatex, lilypond, python3, fluidsynth or the converter and are
# hidden: build/debug/tests/app/zub_app_tests "[.tools]"
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Test)

add_executable(zub_app_tests
    main.cpp
    Support.cpp
    test_resources.cpp
    test_codetextedit.cpp
    test_find_replace.cpp
    test_songbooks_page.cpp
    test_collections.cpp
    test_song_dates.cpp
    test_new_song.cpp
    test_main_window.cpp
    test_song_editor.cpp
    test_score_tab.cpp
    test_tools_score.cpp
    test_tools.cpp
    test_welcome.cpp
)
target_include_directories(zub_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(zub_app_tests PRIVATE zpevnikar_app zub_warnings Qt6::Widgets Qt6::Test
                      Catch2::Catch2)
target_compile_definitions(zub_app_tests PRIVATE
    ZUB_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    ZUB_APP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ZUB_CONVERTER_BUILT="$<TARGET_FILE_DIR:zpevnikar>/chords_converter"
)

catch_discover_tests(zub_app_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180
)
