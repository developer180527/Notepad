# dmgbuild settings for the Notepad installer DMG.
#
#   dmgbuild -s mac/dmg/settings.py -D app=build/Notepad.app \
#            -D background=mac/dmg/background.tiff "Notepad" Notepad-macos.dmg
#
# dmgbuild writes the Finder layout (.DS_Store) directly rather than scripting
# Finder over AppleScript, so it behaves the same on a headless CI runner as on
# a desktop. Icon positions are in points and must agree with the art in
# mac/dmg/background.cpp.
import os.path

application = defines.get("app", "build/Notepad.app")  # noqa: F821 (injected by dmgbuild)
appname = os.path.basename(application)

format = "ULFO"          # LZFSE: noticeably smaller than zlib, and every supported macOS reads it
filesystem = "HFS+"
size = None              # let dmgbuild size the image to its contents

files = [application]
symlinks = {"Applications": "/Applications"}
hide_extension = [appname]          # "Notepad", not "Notepad.app"

icon = defines.get("icon", "assets/notepad_icon.icns")  # noqa: F821 — the mounted volume's icon
background = defines.get("background", "mac/dmg/background.tiff")  # noqa: F821

# Window: no toolbar, sidebar, path or status bar — just the art and two icons.
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
sidebar_width = 0
window_rect = ((200, 140), (660, 400))

default_view = "icon-view"
show_icon_preview = False
include_icon_view_settings = True
include_list_view_settings = False

arrange_by = None
grid_offset = (0, 0)
grid_spacing = 100
scroll_position = (0, 0)
label_pos = "bottom"
text_size = 13
icon_size = 112

icon_locations = {
    appname: (180, 210),
    "Applications": (480, 210),
}
