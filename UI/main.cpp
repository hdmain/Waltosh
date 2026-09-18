#include "MainWindow.h"
#include "SingleInstanceGuard.h"

#include <oclero/qlementine/resources/ResourceInitialization.hpp>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <oclero/qlementine/style/ThemeManager.hpp>

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>

namespace {

[[nodiscard]] QFont pickUiFont(int pointSize = -1)
{
    static const QStringList kFamilies = {
        QStringLiteral("Segoe UI Variable"),
        QStringLiteral("Segoe UI Variable Text"),
        QStringLiteral("Inter"),
        QStringLiteral("Segoe UI"),
        QStringLiteral("IBM Plex Sans"),
        QStringLiteral("Helvetica Neue"),
    };

    QFont font;
    for (const QString& family : kFamilies) {
        if (QFontDatabase::hasFamily(family)) {
            font = QFont(family);
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFont(QStringLiteral("Segoe UI"));
    }
    if (pointSize > 0) {
        font.setPointSize(pointSize);
    }
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(static_cast<QFont::StyleStrategy>(
        QFont::PreferAntialias | QFont::PreferQuality | QFont::NoFontMerging));
    return font;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WALTOSH"));
    QApplication::setOrganizationName(QStringLiteral("WALTOSH"));
    QApplication::setApplicationDisplayName(QStringLiteral("WALTOSH"));

    SingleInstanceGuard instance(QStringLiteral("waltosh"));
    if (!instance.tryBecomePrimary()) {
        if (!instance.notifyPrimary()) {
            QMessageBox::warning(
                nullptr,
                QStringLiteral("WALTOSH"),
                QStringLiteral("WALTOSH is already running."));
        }
        return 0;
    }

    app.setWindowIcon(QIcon(QStringLiteral(":/app/icon.png")));
    if (app.windowIcon().isNull() || app.windowIcon().availableSizes().isEmpty()) {
        app.setWindowIcon(QIcon(QStringLiteral(":/app/waltosh.ico")));
    }

    oclero::qlementine::resources::initializeResources();

    auto* style = new oclero::qlementine::QlementineStyle(&app);
    style->setAnimationsEnabled(true);
    app.setStyle(style);

    auto* themeManager = new oclero::qlementine::ThemeManager(style);
    themeManager->loadDirectory(QStringLiteral(":/themes"));
    themeManager->setCurrentTheme(QStringLiteral("Dark"));

    QFont appFont = pickUiFont(10);
    // Prefer our curated face over Qlementine Inter embedding when system fonts are sharper.
    if (!style->theme().fontRegular.family().isEmpty()
        && !QFontDatabase::hasFamily(QStringLiteral("Segoe UI Variable"))
        && !QFontDatabase::hasFamily(QStringLiteral("Segoe UI"))) {
        appFont = style->theme().fontRegular;
        appFont.setHintingPreference(QFont::PreferNoHinting);
    }
    app.setFont(appFont);

    MainWindow window(style, themeManager);
    window.setWindowIcon(app.windowIcon());
    QObject::connect(&instance, &SingleInstanceGuard::anotherInstanceTriedToStart, &window,
                     [&window]() {
                         window.showFromTray();
                     });
    window.show();

    return app.exec();
}
