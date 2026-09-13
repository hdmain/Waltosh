#pragma once

class QWidget;

namespace WaylandBackdrop {
[[nodiscard]] bool isAvailable();
[[nodiscard]] bool apply(QWidget* window);
void clear(QWidget* window);
}
