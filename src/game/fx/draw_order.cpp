#include "game/fx/draw_order.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
std::vector<DrawWindow> drawSlices(std::vector<float> bigThings, int maxSlices) {
    std::erase_if(bigThings, [](float d) { return !std::isfinite(d) || d < 0.0f; });
    std::sort(bigThings.begin(), bigThings.end(), std::greater<float>());
    bigThings.erase(std::unique(bigThings.begin(), bigThings.end(), [](float a, float b) { return std::abs(a - b) < 1e-3f; }), bigThings.end());

    const std::size_t most = static_cast<std::size_t>(std::max(maxSlices, 2) - 1);
    if (bigThings.size() > most) {
        bigThings.erase(bigThings.begin() + 1, bigThings.begin() + static_cast<std::ptrdiff_t>(bigThings.size() - most + 1));
    }

    std::vector<DrawWindow> windows;
    DrawWindow window;
    for (const float distance : bigThings) {
        window.nearD = distance;
        windows.push_back(window);
        window.farD = distance;
    }
    window.nearD = -1.0f;
    windows.push_back(window);
    return windows;
}

}
