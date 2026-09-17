#ifndef KEYMAP_H
#define KEYMAP_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Every window-level keyboard shortcut, per platform, in one place.
//
// Sequences are written in Qt's portable form. On macOS Qt reads "Ctrl" as ⌘,
// "Alt" as ⌥ and "Meta" as the physical Control key; on Windows and Linux they
// mean what they say. That mapping is why a naive "Ctrl+Tab" meant ⌘Tab on a
// Mac — which the system swallows for the app switcher — and is the reason the
// tables are split by platform rather than shared.
//
// Kept as plain data, independent of the current build, so all three tables can
// be checked for conflicts on any one machine.
namespace Keymap {

enum class Platform { Mac, Windows, Linux };

Platform current();

// (QAction objectName, sequences). The first sequence is the one menus display.
using Table = QList<QPair<QString, QStringList>>;
Table table(Platform platform);

} // namespace Keymap

#endif // KEYMAP_H
