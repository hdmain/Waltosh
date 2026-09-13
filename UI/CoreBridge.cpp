#include "CoreBridge.h"

#include "core/WalletCore.hpp"

#include <ltc/wallet/script.hpp>

#include <QMetaType>
#include <QMutexLocker>

namespace {

waltosh::core::AddressType addressTypeFromIndex(int index)
{
    switch (index) {
    case 1:
        return waltosh::core::AddressType::Native;
    case 2:
        return waltosh::core::AddressType::Legacy;
    case 3:
        return waltosh::core::AddressType::Taproot;
    default:
        return waltosh::core::AddressType::Nested;
    }
}

} // namespace

CoreBridge::CoreBridge(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_core(std::make_unique<waltosh::core::WalletCore>(dataDir.toStdString()))
{
    qRegisterMetaType<waltosh::core::Snapshot>("waltosh::core::Snapshot");
    qRegisterMetaType<waltosh::core::Event>("waltosh::core::Event");

    m_core->set_listener([this](waltosh::core::Event event) { onCoreEvent(std::move(event)); });
    m_core->start();
    {
        QMutexLocker lock(&m_snapMu);
        m_snapshot = m_core->snapshot();
    }
}

CoreBridge::~CoreBridge()
{
    shutdown();
    m_core.reset();
}

waltosh::core::Snapshot CoreBridge::snapshot() const
{
    QMutexLocker lock(&m_snapMu);
    return m_snapshot;
}

bool CoreBridge::isOpen() const
{
    return snapshot().open;
}

bool CoreBridge::walletExists() const
{
    return snapshot().exists;
}

bool CoreBridge::isBusy() const
{
    return snapshot().busy;
}

QString CoreBridge::dataDir() const
{
    return QString::fromStdString(snapshot().data_dir);
}

bool CoreBridge::isValidAddress(const QString& address)
{
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    try {
        (void)ltc::decode_address(trimmed.toStdString());
        return true;
    } catch (...) {
        return false;
    }
}

void CoreBridge::createWallet(const QString& password)
{
    m_core->request_create(password.toStdString());
}

void CoreBridge::importWallet(const QString& mnemonic, const QString& password)
{
    m_core->request_import(mnemonic.trimmed().toStdString(), password.toStdString());
}

void CoreBridge::unlock(const QString& password)
{
    m_core->request_unlock(password.toStdString());
}

void CoreBridge::lock()
{
    m_core->request_lock();
}

void CoreBridge::newAddress(int addressTypeIndex)
{
    m_core->request_new_address(addressTypeFromIndex(addressTypeIndex));
}

void CoreBridge::findVanityAddress(int addressTypeIndex, const QString& customWord, bool prefix)
{
    m_core->request_vanity_address(addressTypeFromIndex(addressTypeIndex),
                                   customWord.trimmed().toStdString(), prefix);
}

void CoreBridge::cancelVanity()
{
    m_core->cancel_vanity();
}

void CoreBridge::send(const QString& toAddress, qint64 amountSats, qint64 feeSatPerVb)
{
    m_core->request_send(toAddress.trimmed().toStdString(), amountSats, feeSatPerVb);
}

void CoreBridge::hardRefresh()
{
    m_core->request_hard_refresh();
}

void CoreBridge::shutdown()
{
    if (m_core) {
        m_core->set_listener({});
        m_core->stop();
    }
}

void CoreBridge::onCoreEvent(waltosh::core::Event event)
{
    // Always marshal onto the GUI thread - core callbacks arrive from worker/poller threads.
    QMetaObject::invokeMethod(
        this,
        "deliverEvent",
        Qt::QueuedConnection,
        Q_ARG(waltosh::core::Event, event));
}

void CoreBridge::deliverEvent(waltosh::core::Event event)
{
    qint64 prevBalance = 0;
    bool wasOpen = false;
    {
        QMutexLocker lock(&m_snapMu);
        wasOpen = m_snapshot.open;
        prevBalance = m_snapshot.balance_sats;
        // Progress events carry an empty snapshot - keep the last real one.
        if (event.kind != waltosh::core::EventKind::VanityProgress) {
            m_snapshot = event.snapshot;
        }
    }

    switch (event.kind) {
    case waltosh::core::EventKind::Snapshot:
        emit snapshotUpdated(event.snapshot);
        break;
    case waltosh::core::EventKind::BusyChanged:
        emit busyChanged(event.snapshot.busy);
        emit snapshotUpdated(event.snapshot);
        break;
    case waltosh::core::EventKind::Opened:
        emit opened();
        emit snapshotUpdated(event.snapshot);
        break;
    case waltosh::core::EventKind::Locked:
        emit locked();
        emit snapshotUpdated(event.snapshot);
        break;
    case waltosh::core::EventKind::Created:
        emit walletCreated(QString::fromStdString(event.message));
        emit snapshotUpdated(event.snapshot);
        break;
    case waltosh::core::EventKind::Imported:
        emit walletImported();
        emit snapshotUpdated(event.snapshot);
        break;
    case waltosh::core::EventKind::AddressGenerated:
        emit addressReady(QString::fromStdString(event.message));
        emit snapshotUpdated(event.snapshot);
        break;
    case waltosh::core::EventKind::Sent:
        emit sendFinished(QString::fromStdString(event.message));
        emit snapshotUpdated(event.snapshot);
        break;
    case waltosh::core::EventKind::VanityProgress: {
        const QString msg = QString::fromStdString(event.message);
        const QStringList parts = msg.split(QLatin1Char('|'));
        const qint64 tried = parts.value(0).toLongLong();
        const int elapsed = parts.value(1).toInt();
        emit vanityProgress(tried, elapsed);
        break;
    }
    case waltosh::core::EventKind::Error:
        emit errorOccurred(QString::fromStdString(event.message));
        emit snapshotUpdated(event.snapshot);
        break;
    }

    if (event.kind != waltosh::core::EventKind::VanityProgress && wasOpen && event.snapshot.open
        && event.snapshot.balance_sats != prevBalance) {
        emit balanceChanged(event.snapshot.balance_sats, event.snapshot.balance_sats - prevBalance);
    }
}
