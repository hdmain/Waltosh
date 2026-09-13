#include "PriceChartWidget.h"

#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QShowEvent>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QAbstractAnimation>

PriceChartWidget::PriceChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

PriceChartWidget::~PriceChartWidget() = default;

void PriceChartWidget::setChart(const MarketChart& chart)
{
    m_chart = chart;
    update();
}

void PriceChartWidget::setAccentColor(const QColor& upColor, const QColor& downColor, const QColor& gridColor, const QColor& textColor)
{
    m_up = upColor;
    m_down = downColor;
    m_grid = gridColor;
    m_text = textColor;
    update();
}

void PriceChartWidget::setLive(bool live)
{
    if (m_live == live) {
        return;
    }
    m_live = live;
    ensurePulse();
    update();
}

void PriceChartWidget::ensurePulse()
{
    if (!m_live) {
        if (m_pulseAnim) {
            m_pulseAnim->stop();
        }
        m_pulse = 0.0;
        return;
    }
    if (!m_pulseAnim) {
        m_pulseAnim = new QVariantAnimation(this);
        m_pulseAnim->setStartValue(0.0);
        m_pulseAnim->setEndValue(1.0);
        m_pulseAnim->setDuration(1600);
        m_pulseAnim->setLoopCount(-1);
        m_pulseAnim->setEasingCurve(QEasingCurve::InOutSine);
        connect(m_pulseAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_pulse = v.toReal();
            update();
        });
    }
    if (isVisible() && m_pulseAnim->state() != QAbstractAnimation::Running) {
        m_pulseAnim->start();
    }
}

void PriceChartWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensurePulse();
}

void PriceChartWidget::hideEvent(QHideEvent* event)
{
    if (m_pulseAnim) {
        m_pulseAnim->stop();
    }
    QWidget::hideEvent(event);
}

void PriceChartWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area = QRectF(rect()).adjusted(12, 12, -12, -28);
    p.fillRect(rect(), Qt::transparent);

    if (!m_chart.valid || m_chart.points.size() < 2) {
        p.setPen(m_text);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Loading LTC chart…"));
        return;
    }

    double minP = m_chart.points.first().price;
    double maxP = minP;
    for (const ChartPoint& pt : m_chart.points) {
        minP = qMin(minP, pt.price);
        maxP = qMax(maxP, pt.price);
    }
    if (qFuzzyCompare(minP, maxP)) {
        maxP = minP + 1.0;
    }
    const double pad = (maxP - minP) * 0.08;
    minP -= pad;
    maxP += pad;

    const bool up = m_chart.points.last().price >= m_chart.points.first().price;
    const QColor line = up ? m_up : m_down;

    p.setPen(QPen(m_grid, 1.0));
    for (int i = 0; i <= 4; ++i) {
        const qreal y = area.top() + area.height() * (i / 4.0);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    QPainterPath path;
    QPainterPath fill;
    const int n = m_chart.points.size();
    for (int i = 0; i < n; ++i) {
        const qreal x = area.left() + area.width() * (static_cast<qreal>(i) / static_cast<qreal>(n - 1));
        const qreal y = area.bottom() - area.height() * ((m_chart.points[i].price - minP) / (maxP - minP));
        if (i == 0) {
            path.moveTo(x, y);
            fill.moveTo(x, area.bottom());
            fill.lineTo(x, y);
        } else {
            path.lineTo(x, y);
            fill.lineTo(x, y);
        }
        if (i == n - 1) {
            fill.lineTo(x, area.bottom());
            fill.closeSubpath();
        }
    }

    QColor fillColor = line;
    fillColor.setAlpha(48);
    p.fillPath(fill, fillColor);
    p.setPen(QPen(line, 2.2));
    p.drawPath(path);

    const qreal lastX = area.right();
    const qreal lastY =
        area.bottom() - area.height() * ((m_chart.points.last().price - minP) / (maxP - minP));

    if (m_live) {
        QColor halo = line;
        halo.setAlpha(static_cast<int>(40 + 50 * (1.0 - m_pulse)));
        p.setBrush(halo);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(lastX, lastY), 3.5 + 4.0 * m_pulse, 3.5 + 4.0 * m_pulse);
    }

    p.setBrush(line);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(lastX, lastY), 3.5, 3.5);

    p.setPen(m_text);
    const QString liveTag = m_live ? QStringLiteral(" · live") : QString();
    p.drawText(QRectF(12, height() - 22, width() - 24, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("LTC / %1 · %2d%3").arg(m_chart.fiatCode).arg(m_chart.days).arg(liveTag));
    p.drawText(QRectF(12, height() - 22, width() - 24, 18),
               Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 — %2")
                   .arg(minP + pad, 0, 'f', 2)
                   .arg(maxP - pad, 0, 'f', 2));
}
