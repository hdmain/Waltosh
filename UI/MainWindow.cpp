#include "MainWindow.h"

#include "CoreBridge.h"
#include "MarketTypes.h"
#include "MnemonicBackupDialog.h"
#include "PriceChartWidget.h"
#include "PriceService.h"

#include <oclero/qlementine/style/ThemeManager.hpp>

#include <QApplication>
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleValidator>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QSvgRenderer>
#include <QSoundEffect>
#include <QSystemTrayIcon>
#include <QTransform>
#include <QUrl>
#include <QWindow>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <thread>
#include <cmath>
#include <functional>

namespace {

constexpr int kNavWidth = 212;
constexpr int kContentRadius = 14;
constexpr int kContentGap = 8;
constexpr int kNavIconSize = 18;

enum class PageIndex : int {
    Gate = 0,
    Overview = 1,
    Charts = 2,
    Receive = 3,
    Send = 4,
    Addresses = 5,
    Sync = 6,
    Settings = 7,
};

struct NavEntry {
    const char* title;
    const char* iconResource;
    PageIndex page;
    bool requiresUnlock;
    bool lockedOnly;
};

constexpr NavEntry kNavEntries[] = {
    {"Login", ":/icons/lock.svg", PageIndex::Gate, false, true},
    {"Overview", ":/icons/wallet.svg", PageIndex::Overview, true, false},
    {"Charts", ":/icons/chart-line.svg", PageIndex::Charts, false, false},
    {"Receive", ":/icons/arrow-down-left.svg", PageIndex::Receive, true, false},
    {"Send", ":/icons/arrow-up-right.svg", PageIndex::Send, true, false},
    {"Addresses", ":/icons/key-round.svg", PageIndex::Addresses, true, false},
    {"Sync", ":/icons/refresh-cw.svg", PageIndex::Sync, true, false},
    {"Settings", ":/icons/settings.svg", PageIndex::Settings, false, false},
};

[[nodiscard]] qreal currentDevicePixelRatio()
{
    if (const QWindow* window = QGuiApplication::focusWindow()) {
        return window->devicePixelRatio();
    }
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        return screen->devicePixelRatio();
    }
    return qApp ? qApp->devicePixelRatio() : 1.0;
}

[[nodiscard]] QIcon tintedSvgIcon(const QString& resourcePath, const QColor& color, int logicalPx)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QByteArray data = file.readAll();
    const QByteArray hex = color.name(QColor::HexRgb).toUtf8();
    data.replace("currentColor", hex);
    data.replace("#000000", hex);
    data.replace("#000", hex);

    QSvgRenderer renderer(data);
    if (!renderer.isValid()) {
        return {};
    }

    const qreal dpr = qMax<qreal>(1.0, currentDevicePixelRatio());
    const int px = qMax(1, qRound(logicalPx * dpr));
    QPixmap pixmap(px, px);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    renderer.render(&painter);
    painter.end();
    return QIcon(pixmap);
}

[[nodiscard]] QIcon appWindowIcon()
{
    QIcon icon(QStringLiteral(":/app/icon.png"));
    if (icon.isNull() || icon.availableSizes().isEmpty()) {
        icon = QIcon(QStringLiteral(":/app/waltosh.ico"));
    }
    return icon;
}

[[nodiscard]] QString formatLtc(int64_t sats)
{
    const bool neg = sats < 0;
    uint64_t v = static_cast<uint64_t>(neg ? -sats : sats);
    const uint64_t whole = v / 100000000ULL;
    const uint64_t frac = v % 100000000ULL;
    QString s = QStringLiteral("%1.%2")
                    .arg(whole)
                    .arg(frac, 8, 10, QLatin1Char('0'));
    while (s.endsWith(QLatin1Char('0')) && s.contains(QLatin1Char('.'))) {
        s.chop(1);
    }
    if (s.endsWith(QLatin1Char('.'))) {
        s.chop(1);
    }
    if (neg) {
        s.prepend(QLatin1Char('-'));
    }
    return s + QStringLiteral(" LTC");
}

[[nodiscard]] QString defaultWalletDataDir()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    const QString walletDir = root + QStringLiteral("/wallet");
    QDir().mkpath(walletDir);
    return walletDir;
}

void styleLargeCombo(QComboBox* combo, int minWidth = 280)
{
    if (!combo) {
        return;
    }
    combo->setMinimumWidth(minWidth);
    combo->setMinimumHeight(40);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    // Show many rows when opened (default popup was clipping to ~1 item).
    combo->setMaxVisibleItems(16);
    if (QAbstractItemView* view = combo->view()) {
        view->setMinimumWidth(minWidth);
        view->setMinimumHeight(220);
        view->setTextElideMode(Qt::ElideNone);
        // Ensure list items are tall enough to read, but leave room for several rows.
        view->setStyleSheet(QStringLiteral(
            "QAbstractItemView {"
            "  min-height: 220px;"
            "  padding: 4px;"
            "}"
            "QAbstractItemView::item {"
            "  min-height: 34px;"
            "  padding: 6px 10px;"
            "}"));
    }
}

[[nodiscard]] QWidget* makeSurface(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("surfacePanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    return panel;
}

[[nodiscard]] QLabel* makeCaption(QWidget* parent, const QString& text)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("fieldCaption"));
    return label;
}

[[nodiscard]] QWidget* makeMetricTile(QWidget* parent, const QString& caption, QLabel** valueOut)
{
    auto* tile = new QWidget(parent);
    tile->setObjectName(QStringLiteral("metricTile"));
    tile->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(tile);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);
    auto* cap = new QLabel(caption, tile);
    cap->setObjectName(QStringLiteral("metricCaption"));
    auto* value = new QLabel(QStringLiteral("-"), tile);
    value->setObjectName(QStringLiteral("metricValue"));
    value->setWordWrap(true);
    layout->addWidget(cap);
    layout->addWidget(value);
    if (valueOut) {
        *valueOut = value;
    }
    return tile;
}

[[nodiscard]] QWidget* makeLabeledField(QWidget* parent, const QString& caption, QWidget* field)
{
    auto* wrap = new QWidget(parent);
    auto* layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(makeCaption(wrap, caption));
    layout->addWidget(field);
    return wrap;
}

struct TxRowData {
    QString kind;
    QString amount;
    QString preview;
    QString txid;
    QString address;
    QString date;
    QString status;
    QString height;
    QString outputs;
    bool outgoing = false;
};

class CopyValueLabel final : public QLabel {
public:
    using QLabel::QLabel;

    std::function<void()> onClicked;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && onClicked && rect().contains(event->pos())) {
            onClicked();
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }
};

class TxActivityRow final : public QWidget {
public:
    std::function<void()> onSizeChanged;
    std::function<void(const QString& what, const QString& value)> onCopy;
    std::function<void(const QString& txid, bool expanded)> onExpandedChanged;

    explicit TxActivityRow(QWidget* parent, const TxRowData& data)
        : QWidget(parent)
        , m_txid(data.txid)
    {
        setObjectName(QStringLiteral("txRow"));
        setAttribute(Qt::WA_StyledBackground, true);
        setCursor(Qt::PointingHandCursor);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_header = new QWidget(this);
        m_header->setObjectName(QStringLiteral("txRowHeader"));
        auto* headerLayout = new QHBoxLayout(m_header);
        headerLayout->setContentsMargins(0, 10, 0, 10);
        headerLayout->setSpacing(12);

        auto* textCol = new QVBoxLayout;
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(2);
        auto* kindLabel = new QLabel(data.kind, m_header);
        kindLabel->setObjectName(QStringLiteral("txRowTitle"));
        auto* detailLabel = new QLabel(data.preview, m_header);
        detailLabel->setObjectName(QStringLiteral("txRowDetail"));
        textCol->addWidget(kindLabel);
        textCol->addWidget(detailLabel);

        auto* amountLabel = new QLabel(data.amount, m_header);
        amountLabel->setObjectName(data.outgoing ? QStringLiteral("txAmountOut")
                                                  : QStringLiteral("txAmountIn"));
        amountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_chevron = new QLabel(m_header);
        m_chevron->setObjectName(QStringLiteral("txRowChevron"));
        m_chevron->setFixedSize(16, 16);
        m_chevron->setPixmap(
            tintedSvgIcon(QStringLiteral(":/icons/chevron-down-light.svg"), QColor(154, 163, 178), 14)
                .pixmap(QSize(14, 14)));

        headerLayout->addLayout(textCol, 1);
        headerLayout->addWidget(amountLabel, 0, Qt::AlignVCenter);
        headerLayout->addWidget(m_chevron, 0, Qt::AlignVCenter);

        m_details = new QWidget(this);
        m_details->setObjectName(QStringLiteral("txDetailPanel"));
        m_details->setVisible(false);
        auto* detailsLayout = new QVBoxLayout(m_details);
        detailsLayout->setContentsMargins(0, 0, 0, 12);
        detailsLayout->setSpacing(8);

        const auto addPlain = [&](const QString& key, const QString& value) {
            if (value.isEmpty()) {
                return;
            }
            auto* row = new QWidget(m_details);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(12);
            auto* keyLab = new QLabel(key, row);
            keyLab->setObjectName(QStringLiteral("txDetailKey"));
            keyLab->setFixedWidth(72);
            auto* valLab = new QLabel(value, row);
            valLab->setObjectName(QStringLiteral("txDetailValue"));
            valLab->setWordWrap(true);
            rowLayout->addWidget(keyLab, 0, Qt::AlignTop);
            rowLayout->addWidget(valLab, 1);
            detailsLayout->addWidget(row);
        };

        const auto addCopyable = [&](const QString& key, const QString& value) {
            if (value.isEmpty()) {
                return;
            }
            auto* row = new QWidget(m_details);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(12);
            auto* keyLab = new QLabel(key, row);
            keyLab->setObjectName(QStringLiteral("txDetailKey"));
            keyLab->setFixedWidth(72);
            auto* valLab = new CopyValueLabel(value, row);
            valLab->setObjectName(QStringLiteral("txCopyValue"));
            valLab->setWordWrap(true);
            valLab->setCursor(Qt::PointingHandCursor);
            valLab->setToolTip(QStringLiteral("Click to copy"));
            valLab->onClicked = [this, key, value]() {
                if (onCopy) {
                    onCopy(key, value);
                }
            };
            rowLayout->addWidget(keyLab, 0, Qt::AlignTop);
            rowLayout->addWidget(valLab, 1);
            detailsLayout->addWidget(row);
        };

        addPlain(QStringLiteral("Amount"), data.amount);
        addPlain(QStringLiteral("Status"), data.status);
        addPlain(QStringLiteral("Date"), data.date);
        addPlain(QStringLiteral("Height"), data.height);
        if (!data.outputs.isEmpty()) {
            addPlain(QStringLiteral("Outputs"), data.outputs);
        }
        addCopyable(QStringLiteral("Txid"), data.txid);
        addCopyable(QStringLiteral("Address"), data.address);

        root->addWidget(m_header);
        root->addWidget(m_details);

        m_details->setMaximumHeight(0);
        m_details->setVisible(true);
    }

    void setExpanded(bool expanded, bool animate = true)
    {
        if (m_expanded == expanded && animate) {
            return;
        }
        m_expanded = expanded;
        if (onExpandedChanged) {
            onExpandedChanged(m_txid, expanded);
        }

        if (m_chevron) {
            const qreal angle = expanded ? 0.0 : -90.0;
            QPixmap src =
                tintedSvgIcon(QStringLiteral(":/icons/chevron-down-light.svg"), QColor(154, 163, 178), 14)
                    .pixmap(QSize(14, 14));
            QTransform t;
            t.rotate(angle);
            m_chevron->setPixmap(src.transformed(t, Qt::SmoothTransformation));
        }

        m_details->setVisible(true);
        const int target = expanded ? qMax(1, m_details->sizeHint().height()) : 0;

        if (!animate) {
            m_details->setMaximumHeight(target);
            adjustSize();
            if (onSizeChanged) {
                onSizeChanged();
            }
            return;
        }

        auto* anim = new QPropertyAnimation(m_details, "maximumHeight", this);
        anim->setDuration(180);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setStartValue(m_details->maximumHeight());
        anim->setEndValue(target);
        connect(anim, &QPropertyAnimation::valueChanged, this, [this]() {
            adjustSize();
            if (onSizeChanged) {
                onSizeChanged();
            }
        });
        connect(anim, &QPropertyAnimation::finished, this, [this, anim, expanded]() {
            anim->deleteLater();
            if (!expanded) {
                m_details->setMaximumHeight(0);
            }
            adjustSize();
            if (onSizeChanged) {
                onSizeChanged();
            }
        });
        anim->start();
    }

    [[nodiscard]] QString txid() const { return m_txid; }

    [[nodiscard]] bool isExpanded() const { return m_expanded; }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_header
            && m_header->geometry().contains(event->pos())) {
            setExpanded(!m_expanded);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    QWidget* m_header = nullptr;
    QWidget* m_details = nullptr;
    QLabel* m_chevron = nullptr;
    QString m_txid;
    bool m_expanded = false;
};

} // namespace

class NavItemDelegate final : public QStyledItemDelegate {
public:
    explicit NavItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void setColors(const QColor& hover, const QColor& selected, const QColor& accent, const QColor& text)
    {
        m_hover = hover;
        m_selected = selected;
        m_accent = accent;
        m_text = text;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->setRenderHint(QPainter::TextAntialiasing, true);

        const QRect pill = opt.rect.adjusted(6, 2, -6, -2);
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = opt.state & QStyle::State_MouseOver;

        if (selected || hovered) {
            QPainterPath path;
            path.addRoundedRect(pill, 8, 8);
            painter->fillPath(path, selected ? m_selected : m_hover);
        }

        if (selected) {
            const QRectF bar(pill.left() + 4, pill.top() + 8, 3, pill.height() - 16);
            QPainterPath accent;
            accent.addRoundedRect(bar, 1.5, 1.5);
            painter->fillPath(accent, m_accent);
        }

        const int iconBox = 18;
        const QRect iconRect(pill.left() + 14, pill.center().y() - iconBox / 2, iconBox, iconBox);
        if (!opt.icon.isNull()) {
            opt.icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::On);
        }

        const QRect textRect = pill.adjusted(iconBox + 22, 0, -10, 0);
        painter->setPen(m_text);
        QFont font = opt.font;
        font.setWeight(selected ? QFont::DemiBold : QFont::Medium);
        font.setPixelSize(13);
        painter->setFont(font);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, opt.text);

        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return {180, 36};
    }

private:
    QColor m_hover{255, 255, 255, 20};
    QColor m_selected{255, 255, 255, 31};
    QColor m_accent{61, 139, 253};
    QColor m_text{255, 255, 255};
};

MainWindow::MainWindow(
    oclero::qlementine::QlementineStyle* style,
    oclero::qlementine::ThemeManager* themeManager,
    QWidget* parent)
    : QMainWindow(parent)
    , m_style(style)
    , m_themeManager(themeManager)
    , m_core(new CoreBridge(defaultWalletDataDir(), this))
    , m_prices(new PriceService(this))
    , m_capability(MicaEffect::probe())
{
    setWindowTitle(QStringLiteral("WALTOSH"));
    {
        const QIcon winIcon = appWindowIcon();
        if (!winIcon.isNull()) {
            setWindowIcon(winIcon);
        }
    }
    resize(1040, 680);
    setMinimumSize(760, 520);

    m_sendSound = new QSoundEffect(this);
    m_sendSound->setSource(QUrl(QStringLiteral("qrc:/sounds/send.wav")));
    m_sendSound->setVolume(0.7);
    m_receiveSound = new QSoundEffect(this);
    m_receiveSound->setSource(QUrl(QStringLiteral("qrc:/sounds/receive.wav")));
    m_receiveSound->setVolume(0.75);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(this);
        QIcon trayIcon = windowIcon();
        if (trayIcon.isNull()) {
            trayIcon = appWindowIcon();
        }
        if (trayIcon.isNull()) {
            trayIcon = QIcon(QStringLiteral(":/icons/wallet.svg"));
        }
        m_trayIcon->setIcon(trayIcon);
        m_trayIcon->setToolTip(QStringLiteral("WALTOSH"));
        m_trayIcon->show();
    }

    rebuildUi();
    loadPersistedSettings();
    applyTheme(m_darkModeCheck ? m_darkModeCheck->isChecked() : true);
    applyShellMode(m_capability.supported);
    setUnlockedUi(false);
    applySnapshot(m_core->snapshot());
    applyMarketQuote(m_prices->quote());
    applyMarketChart(m_prices->chart());

    if (QWindow* win = windowHandle()) {
        connect(win, &QWindow::screenChanged, this, [this](QScreen*) {
            applyTheme(m_darkModeCheck ? m_darkModeCheck->isChecked() : true);
        });
    } else {
        // winId/windowHandle may appear only after show; refresh icons on first show via applyTheme.
    }

    connect(m_core, &CoreBridge::opened, this, [this]() {
        setUnlockedUi(true);
        showMessage(QStringLiteral("Wallet unlocked. SPV sync started on its own thread."));
    });
    connect(m_core, &CoreBridge::locked, this, [this]() {
        m_vanitySearching = false;
        setUnlockedUi(false);
        m_requestedDefaultNested = false;
        showMessage(QStringLiteral("Wallet locked."));
    });
    connect(m_core, &CoreBridge::snapshotUpdated, this, &MainWindow::applySnapshot);
    connect(m_core, &CoreBridge::busyChanged, this, &MainWindow::setBusyUi);
    connect(m_core, &CoreBridge::balanceChanged, this, [this](qint64 newBalance, qint64 delta) {
        const QString sign = delta > 0 ? QStringLiteral("+") : QString();
        showMessage(QStringLiteral("Balance updated: %1 (%2%3)")
                        .arg(formatLtc(newBalance), sign, formatLtc(delta)));
        if (delta > 0) {
            playReceiveSound();
            notifyPaymentReceived(delta);
        }
    });
    connect(m_core, &CoreBridge::errorOccurred, this, [this](const QString& msg) {
        showMessage(msg, true);
        if (m_vanitySearching) {
            m_vanitySearching = false;
            if (m_vanityCancelBtn) {
                m_vanityCancelBtn->setEnabled(false);
            }
            if (m_vanityFindBtn) {
                m_vanityFindBtn->setEnabled(m_core && m_core->isOpen() && !m_core->isBusy());
            }
            if (m_vanityStatusLabel) {
                m_vanityStatusLabel->setText(msg);
            }
        }
    });
    connect(m_prices, &PriceService::quoteUpdated, this, &MainWindow::applyMarketQuote);
    connect(m_prices, &PriceService::chartUpdated, this, &MainWindow::applyMarketChart);
    connect(m_prices, &PriceService::liveStatusChanged, this, [this](bool connected) {
        if (m_priceChart) {
            m_priceChart->setLive(connected);
        }
    });
    connect(m_prices, &PriceService::errorOccurred, this, [this](const QString& msg) {
        // Soft-fail market data so wallet UI stays usable offline.
        if (m_spotPriceLabel) {
            m_spotPriceLabel->setText(QStringLiteral("Market: unavailable"));
        }
        Q_UNUSED(msg);
    });
    connect(m_core, &CoreBridge::walletCreated, this, [this](const QString& mnemonic) {
        MnemonicBackupDialog dlg(mnemonic, this);
        dlg.exec();
        // Best-effort wipe of the queued signal copy we hold.
        QString wipe = mnemonic;
        for (QChar& ch : wipe) {
            ch = QChar(0);
        }
        if (m_passwordEdit) {
            m_passwordEdit->clear();
        }
        if (m_mnemonicEdit) {
            m_mnemonicEdit->clear();
        }
        if (m_restoreBtn) {
            m_restoreBtn->setChecked(false);
        }
    });
    connect(m_core, &CoreBridge::walletImported, this, [this]() {
        m_passwordEdit->clear();
        m_mnemonicEdit->clear();
        if (m_restoreBtn) {
            m_restoreBtn->setChecked(false);
        }
        showMessage(QStringLiteral("Mnemonic imported."));
    });
    connect(m_core, &CoreBridge::addressReady, this, [this](const QString& address) {
        setOverviewReceiveAddress(address);
        if (m_receiveAddressEdit) {
            m_receiveAddressEdit->setText(address);
        }
        if (m_vanitySearching) {
            m_vanitySearching = false;
            if (m_vanityStatusLabel) {
                m_vanityStatusLabel->setText(QStringLiteral("Found: %1").arg(address));
            }
            if (m_vanityCancelBtn) {
                m_vanityCancelBtn->setEnabled(false);
            }
            if (m_vanityFindBtn) {
                m_vanityFindBtn->setEnabled(true);
            }
        }
        showMessage(QStringLiteral("New receive address ready."));
    });
    connect(m_core, &CoreBridge::vanityProgress, this, [this](qint64 tried, int elapsedSec) {
        if (m_vanityStatusLabel) {
            const double rate = elapsedSec > 0 ? (static_cast<double>(tried) / elapsedSec) : 0.0;
            m_vanityStatusLabel->setText(
                QStringLiteral("Searching… %1 tried · %2s / 300s · %3 k/s (multi-core)")
                    .arg(tried)
                    .arg(elapsedSec)
                    .arg(rate / 1000.0, 0, 'f', 1));
        }
    });
    connect(m_core, &CoreBridge::sendFinished, this, [this](const QString& txid) {
        m_pendingSendAmountSats = 0;
        m_pendingSendAddress.clear();
        playSendSound();
        showMessage(QStringLiteral("Broadcast: %1").arg(txid));
        m_sendAmountEdit->clear();
        refreshTxHistory(m_core->snapshot());
    });
}

void MainWindow::applyShellMode(bool translucent)
{
    m_backdropActive = translucent;

    setAttribute(Qt::WA_TranslucentBackground, translucent);
    setAttribute(Qt::WA_NoSystemBackground, translucent);

    if (m_shell) {
        m_shell->setAttribute(Qt::WA_TranslucentBackground, translucent);
        m_shell->setAutoFillBackground(!translucent);
    }
    if (m_nav) {
        m_nav->setAttribute(Qt::WA_TranslucentBackground, translucent);
        m_nav->setAutoFillBackground(!translucent);
        m_nav->setAttribute(Qt::WA_StyledBackground, !translucent);
    }

    update();
}

void MainWindow::rebuildUi()
{
    m_shell = new QWidget(this);
    setCentralWidget(m_shell);

    auto* root = new QHBoxLayout(m_shell);
    root->setContentsMargins(0, kContentGap, kContentGap, kContentGap);
    root->setSpacing(0);

    m_nav = new QWidget(m_shell);
    m_nav->setObjectName(QStringLiteral("navPanel"));
    m_nav->setFixedWidth(kNavWidth);

    auto* navLayout = new QVBoxLayout(m_nav);
    navLayout->setContentsMargins(10, 14, 10, 12);
    navLayout->setSpacing(2);

    auto* brandRow = new QHBoxLayout;
    brandRow->setSpacing(10);
    brandRow->setContentsMargins(8, 0, 8, 0);
    m_brandMark = new QLabel(m_nav);
    m_brandMark->setObjectName(QStringLiteral("brandMark"));
    m_brandMark->setFixedSize(26, 26);
    m_brandMark->setScaledContents(false);
    m_brandMark->setAlignment(Qt::AlignCenter);
    auto* brandCol = new QVBoxLayout;
    brandCol->setSpacing(0);
    brandCol->setContentsMargins(0, 0, 0, 0);
    m_brandLabel = new QLabel(QStringLiteral("WALTOSH"), m_nav);
    m_brandLabel->setObjectName(QStringLiteral("brandLabel"));
    auto* brandSub = new QLabel(QStringLiteral("Litecoin wallet"), m_nav);
    brandSub->setObjectName(QStringLiteral("brandSub"));
    brandCol->addWidget(m_brandLabel);
    brandCol->addWidget(brandSub);
    brandRow->addWidget(m_brandMark, 0, Qt::AlignVCenter);
    brandRow->addLayout(brandCol, 1);
    navLayout->addLayout(brandRow);
    navLayout->addSpacing(10);

    m_navList = new QListWidget(m_nav);
    m_navList->setObjectName(QStringLiteral("navList"));
    m_navList->setFrameShape(QFrame::NoFrame);
    m_navList->setFocusPolicy(Qt::NoFocus);
    m_navList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_navList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_navList->setIconSize(QSize(kNavIconSize, kNavIconSize));
    m_navList->setSpacing(1);
    m_navList->setUniformItemSizes(true);
    m_navDelegate = new NavItemDelegate(m_navList);
    m_navList->setItemDelegate(m_navDelegate);

    for (const NavEntry& entry : kNavEntries) {
        auto* item = new QListWidgetItem(QString::fromUtf8(entry.title));
        item->setData(Qt::UserRole, QString::fromUtf8(entry.iconResource));
        item->setData(Qt::UserRole + 1, static_cast<int>(entry.page));
        item->setData(Qt::UserRole + 2, entry.requiresUnlock);
        item->setData(Qt::UserRole + 3, entry.lockedOnly);
        m_navList->addItem(item);
    }
    navLayout->addWidget(m_navList, 1);

    m_footerStatus = new QLabel(m_nav);
    m_footerStatus->setObjectName(QStringLiteral("footerStatus"));
    m_footerStatus->setWordWrap(true);
    navLayout->addWidget(m_footerStatus);

    m_content = new QWidget(m_shell);
    m_content->setObjectName(QStringLiteral("contentPanel"));
    m_content->setAttribute(Qt::WA_StyledBackground, true);
    m_content->setAttribute(Qt::WA_TranslucentBackground, false);
    m_content->setAutoFillBackground(false);

    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(24, 22, 24, 20);
    contentLayout->setSpacing(10);

    m_pages = new QStackedWidget(m_content);
    m_gatePage = new QWidget(m_pages);
    auto* overviewPage = new QWidget(m_pages);
    auto* chartsPage = new QWidget(m_pages);
    auto* receivePage = new QWidget(m_pages);
    auto* sendPage = new QWidget(m_pages);
    auto* addressesPage = new QWidget(m_pages);
    auto* syncPage = new QWidget(m_pages);
    auto* settingsPage = new QWidget(m_pages);

    rebuildGateUi(m_gatePage);
    rebuildOverview(overviewPage);
    rebuildCharts(chartsPage);
    rebuildReceive(receivePage);
    rebuildSend(sendPage);
    rebuildAddresses(addressesPage);
    rebuildSync(syncPage);
    rebuildSettings(settingsPage);

    m_pages->addWidget(m_gatePage);
    m_pages->addWidget(overviewPage);
    m_pages->addWidget(chartsPage);
    m_pages->addWidget(receivePage);
    m_pages->addWidget(sendPage);
    m_pages->addWidget(addressesPage);
    m_pages->addWidget(syncPage);
    m_pages->addWidget(settingsPage);

    m_toastLabel = new QLabel(m_content);
    m_toastLabel->setObjectName(QStringLiteral("toastLabel"));
    m_toastLabel->setWordWrap(true);

    contentLayout->addWidget(m_pages, 1);
    contentLayout->addWidget(m_toastLabel);

    root->addWidget(m_nav);
    root->addWidget(m_content, 1);

    connect(m_backdropCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_loadingSettings) {
            persistUiSettings();
        }
        reapplyBackdrop();
    });
    connect(m_darkModeCheck, &QCheckBox::toggled, this, [this](bool dark) {
        if (!m_loadingSettings) {
            persistUiSettings();
        }
        applyTheme(dark);
        reapplyBackdrop();
    });
    connect(m_fiatCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_loadingSettings) {
            onFiatChanged();
        }
    });
    connect(m_chartRangeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_loadingSettings) {
            onChartRangeChanged();
        }
    });
    connect(m_navList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || !m_pages) {
            return;
        }
        QListWidgetItem* item = m_navList->item(row);
        if (item->isHidden()) {
            return;
        }
        const bool needsUnlock = item->data(Qt::UserRole + 2).toBool();
        if (needsUnlock && !m_core->isOpen()) {
            const int loginRow = navRowForPage(static_cast<int>(PageIndex::Gate));
            if (loginRow >= 0) {
                m_navList->setCurrentRow(loginRow);
            }
            showPage(static_cast<int>(PageIndex::Gate));
            showMessage(QStringLiteral("Unlock or create a wallet first."), true);
            return;
        }
        showPage(item->data(Qt::UserRole + 1).toInt());
        applySnapshot(m_core->snapshot());
    });
}

void MainWindow::rebuildGateUi(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Login"), page);
    title->setObjectName(QStringLiteral("contentTitle"));

    QWidget* panel = makeSurface(page);
    auto* panelLayout = qobject_cast<QVBoxLayout*>(panel->layout());

    m_gateHint = new QLabel(panel);
    m_gateHint->setObjectName(QStringLiteral("contentBody"));
    m_gateHint->setWordWrap(true);

    m_passwordEdit = new QLineEdit(panel);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("Wallet password"));

    m_mnemonicPanel = new QWidget(panel);
    auto* mnemonicLayout = new QVBoxLayout(m_mnemonicPanel);
    mnemonicLayout->setContentsMargins(0, 8, 0, 0);
    mnemonicLayout->setSpacing(8);

    m_mnemonicEdit = new QPlainTextEdit(m_mnemonicPanel);
    m_mnemonicEdit->setPlaceholderText(QStringLiteral("BIP39 mnemonic (12/24 words)"));
    m_mnemonicEdit->setFixedHeight(88);

    m_importBtn = new QPushButton(QStringLiteral("Import mnemonic"), m_mnemonicPanel);
    m_importBtn->setObjectName(QStringLiteral("secondaryButton"));

    mnemonicLayout->addWidget(
        makeLabeledField(m_mnemonicPanel, QStringLiteral("Mnemonic"), m_mnemonicEdit));
    mnemonicLayout->addWidget(m_importBtn, 0, Qt::AlignLeft);
    m_mnemonicPanel->setVisible(false);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    m_unlockBtn = new QPushButton(QStringLiteral("Unlock"), panel);
    m_unlockBtn->setObjectName(QStringLiteral("primaryButton"));
    m_unlockBtn->setDefault(true);
    m_unlockBtn->setAutoDefault(true);
    m_createBtn = new QPushButton(QStringLiteral("Create wallet"), panel);
    m_createBtn->setObjectName(QStringLiteral("secondaryButton"));
    m_restoreBtn = new QPushButton(QStringLiteral("Restore / Backup"), panel);
    m_restoreBtn->setObjectName(QStringLiteral("secondaryButton"));
    m_restoreBtn->setCheckable(true);
    m_restoreBtn->setToolTip(
        QStringLiteral("Show mnemonic field to restore a wallet from backup words"));
    row->addWidget(m_unlockBtn);
    row->addWidget(m_createBtn);
    row->addWidget(m_restoreBtn);
    row->addStretch(1);

    panelLayout->addWidget(m_gateHint);
    panelLayout->addWidget(makeLabeledField(panel, QStringLiteral("Password"), m_passwordEdit));
    panelLayout->addLayout(row);
    panelLayout->addWidget(m_mnemonicPanel);

    layout->addWidget(title);
    layout->addWidget(panel);
    layout->addStretch(1);

    connect(m_unlockBtn, &QPushButton::clicked, this, [this]() {
        if (m_vanitySearching) {
            m_vanitySearching = false;
            if (m_core) {
                m_core->cancelVanity();
            }
        }
        const QString pass = m_passwordEdit->text();
        m_core->unlock(pass);
        if (m_passwordEdit) {
            m_passwordEdit->clear();
        }
    });
    connect(m_createBtn, &QPushButton::clicked, this, [this]() {
        if (m_core) {
            m_core->cancelVanity();
        }
        m_vanitySearching = false;
        const QString pass = m_passwordEdit->text();
        m_core->createWallet(pass);
        if (m_passwordEdit) {
            m_passwordEdit->clear();
        }
    });
    connect(m_restoreBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_mnemonicPanel) {
            m_mnemonicPanel->setVisible(on);
        }
        if (on && m_mnemonicEdit) {
            m_mnemonicEdit->setFocus();
        } else if (m_mnemonicEdit) {
            m_mnemonicEdit->clear();
        }
        if (m_restoreBtn) {
            m_restoreBtn->setText(on ? QStringLiteral("Hide mnemonic")
                                     : QStringLiteral("Restore / Backup"));
        }
    });
    connect(m_importBtn, &QPushButton::clicked, this, [this]() {
        if (m_core) {
            m_core->cancelVanity();
        }
        m_vanitySearching = false;
        const QString mnemonic = m_mnemonicEdit->toPlainText();
        const QString pass = m_passwordEdit->text();
        m_core->importWallet(mnemonic, pass);
        if (m_mnemonicEdit) {
            m_mnemonicEdit->clear();
        }
        if (m_passwordEdit) {
            m_passwordEdit->clear();
        }
        if (m_restoreBtn) {
            m_restoreBtn->setChecked(false);
        }
    });
}

void MainWindow::rebuildOverview(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("Overview"), page);
    title->setObjectName(QStringLiteral("contentTitle"));

    m_balanceLabel = new QLabel(QStringLiteral("0 LTC"), page);
    m_balanceLabel->setObjectName(QStringLiteral("balanceLabel"));

    // One quiet meta line instead of three boxed metric tiles.
    auto* metaRow = new QHBoxLayout;
    metaRow->setContentsMargins(0, 6, 0, 0);
    metaRow->setSpacing(0);

    m_fiatBalanceLabel = new QLabel(QStringLiteral("-"), page);
    m_fiatBalanceLabel->setObjectName(QStringLiteral("balanceMeta"));

    auto* dot1 = new QLabel(QStringLiteral("  ·  "), page);
    dot1->setObjectName(QStringLiteral("balanceMetaSep"));

    m_change24hLabel = new QLabel(QStringLiteral("-"), page);
    m_change24hLabel->setObjectName(QStringLiteral("change24hLabel"));

    auto* dot2 = new QLabel(QStringLiteral("  ·  "), page);
    dot2->setObjectName(QStringLiteral("balanceMetaSep"));

    m_spotPriceLabel = new QLabel(QStringLiteral("-"), page);
    m_spotPriceLabel->setObjectName(QStringLiteral("balanceMeta"));

    metaRow->addWidget(m_fiatBalanceLabel);
    metaRow->addWidget(dot1);
    metaRow->addWidget(m_change24hLabel);
    metaRow->addWidget(dot2);
    metaRow->addWidget(m_spotPriceLabel);
    metaRow->addStretch(1);

    auto* receiveCaption = new QLabel(QStringLiteral("Receive address"), page);
    receiveCaption->setObjectName(QStringLiteral("fieldCaption"));

    m_overviewAddressLabel = new QLabel(QStringLiteral("Unlock to show an address"), page);
    m_overviewAddressLabel->setObjectName(QStringLiteral("overviewAddress"));
    m_overviewAddressLabel->setWordWrap(true);
    m_overviewAddressLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* addrActions = new QHBoxLayout;
    addrActions->setContentsMargins(0, 6, 0, 0);
    addrActions->setSpacing(8);

    m_overviewReloadBtn = new QPushButton(page);
    m_overviewReloadBtn->setObjectName(QStringLiteral("iconButton"));
    m_overviewReloadBtn->setFixedSize(36, 36);
    m_overviewReloadBtn->setCursor(Qt::PointingHandCursor);
    m_overviewReloadBtn->setToolTip(QStringLiteral("New address"));
    m_overviewReloadBtn->setEnabled(false);

    m_overviewCopyBtn = new QPushButton(page);
    m_overviewCopyBtn->setObjectName(QStringLiteral("iconButton"));
    m_overviewCopyBtn->setFixedSize(36, 36);
    m_overviewCopyBtn->setCursor(Qt::PointingHandCursor);
    m_overviewCopyBtn->setToolTip(QStringLiteral("Copy address"));
    m_overviewCopyBtn->setEnabled(false);

    addrActions->addWidget(m_overviewReloadBtn);
    addrActions->addWidget(m_overviewCopyBtn);
    addrActions->addStretch(1);

    auto* rule = new QFrame(page);
    rule->setObjectName(QStringLiteral("hairline"));
    rule->setFrameShape(QFrame::HLine);
    rule->setFixedHeight(1);

    auto* activityHeader = new QHBoxLayout;
    activityHeader->setContentsMargins(0, 0, 0, 0);
    activityHeader->setSpacing(10);

    m_txHistoryTitle = new QLabel(QStringLiteral("Activity"), page);
    m_txHistoryTitle->setObjectName(QStringLiteral("sectionTitle"));

    auto* overviewHardRefresh = new QPushButton(page);
    overviewHardRefresh->setObjectName(QStringLiteral("iconButton"));
    overviewHardRefresh->setFixedSize(36, 36);
    overviewHardRefresh->setCursor(Qt::PointingHandCursor);
    overviewHardRefresh->setToolTip(
        QStringLiteral("Hard refresh - recheck headers and recent blocks"));
    overviewHardRefresh->setEnabled(false);
    m_hardRefreshBtn = overviewHardRefresh;

    activityHeader->addWidget(m_txHistoryTitle, 0, Qt::AlignVCenter);
    activityHeader->addStretch(1);

    m_refreshStatusLabel = new QLabel(QStringLiteral(""), page);
    m_refreshStatusLabel->setObjectName(QStringLiteral("refreshStatus"));
    m_refreshStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    activityHeader->addWidget(m_refreshStatusLabel, 0, Qt::AlignVCenter);
    activityHeader->addWidget(overviewHardRefresh, 0, Qt::AlignVCenter);

    m_txHistoryList = new QListWidget(page);
    m_txHistoryList->setObjectName(QStringLiteral("txHistoryList"));
    m_txHistoryList->setFrameShape(QFrame::NoFrame);
    m_txHistoryList->setFocusPolicy(Qt::NoFocus);
    m_txHistoryList->setSelectionMode(QAbstractItemView::NoSelection);
    m_txHistoryList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_txHistoryList->setUniformItemSizes(false);
    m_txHistoryList->setSpacing(0);
    m_txHistoryList->setMinimumHeight(200);

    layout->addWidget(title);
    layout->addSpacing(18);
    layout->addWidget(m_balanceLabel);
    layout->addLayout(metaRow);
    layout->addSpacing(18);
    layout->addWidget(receiveCaption);
    layout->addSpacing(4);
    layout->addWidget(m_overviewAddressLabel);
    layout->addLayout(addrActions);
    layout->addSpacing(18);
    layout->addWidget(rule);
    layout->addSpacing(18);
    layout->addLayout(activityHeader);
    layout->addSpacing(8);
    layout->addWidget(m_txHistoryList, 1);

    connect(m_overviewReloadBtn, &QPushButton::clicked, this, [this]() {
        if (!m_core || !m_core->isOpen()) {
            showMessage(QStringLiteral("Unlock the wallet first."), true);
            return;
        }
        const int typeIndex = m_addrTypeCombo ? m_addrTypeCombo->currentIndex() : 0;
        m_core->newAddress(typeIndex);
    });
    connect(overviewHardRefresh, &QPushButton::clicked, this, [this]() {
        if (!m_core || !m_core->isOpen()) {
            showMessage(QStringLiteral("Unlock the wallet first."), true);
            return;
        }
        m_core->hardRefresh();
        showMessage(QStringLiteral("Hard refresh queued…"));
    });
    connect(m_overviewCopyBtn, &QPushButton::clicked, this, [this]() {
        const QString addr = m_overviewAddressLabel ? m_overviewAddressLabel->text().trimmed() : QString();
        if (addr.isEmpty() || addr == QLatin1String("Unlock to show an address")
            || addr == QLatin1String("-")) {
            showMessage(QStringLiteral("No address to copy."), true);
            return;
        }
        if (QClipboard* clip = QGuiApplication::clipboard()) {
            clip->setText(addr);
            showMessage(QStringLiteral("Copied address"));
        }
    });
}

void MainWindow::rebuildCharts(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Charts"), page);
    title->setObjectName(QStringLiteral("contentTitle"));

    QWidget* headerPanel = makeSurface(page);
    auto* headerLayout = qobject_cast<QVBoxLayout*>(headerPanel->layout());

    m_chartHeaderLabel = new QLabel(QStringLiteral("LTC price"), headerPanel);
    m_chartHeaderLabel->setObjectName(QStringLiteral("sectionTitle"));

    m_chartChangeLabel = new QLabel(headerPanel);
    m_chartChangeLabel->setObjectName(QStringLiteral("change24hLabel"));

    m_chartRangeCombo = new QComboBox(headerPanel);
    styleLargeCombo(m_chartRangeCombo, 280);
    m_chartRangeCombo->addItem(QStringLiteral("24 hours"), 1);
    m_chartRangeCombo->addItem(QStringLiteral("7 days"), 7);
    m_chartRangeCombo->addItem(QStringLiteral("30 days"), 30);
    m_chartRangeCombo->addItem(QStringLiteral("90 days"), 90);
    m_chartRangeCombo->setCurrentIndex(1);

    headerLayout->addWidget(m_chartHeaderLabel);
    headerLayout->addWidget(m_chartChangeLabel);
    headerLayout->addWidget(makeLabeledField(headerPanel, QStringLiteral("Range"), m_chartRangeCombo));

    QWidget* chartPanel = makeSurface(page);
    auto* chartLayout = qobject_cast<QVBoxLayout*>(chartPanel->layout());
    chartLayout->setContentsMargins(8, 8, 8, 8);
    m_priceChart = new PriceChartWidget(chartPanel);
    chartLayout->addWidget(m_priceChart, 1);

    layout->addWidget(title);
    layout->addWidget(headerPanel);
    layout->addWidget(chartPanel, 1);
}

void MainWindow::rebuildReceive(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Receive"), page);
    title->setObjectName(QStringLiteral("contentTitle"));

    QWidget* panel = makeSurface(page);
    auto* panelLayout = qobject_cast<QVBoxLayout*>(panel->layout());

    m_addrTypeCombo = new QComboBox(panel);
    styleLargeCombo(m_addrTypeCombo, 320);
    // Nested first - widest Litecoin wallet/exchange support (better than bare Native/Taproot).
    m_addrTypeCombo->addItem(QStringLiteral("Nested SegWit (M…) - recommended"));
    m_addrTypeCombo->addItem(QStringLiteral("Native SegWit (ltc1q…)"));
    m_addrTypeCombo->addItem(QStringLiteral("Legacy (L…)"));
    m_addrTypeCombo->addItem(QStringLiteral("Taproot (ltc1p…)"));
    m_addrTypeCombo->setCurrentIndex(0);

    m_receiveAddressEdit = new QLineEdit(panel);
    m_receiveAddressEdit->setReadOnly(true);
    m_receiveAddressEdit->setPlaceholderText(QStringLiteral("Generate an address to receive LTC"));
    m_receiveAddressEdit->setMinimumHeight(44);

    m_vanityWordEdit = new QLineEdit(panel);
    m_vanityWordEdit->setPlaceholderText(QStringLiteral("e.g. love"));
    m_vanityWordEdit->setMinimumHeight(44);
    m_vanityWordEdit->setClearButtonEnabled(true);
    m_vanityWordEdit->setMaxLength(8);

    m_vanityPrefixCheck = new QCheckBox(QStringLiteral("Word at the start of the address"), panel);
    m_vanityPrefixCheck->setToolTip(
        QStringLiteral("After M/L or ltc1q/ltc1p - e.g. Mlove… or ltc1qlove…"));

    m_vanityHintLabel = new QLabel(panel);
    m_vanityHintLabel->setObjectName(QStringLiteral("fieldCaption"));
    m_vanityHintLabel->setWordWrap(true);

    m_vanityStatusLabel = new QLabel(panel);
    m_vanityStatusLabel->setObjectName(QStringLiteral("fieldCaption"));
    m_vanityStatusLabel->setWordWrap(true);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    m_newAddressBtn = new QPushButton(QStringLiteral("New address"), panel);
    m_newAddressBtn->setObjectName(QStringLiteral("primaryButton"));
    m_copyAddressBtn = new QPushButton(QStringLiteral("Copy"), panel);
    m_copyAddressBtn->setObjectName(QStringLiteral("secondaryButton"));
    row->addWidget(m_newAddressBtn);
    row->addWidget(m_copyAddressBtn);
    row->addStretch(1);

    auto* vanityRow = new QHBoxLayout;
    vanityRow->setSpacing(8);
    m_vanityFindBtn = new QPushButton(QStringLiteral("Find vanity"), panel);
    m_vanityFindBtn->setObjectName(QStringLiteral("secondaryButton"));
    m_vanityCancelBtn = new QPushButton(QStringLiteral("Cancel"), panel);
    m_vanityCancelBtn->setObjectName(QStringLiteral("secondaryButton"));
    m_vanityCancelBtn->setEnabled(false);
    vanityRow->addWidget(m_vanityFindBtn);
    vanityRow->addWidget(m_vanityCancelBtn);
    vanityRow->addStretch(1);

    panelLayout->addWidget(makeLabeledField(panel, QStringLiteral("Address type"), m_addrTypeCombo));
    panelLayout->addWidget(makeLabeledField(panel, QStringLiteral("Receive address"), m_receiveAddressEdit));
    panelLayout->addLayout(row);
    panelLayout->addWidget(
        makeLabeledField(panel, QStringLiteral("Custom word in address"), m_vanityWordEdit));
    panelLayout->addWidget(m_vanityPrefixCheck);
    panelLayout->addWidget(m_vanityHintLabel);
    panelLayout->addLayout(vanityRow);
    panelLayout->addWidget(m_vanityStatusLabel);

    layout->addWidget(title);
    layout->addWidget(panel);
    layout->addStretch(1);

    updateVanityHint();

    connect(m_addrTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { updateVanityHint(); });
    connect(m_vanityWordEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        updateVanityHint();
    });
    connect(m_vanityPrefixCheck, &QCheckBox::toggled, this, [this](bool) { updateVanityHint(); });
    connect(m_newAddressBtn, &QPushButton::clicked, this, [this]() {
        m_core->newAddress(m_addrTypeCombo->currentIndex());
    });
    connect(m_vanityFindBtn, &QPushButton::clicked, this, [this]() {
        if (!m_core || !m_core->isOpen()) {
            showMessage(QStringLiteral("Unlock the wallet first."), true);
            return;
        }
        const QString word = m_vanityWordEdit ? m_vanityWordEdit->text().trimmed() : QString();
        if (word.isEmpty()) {
            showMessage(QStringLiteral("Enter a custom word first."), true);
            return;
        }
        const bool prefix = m_vanityPrefixCheck && m_vanityPrefixCheck->isChecked();
        m_vanitySearching = true;
        if (m_vanityStatusLabel) {
            m_vanityStatusLabel->setText(
                prefix ? QStringLiteral("Searching prefix… (max 5 minutes)")
                       : QStringLiteral("Searching… (max 5 minutes)"));
        }
        if (m_vanityCancelBtn) {
            m_vanityCancelBtn->setEnabled(true);
        }
        if (m_vanityFindBtn) {
            m_vanityFindBtn->setEnabled(false);
        }
        m_core->findVanityAddress(m_addrTypeCombo->currentIndex(), word, prefix);
    });
    connect(m_vanityCancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_core) {
            m_core->cancelVanity();
        }
        if (m_vanityStatusLabel) {
            m_vanityStatusLabel->setText(QStringLiteral("Cancelling…"));
        }
    });
    connect(m_copyAddressBtn, &QPushButton::clicked, this, [this]() {
        const QString addr = m_receiveAddressEdit->text();
        if (addr.isEmpty()) {
            showMessage(QStringLiteral("Generate an address first."), true);
            return;
        }
        QApplication::clipboard()->setText(addr);
        showMessage(QStringLiteral("Address copied."));
    });
}

void MainWindow::rebuildSend(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("Send"), page);
    title->setObjectName(QStringLiteral("contentTitle"));

    auto* card = new QWidget(page);
    card->setObjectName(QStringLiteral("sendCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setMaximumWidth(620);
    card->setMinimumWidth(480);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 22, 22, 22);
    cardLayout->setSpacing(14);

    m_sendAvailableLabel = new QLabel(QStringLiteral("Available -"), card);
    m_sendAvailableLabel->setObjectName(QStringLiteral("fieldCaption"));
    m_sendAvailableLabel->setAlignment(Qt::AlignCenter);

    m_sendToEdit = new QLineEdit(card);
    m_sendToEdit->setObjectName(QStringLiteral("sendPrompt"));
    m_sendToEdit->setPlaceholderText(QStringLiteral("Address"));
    m_sendToEdit->setMinimumHeight(48);
    m_sendToEdit->setClearButtonEnabled(true);
    m_sendAddressWarnAction = m_sendToEdit->addAction(
        QIcon(), QLineEdit::TrailingPosition);
    m_sendAddressWarnAction->setVisible(false);
    m_sendAddressWarnAction->setToolTip(QStringLiteral("This is not a valid Litecoin address"));

    auto* amountWrap = new QWidget(card);
    amountWrap->setObjectName(QStringLiteral("sendAmountWrap"));
    amountWrap->setAttribute(Qt::WA_StyledBackground, true);
    auto* amountLayout = new QHBoxLayout(amountWrap);
    amountLayout->setContentsMargins(12, 0, 6, 0);
    amountLayout->setSpacing(6);

    m_sendAmountEdit = new QLineEdit(amountWrap);
    m_sendAmountEdit->setObjectName(QStringLiteral("sendAmountEdit"));
    m_sendAmountEdit->setPlaceholderText(QStringLiteral("0.00"));
    m_sendAmountEdit->setMinimumHeight(48);
    m_sendAmountEdit->setFrame(false);
    m_sendAmountEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // C locale so "." is always accepted (system locales like pl_PL expect ",").
    auto* amountValidator = new QDoubleValidator(0.0, 1e12, 8, m_sendAmountEdit);
    amountValidator->setLocale(QLocale::c());
    amountValidator->setNotation(QDoubleValidator::StandardNotation);
    m_sendAmountEdit->setValidator(amountValidator);

    m_sendUnitBtn = new QPushButton(amountWrap);
    m_sendUnitBtn->setObjectName(QStringLiteral("sendUnitButton"));
    m_sendUnitBtn->setCursor(Qt::PointingHandCursor);
    m_sendUnitBtn->setFlat(true);
    m_sendUnitBtn->setMinimumHeight(36);
    m_sendUnitBtn->setToolTip(QStringLiteral("Switch LTC / fiat"));

    amountLayout->addWidget(m_sendAmountEdit, 1);
    amountLayout->addWidget(m_sendUnitBtn, 0, Qt::AlignVCenter);

    m_sendAmountHint = new QLabel(QStringLiteral("≈ -"), card);
    m_sendAmountHint->setObjectName(QStringLiteral("sendAmountHint"));
    m_sendAmountHint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* quickRow = new QHBoxLayout;
    quickRow->setContentsMargins(0, 0, 0, 0);
    quickRow->setSpacing(8);
    m_sendHalfBtn = new QPushButton(QStringLiteral("Half"), card);
    m_sendHalfBtn->setObjectName(QStringLiteral("sendQuickButton"));
    m_sendHalfBtn->setCursor(Qt::PointingHandCursor);
    m_sendHalfBtn->setFlat(true);
    m_sendHalfBtn->setToolTip(QStringLiteral("Use half of available balance"));
    m_sendAllBtn = new QPushButton(QStringLiteral("All"), card);
    m_sendAllBtn->setObjectName(QStringLiteral("sendQuickButton"));
    m_sendAllBtn->setCursor(Qt::PointingHandCursor);
    m_sendAllBtn->setFlat(true);
    m_sendAllBtn->setToolTip(QStringLiteral("Use maximum spendable amount (balance minus fee reserve)"));
    quickRow->addWidget(m_sendHalfBtn);
    quickRow->addWidget(m_sendAllBtn);
    quickRow->addStretch(1);

    m_sendBtn = new QPushButton(QStringLiteral("Send"), card);
    m_sendBtn->setObjectName(QStringLiteral("primaryButton"));
    m_sendBtn->setMinimumHeight(44);

    cardLayout->addWidget(m_sendAvailableLabel);
    cardLayout->addWidget(m_sendToEdit);
    cardLayout->addWidget(amountWrap);
    cardLayout->addWidget(m_sendAmountHint);
    cardLayout->addLayout(quickRow);
    cardLayout->addSpacing(4);
    cardLayout->addWidget(m_sendBtn);

    auto* centerRow = new QHBoxLayout;
    centerRow->addStretch(1);
    centerRow->addWidget(card, 0, Qt::AlignCenter);
    centerRow->addStretch(1);

    layout->addWidget(title);
    layout->addStretch(1);
    layout->addLayout(centerRow);
    layout->addStretch(1);

    m_sendAmountInFiat = m_settings.sendAmountInFiat();
    updateSendUnitButton();
    updateSendAmountHint();

    connect(m_sendToEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        updateSendAddressWarning();
    });
    connect(m_sendAmountEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        updateSendAmountHint();
    });
    connect(m_sendUnitBtn, &QPushButton::clicked, this, &MainWindow::toggleSendAmountUnit);
    connect(m_sendHalfBtn, &QPushButton::clicked, this, [this]() {
        fillSendAmountSats(m_lastBalanceSats / 2);
    });
    connect(m_sendAllBtn, &QPushButton::clicked, this, [this]() {
        fillSendAmountSats(sendMaxSpendableSats());
    });
    connect(m_sendBtn, &QPushButton::clicked, this, [this]() {
        const QString to = m_sendToEdit ? m_sendToEdit->text().trimmed() : QString();
        if (!CoreBridge::isValidAddress(to)) {
            showMessage(QStringLiteral("Enter a valid Litecoin address."), true);
            updateSendAddressWarning();
            return;
        }
        const qint64 amount = sendAmountToSats();
        if (amount <= 0) {
            showMessage(QStringLiteral("Enter an amount greater than zero."), true);
            return;
        }
        const qint64 fee = m_settings.feeSatPerVb();
        const auto reply = QMessageBox::question(
            this,
            QStringLiteral("Confirm send"),
            QStringLiteral("Send %1 sats to\n%2\n\nFee rate: %3 sat/vB")
                .arg(amount)
                .arg(to)
                .arg(fee));
        if (reply != QMessageBox::Yes) {
            return;
        }
        m_pendingSendAmountSats = amount;
        m_pendingSendAddress = to;
        m_core->send(to, amount, fee);
    });
}

void MainWindow::rebuildAddresses(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Addresses"), page);
    title->setObjectName(QStringLiteral("contentTitle"));

    QWidget* panel = makeSurface(page);
    auto* panelLayout = qobject_cast<QVBoxLayout*>(panel->layout());

    auto* caption = new QLabel(QStringLiteral("Issued receive addresses"), panel);
    caption->setObjectName(QStringLiteral("sectionTitle"));

    auto* hint = new QLabel(
        QStringLiteral("Only addresses you created or that already received coins. "
                       "Extra look-ahead keys stay internal for SPV sync."),
        panel);
    hint->setObjectName(QStringLiteral("fieldCaption"));
    hint->setWordWrap(true);

    m_addressList = new QPlainTextEdit(panel);
    m_addressList->setReadOnly(true);
    m_addressList->setPlaceholderText(QStringLiteral("Unlocked wallet addresses appear here"));

    panelLayout->addWidget(caption);
    panelLayout->addWidget(hint);
    panelLayout->addWidget(m_addressList, 1);

    layout->addWidget(title);
    layout->addWidget(panel, 1);
}

void MainWindow::rebuildSync(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Sync"), page);
    title->setObjectName(QStringLiteral("contentTitle"));

    auto* body = new QLabel(
        QStringLiteral("Pruned SPV sync over Litecoin P2P (headers + BIP37 bloom filters)."),
        page);
    body->setObjectName(QStringLiteral("contentBody"));
    body->setWordWrap(true);

    QWidget* gridPanel = makeSurface(page);
    auto* gridOuter = qobject_cast<QVBoxLayout*>(gridPanel->layout());
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    grid->addWidget(makeMetricTile(gridPanel, QStringLiteral("Phase"), &m_syncPhaseValue), 0, 0);
    grid->addWidget(makeMetricTile(gridPanel, QStringLiteral("Connection"), &m_syncConnValue), 0, 1);
    grid->addWidget(makeMetricTile(gridPanel, QStringLiteral("Progress"), &m_syncProgressValue), 1, 0);
    grid->addWidget(makeMetricTile(gridPanel, QStringLiteral("Tip height"), &m_syncTipValue), 1, 1);
    grid->addWidget(makeMetricTile(gridPanel, QStringLiteral("Peers"), &m_syncPeersValue), 2, 0);
    grid->addWidget(makeMetricTile(gridPanel, QStringLiteral("Matched txs"), &m_syncMatchedValue), 2, 1);
    gridOuter->addLayout(grid);

    QWidget* extraPanel = makeSurface(page);
    auto* extraLayout = qobject_cast<QVBoxLayout*>(extraPanel->layout());
    auto* extraCaption = new QLabel(QStringLiteral("Details"), extraPanel);
    extraCaption->setObjectName(QStringLiteral("sectionTitle"));
    m_syncExtraLabel = new QLabel(QStringLiteral("-"), extraPanel);
    m_syncExtraLabel->setObjectName(QStringLiteral("contentBody"));
    m_syncExtraLabel->setWordWrap(true);
    m_syncExtraLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    extraLayout->addWidget(extraCaption);
    extraLayout->addWidget(m_syncExtraLabel);

    auto* syncHardRefresh = new QPushButton(QStringLiteral(" Hard refresh"), page);
    syncHardRefresh->setObjectName(QStringLiteral("primaryButton"));
    syncHardRefresh->setMinimumHeight(44);
    syncHardRefresh->setCursor(Qt::PointingHandCursor);
    syncHardRefresh->setToolTip(
        QStringLiteral("Force header sync and rescan recent blocks for confirmations and balance"));
    m_syncHardRefreshBtn = syncHardRefresh;

    m_syncRefreshStatusLabel = new QLabel(QStringLiteral("Refresh idle"), page);
    m_syncRefreshStatusLabel->setObjectName(QStringLiteral("refreshStatus"));
    m_syncRefreshStatusLabel->setWordWrap(true);

    layout->addWidget(title);
    layout->addWidget(body);
    layout->addWidget(gridPanel);
    layout->addWidget(extraPanel, 1);
    layout->addWidget(m_syncRefreshStatusLabel);
    layout->addWidget(syncHardRefresh);

    connect(syncHardRefresh, &QPushButton::clicked, this, [this]() {
        if (!m_core || !m_core->isOpen()) {
            showMessage(QStringLiteral("Unlock the wallet first."), true);
            return;
        }
        m_core->hardRefresh();
        showMessage(QStringLiteral("Hard refresh queued…"));
    });
}

void MainWindow::rebuildSettings(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Settings"), page);
    title->setObjectName(QStringLiteral("contentTitle"));

    QWidget* panel = makeSurface(page);
    auto* panelLayout = qobject_cast<QVBoxLayout*>(panel->layout());

    m_backdropCombo = new QComboBox(panel);
    styleLargeCombo(m_backdropCombo, 280);
    m_backdropCombo->addItem(QStringLiteral("Auto"), static_cast<int>(BackdropType::Auto));
    m_backdropCombo->addItem(QStringLiteral("None"), static_cast<int>(BackdropType::Disabled));
    m_backdropCombo->addItem(QStringLiteral("Mica"), static_cast<int>(BackdropType::Mica));
    m_backdropCombo->addItem(QStringLiteral("Acrylic"), static_cast<int>(BackdropType::Acrylic));
    m_backdropCombo->addItem(QStringLiteral("Mica Alt"), static_cast<int>(BackdropType::MicaAlt));
    m_backdropCombo->setEnabled(m_capability.supported);

    m_fiatCombo = new QComboBox(panel);
    styleLargeCombo(m_fiatCombo, 320);
    for (const FiatCurrency& fiat : supportedFiatCurrencies()) {
        m_fiatCombo->addItem(QStringLiteral("%1 - %2").arg(fiat.code, fiat.name), fiat.code);
    }

    m_darkModeCheck = new QCheckBox(QStringLiteral("Dark mode"), panel);

    m_sendFeeEdit = new QLineEdit(panel);
    m_sendFeeEdit->setPlaceholderText(QStringLiteral("10"));
    m_sendFeeEdit->setText(QString::number(m_settings.feeSatPerVb()));
    m_sendFeeEdit->setValidator(new QIntValidator(1, 100000, m_sendFeeEdit));

    m_dataDirLabel = new QLabel(panel);
    m_dataDirLabel->setObjectName(QStringLiteral("contentBody"));
    m_dataDirLabel->setWordWrap(true);
    m_dataDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_lockBtn = new QPushButton(QStringLiteral("Lock wallet"), panel);
    m_lockBtn->setObjectName(QStringLiteral("secondaryButton"));

    panelLayout->addWidget(makeLabeledField(panel, QStringLiteral("Backdrop"), m_backdropCombo));
    panelLayout->addWidget(makeLabeledField(panel, QStringLiteral("Fiat currency"), m_fiatCombo));
    panelLayout->addWidget(makeLabeledField(panel, QStringLiteral("Fee rate (sat/vB)"), m_sendFeeEdit));
    panelLayout->addWidget(m_darkModeCheck);
    panelLayout->addWidget(makeLabeledField(panel, QStringLiteral("Data directory"), m_dataDirLabel));
    panelLayout->addWidget(m_lockBtn, 0, Qt::AlignLeft);

    layout->addWidget(title);
    layout->addWidget(panel);
    layout->addStretch(1);

    connect(m_lockBtn, &QPushButton::clicked, this, [this]() { m_core->lock(); });
    connect(m_sendFeeEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const qint64 fee = m_sendFeeEdit->text().trimmed().toLongLong(&ok);
        if (ok && fee > 0) {
            m_settings.setFeeSatPerVb(fee);
            m_settings.sync();
        } else if (m_sendFeeEdit) {
            m_sendFeeEdit->setText(QString::number(m_settings.feeSatPerVb()));
        }
    });
}

void MainWindow::setUnlockedUi(bool unlocked)
{
    refreshNavVisibility(unlocked);

    if (m_pages) {
        showPage(unlocked ? static_cast<int>(PageIndex::Overview)
                          : static_cast<int>(PageIndex::Gate),
                 false);
    }
    if (m_navList) {
        const int targetPage = unlocked ? static_cast<int>(PageIndex::Overview)
                                        : static_cast<int>(PageIndex::Gate);
        const int row = navRowForPage(targetPage);
        if (row >= 0) {
            m_navList->setCurrentRow(row);
        }
    }
    if (m_lockBtn) {
        m_lockBtn->setEnabled(unlocked && !m_core->isBusy());
        m_lockBtn->setVisible(unlocked);
    }
    if (!unlocked && m_restoreBtn) {
        m_restoreBtn->setChecked(false);
    }
    if (m_gateHint) {
        m_gateHint->setText(
            m_core->walletExists()
                ? QStringLiteral("A wallet was found on disk. Enter your password to unlock, or use Restore / Backup in a fresh data dir.")
                : QStringLiteral("No wallet yet. Create one, or open Restore / Backup to import a mnemonic."));
    }
    setBusyUi(m_core->isBusy());
}

int MainWindow::navRowForPage(int page) const
{
    if (!m_navList) {
        return -1;
    }
    for (int i = 0; i < m_navList->count(); ++i) {
        QListWidgetItem* item = m_navList->item(i);
        if (item && item->data(Qt::UserRole + 1).toInt() == page && !item->isHidden()) {
            return i;
        }
    }
    return -1;
}

void MainWindow::refreshNavVisibility(bool unlocked)
{
    if (!m_navList) {
        return;
    }

    for (int i = 0; i < m_navList->count(); ++i) {
        QListWidgetItem* item = m_navList->item(i);
        const bool requiresUnlock = item->data(Qt::UserRole + 2).toBool();
        const bool lockedOnly = item->data(Qt::UserRole + 3).toBool();
        const bool visible = unlocked ? !lockedOnly : !requiresUnlock;
        item->setHidden(!visible);
    }
}

void MainWindow::setBusyUi(bool busy)
{
    const bool open = m_core && m_core->isOpen();
    const bool exists = m_core && m_core->walletExists();
    // Unlock/create/import must stay clickable while vanity is grinding - they cancel it.
    if (m_unlockBtn) {
        m_unlockBtn->setEnabled(exists && !open);
    }
    if (m_createBtn) {
        m_createBtn->setEnabled(!open);
    }
    if (m_importBtn) {
        m_importBtn->setEnabled(!open);
    }
    if (m_restoreBtn) {
        m_restoreBtn->setEnabled(!open);
    }
    if (m_newAddressBtn) {
        m_newAddressBtn->setEnabled(!busy && open);
    }
    if (m_vanityFindBtn) {
        m_vanityFindBtn->setEnabled(!busy && open);
    }
    if (m_vanityCancelBtn) {
        m_vanityCancelBtn->setEnabled(m_vanitySearching && open);
    }
    if (m_vanityWordEdit) {
        m_vanityWordEdit->setEnabled(!busy && open);
    }
    if (m_vanityPrefixCheck) {
        m_vanityPrefixCheck->setEnabled(!busy && open);
    }
    if (m_addrTypeCombo) {
        m_addrTypeCombo->setEnabled(!busy && open);
    }
    if (m_overviewReloadBtn) {
        m_overviewReloadBtn->setEnabled(!busy && open);
    }
    if (m_overviewCopyBtn) {
        const QString addr = m_overviewAddressLabel ? m_overviewAddressLabel->text().trimmed() : QString();
        const bool hasAddr = !addr.isEmpty() && addr != QLatin1String("Unlock to show an address")
            && addr != QLatin1String("-");
        m_overviewCopyBtn->setEnabled(!busy && open && hasAddr);
    }
    if (m_sendBtn) {
        m_sendBtn->setEnabled(!busy && open);
    }
    if (m_sendHalfBtn) {
        m_sendHalfBtn->setEnabled(!busy && open && m_lastBalanceSats > 0);
    }
    if (m_sendAllBtn) {
        m_sendAllBtn->setEnabled(!busy && open && sendMaxSpendableSats() > 0);
    }
    if (m_hardRefreshBtn) {
        m_hardRefreshBtn->setEnabled(!busy && open && !(m_core && m_core->snapshot().sync.hard_refresh));
    }
    if (m_syncHardRefreshBtn) {
        m_syncHardRefreshBtn->setEnabled(!busy && open && !(m_core && m_core->snapshot().sync.hard_refresh));
    }
    if (m_lockBtn) {
        // Lock stays available during vanity so the user can abort via cancel+lock.
        m_lockBtn->setEnabled(open);
    }
    if (busy) {
        showMessage(QStringLiteral("Working on worker thread…"));
    }
}

void MainWindow::applySnapshot(const waltosh::core::Snapshot& snap)
{
    if (m_dataDirLabel) {
        m_dataDirLabel->setText(QStringLiteral("Data directory: %1")
                                    .arg(QString::fromStdString(snap.data_dir)));
    }

    if (m_gateHint) {
        QString hint = snap.exists
            ? QStringLiteral("A wallet was found on disk. Enter your password to unlock, or use Restore / Backup in a fresh data dir.")
            : QStringLiteral("No wallet yet. Create one, or open Restore / Backup to import a mnemonic.");
        if (snap.sync.running) {
            hint += QStringLiteral("\n\nChain sync is already warming in the background (peers & headers) - no password needed for that.");
            if (snap.sync.tip_height > 0) {
                hint += QStringLiteral(" Tip height %1 · %2 peer(s).")
                            .arg(snap.sync.tip_height)
                            .arg(snap.sync.peers);
            }
        }
        m_gateHint->setText(hint);
    }

    setBusyUi(snap.busy);

    if (!snap.open) {
        m_lastBalanceSats = 0;
        if (m_sendHalfBtn) {
            m_sendHalfBtn->setEnabled(false);
        }
        if (m_sendAllBtn) {
            m_sendAllBtn->setEnabled(false);
        }
        if (m_balanceLabel) {
            m_balanceLabel->setText(QStringLiteral("-"));
        }
        if (m_fiatBalanceLabel) {
            m_fiatBalanceLabel->setText(QStringLiteral("-"));
        }
        if (m_footerStatus) {
            if (snap.busy) {
                m_footerStatus->setText(QStringLiteral("Busy"));
            } else if (snap.sync.running) {
                int pct = -1;
                const uint32_t target = snap.sync.target_height > 0 ? snap.sync.target_height
                                                                   : snap.sync.tip_height;
                if (target > 0) {
                    pct = static_cast<int>((100.0 * snap.sync.tip_height) / target);
                    if (pct > 100) {
                        pct = 100;
                    }
                }
                m_footerStatus->setText(
                    snap.sync.connected
                        ? (pct >= 0 ? QStringLiteral("Warming · %1%").arg(pct)
                                    : QStringLiteral("Warming…"))
                        : QStringLiteral("Warming · connecting…"));
            } else {
                m_footerStatus->setText(QStringLiteral("Locked"));
            }
        }
        if (m_syncPhaseValue) {
            m_syncPhaseValue->setText(
                snap.sync.running ? QString::fromStdString(snap.sync.phase) : QStringLiteral("-"));
        }
        if (m_syncConnValue) {
            m_syncConnValue->setText(
                snap.sync.running ? QString::fromStdString(snap.sync.connection)
                                  : QStringLiteral("Locked"));
        }
        if (m_syncProgressValue) {
            m_syncProgressValue->setText(
                snap.sync.running ? QString::fromStdString(snap.sync.progress) : QStringLiteral("-"));
        }
        if (m_syncTipValue) {
            m_syncTipValue->setText(snap.sync.running ? QString::number(snap.sync.tip_height)
                                                     : QStringLiteral("-"));
        }
        if (m_syncPeersValue) {
            m_syncPeersValue->setText(snap.sync.running ? QString::number(snap.sync.peers)
                                                       : QStringLiteral("-"));
        }
        if (m_syncMatchedValue) {
            m_syncMatchedValue->setText(QStringLiteral("-"));
        }
        if (m_syncExtraLabel) {
            if (snap.sync.running) {
                const QString detail = QString::fromStdString(snap.sync.detail);
                m_syncExtraLabel->setText(
                    detail.isEmpty()
                        ? QStringLiteral("Pre-login: syncing peers & block headers (no wallet keys).")
                        : detail);
            } else {
                m_syncExtraLabel->setText(
                    QStringLiteral("Unlock the wallet to scan addresses; headers can warm before that."));
            }
        }
        if (m_refreshStatusLabel) {
            m_refreshStatusLabel->clear();
        }
        if (m_syncRefreshStatusLabel) {
            m_syncRefreshStatusLabel->setText(QStringLiteral("Refresh idle"));
        }
        m_wasHardRefreshing = false;
    if (m_sendAvailableLabel) {
        m_sendAvailableLabel->setText(QStringLiteral("Available -"));
    }
        if (m_addressList) {
            m_addressList->clear();
        }
        setOverviewReceiveAddress(QString());
        if (m_txHistoryList) {
            m_txHistoryList->clear();
            m_txHistoryFingerprint.clear();
            m_expandedTxid.clear();
            auto* empty = new QListWidgetItem(QStringLiteral("Unlock to see transaction history."));
            empty->setFlags(Qt::NoItemFlags);
            m_txHistoryList->addItem(empty);
        }
        applyMarketQuote(m_lastQuote);
        return;
    }

    m_lastBalanceSats = snap.balance_sats;
    const QString bal = formatLtc(snap.balance_sats);
    if (m_balanceLabel) {
        m_balanceLabel->setText(bal);
    }
    applyMarketQuote(m_lastQuote);
    if (m_sendAvailableLabel) {
        m_sendAvailableLabel->setText(QStringLiteral("Available %1").arg(bal));
    }
    if (m_sendHalfBtn) {
        m_sendHalfBtn->setEnabled(!snap.busy && m_lastBalanceSats > 0);
    }
    if (m_sendAllBtn) {
        m_sendAllBtn->setEnabled(!snap.busy && sendMaxSpendableSats() > 0);
    }

    const QString phase = QString::fromStdString(snap.sync.phase);
    const QString progress = QString::fromStdString(snap.sync.progress);
    const QString conn = QString::fromStdString(snap.sync.connection);

    QString refreshStatus = QStringLiteral("Idle");
    const QString detail = QString::fromStdString(snap.sync.detail);
    if (snap.sync.hard_refresh && snap.sync.rescanning) {
        refreshStatus = QStringLiteral("Hard refresh · %1/%2")
                            .arg(snap.sync.rescan_height)
                            .arg(snap.sync.tip_height);
    } else if (snap.sync.hard_refresh) {
        refreshStatus = detail.isEmpty() ? QStringLiteral("Hard refresh queued…") : detail;
    } else if (snap.sync.rescanning) {
        refreshStatus = QStringLiteral("Rescan · %1/%2")
                            .arg(snap.sync.rescan_height)
                            .arg(snap.sync.tip_height);
    } else if (detail.startsWith(QStringLiteral("hard refresh complete"))) {
        refreshStatus = QStringLiteral("Hard refresh done");
    } else if (detail.startsWith(QStringLiteral("hard refresh failed"))) {
        refreshStatus = detail;
    } else if (!detail.isEmpty() && (phase.contains(QStringLiteral("reconnect"), Qt::CaseInsensitive)
                                     || conn.contains(QStringLiteral("disconnect"), Qt::CaseInsensitive))) {
        refreshStatus = detail;
    } else if (!progress.isEmpty() && progress != QLatin1String("-")) {
        refreshStatus = progress;
    }

    if (m_wasHardRefreshing && !snap.sync.hard_refresh && !snap.sync.rescanning
        && detail.startsWith(QStringLiteral("hard refresh complete"))) {
        showMessage(QStringLiteral("Hard refresh finished"));
    }
    m_wasHardRefreshing = snap.sync.hard_refresh;

    if (m_refreshStatusLabel) {
        m_refreshStatusLabel->setText(refreshStatus);
    }
    if (m_syncRefreshStatusLabel) {
        m_syncRefreshStatusLabel->setText(
            QStringLiteral("Status: %1\n%2").arg(refreshStatus, detail.isEmpty() ? QStringLiteral("-") : detail));
    }

    const bool refreshing = snap.sync.hard_refresh || snap.sync.rescanning;
    if (m_hardRefreshBtn) {
        m_hardRefreshBtn->setEnabled(!snap.busy && !refreshing);
    }
    if (m_syncHardRefreshBtn) {
        m_syncHardRefreshBtn->setEnabled(!snap.busy && !refreshing);
        m_syncHardRefreshBtn->setText(refreshing ? QStringLiteral(" Refreshing…")
                                                 : QStringLiteral(" Hard refresh"));
    }

    if (m_footerStatus) {
        auto syncPercent = [&]() -> int {
            if (snap.sync.rescanning && snap.sync.tip_height > 0) {
                return static_cast<int>(
                    (100.0 * snap.sync.rescan_height) / snap.sync.tip_height);
            }
            uint32_t target = snap.sync.target_height;
            if (target == 0) {
                target = snap.sync.tip_height;
            }
            if (target == 0) {
                return -1;
            }
            const int pct = static_cast<int>((100.0 * snap.sync.tip_height) / target);
            return pct > 100 ? 100 : pct;
        };
        const int pct = syncPercent();
        QString footer = conn.isEmpty() ? QStringLiteral("-") : conn;
        if (pct >= 0) {
            footer += QStringLiteral("\nblocks: %1%").arg(pct);
        }
        footer += QStringLiteral("\npeers %1 · txs %2")
                      .arg(snap.sync.peers)
                      .arg(snap.sync.matched_txs);
        if (refreshStatus != QLatin1String("Idle")
            && !refreshStatus.contains(QStringLiteral("block"), Qt::CaseInsensitive)
            && !refreshStatus.contains(QLatin1Char('%'))) {
            footer += QStringLiteral("\n%1").arg(refreshStatus);
        }
        m_footerStatus->setText(footer);
    }
    if (m_syncPhaseValue) {
        m_syncPhaseValue->setText(phase.isEmpty() ? QStringLiteral("-") : phase);
    }
    if (m_syncConnValue) {
        m_syncConnValue->setText(conn.isEmpty() ? QStringLiteral("-") : conn);
    }
    if (m_syncProgressValue) {
        m_syncProgressValue->setText(progress.isEmpty() ? QStringLiteral("-") : progress);
    }
    if (m_syncTipValue) {
        m_syncTipValue->setText(QString::number(snap.sync.tip_height));
    }
    if (m_syncPeersValue) {
        m_syncPeersValue->setText(QString::number(snap.sync.peers));
    }
    if (m_syncMatchedValue) {
        m_syncMatchedValue->setText(QString::number(snap.sync.matched_txs));
    }
    if (m_syncExtraLabel) {
        QStringList extra;
        if (!snap.sync.peer_agent.empty()) {
            extra << QStringLiteral("Peer: %1").arg(QString::fromStdString(snap.sync.peer_agent));
        }
        if (!snap.sync.detail.empty()) {
            extra << QStringLiteral("Detail: %1").arg(QString::fromStdString(snap.sync.detail));
        }
        if (snap.sync.hard_refresh) {
            extra << QStringLiteral("Hard refresh: active");
        }
        if (snap.sync.rescanning) {
            extra << QStringLiteral("Rescan height: %1 / tip %2")
                         .arg(snap.sync.rescan_height)
                         .arg(snap.sync.tip_height);
        }
        if (!snap.sync.last_error.empty()) {
            extra << QStringLiteral("Last error: %1").arg(QString::fromStdString(snap.sync.last_error));
        }
        m_syncExtraLabel->setText(extra.isEmpty() ? QStringLiteral("No extra sync details.") : extra.join(QLatin1Char('\n')));
    }
    if (m_addressList) {
        QStringList lines;
        lines.reserve(static_cast<int>(snap.receive_addresses.size()));
        for (const auto& addr : snap.receive_addresses) {
            lines.push_back(QString::fromStdString(addr));
        }
        const QString text = lines.join(QLatin1Char('\n'));
        if (m_addressList->toPlainText() != text) {
            const int scroll = m_addressList->verticalScrollBar()
                ? m_addressList->verticalScrollBar()->value()
                : 0;
            m_addressList->setPlainText(text);
            if (m_addressList->verticalScrollBar()) {
                m_addressList->verticalScrollBar()->setValue(scroll);
            }
        }
    }

    if (snap.open) {
        QString current = m_overviewAddressLabel ? m_overviewAddressLabel->text().trimmed() : QString();
        const bool placeholder = current.isEmpty() || current == QLatin1String("Unlock to show an address")
            || current == QLatin1String("-");

        QString nested;
        QString fallback;
        for (const auto& addr : snap.receive_addresses) {
            const QString s = QString::fromStdString(addr);
            if (s.isEmpty()) {
                continue;
            }
            fallback = s;
            // Litecoin mainnet Nested SegWit (P2SH-P2WPKH) starts with M.
            if (s.startsWith(QLatin1Char('M'))) {
                nested = s;
            }
        }

        if (placeholder) {
            if (!nested.isEmpty()) {
                setOverviewReceiveAddress(nested);
            } else if (!fallback.isEmpty()) {
                setOverviewReceiveAddress(fallback);
            } else if (m_receiveAddressEdit && !m_receiveAddressEdit->text().trimmed().isEmpty()) {
                setOverviewReceiveAddress(m_receiveAddressEdit->text().trimmed());
            } else {
                setOverviewReceiveAddress(QStringLiteral("-"));
            }
        } else if (!nested.isEmpty() && current.startsWith(QStringLiteral("ltc1"))) {
            // Prefer Nested over Native/Taproot when both exist - better wallet support.
            setOverviewReceiveAddress(nested);
        }

        // Existing wallets may only have Native - mint Nested once as the default type.
        if (nested.isEmpty() && !m_requestedDefaultNested && !snap.busy) {
            m_requestedDefaultNested = true;
            m_core->newAddress(0);
        }
    }

    refreshTxHistory(snap);
}

void MainWindow::loadPersistedSettings()
{
    if (!m_backdropCombo || !m_darkModeCheck || !m_fiatCombo) {
        return;
    }

    m_loadingSettings = true;
    const QSignalBlocker blockBackdrop(m_backdropCombo);
    const QSignalBlocker blockDark(m_darkModeCheck);
    const QSignalBlocker blockFiat(m_fiatCombo);
    QSignalBlocker blockRange(m_chartRangeCombo);

    m_darkModeCheck->setChecked(m_settings.darkMode());

    const int wanted = static_cast<int>(m_settings.backdropType());
    int index = m_backdropCombo->findData(wanted);
    if (index < 0) {
        index = m_backdropCombo->findData(static_cast<int>(BackdropType::Mica));
    }
    if (index >= 0) {
        m_backdropCombo->setCurrentIndex(index);
    }

    const QString fiat = fiatByCode(m_settings.fiatCurrency()).code;
    int fiatIndex = m_fiatCombo->findData(fiat);
    if (fiatIndex < 0) {
        fiatIndex = m_fiatCombo->findData(QStringLiteral("USD"));
    }
    if (fiatIndex >= 0) {
        m_fiatCombo->setCurrentIndex(fiatIndex);
    }
    if (m_prices) {
        m_prices->setFiat(fiat);
    }

    if (m_chartRangeCombo) {
        const int days = m_settings.chartDays();
        int rangeIndex = m_chartRangeCombo->findData(days);
        if (rangeIndex < 0) {
            rangeIndex = m_chartRangeCombo->findData(7);
        }
        if (rangeIndex >= 0) {
            m_chartRangeCombo->setCurrentIndex(rangeIndex);
        }
        if (m_prices) {
            m_prices->setChartDays(days);
        }
    }
    if (m_sendFeeEdit) {
        m_sendFeeEdit->setText(QString::number(m_settings.feeSatPerVb()));
    }

    m_sendAmountInFiat = m_settings.sendAmountInFiat();
    updateSendUnitButton();
    updateSendAmountHint();

    m_loadingSettings = false;
}

void MainWindow::persistUiSettings()
{
    if (!m_backdropCombo || !m_darkModeCheck || !m_fiatCombo) {
        return;
    }
    m_settings.setDarkMode(m_darkModeCheck->isChecked());
    m_settings.setBackdropType(static_cast<BackdropType>(m_backdropCombo->currentData().toInt()));
    m_settings.setFiatCurrency(m_fiatCombo->currentData().toString());
    if (m_chartRangeCombo) {
        m_settings.setChartDays(m_chartRangeCombo->currentData().toInt());
    }
    m_settings.sync();
}

void MainWindow::onFiatChanged()
{
    const QString code = m_fiatCombo ? m_fiatCombo->currentData().toString() : QStringLiteral("USD");
    persistUiSettings();
    if (m_prices) {
        m_prices->setFiat(code);
    }
    applyMarketQuote(m_prices ? m_prices->quote() : MarketQuote{});
    updateSendUnitButton();
}

void MainWindow::onChartRangeChanged()
{
    if (!m_chartRangeCombo || !m_prices) {
        return;
    }
    persistUiSettings();
    m_prices->setChartDays(m_chartRangeCombo->currentData().toInt());
}

QString MainWindow::formatFiatAmount(double amount) const
{
    const FiatCurrency fiat = fiatByCode(m_prices ? m_prices->fiat() : m_settings.fiatCurrency());
    QLocale locale = QLocale::system();
    const int decimals = (fiat.code == QLatin1String("JPY") || fiat.code == QLatin1String("KRW")) ? 0 : 2;
    return QStringLiteral("%1%2 %3")
        .arg(fiat.symbol)
        .arg(locale.toString(amount, 'f', decimals))
        .arg(fiat.code);
}

void MainWindow::applyMarketQuote(const MarketQuote& quote)
{
    if (quote.valid) {
        m_lastQuote = quote;
    }

    const MarketQuote& q = m_lastQuote;
    const bool up = q.change24hPct >= 0.0;
    const QColor trendColor = !q.valid ? QColor(QStringLiteral("#9aa3b2"))
                                       : (up ? QColor(QStringLiteral("#3ecf8e")) : QColor(QStringLiteral("#f07178")));
    const QString changeText = q.valid
        ? QStringLiteral("%1%").arg(QLocale::system().toString(qAbs(q.change24hPct), 'f', 2))
        : QStringLiteral("-");
    const QPixmap trendPix = q.valid
        ? tintedSvgIcon(
                  up ? QStringLiteral(":/icons/trending-up.svg") : QStringLiteral(":/icons/trending-down.svg"),
                  trendColor,
                  14)
              .pixmap(QSize(14, 14))
        : QPixmap();

    const auto styleChangeLabel = [&](QLabel* label, const QString& text) {
        if (!label) {
            return;
        }
        label->setText(text);
        label->setPixmap(trendPix);
        label->setStyleSheet(
            QStringLiteral(
                "QLabel#change24hLabel {"
                "  color: %1;"
                "  font-size: 13px;"
                "  font-weight: 500;"
                "  background: transparent;"
                "}")
                .arg(trendColor.name(QColor::HexRgb)));
    };

    styleChangeLabel(m_change24hLabel, changeText);
    styleChangeLabel(
        m_chartChangeLabel,
        q.valid ? QStringLiteral("%1 24h").arg(changeText) : QStringLiteral("24h change: -"));

    if (m_spotPriceLabel) {
        m_spotPriceLabel->setText(
            q.valid ? QStringLiteral("%1 / LTC").arg(formatFiatAmount(q.price))
                    : QStringLiteral("…"));
    }

    if (m_fiatBalanceLabel) {
        if (m_lastBalanceSats > 0 && q.valid) {
            const double ltc = static_cast<double>(m_lastBalanceSats) / 100000000.0;
            m_fiatBalanceLabel->setText(formatFiatAmount(ltc * q.price));
        } else if (!m_core->isOpen()) {
            m_fiatBalanceLabel->setText(QStringLiteral("-"));
        } else if (q.valid) {
            m_fiatBalanceLabel->setText(formatFiatAmount(0.0));
        } else {
            m_fiatBalanceLabel->setText(QStringLiteral("…"));
        }
    }

    if (m_chartHeaderLabel) {
        const FiatCurrency fiat =
            fiatByCode(q.valid ? q.fiatCode : (m_prices ? m_prices->fiat() : QStringLiteral("USD")));
        m_chartHeaderLabel->setText(
            q.valid ? QStringLiteral("LTC / %1 · %2").arg(fiat.code, formatFiatAmount(q.price))
                    : QStringLiteral("LTC / %1 · loading…").arg(fiat.code));
    }

    updateSendAmountHint();
}

void MainWindow::applyMarketChart(const MarketChart& chart)
{
    if (m_priceChart) {
        m_priceChart->setChart(chart);
        m_priceChart->setLive(m_prices && m_prices->liveConnected());
    }
}

void MainWindow::showPage(int pageIndex, bool animate)
{
    if (!m_pages || pageIndex < 0 || pageIndex >= m_pages->count()) {
        return;
    }
    if (m_pages->currentIndex() == pageIndex) {
        return;
    }

    m_pages->setCurrentIndex(pageIndex);
    QWidget* page = m_pages->currentWidget();
    if (!page || !animate) {
        return;
    }

    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(page->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(page);
        page->setGraphicsEffect(effect);
    }
    effect->setOpacity(0.0);

    auto* anim = new QPropertyAnimation(effect, "opacity", page);
    anim->setDuration(160);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, anim, &QObject::deleteLater);
    anim->start();
}

void MainWindow::refreshTxHistory(const waltosh::core::Snapshot& snap)
{
    if (!m_txHistoryList) {
        return;
    }

    struct Row {
        TxRowData data;
        qint64 sortKey = 0;
    };
    QVector<Row> rows;

    const auto shortId = [](const QString& id) {
        return id.size() > 18 ? (id.left(10) + QStringLiteral("…") + id.right(8)) : id;
    };

    for (const auto& entry : snap.tx_history) {
        Row row;
        const QString txid = QString::fromStdString(entry.txid);
        const QString address = QString::fromStdString(entry.address);
        const uint32_t tip = snap.sync.tip_height;
        const uint32_t confs =
            entry.height > 0 && tip >= entry.height ? (tip - entry.height + 1) : 0;
        const QString confLabel = entry.height == 0
            ? QStringLiteral("unconfirmed")
            : (confs == 1 ? QStringLiteral("1 conf")
                          : QStringLiteral("%1 confs").arg(confs));

        row.data.outgoing = entry.outgoing;
        row.data.txid = txid;
        row.data.address = address;
        row.data.amount = QStringLiteral("%1%2")
                              .arg(entry.outgoing ? QStringLiteral("−") : QStringLiteral("+"))
                              .arg(formatLtc(entry.amount_sats));
        if (entry.outgoing) {
            row.data.kind = QStringLiteral("Sent");
            row.data.status = QStringLiteral("Sent · %1").arg(confLabel);
        } else {
            row.data.kind = QStringLiteral("Received · %1").arg(confLabel);
            row.data.status = confLabel.left(1).toUpper() + confLabel.mid(1);
        }
        row.data.height = entry.height > 0 ? QString::number(entry.height) : QStringLiteral("mempool");
        row.data.outputs.clear();
        row.data.date = entry.height > 0 ? QStringLiteral("block %1").arg(entry.height)
                                         : QStringLiteral("mempool");
        row.data.preview = address.isEmpty()
            ? (entry.height > 0 ? QStringLiteral("%1 · height %2").arg(shortId(txid)).arg(entry.height)
                                : QStringLiteral("%1 · mempool").arg(shortId(txid)))
            : (entry.height > 0
                   ? QStringLiteral("%1 · h%2").arg(shortId(address), QString::number(entry.height))
                   : QStringLiteral("%1 · mempool").arg(shortId(address)));
        // Unconfirmed (height 0) sort above tip so they stay on top of Activity.
        row.sortKey = entry.height == 0
            ? (static_cast<qint64>(tip) + 1)
            : static_cast<qint64>(entry.height);
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.sortKey != b.sortKey) {
            return a.sortKey > b.sortKey;
        }
        return a.data.txid > b.data.txid;
    });

    QString fingerprint;
    fingerprint.reserve(rows.size() * 48);
    for (const Row& row : rows) {
        fingerprint += row.data.txid;
        fingerprint += QLatin1Char('|');
        fingerprint += row.data.amount;
        fingerprint += QLatin1Char('|');
        fingerprint += row.data.status;
        fingerprint += QLatin1Char('|');
        fingerprint += row.data.height;
        fingerprint += QLatin1Char('|');
        fingerprint += row.data.address;
        fingerprint += QLatin1Char(';');
    }
    fingerprint += snap.open ? QLatin1Char('1') : QLatin1Char('0');
    fingerprint += QLatin1Char('|');
    fingerprint += QString::number(snap.sync.tip_height);

    if (fingerprint == m_txHistoryFingerprint && m_txHistoryList->count() > 0) {
        return;
    }
    m_txHistoryFingerprint = fingerprint;

    const QString keepExpanded = m_expandedTxid;
    m_txHistoryList->clear();

    if (rows.isEmpty()) {
        m_expandedTxid.clear();
        auto* empty = new QListWidgetItem(
            snap.open ? QStringLiteral("No transactions yet. Sync to load chain activity.")
                      : QStringLiteral("Unlock to see transaction history."));
        empty->setFlags(Qt::NoItemFlags);
        m_txHistoryList->addItem(empty);
        return;
    }

    for (const Row& row : rows) {
        auto* item = new QListWidgetItem(m_txHistoryList);
        item->setFlags(Qt::ItemIsEnabled);
        auto* widget = new TxActivityRow(m_txHistoryList, row.data);
        widget->onCopy = [this](const QString& what, const QString& value) {
            if (QClipboard* clip = QGuiApplication::clipboard()) {
                clip->setText(value);
                showMessage(QStringLiteral("Copied %1").arg(what.toLower()));
            }
        };
        widget->onExpandedChanged = [this](const QString& txid, bool expanded) {
            m_expandedTxid = expanded ? txid : QString();
        };
        widget->onSizeChanged = [this, item, widget]() {
            item->setSizeHint(widget->sizeHint().expandedTo(QSize(0, 50)));
            if (m_txHistoryList) {
                m_txHistoryList->doItemsLayout();
            }
        };
        item->setSizeHint(widget->sizeHint().expandedTo(QSize(0, 50)));
        m_txHistoryList->addItem(item);
        m_txHistoryList->setItemWidget(item, widget);

        if (!keepExpanded.isEmpty() && row.data.txid == keepExpanded) {
            widget->setExpanded(true, false);
            m_expandedTxid = keepExpanded;
        }
    }
}

void MainWindow::showMessage(const QString& text, bool error)
{
    if (!m_toastLabel) {
        return;
    }
    m_toastLabel->setProperty("error", error);
    m_toastLabel->style()->unpolish(m_toastLabel);
    m_toastLabel->style()->polish(m_toastLabel);
    m_toastLabel->setText(text);
    m_toastLabel->show();
}

void MainWindow::playSendSound()
{
    if (m_sendSound && m_sendSound->status() != QSoundEffect::Error) {
        m_sendSound->play();
    }
}

void MainWindow::playReceiveSound()
{
    if (m_receiveSound && m_receiveSound->status() != QSoundEffect::Error) {
        m_receiveSound->play();
    }
}

void MainWindow::notifyPaymentReceived(qint64 amountSats)
{
    const QString amount = formatLtc(amountSats);
    const QString title = QStringLiteral("Payment received");
    const QString body = QStringLiteral("You received %1").arg(amount);
    showMessage(body);
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, body, QSystemTrayIcon::Information, 8000);
    }
}

void MainWindow::updateVanityHint()
{
    if (!m_vanityHintLabel || !m_addrTypeCombo) {
        return;
    }
    const int idx = m_addrTypeCombo->currentIndex();
    // Nested=0, Native=1, Legacy=2, Taproot=3
    const bool bech = (idx == 1 || idx == 3);
    const bool prefix = m_vanityPrefixCheck && m_vanityPrefixCheck->isChecked();
    const int maxLen = prefix ? (bech ? 5 : 4) : (bech ? 7 : 6);
    const QString charset = bech
        ? QStringLiteral("bech32: qpzry9x8gf2tvdw0s3jn54khce6mua7l")
        : QStringLiteral("Base58 (no 0 O I l); match is case-insensitive");
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());

    const QString fixed = (idx == 0)   ? QStringLiteral("M")
                          : (idx == 1) ? QStringLiteral("ltc1q")
                          : (idx == 2) ? QStringLiteral("L")
                                       : QStringLiteral("ltc1p");

    QString stripNote;
    QString word = m_vanityWordEdit ? m_vanityWordEdit->text().trimmed() : QString();
    if (prefix && !word.isEmpty()) {
        const QString wordCmp = bech ? word.toLower() : word;
        bool overlap = false;
        if (bech) {
            overlap = wordCmp.startsWith(fixed);
        } else if (!word.isEmpty() && !fixed.isEmpty()) {
            overlap = word.at(0).toLower() == fixed.at(0).toLower();
        }
        if (overlap) {
            const QString rest = word.mid(fixed.size());
            if (!rest.isEmpty()) {
                stripNote = QStringLiteral(" “%1” → %2%3… (leading %2 skipped).")
                                .arg(word, fixed, rest);
            }
        }
    }

    const QString where = prefix
        ? QStringLiteral(
              "Word right after %1 (e.g. %1love…). If the word starts with %1 it is stripped. "
              "Harder - max %2 effective chars.")
              .arg(fixed)
              .arg(maxLen)
        : QStringLiteral("Word anywhere in the address. Max %1 chars.").arg(maxLen);
    m_vanityHintLabel->setText(
        QStringLiteral("%1%2 ~5 min · %3 threads. %4")
            .arg(where, stripNote, QString::number(threads), charset));
    if (m_vanityWordEdit) {
        // Allow typing the version letter extra (Love on L…) - limit applies after strip.
        m_vanityWordEdit->setMaxLength(maxLen + (prefix ? fixed.size() : 0));
    }
}

void MainWindow::refreshNavIcons(bool dark)
{
    Q_UNUSED(dark);
    if (!m_navList || !m_style) {
        return;
    }

    const QColor iconColor = m_style->theme().palette.color(QPalette::WindowText);
    for (int i = 0; i < m_navList->count(); ++i) {
        QListWidgetItem* item = m_navList->item(i);
        const QString path = item->data(Qt::UserRole).toString();
        item->setIcon(tintedSvgIcon(path, iconColor, kNavIconSize));
    }
    refreshOverviewAddressIcons(dark);
}

void MainWindow::refreshOverviewAddressIcons(bool dark)
{
    Q_UNUSED(dark);
    if (!m_style) {
        return;
    }
    const QColor iconColor = m_style->theme().palette.color(QPalette::WindowText);
    if (m_overviewReloadBtn) {
        m_overviewReloadBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/refresh-cw.svg"), iconColor, 16));
        m_overviewReloadBtn->setIconSize(QSize(16, 16));
    }
    if (m_overviewCopyBtn) {
        m_overviewCopyBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/copy.svg"), iconColor, 16));
        m_overviewCopyBtn->setIconSize(QSize(16, 16));
    }
    if (m_hardRefreshBtn) {
        m_hardRefreshBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/rotate-cw.svg"), iconColor, 16));
        m_hardRefreshBtn->setIconSize(QSize(16, 16));
    }
}

void MainWindow::setOverviewReceiveAddress(const QString& address)
{
    if (!m_overviewAddressLabel) {
        return;
    }
    if (address.isEmpty()) {
        m_overviewAddressLabel->setText(QStringLiteral("Unlock to show an address"));
    } else {
        m_overviewAddressLabel->setText(address);
    }
    const bool open = m_core && m_core->isOpen();
    const bool busy = m_core && m_core->isBusy();
    const bool hasAddr = !address.isEmpty() && address != QLatin1String("-");
    if (m_overviewReloadBtn) {
        m_overviewReloadBtn->setEnabled(open && !busy);
    }
    if (m_overviewCopyBtn) {
        m_overviewCopyBtn->setEnabled(open && !busy && hasAddr);
    }
}

void MainWindow::updateSendAddressWarning()
{
    if (!m_sendToEdit || !m_sendAddressWarnAction) {
        return;
    }
    const QString text = m_sendToEdit->text().trimmed();
    const bool show = !text.isEmpty() && !CoreBridge::isValidAddress(text);
    if (show && m_style) {
        m_sendAddressWarnAction->setIcon(
            tintedSvgIcon(QStringLiteral(":/icons/triangle-alert.svg"), QColor(0xE6, 0xA8, 0x17), 16));
    }
    m_sendAddressWarnAction->setVisible(show);
}

void MainWindow::updateSendUnitButton()
{
    if (!m_sendUnitBtn) {
        return;
    }
    const QString unit = m_sendAmountInFiat
        ? fiatByCode(m_prices ? m_prices->fiat() : m_settings.fiatCurrency()).code
        : QStringLiteral("LTC");
    m_sendUnitBtn->setText(unit);
    if (m_style) {
        const QColor iconColor = m_style->theme().palette.color(QPalette::WindowText);
        m_sendUnitBtn->setIcon(
            tintedSvgIcon(QStringLiteral(":/icons/arrow-up-down.svg"), iconColor, 14));
        m_sendUnitBtn->setIconSize(QSize(14, 14));
    }
    if (m_sendAmountEdit) {
        m_sendAmountEdit->setPlaceholderText(QStringLiteral("0.00"));
    }
}

void MainWindow::toggleSendAmountUnit()
{
    if (!m_sendAmountEdit) {
        return;
    }
    bool ok = false;
    const QString raw = m_sendAmountEdit->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
    const double current = raw.toDouble(&ok);
    const double px = m_lastQuote.valid ? m_lastQuote.price : 0.0;

    if (ok && current > 0.0 && px > 0.0) {
        if (!m_sendAmountInFiat) {
            // LTC -> fiat (always '.' so the amount validator accepts it)
            m_sendAmountEdit->setText(QLocale::c().toString(current * px, 'f', 2));
        } else {
            // fiat -> LTC
            m_sendAmountEdit->setText(QLocale::c().toString(current / px, 'f', 8));
        }
    }

    m_sendAmountInFiat = !m_sendAmountInFiat;
    m_settings.setSendAmountInFiat(m_sendAmountInFiat);
    m_settings.sync();
    updateSendUnitButton();
    updateSendAmountHint();
}

void MainWindow::fillSendAmountSats(qint64 amountSats)
{
    if (!m_sendAmountEdit || amountSats <= 0) {
        if (m_sendAmountEdit) {
            m_sendAmountEdit->clear();
        }
        updateSendAmountHint();
        return;
    }

    const double ltc = static_cast<double>(amountSats) / 100000000.0;
    if (m_sendAmountInFiat) {
        if (!m_lastQuote.valid || m_lastQuote.price <= 0.0) {
            showMessage(QStringLiteral("Waiting for price before filling fiat amount."), true);
            return;
        }
        m_sendAmountEdit->setText(QLocale::c().toString(ltc * m_lastQuote.price, 'f', 2));
    } else {
        m_sendAmountEdit->setText(QLocale::c().toString(ltc, 'f', 8));
    }
    updateSendAmountHint();
}

qint64 MainWindow::sendMaxSpendableSats() const
{
    // Conservative fee cushion so "All" still builds (fee comes from balance).
    // ~8 native inputs + 1 output ≈ 585 vB; use 750 vB headroom.
    const qint64 reserve = m_settings.feeSatPerVb() * 750;
    return qMax<qint64>(0, m_lastBalanceSats - reserve);
}

void MainWindow::updateSendAmountHint()
{
    if (!m_sendAmountHint) {
        return;
    }
    if (!m_sendAmountEdit) {
        m_sendAmountHint->setText(QStringLiteral("≈ -"));
        return;
    }

    bool ok = false;
    const QString raw = m_sendAmountEdit->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
    const double value = raw.toDouble(&ok);
    if (!ok || value <= 0.0) {
        m_sendAmountHint->setText(QStringLiteral("≈ -"));
        return;
    }
    if (!m_lastQuote.valid || m_lastQuote.price <= 0.0) {
        m_sendAmountHint->setText(QStringLiteral("≈ waiting for price…"));
        return;
    }

    if (m_sendAmountInFiat) {
        const double ltc = value / m_lastQuote.price;
        m_sendAmountHint->setText(
            QStringLiteral("≈ %1 LTC").arg(QLocale::c().toString(ltc, 'f', 8)));
    } else {
        const double fiat = value * m_lastQuote.price;
        m_sendAmountHint->setText(QStringLiteral("≈ %1").arg(formatFiatAmount(fiat)));
    }
}

qint64 MainWindow::sendAmountToSats() const
{
    if (!m_sendAmountEdit) {
        return 0;
    }
    bool ok = false;
    const double value = m_sendAmountEdit->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok || value <= 0.0) {
        return 0;
    }
    double ltc = value;
    if (m_sendAmountInFiat) {
        if (!m_lastQuote.valid || m_lastQuote.price <= 0.0) {
            return 0;
        }
        ltc = value / m_lastQuote.price;
    }
    return static_cast<qint64>(std::llround(ltc * 100000000.0));
}

void MainWindow::applyTheme(bool dark)
{
    if (m_themeManager) {
        m_themeManager->setCurrentTheme(dark ? QStringLiteral("Dark") : QStringLiteral("Light"));
    }

    if (!m_style) {
        return;
    }

    const auto& theme = m_style->theme();
    const QString navSolid = theme.backgroundColorMain2.name(QColor::HexRgb);
    const QColor contentBase = theme.backgroundColorMain1;
    const QString contentBg = contentBase.name(QColor::HexRgb);
    const QColor windowText = theme.palette.color(QPalette::WindowText);
    const QString contentFg = windowText.name(QColor::HexRgb);
    // Keep secondary copy nearly as strong as primary - muted blues/grays wash out on Mica.
    const QColor bodyColor = dark ? QColor(236, 239, 245) : QColor(28, 28, 28);
    const QColor mutedColor = dark ? QColor(210, 216, 228) : QColor(55, 55, 55);
    const QString bodyFg = bodyColor.name(QColor::HexRgb);
    const QString mutedFg = mutedColor.name(QColor::HexRgb);
    const QString accent = theme.primaryColor.name(QColor::HexRgb);
    const QString border = theme.borderColor.name(QColor::HexRgb);
    const QString surfaceBg = theme.backgroundColorMain3.name(QColor::HexRgb);
    const QString buttonBg = theme.neutralColor.name(QColor::HexRgb);
    const QString buttonBgHover = theme.neutralColorHovered.name(QColor::HexRgb);
    const QString buttonBgPressed = theme.neutralColorPressed.name(QColor::HexRgb);
    const QString navBg = m_backdropActive ? QStringLiteral("transparent") : navSolid;
    const QColor navHover = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 14);
    const QColor navSelected = dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 20);
    const QString toastColor = m_toastLabel && m_toastLabel->property("error").toBool()
        ? (dark ? QStringLiteral("#ff9a9a") : QStringLiteral("#b42318"))
        : bodyFg;

    if (m_navDelegate) {
        m_navDelegate->setColors(navHover, navSelected, theme.primaryColor, windowText);
    }

    refreshNavIcons(dark);
    refreshOverviewAddressIcons(dark);
    updateSendUnitButton();
    updateSendAddressWarning();

    if (m_brandMark) {
        const QIcon mark = appWindowIcon();
        m_brandMark->setPixmap(mark.pixmap(QSize(26, 26)));
        m_brandMark->setStyleSheet(QStringLiteral(
            "QLabel#brandMark {"
            "  background: transparent;"
            "  border: none;"
            "}"));
    }

    if (m_hardRefreshBtn) {
        m_hardRefreshBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/rotate-cw.svg"), windowText, 16));
        m_hardRefreshBtn->setIconSize(QSize(16, 16));
    }
    if (m_syncHardRefreshBtn) {
        m_syncHardRefreshBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/rotate-cw.svg"), QColor(255, 255, 255), 16));
        m_syncHardRefreshBtn->setIconSize(QSize(16, 16));
    }

    m_nav->setStyleSheet(QStringLiteral(
        "QWidget#navPanel { background-color: %1; }"
        "QLabel#brandLabel {"
        "  color: %2;"
        "  font-size: 16px;"
        "  font-weight: 700;"
        "  letter-spacing: 1.2px;"
        "  background: transparent;"
        "}"
        "QLabel#brandSub, QLabel#footerStatus {"
        "  color: %3;"
        "  font-size: 12px;"
        "  background: transparent;"
        "}"
        "QListWidget#navList {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "  color: %2;"
        "  font-size: 13px;"
        "}")
                             .arg(navBg, contentFg, bodyFg));

    m_content->setStyleSheet(
        QStringLiteral(
            "QWidget#contentPanel {"
            "  background-color: __BG__;"
            "  color: __FG__;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: __RADIUS__px;"
            "}"
            "QLabel#contentTitle {"
            "  color: __MUTED__;"
            "  font-size: 12px;"
            "  font-weight: 600;"
            "  letter-spacing: 1.4px;"
            "  text-transform: uppercase;"
            "  background: transparent;"
            "}"
            "QLabel#sectionTitle {"
            "  color: __FG__;"
            "  font-size: 13px;"
            "  font-weight: 600;"
            "  letter-spacing: 0.2px;"
            "  background: transparent;"
            "}"
            "QLabel#fieldCaption, QLabel#metricCaption {"
            "  color: __MUTED__;"
            "  font-size: 11px;"
            "  font-weight: 500;"
            "  letter-spacing: 0.3px;"
            "  background: transparent;"
            "}"
            "QLabel#metricValue {"
            "  color: __FG__;"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  background: transparent;"
            "}"
            "QLabel#balanceMeta, QLabel#balanceMetaSep {"
            "  color: __MUTED__;"
            "  font-size: 13px;"
            "  font-weight: 400;"
            "  background: transparent;"
            "}"
            "QLabel#overviewAddress {"
            "  color: __FG__;"
            "  font-size: 13px;"
            "  font-weight: 500;"
            "  background: transparent;"
            "}"
            "QWidget#sendCard {"
            "  background-color: __SURFACE__;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: 16px;"
            "}"
            "QWidget#sendAmountWrap {"
            "  background-color: __BG__;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: 10px;"
            "}"
            "QLineEdit#sendPrompt {"
            "  color: __FG__;"
            "  background-color: __BG__;"
            "  font-size: 14px;"
            "  min-height: 48px;"
            "  padding: 8px 12px;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: 10px;"
            "}"
            "QLineEdit#sendPrompt:focus {"
            "  border: 1px solid __ACCENT__;"
            "}"
            "QLineEdit#sendAmountEdit {"
            "  color: __FG__;"
            "  background: transparent;"
            "  font-size: 18px;"
            "  font-weight: 500;"
            "  border: none;"
            "  padding: 4px 0;"
            "}"
            "QLabel#sendAmountHint {"
            "  color: __MUTED__;"
            "  font-size: 11px;"
            "  font-weight: 400;"
            "  background: transparent;"
            "  padding: 0 2px;"
            "}"
            "QLabel#refreshStatus {"
            "  color: __MUTED__;"
            "  font-size: 11px;"
            "  font-weight: 500;"
            "  background: transparent;"
            "  padding: 0 6px;"
            "}"
            "QPushButton#sendUnitButton {"
            "  color: __FG__;"
            "  background: transparent;"
            "  border: none;"
            "  font-size: 12px;"
            "  font-weight: 600;"
            "  padding: 4px 8px;"
            "  min-width: 64px;"
            "}"
            "QPushButton#sendUnitButton:hover {"
            "  color: __ACCENT__;"
            "}"
            "QPushButton#sendQuickButton {"
            "  color: __MUTED__;"
            "  background: transparent;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: 8px;"
            "  font-size: 12px;"
            "  font-weight: 600;"
            "  padding: 4px 12px;"
            "  min-height: 28px;"
            "}"
            "QPushButton#sendQuickButton:hover {"
            "  color: __FG__;"
            "  border-color: __ACCENT__;"
            "}"
            "QPushButton#sendQuickButton:disabled {"
            "  color: __MUTED__;"
            "  border-color: __BORDER__;"
            "}"
            "QFrame#hairline {"
            "  background-color: __BORDER__;"
            "  border: none;"
            "  max-height: 1px;"
            "}"
            "QWidget#surfacePanel {"
            "  background-color: transparent;"
            "  border: none;"
            "  border-radius: 0;"
            "}"
            "QWidget#metricTile {"
            "  background-color: transparent;"
            "  border: none;"
            "  border-bottom: 1px solid __BORDER__;"
            "  border-radius: 0;"
            "}"
            "QWidget#txRow {"
            "  background: transparent;"
            "  border: none;"
            "  border-bottom: 1px solid __BORDER__;"
            "  border-radius: 0;"
            "}"
            "QLabel#txRowTitle {"
            "  color: __FG__;"
            "  font-size: 13px;"
            "  font-weight: 500;"
            "  background: transparent;"
            "}"
            "QLabel#txRowDetail {"
            "  color: __MUTED__;"
            "  font-size: 11px;"
            "  font-weight: 400;"
            "  background: transparent;"
            "}"
            "QLabel#txRowChevron { background: transparent; }"
            "QWidget#txDetailPanel {"
            "  background: transparent;"
            "  border: none;"
            "}"
            "QLabel#txDetailKey {"
            "  color: __MUTED__;"
            "  font-size: 11px;"
            "  font-weight: 500;"
            "  background: transparent;"
            "}"
            "QLabel#txDetailValue {"
            "  color: __FG__;"
            "  font-size: 12px;"
            "  font-weight: 400;"
            "  background: transparent;"
            "}"
            "QLabel#txCopyValue {"
            "  color: __FG__;"
            "  font-size: 12px;"
            "  font-weight: 500;"
            "  background: transparent;"
            "  text-decoration: underline;"
            "  text-decoration-color: __BORDER__;"
            "}"
            "QLabel#txAmountIn { color: #3ecf8e; font-size: 13px; font-weight: 600; background: transparent; }"
            "QLabel#txAmountOut { color: #f07178; font-size: 13px; font-weight: 600; background: transparent; }"
            "QLabel#balanceLabel {"
            "  color: __FG__;"
            "  font-size: 40px;"
            "  font-weight: 300;"
            "  letter-spacing: -1.2px;"
            "  background: transparent;"
            "}"
            "QLabel#fiatBalanceLabel {"
            "  color: __MUTED__;"
            "  font-size: 13px;"
            "  font-weight: 400;"
            "  background: transparent;"
            "}"
            "QLabel#contentBody, QLabel#contentHint, QLabel#formLabel {"
            "  color: __BODY__;"
            "  font-size: 13px;"
            "  font-weight: 400;"
            "  background: transparent;"
            "}"
            "QLabel#toastLabel {"
            "  color: __TOAST__;"
            "  font-size: 12px;"
            "  background: transparent;"
            "}"
            "QLabel#formLabel { font-weight: 500; min-width: 64px; color: __FG__; }"
            "QLabel { color: __BODY__; background: transparent; }"
            "QCheckBox { color: __BODY__; font-size: 13px; spacing: 8px; }"
            "QComboBox {"
            "  color: __FG__;"
            "  background-color: __BG__;"
            "  font-size: 14px;"
            "  min-height: 40px;"
            "  min-width: 260px;"
            "  padding: 6px 12px;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: 8px;"
            "  combobox-popup: 0;"
            "}"
            "QComboBox:focus, QComboBox:on {"
            "  border: 1px solid __ACCENT__;"
            "}"
            "QComboBox QAbstractItemView {"
            "  color: __FG__;"
            "  background-color: __BG__;"
            "  font-size: 14px;"
            "  min-width: 260px;"
            "  min-height: 220px;"
            "  outline: none;"
            "  padding: 6px;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: 8px;"
            "}"
            "QComboBox QAbstractItemView::item {"
            "  min-height: 34px;"
            "  padding: 6px 10px;"
            "}"
            "QLineEdit, QPlainTextEdit {"
            "  color: __FG__;"
            "  background-color: __BG__;"
            "  font-size: 13px;"
            "  min-height: 32px;"
            "  padding: 6px 10px;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: 8px;"
            "}"
            "QLineEdit:focus, QPlainTextEdit:focus {"
            "  border: 1px solid __ACCENT__;"
            "}"
            "QLineEdit:disabled, QPlainTextEdit:disabled {"
            "  border: 1px solid __BORDER__;"
            "  color: __MUTED__;"
            "}"
            "QListWidget#txHistoryList {"
            "  background: transparent;"
            "  border: none;"
            "  color: __BODY__;"
            "  font-size: 12px;"
            "  outline: none;"
            "}"
            "QPushButton {"
            "  color: __FG__;"
            "  background-color: __BTN__;"
            "  border: 1px solid __BORDER__;"
            "  font-size: 13px;"
            "  font-weight: 600;"
            "  min-height: 32px;"
            "  padding: 5px 14px;"
            "  border-radius: 8px;"
            "}"
            "QPushButton:hover { background-color: __BTN_HOVER__; color: __FG__; }"
            "QPushButton:pressed { background-color: __BTN_PRESS__; color: __FG__; }"
            "QPushButton:disabled { color: __MUTED__; }"
            "QPushButton#secondaryButton,"
            "QPushButton#secondaryButton:hover,"
            "QPushButton#secondaryButton:pressed {"
            "  color: __FG__;"
            "}"
            "QPushButton#secondaryButton:disabled { color: __MUTED__; }"
            "QPushButton#iconButton {"
            "  color: __FG__;"
            "  background-color: __BTN__;"
            "  border: 1px solid __BORDER__;"
            "  border-radius: 8px;"
            "  min-width: 36px;"
            "  max-width: 36px;"
            "  min-height: 36px;"
            "  max-height: 36px;"
            "  padding: 0;"
            "}"
            "QPushButton#iconButton:hover { background-color: __BTN_HOVER__; }"
            "QPushButton#iconButton:pressed { background-color: __BTN_PRESS__; }"
            "QPushButton#iconButton:disabled { color: __MUTED__; }"
            "QPushButton#primaryButton {"
            "  background-color: __ACCENT__;"
            "  color: #ffffff;"
            "  border: none;"
            "}"
            "QPushButton#primaryButton:hover { background-color: __ACCENT_HOVER__; color: #ffffff; }"
            "QPushButton#primaryButton:pressed { background-color: __ACCENT_PRESS__; color: #ffffff; }"
            "QPushButton#primaryButton:disabled { background-color: __ACCENT_DISABLED__; color: __MUTED__; }")
            .replace(QLatin1String("__BG__"), contentBg)
            .replace(QLatin1String("__FG__"), contentFg)
            .replace(QLatin1String("__BORDER__"), border)
            .replace(QLatin1String("__RADIUS__"), QString::number(kContentRadius))
            .replace(QLatin1String("__BODY__"), bodyFg)
            .replace(QLatin1String("__TOAST__"), toastColor)
            .replace(QLatin1String("__BTN__"), buttonBg)
            .replace(QLatin1String("__BTN_HOVER__"), buttonBgHover)
            .replace(QLatin1String("__BTN_PRESS__"), buttonBgPressed)
            .replace(QLatin1String("__MUTED__"), mutedFg)
            .replace(QLatin1String("__SURFACE__"), surfaceBg)
            .replace(QLatin1String("__ACCENT__"), accent)
            .replace(QLatin1String("__ACCENT_HOVER__"), theme.primaryColorHovered.name(QColor::HexRgb))
            .replace(QLatin1String("__ACCENT_PRESS__"), theme.primaryColorPressed.name(QColor::HexRgb))
            .replace(QLatin1String("__ACCENT_DISABLED__"), theme.primaryColorDisabled.name(QColor::HexRgb)));

    QPalette pal = m_content->palette();
    pal.setColor(QPalette::Window, contentBase);
    pal.setColor(QPalette::Base, contentBase);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Text, windowText);
    pal.setColor(QPalette::ButtonText, windowText);
    pal.setColor(QPalette::PlaceholderText, mutedColor);
    m_content->setPalette(pal);

    const auto applyReadablePalette = [&](QWidget* w) {
        if (!w) {
            return;
        }
        QPalette p = w->palette();
        p.setColor(QPalette::WindowText, bodyColor);
        p.setColor(QPalette::Text, windowText);
        p.setColor(QPalette::ButtonText, windowText);
        p.setColor(QPalette::PlaceholderText, mutedColor);
        w->setPalette(p);
    };
    applyReadablePalette(m_passwordEdit);
    applyReadablePalette(m_mnemonicEdit);
    applyReadablePalette(m_receiveAddressEdit);
    applyReadablePalette(m_sendToEdit);
    applyReadablePalette(m_sendAmountEdit);
    applyReadablePalette(m_sendFeeEdit);
    applyReadablePalette(m_addressList);
    applyReadablePalette(m_addrTypeCombo);
    applyReadablePalette(m_backdropCombo);
    applyReadablePalette(m_fiatCombo);
    applyReadablePalette(m_chartRangeCombo);
    applyReadablePalette(m_darkModeCheck);
    applyReadablePalette(m_gateHint);
    applyReadablePalette(m_spotPriceLabel);
    applyReadablePalette(m_fiatBalanceLabel);
    applyReadablePalette(m_chartHeaderLabel);
    applyReadablePalette(m_sendAvailableLabel);
    applyReadablePalette(m_syncPhaseValue);
    applyReadablePalette(m_syncConnValue);
    applyReadablePalette(m_syncProgressValue);
    applyReadablePalette(m_syncTipValue);
    applyReadablePalette(m_syncPeersValue);
    applyReadablePalette(m_syncMatchedValue);
    applyReadablePalette(m_syncExtraLabel);
    applyReadablePalette(m_dataDirLabel);
    applyReadablePalette(m_footerStatus);

    if (m_balanceLabel) {
        QFont balanceFont = m_balanceLabel->font();
        balanceFont.setPointSize(28);
        balanceFont.setWeight(QFont::Light);
        balanceFont.setHintingPreference(QFont::PreferNoHinting);
        balanceFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(
            QFont::PreferAntialias | QFont::PreferQuality));
        m_balanceLabel->setFont(balanceFont);
        applyReadablePalette(m_balanceLabel);
    }
    applyReadablePalette(m_toastLabel);
    applyReadablePalette(m_createBtn);
    applyReadablePalette(m_importBtn);
    applyReadablePalette(m_restoreBtn);
    applyReadablePalette(m_unlockBtn);
    applyReadablePalette(m_lockBtn);
    applyReadablePalette(m_newAddressBtn);
    applyReadablePalette(m_copyAddressBtn);
    applyReadablePalette(m_sendBtn);
    applyReadablePalette(m_sendHalfBtn);
    applyReadablePalette(m_sendAllBtn);

    if (m_priceChart) {
        m_priceChart->setAccentColor(
            QColor(46, 204, 113),
            QColor(231, 76, 60),
            dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 28),
            bodyColor);
    }

    // Keep 24h indicator colors after theme restyle.
    if (m_lastQuote.valid || m_change24hLabel || m_chartChangeLabel) {
        const bool up = m_lastQuote.change24hPct >= 0.0;
        const QString color = !m_lastQuote.valid ? mutedFg
                                                 : (up ? QStringLiteral("#2ecc71") : QStringLiteral("#e74c3c"));
        const QString css =
            QStringLiteral("QLabel#change24hLabel { color: %1; font-size: 14px; font-weight: 600; background: transparent; }")
                .arg(color);
        if (m_change24hLabel) {
            m_change24hLabel->setStyleSheet(css);
        }
        if (m_chartChangeLabel) {
            m_chartChangeLabel->setStyleSheet(css);
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Tear down network threads synchronously so Quit does not leave a zombie process.
    if (m_prices) {
        m_prices->stop();
    }
    if (m_core) {
        m_core->shutdown();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (QWindow* win = windowHandle()) {
        disconnect(win, &QWindow::screenChanged, this, nullptr);
        connect(win, &QWindow::screenChanged, this, [this](QScreen*) {
            applyTheme(m_darkModeCheck ? m_darkModeCheck->isChecked() : true);
        });
    }
    if (!m_backdropAppliedOnce) {
        reapplyBackdrop();
        m_backdropAppliedOnce = true;
    }
    // Re-rasterize icons now that the real window DPR is known.
    applyTheme(m_darkModeCheck ? m_darkModeCheck->isChecked() : true);
}

void MainWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    if (m_backdropActive) {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), QColor(0, 0, 0, 0));
    } else if (m_style) {
        painter.fillRect(rect(), m_style->theme().backgroundColorMain2);
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Intentionally no backdrop/theme reapply here - DWM Mica/Acrylic already
    // tracks the frame, and re-running applyTheme on every resize spiked CPU.
}

void MainWindow::reapplyBackdrop()
{
    const auto type = static_cast<BackdropType>(m_backdropCombo->currentData().toInt());
    const bool dark = m_darkModeCheck->isChecked();

    if (!m_capability.supported || type == BackdropType::Disabled) {
        MicaEffect::clear(this);
        applyShellMode(false);
        applyTheme(dark);
        return;
    }

    const bool ok = MicaEffect::apply(this, type, dark);
    applyShellMode(ok);
    applyTheme(dark);
}
