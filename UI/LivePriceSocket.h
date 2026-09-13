#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>

class QSslSocket;

// Minimal client for Binance public miniTicker (LTCUSDT) over TLS WebSocket.
// Avoids depending on the Qt WebSockets module.
class LivePriceSocket final : public QObject {
    Q_OBJECT

public:
    explicit LivePriceSocket(QObject* parent = nullptr);
    ~LivePriceSocket() override;

    void start();
    void stop();
    [[nodiscard]] bool isConnected() const;

signals:
    void tick(double usdPrice, double change24hPct, qint64 eventMs);
    void connectedChanged(bool connected);

private:
    void connectSocket();
    void scheduleReconnect();
    void onConnected();
    void onReadyRead();
    void onSocketError();
    void onDisconnected();
    void processBuffer();
    [[nodiscard]] bool tryConsumeHttpHandshake();
    void handleWsFrame(quint8 opcode, const QByteArray& payload);
    void sendPong(const QByteArray& payload);

    QSslSocket* m_socket = nullptr;
    QTimer m_reconnect;
    QByteArray m_buffer;
    bool m_handshakeDone = false;
    bool m_wantRunning = false;
    int m_backoffMs = 1500;
};
