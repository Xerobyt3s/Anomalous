#pragma once

#include <limits>
#include <vector>

namespace ghost::game {
struct DrawWindow {
    float nearD = -1.0f;
    float farD = std::numeric_limits<float>::infinity();

    bool holds(float distance) const { return distance > nearD && distance <= farD; }
};

std::vector<DrawWindow> drawSlices(std::vector<float> bigThings, int maxSlices = 24);

}
