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
    winregister.cpp
    winregister.h
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

target_link_libraries(Notepad
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Svg
)

# macOS: register the .note document type, and add Quick Look + thumbnail
# extensions so Finder shows a content preview of .note files.
if(APPLE)
    enable_language(OBJC)

    set_target_properties(Notepad PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    )

    set(NOTEPAD_SIGN_IDENTITY "Apple Development"
        CACHE STRING "codesign identity for Notepad and its extensions")

    # --- Thumbnail extension (Finder icon preview) ---
    add_executable(NotepadThumbnail MACOSX_BUNDLE
        mac/NoteParser.h mac/NoteParser.m
        mac/ThumbnailExtension/ThumbnailProvider.h
        mac/ThumbnailExtension/ThumbnailProvider.m)
    set_target_properties(NotepadThumbnail PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "appex"
        OUTPUT_NAME "NotepadThumbnail"
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/ThumbnailExtension/Info.plist)
    target_compile_options(NotepadThumbnail PRIVATE -fobjc-arc -fapplication-extension)
    target_include_directories(NotepadThumbnail PRIVATE mac)
    target_link_libraries(NotepadThumbnail PRIVATE
        "-framework Foundation" "-framework AppKit" "-framework CoreGraphics"
        "-framework QuickLookThumbnailing")
    # App extensions use the system NSExtensionMain entry point (no main()).
    target_link_options(NotepadThumbnail PRIVATE -fapplication-extension -Wl,-e,_NSExtensionMain)

    # --- Quick Look preview extension (spacebar) ---
    add_executable(NotepadPreview MACOSX_BUNDLE
        mac/NoteParser.h mac/NoteParser.m
        mac/PreviewExtension/PreviewProvider.h
        mac/PreviewExtension/PreviewProvider.m)
    set_target_properties(NotepadPreview PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "appex"
        OUTPUT_NAME "NotepadPreview"
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/PreviewExtension/Info.plist)
    target_compile_options(NotepadPreview PRIVATE -fobjc-arc -fapplication-extension)
    target_include_directories(NotepadPreview PRIVATE mac)
    target_link_libraries(NotepadPreview PRIVATE
        "-framework Foundation" "-framework AppKit" "-framework CoreGraphics"
        "-framework QuickLookUI")
    target_link_options(NotepadPreview PRIVATE -fapplication-extension -Wl,-e,_NSExtensionMain)

    add_dependencies(Notepad NotepadThumbnail NotepadPreview)

    set(_lsregister /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister)
    set(_thumb_ent ${CMAKE_CURRENT_SOURCE_DIR}/mac/ThumbnailExtension/Thumbnail.entitlements)
    set(_prev_ent ${CMAKE_CURRENT_SOURCE_DIR}/mac/PreviewExtension/Preview.entitlements)
    set(_plugins $<TARGET_BUNDLE_DIR:Notepad>/Contents/PlugIns)

    # Embed the extensions, sign nested-first, then register with Launch Services
    # and reset the Quick Look cache so Finder picks them up immediately.
    add_custom_command(TARGET Notepad POST_BUILD
        COMMENT "Embedding + signing + registering Quick Look extensions"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_plugins}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_plugins}/NotepadThumbnail.appex
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                $<TARGET_BUNDLE_DIR:NotepadThumbnail> ${_plugins}/NotepadThumbnail.appex
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_plugins}/NotepadPreview.appex
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                $<TARGET_BUNDLE_DIR:NotepadPreview> ${_plugins}/NotepadPreview.appex
        COMMAND codesign --force --options runtime --timestamp=none
                --entitlements ${_thumb_ent} --sign ${NOTEPAD_SIGN_IDENTITY}
                ${_plugins}/NotepadThumbnail.appex
        COMMAND codesign --force --options runtime --timestamp=none
                --entitlements ${_prev_ent} --sign ${NOTEPAD_SIGN_IDENTITY}
                ${_plugins}/NotepadPreview.appex
        # No hardened runtime on the app itself: it loads externally-signed Qt
        # frameworks (different Team ID), which library validation would block.
        COMMAND codesign --force --timestamp=none
                --sign ${NOTEPAD_SIGN_IDENTITY} $<TARGET_BUNDLE_DIR:Notepad>
        COMMAND ${_lsregister} -f $<TARGET_BUNDLE_DIR:Notepad>
        COMMAND qlmanage -r
        COMMAND qlmanage -r cache
        VERBATIM)
endif()

# Linux: a freedesktop thumbnailer + the MIME/desktop/thumbnailer registration
# files. The app already opens a .note from argv (double-click via the desktop
# entry). Run update-mime-database / update-desktop-database after install.
if(UNIX AND NOT APPLE)
    add_executable(note-thumbnailer linux/note-thumbnailer.cpp)
    target_link_libraries(note-thumbnailer PRIVATE Qt::Gui)
    install(TARGETS note-thumbnailer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES linux/notepad.desktop DESTINATION share/applications)
    install(FILES linux/notepad-note.xml DESTINATION share/mime/packages)
    install(FILES linux/notepad.thumbnailer DESTINATION share/thumbnailers)
endif()

# Windows: an IThumbnailProvider COM DLL. The app self-registers the .note
# association + this DLL's CLSID on launch (winregister.cpp), so it must sit
# next to Notepad.exe.
if(WIN32)
    add_library(NoteThumbnail SHARED
        windows/NoteThumbnailProvider.cpp
        windows/NoteThumbnailProvider.def)
    set_target_properties(NoteThumbnail PROPERTIES OUTPUT_NAME "NoteThumbnail" PREFIX "")
    target_link_libraries(NoteThumbnail PRIVATE shlwapi windowscodecs ole32 advapi32 user32)
    add_dependencies(Notepad NoteThumbnail)
    install(TARGETS NoteThumbnail RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS Notepad
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Qt's deploy script (used by `cmake --install`) misbehaves on Linux; there we
# package with linuxdeploy/AppImage instead. Keep it for macOS/Windows.
if(APPLE OR WIN32)
    qt_generate_deploy_app_script(
        TARGET Notepad
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()
