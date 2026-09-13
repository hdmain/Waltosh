#include "MicaEffect.h"

#include "WaylandBackdrop.h"

#include <QGuiApplication>
#include <QWindow>

#ifdef Q_OS_WIN
#  include <Windows.h>
#  include <dwmapi.h>

#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  endif
#  ifndef DWMWA_SYSTEMBACKDROP_TYPE
#    define DWMWA_SYSTEMBACKDROP_TYPE 38
#  endif
#  ifndef DWMWA_MICA_EFFECT
#    define DWMWA_MICA_EFFECT 1029
#  endif
#endif

#if defined(Q_OS_LINUX) && defined(HAVE_X11)
#  include <X11/Xatom.h>
#  include <X11/Xlib.h>
#endif

namespace MicaEffect {
namespace {

#ifdef Q_OS_WIN
HWND hwndOf(QWidget* window)
{
    if (!window) {
        return nullptr;
    }
    return reinterpret_cast<HWND>(window->winId());
}

bool applyWindows(QWidget* window, BackdropType type, bool darkMode)
{
    HWND hwnd = hwndOf(window);
    if (!hwnd) {
        return false;
    }

    window->setAttribute(Qt::WA_TranslucentBackground, true);
    window->setAttribute(Qt::WA_NoSystemBackground, true);

    BOOL immersive = darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersive, sizeof(immersive));

    if (type == BackdropType::Disabled) {
        const int none = static_cast<int>(BackdropType::Disabled);
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &none, sizeof(none));
        return false;
    }

    const int backdrop = static_cast<int>(type == BackdropType::Auto ? BackdropType::Mica : type);
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    if (FAILED(hr)) {
        const BOOL mica = (type == BackdropType::Mica || type == BackdropType::MicaAlt
                           || type == BackdropType::Auto || type == BackdropType::Acrylic)
            ? TRUE
            : FALSE;
        hr = DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFECT, &mica, sizeof(mica));
    }

    MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    return SUCCEEDED(hr);
}
#endif

#if defined(Q_OS_LINUX)
bool isKdePlasmaSession()
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    const QString session = qEnvironmentVariable("DESKTOP_SESSION").toLower();
    return desktop.contains(QStringLiteral("kde"))
        || desktop.contains(QStringLiteral("plasma"))
        || session.contains(QStringLiteral("plasma"))
        || qEnvironmentVariableIsSet("KDE_FULL_SESSION");
}

bool isCosmicSession()
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    const QString session = qEnvironmentVariable("DESKTOP_SESSION").toLower();
    return desktop.contains(QStringLiteral("cosmic"))
        || session.contains(QStringLiteral("cosmic"))
        || qEnvironmentVariableIsSet("COSMIC_SESSION");
}

bool isWayland()
{
    const QString platform = QGuiApplication::platformName();
    return platform.contains(QStringLiteral("wayland"), Qt::CaseInsensitive)
        || qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
}

bool isXcb()
{
    return QGuiApplication::platformName().contains(QStringLiteral("xcb"), Qt::CaseInsensitive);
}

#if defined(HAVE_X11)
bool applyKdeX11Blur(QWidget* window, bool enable)
{
    auto* x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11App) {
        return false;
    }

    Display* display = x11App->display();
    if (!display) {
        return false;
    }

    const WId wid = window->winId();
    Atom atom = XInternAtom(display, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
    if (atom == None) {
        return false;
    }

    if (!enable) {
        XDeleteProperty(display, static_cast<Window>(wid), atom);
        XFlush(display);
        return false;
    }

    // Empty property = blur the entire window (KWin convention).
    XChangeProperty(display,
                    static_cast<Window>(wid),
                    atom,
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    nullptr,
                    0);
    XFlush(display);
    return true;
}
#endif

bool applyLinux(QWidget* window, BackdropType type, bool /*darkMode*/)
{
    if (!window) {
        return false;
    }

    if (type == BackdropType::Disabled || !isSupported()) {
        window->setAttribute(Qt::WA_TranslucentBackground, false);
        window->setAttribute(Qt::WA_NoSystemBackground, false);
        WaylandBackdrop::clear(window);
#if defined(HAVE_X11)
        if (isKdePlasmaSession() && isXcb()) {
            applyKdeX11Blur(window, false);
        }
#endif
        return false;
    }

    // COSMIC / Plasma 6.7+ / niri: ext-background-effect-v1 on Wayland.
    if (isWayland() && WaylandBackdrop::isAvailable()) {
        window->setAttribute(Qt::WA_TranslucentBackground, true);
        window->setAttribute(Qt::WA_NoSystemBackground, true);
        return WaylandBackdrop::apply(window);
    }

    // KWin blur on X11 (legacy KDE protocol).
    if (isKdePlasmaSession() && isXcb()) {
#if defined(HAVE_X11)
        window->setAttribute(Qt::WA_TranslucentBackground, true);
        window->setAttribute(Qt::WA_NoSystemBackground, true);
        return applyKdeX11Blur(window, true);
#else
        Q_UNUSED(window);
        return false;
#endif
    }

    return false;
}
#endif

} // namespace

BackdropCapability probe()
{
    BackdropCapability cap;

#ifdef Q_OS_WIN
    cap.supported = true;
    cap.backend = BackdropBackend::WindowsDwm;
    cap.name = QStringLiteral("Windows DWM");
    cap.detail = QStringLiteral("DwmSetWindowAttribute (SYSTEMBACKDROP_TYPE / Mica)");
    return cap;
#elif defined(Q_OS_LINUX)
    if (isWayland() && WaylandBackdrop::isAvailable()) {
        cap.supported = true;
        cap.backend = BackdropBackend::LinuxWaylandBlur;
        if (isCosmicSession()) {
            cap.name = QStringLiteral("COSMIC (ext-background-effect-v1)");
            cap.detail = QStringLiteral("Frosted glass via cosmic-comp on Wayland");
        } else {
            cap.name = QStringLiteral("Wayland ext-background-effect-v1");
            cap.detail = QStringLiteral("Background blur via ext-background-effect-v1");
        }
        return cap;
    }

    if (isKdePlasmaSession() && isXcb()) {
#  if defined(HAVE_X11)
        cap.supported = true;
        cap.backend = BackdropBackend::LinuxKdeBlur;
        cap.name = QStringLiteral("KDE / KWin blur");
        cap.detail = QStringLiteral("X11 atom _KDE_NET_WM_BLUR_BEHIND_REGION");
#  else
        cap.supported = false;
        cap.backend = BackdropBackend::Disabled;
        cap.name = QStringLiteral("KDE detected (no X11 build)");
        cap.detail = QStringLiteral("Rebuild with X11 to enable KWin blur; using opaque Qt fallback");
#  endif
        return cap;
    }

    cap.supported = false;
    cap.backend = BackdropBackend::Disabled;
    if (isCosmicSession() && isWayland()) {
        cap.name = QStringLiteral("COSMIC (Wayland)");
        cap.detail = QStringLiteral("ext-background-effect-v1 not available; rebuild with wayland-scanner or use opaque Qt fallback");
    } else if (isWayland()) {
        cap.name = QStringLiteral("Wayland");
        cap.detail = QStringLiteral("No portable window blur API for Qt; using opaque Qt fallback");
    } else if (!isKdePlasmaSession()) {
        cap.name = QStringLiteral("Linux compositor");
        cap.detail = QStringLiteral("Blur API not available (need Plasma/KWin on X11); using opaque Qt fallback");
    } else {
        cap.name = QStringLiteral("Linux");
        cap.detail = QStringLiteral("Backdrop effect unavailable; using opaque Qt fallback");
    }
    return cap;
#else
    cap.supported = false;
    cap.backend = BackdropBackend::Disabled;
    cap.name = QStringLiteral("Unsupported platform");
    cap.detail = QStringLiteral("Using opaque Qt fallback");
    return cap;
#endif
}

bool isSupported()
{
    return probe().supported;
}

bool apply(QWidget* window, BackdropType type, bool darkMode)
{
    if (!window) {
        return false;
    }

#ifdef Q_OS_WIN
    return applyWindows(window, type, darkMode);
#elif defined(Q_OS_LINUX)
    return applyLinux(window, type, darkMode);
#else
    Q_UNUSED(type);
    Q_UNUSED(darkMode);
    clear(window);
    return false;
#endif
}

void clear(QWidget* window)
{
    if (!window) {
        return;
    }

#if defined(Q_OS_LINUX)
    WaylandBackdrop::clear(window);
#  if defined(HAVE_X11)
    if (isKdePlasmaSession() && isXcb()) {
        applyKdeX11Blur(window, false);
    }
#  endif
#endif

    window->setAttribute(Qt::WA_TranslucentBackground, false);
    window->setAttribute(Qt::WA_NoSystemBackground, false);
}

void setDarkMode(QWidget* window, bool enabled)
{
#ifdef Q_OS_WIN
    HWND hwnd = hwndOf(window);
    if (!hwnd) {
        return;
    }
    BOOL value = enabled ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
#else
    Q_UNUSED(window);
    Q_UNUSED(enabled);
#endif
}

} // namespace MicaEffect
