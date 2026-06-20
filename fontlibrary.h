#ifndef FONTLIBRARY_H
#define FONTLIBRARY_H

#include <QByteArray>
#include <QString>
#include <QStringList>

// Bundled, open-licensed fonts shipped inside the binary so a document can look
// identical on any machine. Families used in a document are also embedded into
// the .note file (see MainWindow::writeNote/readNote), so a note is portable
// even to a build that lacks these resources.
namespace FontLibrary {

// Register the bundled fonts with the application font database. Call once,
// after the QApplication exists.
void load();

// Bundled family names (e.g. "Inter", "Lora", "JetBrains Mono").
const QStringList &families();

// Preferred default document family (a bundled sans), or empty if none loaded.
QString defaultFamily();

// Raw TrueType bytes for a bundled family, or an empty array if not bundled.
QByteArray fontData(const QString &family);

// Register font bytes that arrived embedded in a .note file.
void registerFontData(const QByteArray &data);

} // namespace FontLibrary

#endif // FONTLIBRARY_H
