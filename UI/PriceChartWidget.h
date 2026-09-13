#pragma once

#include "MarketTypes.h"

#include <QColor>
#include <QWidget>

class QVariantAnimation;

class PriceChartWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PriceChartWidget(QWidget* parent = nullptr);
    ~PriceChartWidget() override;

    void setChart(const MarketChart& chart);
    void setAccentColor(const QColor& upColor, const QColor& downColor, const QColor& gridColor, const QColor& textColor);
    void setLive(bool live);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void ensurePulse();

    MarketChart m_chart;
    QColor m_up{46, 204, 113};
    QColor m_down{231, 76, 60};
    QColor m_grid{120, 120, 120, 80};
    QColor m_text{200, 200, 200};
    bool m_live = false;
    qreal m_pulse = 0.0;
    QVariantAnimation* m_pulseAnim = nullptr;
};
