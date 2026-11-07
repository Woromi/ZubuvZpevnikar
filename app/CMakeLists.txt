find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(zubuvzpevnik-ui
    main.cpp
    MainWindow.hpp
    MainWindow.cpp
    SongFilterProxy.hpp
    SongFilterProxy.cpp
    SongTableModel.hpp
    SongTableModel.cpp
    convert.hpp
)
target_link_libraries(zubuvzpevnik-ui PRIVATE Zub::core zub_warnings Qt6::Widgets)
