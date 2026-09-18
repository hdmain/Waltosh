#include "SingleInstanceGuard.h"

#include <QDir>
#include <QStandardPaths>

namespace {

QString makeLockPath(const QString& key)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QLatin1Char('/') + key + QStringLiteral(".lock");
}

} // namespace

SingleInstanceGuard::SingleInstanceGuard(const QString& key, QObject* parent)
    : QObject(parent)
    , m_key(key)
    , m_lockPath(makeLockPath(key))
    , m_serverName(key + QStringLiteral("-ipc"))
    , m_lock(m_lockPath)
{
    m_lock.setStaleLockTime(30'000);
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_server) {
        m_server->close();
    }
    if (m_lock.isLocked()) {
        m_lock.unlock();
    }
}

bool SingleInstanceGuard::tryBecomePrimary()
{
    if (!m_lock.tryLock(100)) {
        return false;
    }

    QLocalServer::removeServer(m_serverName);
    m_server = new QLocalServer(this);
    if (!m_server->listen(m_serverName)) {
        // Still primary via lock; IPC raise may be unavailable.
        return true;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (m_server->hasPendingConnections()) {
            QLocalSocket* sock = m_server->nextPendingConnection();
            if (!sock) {
                continue;
            }
            connect(sock, &QLocalSocket::readyRead, this, [this, sock]() {
                sock->readAll();
                emit anotherInstanceTriedToStart();
                sock->disconnectFromServer();
                sock->deleteLater();
            });
            connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        }
    });
    return true;
}

bool SingleInstanceGuard::notifyPrimary()
{
    QLocalSocket sock;
    sock.connectToServer(m_serverName);
    if (!sock.waitForConnected(500)) {
        return false;
    }
    sock.write("raise\n");
    sock.flush();
    sock.waitForBytesWritten(500);
    sock.disconnectFromServer();
    return true;
}
