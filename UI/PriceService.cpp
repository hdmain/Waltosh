#include "PriceService.h"

#include "LivePriceSocket.h"
#include "PriceFetcher.h"

#include <QDateTime>
#include <QMetaType>
#include <QThread>

PriceService::PriceService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<MarketQuote>("MarketQuote");
    qRegisterMetaType<MarketChart>("MarketChart");

    m_thread = new QThread(this);
    m_fetcher = new PriceFetcher;
    m_fetcher->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_fetcher, &PriceFetcher::init);
    connect(m_thread, &QThread::finished, m_fetcher, &QObject::deleteLater);
    connect(m_fetcher, &PriceFetcher::quoteReady, this, &PriceService::onQuote);
    connect(m_fetcher, &PriceFetcher::chartReady, this, &PriceService::onChart);
    connect(m_fetcher, &PriceFetcher::failed, this, &PriceService::onFetchError);

    m_live = new LivePriceSocket(this);
    connect(m_live, &LivePriceSocket::tick, this, &PriceService::onLiveTick);
    connect(m_live, &LivePriceSocket::connectedChanged, this, &PriceService::liveStatusChanged);

    m_pollTimer.setInterval(60 * 1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &PriceService::refresh);

    m_chartEmitTimer.setSingleShot(true);
    m_chartEmitTimer.setInterval(350);
    connect(&m_chartEmitTimer, &QTimer::timeout, this, &PriceService::emitLiveChart);

    m_thread->start();
    m_pollTimer.start();
    m_live->start();

    QMetaObject::invokeMethod(this, &PriceService::refresh, Qt::QueuedConnection);
}

void PriceService::stop()
{
    m_pollTimer.stop();
    m_chartEmitTimer.stop();
    if (m_live) {
        m_live->stop();
    }
    if (m_thread && m_thread->isRunning()) {
        if (m_fetcher) {
            QMetaObject::invokeMethod(m_fetcher, "abortAll", Qt::BlockingQueuedConnection);
        }
        m_thread->quit();
        if (!m_thread->wait(8000)) {
            m_thread->terminate();
            m_thread->wait(1000);
        }
    }
}

PriceService::~PriceService()
{
    stop();
}

void PriceService::setFiat(const QString& code)
{
    const QString next = fiatByCode(code).code;
    if (m_fiat == next) {
        return;
    }
    m_fiat = next;
    refresh();
}

void PriceService::setChartDays(int days)
{
    const int next = qBound(1, days, 90);
    if (m_chartDays == next) {
        return;
    }
    m_chartDays = next;
    refreshChart();
}

QString PriceService::fiat() const
{
    return m_fiat;
}

int PriceService::chartDays() const
{
    return m_chartDays;
}

MarketQuote PriceService::quote() const
{
    return m_quote;
}

MarketChart PriceService::chart() const
{
    return m_chart;
}

bool PriceService::liveConnected() const
{
    return m_live && m_live->isConnected();
}

void PriceService::refresh()
{
    refreshQuote();
    refreshChart();
}

void PriceService::refreshQuote()
{
    QMetaObject::invokeMethod(m_fetcher, "fetchQuote", Qt::QueuedConnection, Q_ARG(QString, m_fiat));
}

void PriceService::refreshChart()
{
    QMetaObject::invokeMethod(
        m_fetcher, "fetchChart", Qt::QueuedConnection, Q_ARG(QString, m_fiat), Q_ARG(int, m_chartDays));
}

void PriceService::onQuote(const MarketQuote& quote)
{
    m_quote = quote;
    if (quote.usdPrice > 0.0) {
        m_restUsd = quote.usdPrice;
        m_fiatPerUsd = quote.price / quote.usdPrice;
    } else if (quote.fiatCode == QLatin1String("USD") && quote.price > 0.0) {
        m_restUsd = quote.price;
        m_fiatPerUsd = 1.0;
    }
    emit quoteUpdated(m_quote);
}

void PriceService::onChart(const MarketChart& chart)
{
    m_chart = chart;
    emit chartUpdated(m_chart);
}

void PriceService::onFetchError(const QString& message)
{
    emit errorOccurred(message);
}

void PriceService::onLiveTick(double usdPrice, double change24hPct, qint64 eventMs)
{
    if (usdPrice <= 0.0) {
        return;
    }

    if (m_restUsd <= 0.0) {
        m_restUsd = usdPrice;
        if (m_fiat == QLatin1String("USD")) {
            m_fiatPerUsd = 1.0;
        }
    }

    const double fiatPrice =
        (m_fiat == QLatin1String("USD")) ? usdPrice : (usdPrice * m_fiatPerUsd);

    m_quote.fiatCode = m_fiat;
    m_quote.price = fiatPrice;
    m_quote.usdPrice = usdPrice;
    m_quote.valid = true;
    m_quote.live = true;
    m_quote.updatedMs = eventMs > 0 ? eventMs : QDateTime::currentMSecsSinceEpoch();
    if (m_fiat == QLatin1String("USD")) {
        m_quote.change24hPct = change24hPct;
    }
    emit quoteUpdated(m_quote);

    if (!m_chart.valid || m_chart.fiatCode != m_fiat) {
        return;
    }

    const qint64 now = m_quote.updatedMs;
    if (m_chart.points.isEmpty()) {
        m_chart.points.push_back({now, fiatPrice});
    } else if (now - m_chart.points.last().unixMs < 2500) {
        m_chart.points.last().price = fiatPrice;
        m_chart.points.last().unixMs = now;
    } else {
        m_chart.points.push_back({now, fiatPrice});
        // Keep chart from growing forever on long-lived sessions.
        const int cap = m_chartDays <= 1 ? 1500 : 2500;
        if (m_chart.points.size() > cap) {
            m_chart.points.remove(0, m_chart.points.size() - cap);
        }
    }

    if (!m_chartEmitTimer.isActive()) {
        m_chartEmitTimer.start();
    }
}

void PriceService::emitLiveChart()
{
    if (m_chart.valid) {
        emit chartUpdated(m_chart);
    }
}
