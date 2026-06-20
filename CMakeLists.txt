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
    rulerwidget.cpp
    rulerwidget.h
    fontlibrary.cpp
    fontlibrary.h
)

# Embed the bundled portable fonts as :/fonts/*.ttf (AUTORCC is not enabled by
# qt_standard_project_setup, so register them explicitly).
qt_add_resources(Notepad "appfonts"
    PREFIX "/fonts"
    BASE "fonts"
    FILES
        fonts/Inter.ttf
        fonts/Lora.ttf
        fonts/JetBrainsMono.ttf
)

qt_add_translations(
    TARGETS Notepad
    TS_FILES Notepad_en_IN.ts
)

# macOS: register the .note document type so Finder can open it with Notepad.
if(APPLE)
    set_target_properties(Notepad PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    )
endif()

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
