#include "singleinstance.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

namespace {
constexpr int kConnectTimeoutMs = 300;
constexpr int kWriteTimeoutMs = 800;
} // namespace

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
    // Probing with a real connection is the only reliable liveness test: a
    // stale socket file left by a crash still exists, but nothing answers it.
    QLocalSocket probe;
    probe.connectToServer(m_key);
    m_running = probe.waitForConnected(kConnectTimeoutMs);
    if (m_running)
        probe.disconnectFromServer();
}

bool SingleInstance::sendPaths(const QStringList &paths)
{
    QLocalSocket socket;
    socket.connectToServer(m_key);
    if (!socket.waitForConnected(kConnectTimeoutMs))
        return false;

    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_5);
    out << paths;

    socket.write(payload);
    const bool sent = socket.waitForBytesWritten(kWriteTimeoutMs);
    socket.disconnectFromServer();
    return sent;
}

void SingleInstance::listen()
{
    // Clear any socket left behind by a previous crash, otherwise listen() fails
    // and every later launch would spawn its own window.
    QLocalServer::removeServer(m_key);

    m_server = new QLocalServer(this);
    if (!m_server->listen(m_key))
        return;      // not fatal: we simply lose forwarding, not the app

    connect(m_server, &QLocalServer::newConnection, this, [this] {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            return;
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        // Payloads are tiny (a few paths), so a bounded wait is simpler and
        // safe compared to accumulating across readyRead.
        if (!socket->waitForReadyRead(kWriteTimeoutMs))
            return;
        QByteArray payload = socket->readAll();
        QDataStream in(&payload, QIODevice::ReadOnly);
        in.setVersion(QDataStream::Qt_6_5);
        QStringList paths;
        in >> paths;
        if (!paths.isEmpty())
            emit pathsReceived(paths);
    });
}
