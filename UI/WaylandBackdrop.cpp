#include "WaylandBackdrop.h"

#if defined(Q_OS_LINUX) && defined(HAVE_WAYLAND)

#  include <QGuiApplication>
#  include <QHash>
#  include <QWindow>

#  include <cstring>

#  include <wayland-client.h>

#  include "ext-background-effect-v1-client.h"

namespace WaylandBackdrop {
namespace {

struct State {
    ext_background_effect_manager_v1* manager = nullptr;
    wl_compositor* compositor = nullptr;
    bool blurCapability = false;
    bool registryDone = false;
};

State& state()
{
    static State s;
    return s;
}

struct WindowState {
    ext_background_effect_surface_v1* effect = nullptr;
    wl_surface* surface = nullptr;
};

QHash<WId, WindowState>& windowStates()
{
    static QHash<WId, WindowState> states;
    return states;
}

wl_display* waylandDisplay()
{
    if (!qGuiApp) {
        return nullptr;
    }
    auto* iface = qGuiApp->platformNativeInterface();
    if (!iface) {
        return nullptr;
    }
    return static_cast<wl_display*>(iface->nativeResourceForIntegration(QByteArrayLiteral("display")));
}

wl_surface* surfaceFor(QWidget* window)
{
    if (!window || !window->windowHandle() || !qGuiApp) {
        return nullptr;
    }
    auto* iface = qGuiApp->platformNativeInterface();
    if (!iface) {
        return nullptr;
    }
    return static_cast<wl_surface*>(
        iface->nativeResourceForWindow(QByteArrayLiteral("surface"), window->windowHandle()));
}

void registryGlobal(void* data,
                    wl_registry* registry,
                    uint32_t name,
                    const char* interface,
                    uint32_t version)
{
    auto& s = *static_cast<State*>(data);
    if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0) {
        s.manager = static_cast<ext_background_effect_manager_v1*>(
            wl_registry_bind(registry,
                             name,
                             &ext_background_effect_manager_v1_interface,
                             std::min(version, 1u)));
    } else if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        s.compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    }
}

void registryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/)
{
}

void managerCapability(void* data,
                       ext_background_effect_manager_v1* /*manager*/,
                       uint32_t capability)
{
    auto& s = *static_cast<State*>(data);
    s.blurCapability = (capability & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR) != 0;
}

const ext_background_effect_manager_v1_listener kManagerListener = {
    managerCapability,
};

const wl_registry_listener kRegistryListener = {
    registryGlobal,
    registryGlobalRemove,
};

bool ensureGlobals()
{
    auto& s = state();
    if (s.registryDone) {
        return s.manager != nullptr && s.compositor != nullptr && s.blurCapability;
    }

    wl_display* disp = waylandDisplay();
    if (!disp) {
        return false;
    }

    wl_registry* registry = wl_display_get_registry(disp);
    if (!registry) {
        return false;
    }

    wl_registry_add_listener(registry, &kRegistryListener, &s);
    wl_display_roundtrip(disp);

    if (s.manager) {
        ext_background_effect_manager_v1_add_listener(s.manager, &kManagerListener, &s);
        wl_display_roundtrip(disp);
    }

    wl_registry_destroy(registry);
    s.registryDone = true;
    return s.manager != nullptr && s.compositor != nullptr && s.blurCapability;
}

bool applyRegion(QWidget* window, bool enable)
{
    if (!window || !window->windowHandle()) {
        return false;
    }
    if (!ensureGlobals()) {
        return false;
    }

    auto& s = state();
    const WId wid = window->winId();
    wl_surface* surf = surfaceFor(window);
    if (!surf) {
        return false;
    }

    WindowState& ws = windowStates()[wid];
    if (!ws.effect || ws.surface != surf) {
        if (ws.effect) {
            ext_background_effect_surface_v1_destroy(ws.effect);
            ws.effect = nullptr;
        }
        ws.surface = surf;
        ws.effect = ext_background_effect_manager_v1_get_background_effect(s.manager, surf);
    }

    if (!enable) {
        ext_background_effect_surface_v1_set_blur_region(ws.effect, nullptr);
        wl_surface_commit(surf);
        return false;
    }

    const int w = window->width();
    const int h = window->height();
    if (w <= 0 || h <= 0) {
        return false;
    }

    wl_region* region = wl_compositor_create_region(s.compositor);
    wl_region_add(region, 0, 0, w, h);
    ext_background_effect_surface_v1_set_blur_region(ws.effect, region);
    wl_region_destroy(region);
    wl_surface_commit(surf);
    return true;
}

} // namespace

bool isAvailable()
{
    if (!QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive)) {
        return false;
    }
    return ensureGlobals();
}

bool apply(QWidget* window)
{
    return applyRegion(window, true);
}

void clear(QWidget* window)
{
    if (!window) {
        return;
    }

    const WId wid = window->winId();
    if (!windowStates().contains(wid)) {
        return;
    }

    applyRegion(window, false);
    WindowState ws = windowStates().take(wid);
    if (ws.effect) {
        ext_background_effect_surface_v1_destroy(ws.effect);
    }
}

} // namespace WaylandBackdrop

#else

namespace WaylandBackdrop {

bool isAvailable()
{
    return false;
}

bool apply(QWidget* /*window*/)
{
    return false;
}

void clear(QWidget* /*window*/)
{
}

} // namespace WaylandBackdrop

#endif
