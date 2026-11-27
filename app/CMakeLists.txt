find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg Pdf PdfWidgets Multimedia LinguistTools)
find_package(KF6SyntaxHighlighting REQUIRED)
qt_standard_project_setup(REQUIRES 6.5)

# Everything but main() is a static library, so the app tests (tests/app) link the same code.
qt_add_library(zpevnikar_app STATIC
    ActivityBar.hpp
    FindBar.hpp
    FindBar.cpp
    FindMatches.hpp
    FindMatches.cpp
    ActivityBar.cpp
    BuildFiles.hpp
    DesktopIntegration.hpp
    DesktopIntegration.cpp
    BuildFiles.cpp
    ExternalTools.hpp
    ExternalTools.cpp
    ProcessTree.hpp
    ProcessTree.cpp
    SettingsDialog.hpp
    SettingsDialog.cpp
    SongListPage.hpp
    PageHeader.cpp
    SongListPage.cpp
    ImportDialog.hpp
    ImportDialog.cpp
    MainWindow.hpp
    EditorFont.hpp
    EditorFont.cpp
    KeyTips.hpp
    KeyTips.cpp
    ShortcutsDialog.hpp
    ShortcutsDialog.cpp
    MainWindow.cpp
    ScoreTabs.hpp
    ScoreTabs.cpp
    SongActions.hpp
    SongActions.cpp
    SongDatesRunner.hpp
    SongDatesRunner.cpp
    SongbookCheck.hpp
    SongbookCheck.cpp
    PreviewPane.hpp
    PreviewPane.cpp
    PreviewRunner.hpp
    PreviewRunner.cpp
    FretboardWidget.hpp
    FretboardWidget.cpp
    ScoreInputBar.hpp
    ScoreInputBar.cpp
    NoteSounds.hpp
    NoteSounds.cpp
    WavFile.hpp
    WavFile.cpp
    PlaybackRunner.hpp
    PlaybackRunner.cpp
    ScorePlayer.hpp
    ScorePlayer.cpp
    SoundTools.hpp
    SoundTools.cpp
    ScorePreviewPane.hpp
    ScorePreviewPane.cpp
    ScoreRunner.hpp
    ScoreRunner.cpp
    ConverterRunner.hpp
    ConverterRunner.cpp
    ConverterVersion.hpp
    ConverterVersion.cpp
    ShrinkDialog.hpp
    ShrinkDialog.cpp
    SongbookScan.cpp
    SongbookSorter.hpp
    SongbookSorter.cpp
    WelcomePage.cpp
    HomeData.hpp
    BookAdder.hpp
    BookAdder.cpp
    AddToBooksDialog.hpp
    AddToBooksDialog.cpp
    HomeData.cpp
    HomePage.hpp
    HomePage.cpp
    LibraryDialog.hpp
    LibraryDialog.cpp
    SongbookExporter.hpp
    BookPreview.cpp
    BookOverview.hpp
    BookOverview.cpp
    PdfLinkView.hpp
    PdfLinkView.cpp
    SongbookExporter.cpp
    SongbookTree.hpp
    SongbookTree.cpp
    SongbooksPage.hpp
    SongbooksPage.cpp
    SongPickerModel.hpp
    SongPickerModel.cpp
    LabelFilterBox.hpp
    LabelFilterBox.cpp
    LabelsDialog.hpp
    LabelsDialog.cpp
    NewSongDialog.hpp
    NewSongDialog.cpp
    DiffView.hpp
    DiffView.cpp
    CodeTextEdit.hpp
    CodeTextEdit.cpp
    CollectionTabs.cpp
    SongEditor.hpp
    SongEditor.cpp
    SongFilterProxy.hpp
    SongFilterProxy.cpp
    SongTableModel.hpp
    SongTableModel.cpp
    convert.hpp
)
target_include_directories(zpevnikar_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(zpevnikar_app
    PUBLIC Zub::core Qt6::Widgets Qt6::Svg Qt6::Pdf Qt6::PdfWidgets Qt6::Multimedia KF6::SyntaxHighlighting
    PRIVATE zub_warnings)

qt_add_executable(zpevnikar main.cpp)
target_link_libraries(zpevnikar PRIVATE zpevnikar_app Zub::core zub_warnings Qt6::Widgets)
if(WIN32)
    # Its icon, and no console window next to it.
    target_sources(zpevnikar PRIVATE zpevnikar.rc)
    set_target_properties(zpevnikar PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# The LaTeX package that builds the songs (external/zubuvzpevnik-package, a git submodule), for
# songbooks without one of their own: found in the source tree by builds that run from here, and
# installed into share/zpevnikar/zubuvzpevnik (MainWindow::bundledPackage). Skipped (with a message)
# without the submodule checked out.
set(zub_package_dir ${PROJECT_SOURCE_DIR}/external/zubuvzpevnik-package)
if(EXISTS ${zub_package_dir}/zubuvzpevnik.sty)
    # Only for programs run from this build dir (MainWindow::bundledPackage): an installed copy
    # never looks for the build machine's source tree.
    target_compile_definitions(zpevnikar_app PRIVATE
        ZUB_BUNDLED_PACKAGE_DIR="${zub_package_dir}" ZUB_BUILD_DIR="${CMAKE_BINARY_DIR}")
else()
    message(STATUS "zubuvzpevnik-package: submodule not checked out "
                   "(git submodule update --init), the app gets no copy of the package")
endif()

# UI translations (source language English). zpevnikar_cs.ts is the Czech translation,
# zpevnikar_en.ts only holds the plural forms of the English texts ("%n song(s)"). The .qm files
# are compiled by the normal build and embedded under ":/i18n". Target update_translations runs
# lupdate over the sources and rewrites the .ts files.
qt_add_translations(zpevnikar_app
    TS_FILES i18n/zpevnikar_cs.ts
    PLURALS_TS_FILE i18n/zpevnikar_en.ts
    LUPDATE_OPTIONS -no-obsolete -locations none
    RESOURCE_PREFIX /i18n
)

# Song syntax highlighting, found by KSyntaxHighlighting::Repository in this resource path.
qt_add_resources(zpevnikar_app syntax
    PREFIX /org.kde.syntax-highlighting/syntax-addons
    BASE syntax
    FILES syntax/zubuvzpevnik-song.xml
)

# The app icon (from a Gemini-generated image), embedded for the window icon and installed into
# the hicolor theme for the launcher entry below.
set(zub_icon_sizes 16 24 32 48 64 128 256 512)
set(zub_icon_files)
foreach(size IN LISTS zub_icon_sizes)
    list(APPEND zub_icon_files icons/zpevnikar-${size}.png)
endforeach()
qt_add_resources(zpevnikar icons
    PREFIX /icons
    BASE icons
    FILES ${zub_icon_files}
)

# cmake --install build/release --prefix ~/.local installs the app and its converter side by side
# (the app looks for chords_converter next to itself), so the converter also works from a shell.
include(GNUInstallDirs)
install(TARGETS zpevnikar RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES zpevnikar.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
if(EXISTS ${zub_package_dir}/zubuvzpevnik.sty)
    # The TeX side and scripts/ (build.sh, songidx.lua, checkpaper.py for the converter), not
    # the docs or git files; the scripts stay executable. (Configure again after fetching the
    # submodule: the EXISTS test runs at configure time.)
    install(DIRECTORY ${zub_package_dir}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/zpevnikar/zubuvzpevnik
            USE_SOURCE_PERMISSIONS
            PATTERN ".git*" EXCLUDE
            PATTERN "docs" EXCLUDE
            PATTERN "__pycache__" EXCLUDE)
endif()
foreach(size IN LISTS zub_icon_sizes)
    install(FILES icons/zpevnikar-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME zpevnikar.png)
endforeach()

# chords_converter from the git submodule, built with cargo and copied next to the app, where
# MainWindow::findConverter() looks for it. Skipped (with a message) without cargo or without
# the submodule checked out (git submodule update --init).
if(ZUB_BUILD_CONVERTER)
    set(converter_dir ${PROJECT_SOURCE_DIR}/external/chords_converter)
    find_program(ZUB_CARGO cargo HINTS $ENV{HOME}/.cargo/bin)
    if(NOT ZUB_CARGO)
        message(STATUS "chords_converter: cargo not found, not building it")
    elseif(NOT EXISTS ${converter_dir}/Cargo.toml)
        message(STATUS "chords_converter: submodule not checked out "
                       "(git submodule update --init), not building it")
    else()
        set(converter_target_dir ${CMAKE_BINARY_DIR}/chords_converter)
        # Always runs: cargo decides what is out of date (a no-op build takes well under a second).
        add_custom_target(chords_converter ALL
            COMMAND ${ZUB_CARGO} build --release --quiet --locked
                    --manifest-path ${converter_dir}/Cargo.toml
                    --target-dir ${converter_target_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${converter_target_dir}/release/chords_converter${CMAKE_EXECUTABLE_SUFFIX}
                    $<TARGET_FILE_DIR:zpevnikar>
            COMMENT "Building chords_converter with cargo"
            USES_TERMINAL
        )
        install(PROGRAMS ${converter_target_dir}/release/chords_converter${CMAKE_EXECUTABLE_SUFFIX}
                DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
