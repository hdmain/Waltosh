#pragma once

#include <QString>
#include <QWidget>

enum class BackdropType {
    Auto = 0,
    Disabled = 1,
    Mica = 2,
    Acrylic = 3,
    MicaAlt = 4,
};

enum class BackdropBackend {
    Disabled = 0,
    WindowsDwm,
    LinuxKdeBlur,
    LinuxWaylandBlur,
};

struct BackdropCapability {
    bool supported = false;
    BackdropBackend backend = BackdropBackend::Disabled;
    QString name;
    QString detail;
};

namespace MicaEffect {
[[nodiscard]] BackdropCapability probe();
[[nodiscard]] bool isSupported();
[[nodiscard]] bool apply(QWidget* window, BackdropType type = BackdropType::Mica, bool darkMode = true);
void clear(QWidget* window);
void setDarkMode(QWidget* window, bool enabled);
}
