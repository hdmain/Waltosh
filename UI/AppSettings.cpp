#include "AppSettings.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace {

constexpr auto kDarkMode = "ui/darkMode";
constexpr auto kBackdrop = "ui/backdropType";
constexpr auto kFiat = "ui/fiatCurrency";
constexpr auto kChartDays = "ui/chartDays";
constexpr auto kFeeSatPerVb = "ui/feeSatPerVb";
constexpr auto kSendAmountInFiat = "ui/sendAmountInFiat";
constexpr auto kRecentTx = "history/recentTransactions";
constexpr int kMaxStoredTx = 100;

} // namespace

AppSettings::AppSettings()
    : m_settings(QStringLiteral("WALTOSH"), QStringLiteral("WALTOSH"))
{
}

bool AppSettings::darkMode() const
{
    return m_settings.value(QLatin1String(kDarkMode), true).toBool();
}

void AppSettings::setDarkMode(bool enabled)
{
    m_settings.setValue(QLatin1String(kDarkMode), enabled);
}

BackdropType AppSettings::backdropType() const
{
    const int raw = m_settings.value(QLatin1String(kBackdrop), static_cast<int>(BackdropType::Mica)).toInt();
    switch (raw) {
    case static_cast<int>(BackdropType::Auto):
    case static_cast<int>(BackdropType::Disabled):
    case static_cast<int>(BackdropType::Mica):
    case static_cast<int>(BackdropType::Acrylic):
    case static_cast<int>(BackdropType::MicaAlt):
        return static_cast<BackdropType>(raw);
    default:
        return BackdropType::Mica;
    }
}

void AppSettings::setBackdropType(BackdropType type)
{
    m_settings.setValue(QLatin1String(kBackdrop), static_cast<int>(type));
}

QString AppSettings::fiatCurrency() const
{
    return m_settings.value(QLatin1String(kFiat), QStringLiteral("USD")).toString().trimmed().toUpper();
}

void AppSettings::setFiatCurrency(const QString& code)
{
    m_settings.setValue(QLatin1String(kFiat), code.trimmed().toUpper());
}

int AppSettings::chartDays() const
{
    return qBound(1, m_settings.value(QLatin1String(kChartDays), 7).toInt(), 90);
}

void AppSettings::setChartDays(int days)
{
    m_settings.setValue(QLatin1String(kChartDays), qBound(1, days, 90));
}

qint64 AppSettings::feeSatPerVb() const
{
    return qMax<qint64>(1, m_settings.value(QLatin1String(kFeeSatPerVb), 10).toLongLong());
}

void AppSettings::setFeeSatPerVb(qint64 fee)
{
    m_settings.setValue(QLatin1String(kFeeSatPerVb), qMax<qint64>(1, fee));
}

bool AppSettings::sendAmountInFiat() const
{
    return m_settings.value(QLatin1String(kSendAmountInFiat), false).toBool();
}

void AppSettings::setSendAmountInFiat(bool inFiat)
{
    m_settings.setValue(QLatin1String(kSendAmountInFiat), inFiat);
}

QVector<PersistedTx> AppSettings::recentTransactions(int limit) const
{
    const QByteArray raw = m_settings.value(QLatin1String(kRecentTx)).toByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray()) {
        return {};
    }

    QVector<PersistedTx> out;
    const QJsonArray arr = doc.array();
    out.reserve(qMin(limit, arr.size()));
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject o = v.toObject();
        PersistedTx tx;
        tx.txid = o.value(QStringLiteral("txid")).toString();
        tx.address = o.value(QStringLiteral("address")).toString();
        tx.amountSats = static_cast<qint64>(o.value(QStringLiteral("amountSats")).toDouble());
        tx.kind = o.value(QStringLiteral("kind")).toString();
        tx.unixMs = static_cast<qint64>(o.value(QStringLiteral("unixMs")).toDouble());
        if (tx.txid.isEmpty()) {
            continue;
        }
        out.push_back(tx);
        if (out.size() >= limit) {
            break;
        }
    }
    return out;
}

void AppSettings::addTransaction(const PersistedTx& tx)
{
    if (tx.txid.isEmpty()) {
        return;
    }

    QVector<PersistedTx> list = recentTransactions(kMaxStoredTx);
    list.erase(std::remove_if(list.begin(),
                               list.end(),
                               [&](const PersistedTx& existing) { return existing.txid == tx.txid; }),
               list.end());

    PersistedTx copy = tx;
    if (copy.unixMs <= 0) {
        copy.unixMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (copy.kind.isEmpty()) {
        copy.kind = QStringLiteral("sent");
    }
    list.prepend(copy);
    if (list.size() > kMaxStoredTx) {
        list.resize(kMaxStoredTx);
    }

    QJsonArray arr;
    for (const PersistedTx& item : list) {
        QJsonObject o;
        o.insert(QStringLiteral("txid"), item.txid);
        o.insert(QStringLiteral("address"), item.address);
        o.insert(QStringLiteral("amountSats"), static_cast<double>(item.amountSats));
        o.insert(QStringLiteral("kind"), item.kind);
        o.insert(QStringLiteral("unixMs"), static_cast<double>(item.unixMs));
        arr.append(o);
    }
    m_settings.setValue(QLatin1String(kRecentTx), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void AppSettings::sync()
{
    m_settings.sync();
}
