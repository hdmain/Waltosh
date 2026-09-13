#include "PriceFetcher.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString joinFiatCodes()
{
    QStringList codes;
    for (const FiatCurrency& f : supportedFiatCurrencies()) {
        codes.push_back(f.code.toLower());
    }
    return codes.join(QLatin1Char(','));
}

} // namespace

PriceFetcher::PriceFetcher(QObject* parent)
    : QObject(parent)
{
}

void PriceFetcher::init()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }
}

void PriceFetcher::abortAll()
{
    if (!m_nam) {
        return;
    }
    const auto replies = m_nam->findChildren<QNetworkReply*>();
    for (QNetworkReply* reply : replies) {
        reply->abort();
    }
}

void PriceFetcher::fetchQuote(const QString& fiatCode)
{
    if (!m_nam) {
        init();
    }

    QUrl url(QStringLiteral("https://api.coingecko.com/api/v3/simple/price"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ids"), QStringLiteral("litecoin"));
    query.addQueryItem(QStringLiteral("vs_currencies"), joinFiatCodes());
    query.addQueryItem(QStringLiteral("include_24hr_change"), QStringLiteral("true"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("WALTOSH/0.1"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fiatCode]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("Price fetch failed: %1").arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject ltc = doc.object().value(QStringLiteral("litecoin")).toObject();
        if (ltc.isEmpty()) {
            emit failed(QStringLiteral("Price response missing litecoin data."));
            return;
        }

        const QString key = fiatCode.toLower();
        MarketQuote quote;
        quote.fiatCode = fiatCode.toUpper();
        quote.price = ltc.value(key).toDouble();
        quote.usdPrice = ltc.value(QStringLiteral("usd")).toDouble();
        quote.change24hPct = ltc.value(key + QStringLiteral("_24h_change")).toDouble();
        quote.valid = quote.price > 0.0;
        quote.live = false;
        quote.updatedMs = QDateTime::currentMSecsSinceEpoch();
        if (quote.usdPrice <= 0.0 && quote.fiatCode == QLatin1String("USD")) {
            quote.usdPrice = quote.price;
        }
        if (!quote.valid) {
            emit failed(QStringLiteral("No price for %1.").arg(quote.fiatCode));
            return;
        }
        emit quoteReady(quote);
    });
}

void PriceFetcher::fetchChart(const QString& fiatCode, int days)
{
    if (!m_nam) {
        init();
    }

    const int safeDays = qBound(1, days, 90);
    QUrl url(QStringLiteral("https://api.coingecko.com/api/v3/coins/litecoin/market_chart"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("vs_currency"), fiatCode.toLower());
    query.addQueryItem(QStringLiteral("days"), QString::number(safeDays));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("WALTOSH/0.1"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fiatCode, safeDays]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("Chart fetch failed: %1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonArray prices = doc.object().value(QStringLiteral("prices")).toArray();
        MarketChart chart;
        chart.fiatCode = fiatCode.toUpper();
        chart.days = safeDays;
        chart.points.reserve(prices.size());
        for (const QJsonValue& v : prices) {
            const QJsonArray pair = v.toArray();
            if (pair.size() < 2) {
                continue;
            }
            ChartPoint pt;
            pt.unixMs = static_cast<qint64>(pair.at(0).toDouble());
            pt.price = pair.at(1).toDouble();
            if (pt.price > 0.0) {
                chart.points.push_back(pt);
            }
        }
        chart.valid = !chart.points.isEmpty();
        if (!chart.valid) {
            emit failed(QStringLiteral("Chart response was empty."));
            return;
        }
        emit chartReady(chart);
    });
}
