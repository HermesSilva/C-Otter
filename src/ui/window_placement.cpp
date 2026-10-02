#include "ui/window_placement.hpp"

#include <algorithm>

namespace otter::ui {
namespace {

// Quanto da janela precisa estar dentro de um monitor para ela contar como
// "visivel": o bastante para achar e arrastar a barra de titulo.
constexpr int kMinVisibleWidth  = 160;
constexpr int kMinVisibleHeight = 80;

int overlap(int a_begin, int a_size, int b_begin, int b_size) noexcept {
    return std::min(a_begin + a_size, b_begin + b_size) - std::max(a_begin, b_begin);
}

WindowPlacement centered(int width, int height, const WorkArea& area) {
    WindowPlacement out;
    out.width  = area.width > 0 ? std::min(width, area.width) : width;
    out.height = area.height > 0 ? std::min(height, area.height) : height;
    out.x      = area.x + (area.width - out.width) / 2;
    out.y      = area.y + (area.height - out.height) / 2;
    return out;
}

} // namespace

WindowPlacement fit_window_placement(const WindowPlacement& saved,
                                     std::span<const WorkArea> monitors,
                                     int default_width, int default_height) {
    if (monitors.empty()) {
        // Sem monitor conhecido nao ha' o que conferir.
        if (saved.saved()) return saved;
        WindowPlacement out;
        out.width  = default_width;
        out.height = default_height;
        return out;
    }
    const WorkArea& primary = monitors.front();

    if (!saved.saved()) return centered(default_width, default_height, primary);

    const int width  = std::max(saved.width, kMinWindowWidth);
    const int height = std::max(saved.height, kMinWindowHeight);

    // O monitor em que a janela mais aparece.
    const WorkArea* best = nullptr;
    long long best_area = 0;
    for (const WorkArea& monitor : monitors) {
        const int w = overlap(saved.x, width, monitor.x, monitor.width);
        const int h = overlap(saved.y, height, monitor.y, monitor.height);
        if (w < kMinVisibleWidth || h < kMinVisibleHeight) continue;
        const long long area = static_cast<long long>(w) * h;
        if (area > best_area) {
            best_area = area;
            best      = &monitor;
        }
    }

    WindowPlacement out;
    if (best == nullptr) {
        // O monitor em que ela estava nao existe mais.
        out = centered(width, height, primary);
    } else {
        out.width  = std::min(width, best->width);
        out.height = std::min(height, best->height);
        out.x      = saved.x;
        // O topo nunca acima da area util: a barra de titulo ficaria fora.
        out.y      = std::max(saved.y, best->y);
    }
    out.maximized = saved.maximized;
    return out;
}

} // namespace otter::ui
