#pragma once

#include "AppSettings.h"
#include "MarketTypes.h"
#include "MicaEffect.h"

#include <QMainWindow>

class CoreBridge;
class NavItemDelegate;
class PriceChartWidget;
class PriceService;
class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSoundEffect;
class QStackedWidget;
class QSystemTrayIcon;
class QWidget;

namespace oclero::qlementine {
class QlementineStyle;
class ThemeManager;
} // namespace oclero::qlementine

namespace waltosh::core {
struct Snapshot;
}

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        oclero::qlementine::QlementineStyle* style,
        oclero::qlementine::ThemeManager* themeManager,
        QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildUi();
    void rebuildGateUi(QWidget* page);
    void rebuildOverview(QWidget* page);
    void rebuildCharts(QWidget* page);
    void rebuildReceive(QWidget* page);
    void rebuildSend(QWidget* page);
    void rebuildAddresses(QWidget* page);
    void rebuildSync(QWidget* page);
    void rebuildSettings(QWidget* page);

    void loadPersistedSettings();
    void persistUiSettings();
    void refreshTxHistory(const waltosh::core::Snapshot& snap);
    void reapplyBackdrop();
    void applyTheme(bool dark);
    void applyShellMode(bool translucent);
    void refreshNavIcons(bool dark);
    void refreshOverviewAddressIcons(bool dark);
    void setOverviewReceiveAddress(const QString& address);
    void updateSendAddressWarning();
    void updateSendUnitButton();
    void updateSendAmountHint();
    void toggleSendAmountUnit();
    void updateVanityHint();
    [[nodiscard]] qint64 sendAmountToSats() const;
    void refreshNavVisibility(bool unlocked);
    void setUnlockedUi(bool unlocked);
    void setBusyUi(bool busy);
    void applySnapshot(const waltosh::core::Snapshot& snap);
    void applyMarketQuote(const MarketQuote& quote);
    void applyMarketChart(const MarketChart& chart);
    void showPage(int pageIndex, bool animate = true);
    void showMessage(const QString& text, bool error = false);
    void playSendSound();
    void playReceiveSound();
    void notifyPaymentReceived(qint64 amountSats);
    [[nodiscard]] int navRowForPage(int page) const;
    [[nodiscard]] QString formatFiatAmount(double amount) const;
    void onFiatChanged();
    void onChartRangeChanged();

    oclero::qlementine::QlementineStyle* m_style = nullptr;
    oclero::qlementine::ThemeManager* m_themeManager = nullptr;
    CoreBridge* m_core = nullptr;
    PriceService* m_prices = nullptr;
    AppSettings m_settings;

    QWidget* m_shell = nullptr;
    QWidget* m_nav = nullptr;
    QWidget* m_content = nullptr;
    QListWidget* m_navList = nullptr;
    NavItemDelegate* m_navDelegate = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_brandLabel = nullptr;
    QLabel* m_brandMark = nullptr;
    QLabel* m_footerStatus = nullptr;

    QWidget* m_gatePage = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QPlainTextEdit* m_mnemonicEdit = nullptr;
    QLabel* m_gateHint = nullptr;
    QPushButton* m_unlockBtn = nullptr;
    QPushButton* m_createBtn = nullptr;
    QPushButton* m_importBtn = nullptr;

    QLabel* m_balanceLabel = nullptr;
    QLabel* m_fiatBalanceLabel = nullptr;
    QLabel* m_change24hLabel = nullptr;
    QLabel* m_spotPriceLabel = nullptr;
    QLabel* m_overviewAddressLabel = nullptr;
    QPushButton* m_overviewReloadBtn = nullptr;
    QPushButton* m_overviewCopyBtn = nullptr;
    QLabel* m_txHistoryTitle = nullptr;
    QLabel* m_refreshStatusLabel = nullptr;
    QListWidget* m_txHistoryList = nullptr;

    PriceChartWidget* m_priceChart = nullptr;
    QLabel* m_chartHeaderLabel = nullptr;
    QLabel* m_chartChangeLabel = nullptr;
    QComboBox* m_chartRangeCombo = nullptr;

    QComboBox* m_addrTypeCombo = nullptr;
    QLineEdit* m_receiveAddressEdit = nullptr;
    QLineEdit* m_vanityWordEdit = nullptr;
    QCheckBox* m_vanityPrefixCheck = nullptr;
    QLabel* m_vanityHintLabel = nullptr;
    QLabel* m_vanityStatusLabel = nullptr;
    QPushButton* m_newAddressBtn = nullptr;
    QPushButton* m_vanityFindBtn = nullptr;
    QPushButton* m_vanityCancelBtn = nullptr;
    QPushButton* m_copyAddressBtn = nullptr;
    bool m_vanitySearching = false;

    QLineEdit* m_sendToEdit = nullptr;
    QLineEdit* m_sendAmountEdit = nullptr;
    QLineEdit* m_sendFeeEdit = nullptr;
    QLabel* m_sendAvailableLabel = nullptr;
    QLabel* m_sendAmountHint = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_sendUnitBtn = nullptr;
    QAction* m_sendAddressWarnAction = nullptr;
    bool m_sendAmountInFiat = false;
    qint64 m_pendingSendAmountSats = 0;
    QString m_pendingSendAddress;

    QPlainTextEdit* m_addressList = nullptr;
    QLabel* m_syncPhaseValue = nullptr;
    QLabel* m_syncConnValue = nullptr;
    QLabel* m_syncProgressValue = nullptr;
    QLabel* m_syncTipValue = nullptr;
    QLabel* m_syncPeersValue = nullptr;
    QLabel* m_syncMatchedValue = nullptr;
    QLabel* m_syncExtraLabel = nullptr;
    QLabel* m_syncRefreshStatusLabel = nullptr;
    QPushButton* m_hardRefreshBtn = nullptr;
    QPushButton* m_syncHardRefreshBtn = nullptr;
    QComboBox* m_backdropCombo = nullptr;
    QComboBox* m_fiatCombo = nullptr;
    QCheckBox* m_darkModeCheck = nullptr;
    QLabel* m_dataDirLabel = nullptr;
    QPushButton* m_lockBtn = nullptr;

    QLabel* m_toastLabel = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QSoundEffect* m_sendSound = nullptr;
    QSoundEffect* m_receiveSound = nullptr;

    QString m_expandedTxid;
    QString m_txHistoryFingerprint;
    qint64 m_lastBalanceSats = 0;
    bool m_wasHardRefreshing = false;
    bool m_requestedDefaultNested = false;
    MarketQuote m_lastQuote;
    bool m_backdropAppliedOnce = false;
    bool m_backdropActive = false;
    bool m_loadingSettings = false;
    BackdropCapability m_capability;
};
