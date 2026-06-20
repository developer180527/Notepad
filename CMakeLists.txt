cmake_minimum_required(VERSION 3.19)
project(Notepad LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Svg LinguistTools)

qt_standard_project_setup()

qt_add_executable(Notepad
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    pagedocumentitem.cpp
    pagedocumentitem.h
    iconfactory.h
    canvasview.cpp
    canvasview.h
    findbar.cpp
    findbar.h
    pagesetupdialog.cpp
    pagesetupdialog.h
)

qt_add_translations(
    TARGETS Notepad
    TS_FILES Notepad_en_IN.ts
)

target_link_libraries(Notepad
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Svg
)

install(TARGETS Notepad
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Notepad
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
