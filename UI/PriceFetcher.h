#pragma once

#include "MarketTypes.h"

#include <QObject>

class QNetworkAccessManager;

class PriceFetcher final : public QObject {
    Q_OBJECT

public:
    explicit PriceFetcher(QObject* parent = nullptr);

public slots:
    void init();
    void abortAll();
    void fetchQuote(const QString& fiatCode);
    void fetchChart(const QString& fiatCode, int days);

signals:
    void quoteReady(const MarketQuote& quote);
    void chartReady(const MarketChart& chart);
    void failed(const QString& message);

private:
    QNetworkAccessManager* m_nam = nullptr;
};
