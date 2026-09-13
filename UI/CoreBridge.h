#pragma once

#include "core/Types.hpp"

#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace waltosh::core {
class WalletCore;
}

// Qt-side adapter only. Owns no wallet crypto; marshals core events onto the GUI thread.
class CoreBridge final : public QObject {
    Q_OBJECT

public:
    explicit CoreBridge(const QString& dataDir, QObject* parent = nullptr);
    ~CoreBridge() override;

    [[nodiscard]] waltosh::core::Snapshot snapshot() const;
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] bool walletExists() const;
    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] QString dataDir() const;
    [[nodiscard]] static bool isValidAddress(const QString& address);

public slots:
    void createWallet(const QString& password);
    void importWallet(const QString& mnemonic, const QString& password);
    void unlock(const QString& password);
    void lock();
    void newAddress(int addressTypeIndex);
    void findVanityAddress(int addressTypeIndex, const QString& customWord, bool prefix = false);
    void cancelVanity();
    void send(const QString& toAddress, qint64 amountSats, qint64 feeSatPerVb);
    void hardRefresh();
    void shutdown();

signals:
    void snapshotUpdated(const waltosh::core::Snapshot& snapshot);
    void balanceChanged(qint64 newBalanceSats, qint64 deltaSats);
    void opened();
    void locked();
    void walletCreated(const QString& mnemonic);
    void walletImported();
    void addressReady(const QString& address);
    void sendFinished(const QString& txid);
    void vanityProgress(qint64 tried, int elapsedSec);
    void errorOccurred(const QString& message);
    void busyChanged(bool busy);

private:
    Q_INVOKABLE void deliverEvent(waltosh::core::Event event);
    void onCoreEvent(waltosh::core::Event event);

    std::unique_ptr<waltosh::core::WalletCore> m_core;
    mutable QMutex m_snapMu;
    waltosh::core::Snapshot m_snapshot;
};

// Allow queued delivery of Snapshot / Event across threads.
Q_DECLARE_METATYPE(waltosh::core::Snapshot)
Q_DECLARE_METATYPE(waltosh::core::Event)
