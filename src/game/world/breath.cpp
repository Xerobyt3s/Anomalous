#include "game/world/breath.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
float breathReach(const Breath& breath) {
    if (breath.duration <= 0.0f) {
        return breath.range;
    }
    const float t = std::clamp(breath.age / (breath.duration * 0.5f), 0.0f, 1.0f);
    return breath.range * (1.0f - (1.0f - t) * (1.0f - t));
}

bool breathTouches(const Breath& breath, const glm::vec3& point) {
    if (breath.age >= breath.duration) {
        return false;
    }
    const glm::vec3 to = point - breath.origin;
    const float along = glm::dot(to, breath.direction);
    if (along < 0.0f || along > breathReach(breath)) {
        return false;
    }
    const float off = glm::length(to - breath.direction * along);

    return off <= along * std::tan(breath.halfAngle) + 0.15f;
}

}
