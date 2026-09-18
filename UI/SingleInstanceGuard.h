#pragma once

#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>

// Single-instance guard (Windows + Linux): lock file + local socket to raise the
// already-running window when a second process is started.
class SingleInstanceGuard final : public QObject {
    Q_OBJECT
public:
    explicit SingleInstanceGuard(const QString& key, QObject* parent = nullptr);
    ~SingleInstanceGuard() override;

    [[nodiscard]] bool tryBecomePrimary();
    // Tell the primary instance to show/raise its window. Returns false if unreachable.
    bool notifyPrimary();

signals:
    void anotherInstanceTriedToStart();

private:
    QString m_key;
    QString m_lockPath;
    QString m_serverName;
    QLockFile m_lock;
    QLocalServer* m_server = nullptr;
};
