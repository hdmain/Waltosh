#pragma once

#include "MarketTypes.h"

#include <QObject>
#include <QTimer>

class QThread;
class PriceFetcher;
class LivePriceSocket;

// CoinGecko REST for chart/fiat + Binance WS for live LTCUSDT spot.
class PriceService final : public QObject {
    Q_OBJECT

public:
    explicit PriceService(QObject* parent = nullptr);
    ~PriceService() override;

    void setFiat(const QString& code);
    void setChartDays(int days);
    void stop();
    [[nodiscard]] QString fiat() const;
    [[nodiscard]] int chartDays() const;
    [[nodiscard]] MarketQuote quote() const;
    [[nodiscard]] MarketChart chart() const;
    [[nodiscard]] bool liveConnected() const;

public slots:
    void refresh();
    void refreshQuote();
    void refreshChart();

signals:
    void quoteUpdated(const MarketQuote& quote);
    void chartUpdated(const MarketChart& chart);
    void errorOccurred(const QString& message);
    void liveStatusChanged(bool connected);

private:
    void onQuote(const MarketQuote& quote);
    void onChart(const MarketChart& chart);
    void onFetchError(const QString& message);
    void onLiveTick(double usdPrice, double change24hPct, qint64 eventMs);
    void emitLiveChart();

    QThread* m_thread = nullptr;
    PriceFetcher* m_fetcher = nullptr;
    LivePriceSocket* m_live = nullptr;
    QTimer m_pollTimer;
    QTimer m_chartEmitTimer;
    QString m_fiat = QStringLiteral("USD");
    int m_chartDays = 7;
    MarketQuote m_quote;
    MarketChart m_chart;
    double m_restUsd = 0.0;
    double m_fiatPerUsd = 1.0;
};
