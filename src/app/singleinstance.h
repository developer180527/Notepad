#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

// Keeps the app to one process per user, and forwards file arguments from later
// launches into the running one.
//
// This is what makes double-clicking a document behave the same everywhere.
// macOS already delivers a QFileOpenEvent to the running app, but Windows and
// Linux start a *new* process for every double-click — which would open a second
// copy of the editor instead of a tab in the one already on screen. The second
// process hands its paths over the local socket and exits immediately.
class SingleInstance : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);

    // True if another instance was already running (this process should forward
    // its arguments and quit).
    bool isRunning() const { return m_running; }

    // Ask the running instance to open these paths. Returns false if it could
    // not be reached, in which case the caller should carry on as primary.
    bool sendPaths(const QStringList &paths);

    // Start listening as the primary instance.
    void listen();

signals:
    void pathsReceived(const QStringList &paths);

private:
    QString m_key;
    QLocalServer *m_server = nullptr;
    bool m_running = false;
};

#endif // SINGLEINSTANCE_H
