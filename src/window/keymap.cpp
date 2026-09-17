#include "window/keymap.h"

namespace Keymap {

Platform current()
{
#if defined(Q_OS_MACOS)
    return Platform::Mac;
#elif defined(Q_OS_WIN)
    return Platform::Windows;
#else
    return Platform::Linux;
#endif
}

Table table(Platform p)
{
    const bool mac = p == Platform::Mac;
    const bool win = p == Platform::Windows;

    Table t;
    auto add = [&t](const char *action, QStringList keys) {
        t.append({QString::fromLatin1(action), std::move(keys)});
    };

    // --- File -------------------------------------------------------------
    // ⌘N is a new *window* and ⌘T a new tab, as in every tabbed Mac app and in
    // browsers and editors on Windows and Linux.
    add("actionNewWindow", {"Ctrl+N"});
    add("actionNew", {"Ctrl+T"});                       // new tab
    add("actionOpen", {"Ctrl+O"});
    add("actionSave", {"Ctrl+S"});
    add("actionSaveAs", {"Ctrl+Shift+S"});
    add("actionReopenClosedTab", {"Ctrl+Shift+T"});
    add("actionCloseTab", win ? QStringList{"Ctrl+W", "Ctrl+F4"} : QStringList{"Ctrl+W"});
    add("actionCloseWindow", {"Ctrl+Shift+W"});
    add("actionPrint", {"Ctrl+P"});
    add("actionConvert", {"Ctrl+Shift+E"});
    // ⌥⌘R is "Reveal in Finder"; Shift+Alt+R the Windows/Linux equivalent.
    add("actionShowInFolder", mac ? QStringList{"Ctrl+Alt+R"} : QStringList{"Shift+Alt+R"});
    add("actionSettings", {"Ctrl+,"});
    // Windows has no quit shortcut by convention (Alt+F4 closes the window).
    if (!win)
        add("actionQuit", {"Ctrl+Q"});

    // --- Edit -------------------------------------------------------------
    add("actionUndo", win ? QStringList{"Ctrl+Z", "Alt+Backspace"} : QStringList{"Ctrl+Z"});
    // Ctrl+Y is the Windows redo, and common enough on Linux (LibreOffice) to keep.
    add("actionRedo", mac ? QStringList{"Ctrl+Shift+Z"} : QStringList{"Ctrl+Y", "Ctrl+Shift+Z"});
    // Shift+Del / Ctrl+Ins / Shift+Ins are the old CUA clipboard keys, still
    // honoured on Windows and KDE.
    add("actionCut", mac ? QStringList{"Ctrl+X"} : QStringList{"Ctrl+X", "Shift+Del"});
    add("actionCopy", mac ? QStringList{"Ctrl+C"} : QStringList{"Ctrl+C", "Ctrl+Ins"});
    add("actionPaste", mac ? QStringList{"Ctrl+V"} : QStringList{"Ctrl+V", "Shift+Ins"});
    add("actionPasteMatchStyle",
        mac ? QStringList{"Ctrl+Alt+Shift+V"} : QStringList{"Ctrl+Shift+V"});
    add("actionSelectAll", {"Ctrl+A"});
    add("actionFind", {"Ctrl+F"});
    add("actionFindNext", mac ? QStringList{"Ctrl+G"} : QStringList{"F3", "Ctrl+G"});
    add("actionFindPrevious",
        mac ? QStringList{"Ctrl+Shift+G"} : QStringList{"Shift+F3", "Ctrl+Shift+G"});
    // ⌘H hides the app on a Mac, so replace lives on ⌥⌘F there.
    add("actionReplace", mac ? QStringList{"Ctrl+Alt+F"} : QStringList{"Ctrl+H"});

    // --- Format -----------------------------------------------------------
    add("actionBold", {"Ctrl+B"});
    add("actionItalic", {"Ctrl+I"});
    add("actionUnderline", {"Ctrl+U"});
    add("actionAlignLeft", {"Ctrl+L"});
    add("actionAlignCenter", {"Ctrl+E"});
    add("actionAlignRight", {"Ctrl+R"});
    add("actionAlignJustify", {"Ctrl+J"});

    // Font size and zoom genuinely differ by platform. On a Mac, TextEdit and
    // Pages use ⌘+ / ⌘− for Bigger/Smaller text and ⌘> / ⌘< for zoom. On Windows
    // and Linux, Notepad and gedit use Ctrl+ / Ctrl− for zoom, while Word and
    // LibreOffice resize text with Ctrl+Shift+> / < and Ctrl+] / [.
    if (mac) {
        add("actionIncreaseFontSize", {"Ctrl+=", "Ctrl++"});
        add("actionDecreaseFontSize", {"Ctrl+-"});
        add("actionZoomIn", {"Ctrl+Shift+.", "Ctrl+>"});
        add("actionZoomOut", {"Ctrl+Shift+,", "Ctrl+<"});
    } else {
        add("actionIncreaseFontSize", {"Ctrl+Shift+.", "Ctrl+>", "Ctrl+]"});
        add("actionDecreaseFontSize", {"Ctrl+Shift+,", "Ctrl+<", "Ctrl+["});
        add("actionZoomIn", {"Ctrl+=", "Ctrl++"});
        add("actionZoomOut", {"Ctrl+-"});
    }
    add("actionResetZoom", {"Ctrl+0"});
    add("actionMarkdownSource", {"Ctrl+Shift+M"});

    // --- Window and tabs --------------------------------------------------
    if (mac) {
        add("actionMinimize", {"Ctrl+M"});
        add("actionFullScreen", {"Meta+Ctrl+F"});   // ⌃⌘F
        // ⌥⌘→ and ⇧⌘] are the Safari/Finder/Xcode tab keys; ⌃Tab (physical
        // Control) is what Chrome and VS Code users reach for.
        add("actionNextTab", {"Ctrl+Alt+Right", "Ctrl+Shift+]", "Ctrl+}", "Meta+Tab"});
        add("actionPrevTab",
            {"Ctrl+Alt+Left", "Ctrl+Shift+[", "Ctrl+{", "Meta+Shift+Tab", "Meta+Shift+Backtab"});
    } else {
        add("actionFullScreen", {"F11"});
        add("actionNextTab", {"Ctrl+Tab", "Ctrl+PgDown"});
        add("actionPrevTab", {"Ctrl+Shift+Tab", "Ctrl+Shift+Backtab", "Ctrl+PgUp"});
    }
    // ⌘1…⌘8 pick a tab; ⌘9 always means the last one, as in browsers.
    for (int i = 1; i <= 9; ++i) {
        t.append({QStringLiteral("actionSelectTab%1").arg(i),
                  {QStringLiteral("Ctrl+%1").arg(i)}});
    }

    return t;
}

} // namespace Keymap
