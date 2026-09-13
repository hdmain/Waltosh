#include "LivePriceSocket.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSslSocket>

#include <QtEndian>

namespace {

[[nodiscard]] QByteArray makeWsKey()
{
    QByteArray raw(16, Qt::Uninitialized);
    for (int i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    }
    return raw.toBase64();
}

[[nodiscard]] QByteArray maskKey()
{
    QByteArray key(4, Qt::Uninitialized);
    for (int i = 0; i < 4; ++i) {
        key[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    }
    return key;
}

} // namespace

LivePriceSocket::LivePriceSocket(QObject* parent)
    : QObject(parent)
{
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &LivePriceSocket::connectSocket);
}

LivePriceSocket::~LivePriceSocket()
{
    stop();
}

void LivePriceSocket::start()
{
    m_wantRunning = true;
    m_backoffMs = 1500;
    connectSocket();
}

void LivePriceSocket::stop()
{
    m_wantRunning = false;
    m_reconnect.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_handshakeDone = false;
    m_buffer.clear();
    emit connectedChanged(false);
}

bool LivePriceSocket::isConnected() const
{
    return m_socket && m_handshakeDone && m_socket->state() == QAbstractSocket::ConnectedState;
}

void LivePriceSocket::connectSocket()
{
    if (!m_wantRunning) {
        return;
    }
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_handshakeDone = false;
    m_buffer.clear();
    m_socket = new QSslSocket(this);
    connect(m_socket, &QSslSocket::encrypted, this, &LivePriceSocket::onConnected);
    connect(m_socket, &QSslSocket::readyRead, this, &LivePriceSocket::onReadyRead);
    connect(m_socket, &QSslSocket::errorOccurred, this, &LivePriceSocket::onSocketError);
    connect(m_socket, &QSslSocket::disconnected, this, &LivePriceSocket::onDisconnected);

    m_socket->connectToHostEncrypted(QStringLiteral("stream.binance.com"), 443);
}

void LivePriceSocket::scheduleReconnect()
{
    if (!m_wantRunning || m_reconnect.isActive()) {
        return;
    }
    m_reconnect.start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, 20000);
}

void LivePriceSocket::onConnected()
{
    if (!m_socket) {
        return;
    }
    const QByteArray key = makeWsKey();
    const QByteArray req =
        "GET /ws/ltcusdt@miniTicker HTTP/1.1\r\n"
        "Host: stream.binance.com\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: "
        + key
        + "\r\n"
          "Sec-WebSocket-Version: 13\r\n"
          "\r\n";
    m_socket->write(req);
    m_socket->flush();
}

void LivePriceSocket::onReadyRead()
{
    if (!m_socket) {
        return;
    }
    m_buffer.append(m_socket->readAll());
    processBuffer();
}

void LivePriceSocket::onSocketError()
{
    emit connectedChanged(false);
    if (m_wantRunning) {
        scheduleReconnect();
    }
}

void LivePriceSocket::onDisconnected()
{
    m_handshakeDone = false;
    emit connectedChanged(false);
    if (m_wantRunning) {
        scheduleReconnect();
    }
}

void LivePriceSocket::processBuffer()
{
    if (!m_handshakeDone) {
        if (!tryConsumeHttpHandshake()) {
            return;
        }
        m_backoffMs = 1500;
        emit connectedChanged(true);
    }

    while (true) {
        if (m_buffer.size() < 2) {
            return;
        }
        const auto* data = reinterpret_cast<const quint8*>(m_buffer.constData());
        const quint8 opcode = data[0] & 0x0f;
        const bool masked = (data[1] & 0x80) != 0;
        quint64 payloadLen = data[1] & 0x7f;
        int headerLen = 2;
        if (payloadLen == 126) {
            if (m_buffer.size() < 4) {
                return;
            }
            payloadLen = qFromBigEndian<quint16>(data + 2);
            headerLen = 4;
        } else if (payloadLen == 127) {
            if (m_buffer.size() < 10) {
                return;
            }
            payloadLen = qFromBigEndian<quint64>(data + 2);
            headerLen = 10;
        }
        if (masked) {
            headerLen += 4;
        }
        if (payloadLen > 1024 * 1024) {
            m_buffer.clear();
            if (m_socket) {
                m_socket->abort();
            }
            return;
        }
        const int total = headerLen + static_cast<int>(payloadLen);
        if (m_buffer.size() < total) {
            return;
        }

        QByteArray payload = m_buffer.mid(headerLen, static_cast<int>(payloadLen));
        if (masked) {
            const QByteArray key = m_buffer.mid(headerLen - 4, 4);
            for (int i = 0; i < payload.size(); ++i) {
                payload[i] = payload[i] ^ key[i % 4];
            }
        }
        m_buffer.remove(0, total);
        handleWsFrame(opcode, payload);
    }
}

bool LivePriceSocket::tryConsumeHttpHandshake()
{
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        return false;
    }
    const QByteArray header = m_buffer.left(end);
    m_buffer.remove(0, end + 4);
    if (!header.startsWith("HTTP/1.1 101") && !header.startsWith("HTTP/1.0 101")) {
        if (m_socket) {
            m_socket->abort();
        }
        return false;
    }
    m_handshakeDone = true;
    return true;
}

void LivePriceSocket::handleWsFrame(quint8 opcode, const QByteArray& payload)
{
    switch (opcode) {
    case 0x1: { // text
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        const QJsonObject obj = doc.object();
        const double closePx = obj.value(QStringLiteral("c")).toString().toDouble();
        const double openPx = obj.value(QStringLiteral("o")).toString().toDouble();
        if (closePx <= 0.0) {
            return;
        }
        double change = 0.0;
        if (openPx > 0.0) {
            change = ((closePx - openPx) / openPx) * 100.0;
        }
        const qint64 eventMs = static_cast<qint64>(obj.value(QStringLiteral("E")).toDouble());
        emit tick(closePx, change, eventMs > 0 ? eventMs : QDateTime::currentMSecsSinceEpoch());
        break;
    }
    case 0x8: // close
        if (m_socket) {
            m_socket->disconnectFromHost();
        }
        break;
    case 0x9: // ping
        sendPong(payload);
        break;
    case 0xA: // pong
        break;
    default:
        break;
    }
}

void LivePriceSocket::sendPong(const QByteArray& payload)
{
    if (!m_socket || !m_handshakeDone) {
        return;
    }
    QByteArray frame;
    frame.append(static_cast<char>(0x8A)); // FIN + pong
    const QByteArray key = maskKey();
    if (payload.size() < 126) {
        frame.append(static_cast<char>(0x80 | payload.size()));
    } else {
        frame.append(static_cast<char>(0x80 | 126));
        char lenBytes[2];
        qToBigEndian(static_cast<quint16>(payload.size()), lenBytes);
        frame.append(lenBytes, 2);
    }
    frame.append(key);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) {
        masked[i] = masked[i] ^ key[i % 4];
    }
    frame.append(masked);
    m_socket->write(frame);
}
