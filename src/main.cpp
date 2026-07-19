#include "mainwindow.h"
#include "singleinstance.h"
#include "winregister.h"

#include <QApplication>
#include <QFileOpenEvent>
#include <QIcon>
#include <QLocale>

#include <cstdlib>
#include <QString>
#include <QStringList>
#include <QTranslator>

// A QApplication that also routes macOS "open document" events (Finder
// double-click, drag-onto-dock). If an event arrives before any window exists,
// the path is buffered and replayed once the first window is up.
class NotepadApplication : public QApplication
{
public:
    using QApplication::QApplication;

    void flushPendingOpens()
    {
        const QStringList pending = m_pendingFiles;
        m_pendingFiles.clear();
        for (const QString &file : pending)
            MainWindow::routeOpenPath(file);
        m_ready = true;
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::FileOpen) {
            const QString file = static_cast<QFileOpenEvent *>(e)->file();
            if (m_ready)
                MainWindow::routeOpenPath(file);
            else
                m_pendingFiles << file;
            return true;
        }
        return QApplication::event(e);
    }

private:
    QStringList m_pendingFiles;
    bool m_ready = false;
};

int main(int argc, char *argv[])
{
    NotepadApplication a(argc, argv);
    // Identify the app so QSettings persists in a stable, per-app location.
    QApplication::setOrganizationName(QStringLiteral("Notepad"));
    QApplication::setOrganizationDomain(QStringLiteral("org.notepad"));
    QApplication::setApplicationName(QStringLiteral("Notepad"));
    a.setWindowIcon(QIcon(QStringLiteral(":/icons/notepad.png")));

    // File paths from the command line: on Windows and Linux this is how a
    // double-click arrives; on macOS it's a QFileOpenEvent instead.
    QStringList files = QApplication::arguments().mid(1);
    files.removeIf([](const QString &s) { return s.startsWith(QLatin1Char('-')); });

    // One process per user. A second launch (another double-click) hands its
    // paths to the running instance and exits, so documents open as tabs in the
    // window that's already on screen rather than in a brand-new copy of the app.
    SingleInstance instance(QStringLiteral("org.notepad.instance"));
    if (instance.isRunning() && instance.sendPaths(files))
        return 0;
    instance.listen();
    QObject::connect(&instance, &SingleInstance::pathsReceived, &a, [](const QStringList &paths) {
        for (const QString &p : paths)
            MainWindow::routeOpenPath(p);
        if (paths.isEmpty()) {
            // Bare relaunch (clicking the dock/taskbar icon): surface a window.
            if (MainWindow *w = MainWindow::mostRecentWindow()) {
                w->show();
                w->raise();
                w->activateWindow();
            }
        }
    });

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

    MainWindow::createWindow();
    for (const QString &file : std::as_const(files))
        MainWindow::routeOpenPath(file);
    a.flushPendingOpens();     // replay any macOS open events that beat the window

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
