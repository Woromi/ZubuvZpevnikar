find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Pdf PdfWidgets)
find_package(KF6SyntaxHighlighting REQUIRED)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(zubuvzpevnik-ui
    main.cpp
    ImportDialog.hpp
    ImportDialog.cpp
    MainWindow.hpp
    MainWindow.cpp
    PreviewPane.hpp
    PreviewPane.cpp
    PreviewRunner.hpp
    PreviewRunner.cpp
    ConverterRunner.hpp
    ConverterRunner.cpp
    ConverterVersion.hpp
    ConverterVersion.cpp
    ShrinkDialog.hpp
    ShrinkDialog.cpp
    SongbookScan.cpp
    DiffView.hpp
    DiffView.cpp
    SongEditor.hpp
    SongEditor.cpp
    SongFilterProxy.hpp
    SongFilterProxy.cpp
    SongTableModel.hpp
    SongTableModel.cpp
    convert.hpp
)
target_link_libraries(zubuvzpevnik-ui PRIVATE Zub::core zub_warnings Qt6::Widgets Qt6::Pdf Qt6::PdfWidgets KF6::SyntaxHighlighting)

# Song syntax highlighting, found by KSyntaxHighlighting::Repository in this resource path.
qt_add_resources(zubuvzpevnik-ui syntax
    PREFIX /org.kde.syntax-highlighting/syntax-addons
    BASE syntax
    FILES syntax/zubuvzpevnik-song.xml
)

# cmake --install build/release --prefix ~/.local installs the app and its converter side by side
# (the app looks for chords_converter next to itself), so the converter also works from a shell.
include(GNUInstallDirs)
install(TARGETS zubuvzpevnik-ui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

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
            COMMAND ${ZUB_CARGO} build --release --quiet
                    --manifest-path ${converter_dir}/Cargo.toml
                    --target-dir ${converter_target_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${converter_target_dir}/release/chords_converter${CMAKE_EXECUTABLE_SUFFIX}
                    $<TARGET_FILE_DIR:zubuvzpevnik-ui>
            COMMENT "Building chords_converter with cargo"
            USES_TERMINAL
        )
        install(PROGRAMS ${converter_target_dir}/release/chords_converter${CMAKE_EXECUTABLE_SUFFIX}
                DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
