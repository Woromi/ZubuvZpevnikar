find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(zubuvzpevnik-ui
    main.cpp
    MainWindow.hpp
    MainWindow.cpp
    SongEditor.hpp
    SongEditor.cpp
    SongFilterProxy.hpp
    SongFilterProxy.cpp
    SongTableModel.hpp
    SongTableModel.cpp
    convert.hpp
)
target_link_libraries(zubuvzpevnik-ui PRIVATE Zub::core zub_warnings Qt6::Widgets KF6::SyntaxHighlighting)

# Song syntax highlighting, found by KSyntaxHighlighting::Repository in this resource path.
qt_add_resources(zubuvzpevnik-ui syntax
    PREFIX /org.kde.syntax-highlighting/syntax-addons
    BASE syntax
    FILES syntax/zubuvzpevnik-song.xml
)
