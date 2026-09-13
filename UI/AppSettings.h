#pragma once

#include "MicaEffect.h"

#include <QSettings>
#include <QString>
#include <QVector>

struct PersistedTx {
    QString txid;
    QString address;
    qint64 amountSats = 0;
    QString kind; // "sent" | "received"
    qint64 unixMs = 0;
};

// Thin QSettings wrapper for UI preferences and lightweight local history.
class AppSettings final {
public:
    AppSettings();

    [[nodiscard]] bool darkMode() const;
    void setDarkMode(bool enabled);

    [[nodiscard]] BackdropType backdropType() const;
    void setBackdropType(BackdropType type);

    [[nodiscard]] QString fiatCurrency() const;
    void setFiatCurrency(const QString& code);

    [[nodiscard]] int chartDays() const;
    void setChartDays(int days);

    [[nodiscard]] qint64 feeSatPerVb() const;
    void setFeeSatPerVb(qint64 fee);

    [[nodiscard]] QVector<PersistedTx> recentTransactions(int limit = 50) const;
    void addTransaction(const PersistedTx& tx);

    void sync();

private:
    QSettings m_settings;
};
