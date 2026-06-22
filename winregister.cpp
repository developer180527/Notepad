#include "winregister.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QString>

// CLSID must match windows/NoteThumbnailProvider.cpp.
static const QString kClsid = QStringLiteral("{7E4F9A2C-3B1D-4E6A-9C7F-2A5B8D1E0F33}");
// Well-known IThumbnailProvider shell-extension interface id.
static const QString kThumbIface = QStringLiteral("{e357fccd-a995-4576-b01f-234630154e96}");

void registerWindowsIntegration()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString dll = QDir::toNativeSeparators(
        QCoreApplication::applicationDirPath() + QStringLiteral("/NoteThumbnail.dll"));

    // QSettings with NativeFormat writes the Windows registry; "/." sets a key's
    // (Default) value.
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);

    // File type + double-click open command (routes to argv handling in main()).
    classes.setValue(QStringLiteral(".note/."), QStringLiteral("Notepad.Note"));
    classes.setValue(QStringLiteral("Notepad.Note/."), QStringLiteral("Notepad Note"));
    classes.setValue(QStringLiteral("Notepad.Note/DefaultIcon/."), exe + QStringLiteral(",0"));
    classes.setValue(QStringLiteral("Notepad.Note/shell/open/command/."),
                     QStringLiteral("\"") + exe + QStringLiteral("\" \"%1\""));

    // Thumbnail handler: register CLSID -> our DLL, then hook .note to that CLSID.
    classes.setValue(QStringLiteral("CLSID/") + kClsid + QStringLiteral("/."),
                     QStringLiteral("Notepad Note Thumbnail Handler"));
    classes.setValue(QStringLiteral("CLSID/") + kClsid + QStringLiteral("/InprocServer32/."), dll);
    classes.setValue(QStringLiteral("CLSID/") + kClsid
                         + QStringLiteral("/InprocServer32/ThreadingModel"),
                     QStringLiteral("Apartment"));
    classes.setValue(QStringLiteral(".note/ShellEx/") + kThumbIface + QStringLiteral("/."), kClsid);
    classes.setValue(QStringLiteral("Notepad.Note/ShellEx/") + kThumbIface + QStringLiteral("/."),
                     kClsid);

    // Register as an *opener* (not the forced default — Windows protects the
    // default via UserChoice) for text-ish files: a ProgId + open command, then
    // add it to each extension's "Open with" list. The user can then pick
    // Notepad via "Open with > Choose another app > Always".
    classes.setValue(QStringLiteral("Notepad.Text/."), QStringLiteral("Text Document"));
    classes.setValue(QStringLiteral("Notepad.Text/DefaultIcon/."), exe + QStringLiteral(",0"));
    classes.setValue(QStringLiteral("Notepad.Text/shell/open/command/."),
                     QStringLiteral("\"") + exe + QStringLiteral("\" \"%1\""));
    const QStringList textExts = {QStringLiteral(".txt"), QStringLiteral(".md"),
                                  QStringLiteral(".markdown"), QStringLiteral(".html"),
                                  QStringLiteral(".htm")};
    for (const QString &ext : textExts)
        classes.setValue(ext + QStringLiteral("/OpenWithProgids/Notepad.Text"), QString());

    classes.sync();
}
#else
void registerWindowsIntegration() {}
#endif
