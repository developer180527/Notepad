#include "mainwindow.h"
#include "winregister.h"

#include <QApplication>
#include <QFileOpenEvent>
#include <QIcon>
#include <QLocale>

#include <cstdlib>
#include <QPointer>
#include <QString>
#include <QTranslator>

// A QApplication that also routes macOS "open document" events (Finder
// double-click, drag-onto-dock) to the main window. If the event arrives before
// the window is ready, the path is buffered and replayed via setWindow().
class NotepadApplication : public QApplication
{
public:
    using QApplication::QApplication;

    void setWindow(MainWindow *window)
    {
        m_window = window;
        if (m_window && !m_pendingFile.isEmpty()) {
            m_window->openPath(m_pendingFile);
            m_pendingFile.clear();
        }
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::FileOpen) {
            const QString file = static_cast<QFileOpenEvent *>(e)->file();
            if (m_window)
                m_window->openPath(file);
            else
                m_pendingFile = file;
            return true;
        }
        return QApplication::event(e);
    }

private:
    QPointer<MainWindow> m_window;
    QString m_pendingFile;
};

int main(int argc, char *argv[])
{
    NotepadApplication a(argc, argv);
    // Identify the app so QSettings persists in a stable, per-app location.
    QApplication::setOrganizationName(QStringLiteral("Notepad"));
    QApplication::setOrganizationDomain(QStringLiteral("org.notepad"));
    QApplication::setApplicationName(QStringLiteral("Notepad"));
    a.setWindowIcon(QIcon(QStringLiteral(":/icons/notepad.png")));

    // On Windows, ensure the .note association + thumbnail handler are registered
    // (per-user). No-op elsewhere.
    registerWindowsIntegration();

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "Notepad_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    MainWindow w;
    a.setWindow(&w);

    // A file path on the command line (Windows/Linux double-click, or
    // `Notepad file.note`) opens it directly.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        w.openPath(args.at(1));

    w.show();

    const int status = QApplication::exec();

#ifdef Q_OS_MACOS
    // Work around a crash in Qt's cocoa plugin while releasing cached native
    // touch/gesture events during QApplication teardown (segfaults on quit
    // after trackpad use). All persistent state (settings, window geometry) is
    // flushed before exec() returns, so exit now and skip the buggy teardown.
    std::fflush(nullptr);
    std::_Exit(status);
#endif
    return status;
}
