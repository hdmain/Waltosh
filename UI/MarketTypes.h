#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

struct FiatCurrency {
    QString code;   // USD
    QString name;   // US Dollar
    QString symbol; // $
};

[[nodiscard]] inline QVector<FiatCurrency> supportedFiatCurrencies()
{
    return {
        {QStringLiteral("USD"), QStringLiteral("US Dollar"), QStringLiteral("$")},
        {QStringLiteral("EUR"), QStringLiteral("Euro"), QStringLiteral("€")},
        {QStringLiteral("GBP"), QStringLiteral("British Pound"), QStringLiteral("£")},
        {QStringLiteral("JPY"), QStringLiteral("Japanese Yen"), QStringLiteral("¥")},
        {QStringLiteral("CAD"), QStringLiteral("Canadian Dollar"), QStringLiteral("C$")},
        {QStringLiteral("AUD"), QStringLiteral("Australian Dollar"), QStringLiteral("A$")},
        {QStringLiteral("CHF"), QStringLiteral("Swiss Franc"), QStringLiteral("CHF")},
        {QStringLiteral("PLN"), QStringLiteral("Polish Zloty"), QStringLiteral("zł")},
        {QStringLiteral("CZK"), QStringLiteral("Czech Koruna"), QStringLiteral("Kč")},
        {QStringLiteral("SEK"), QStringLiteral("Swedish Krona"), QStringLiteral("kr")},
        {QStringLiteral("NOK"), QStringLiteral("Norwegian Krone"), QStringLiteral("kr")},
        {QStringLiteral("BRL"), QStringLiteral("Brazilian Real"), QStringLiteral("R$")},
        {QStringLiteral("INR"), QStringLiteral("Indian Rupee"), QStringLiteral("₹")},
        {QStringLiteral("KRW"), QStringLiteral("South Korean Won"), QStringLiteral("₩")},
        {QStringLiteral("TRY"), QStringLiteral("Turkish Lira"), QStringLiteral("₺")},
        {QStringLiteral("MXN"), QStringLiteral("Mexican Peso"), QStringLiteral("Mex$")},
        {QStringLiteral("SGD"), QStringLiteral("Singapore Dollar"), QStringLiteral("S$")},
        {QStringLiteral("HKD"), QStringLiteral("Hong Kong Dollar"), QStringLiteral("HK$")},
        {QStringLiteral("NZD"), QStringLiteral("New Zealand Dollar"), QStringLiteral("NZ$")},
        {QStringLiteral("ZAR"), QStringLiteral("South African Rand"), QStringLiteral("R")},
    };
}

[[nodiscard]] inline FiatCurrency fiatByCode(const QString& code)
{
    const QString upper = code.trimmed().toUpper();
    for (const FiatCurrency& f : supportedFiatCurrencies()) {
        if (f.code == upper) {
            return f;
        }
    }
    return supportedFiatCurrencies().first();
}

struct MarketQuote {
    QString fiatCode = QStringLiteral("USD");
    double price = 0.0;
    double usdPrice = 0.0; // spot in USD for live FX from USDT stream
    double change24hPct = 0.0;
    bool valid = false;
    bool live = false;
    qint64 updatedMs = 0;
};

struct ChartPoint {
    qint64 unixMs = 0;
    double price = 0.0;
};

struct MarketChart {
    QString fiatCode = QStringLiteral("USD");
    int days = 7;
    QVector<ChartPoint> points;
    bool valid = false;
};

Q_DECLARE_METATYPE(MarketQuote)
Q_DECLARE_METATYPE(MarketChart)
