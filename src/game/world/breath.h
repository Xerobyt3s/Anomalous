#pragma once

#include "game/ammo/element.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace ghost::game {
struct Breath {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float range = 5.0f;
    float halfAngle = 0.28f;
    float age = 0.0f;
    float duration = 0.5f;
    ElementId element = kPlainElement;
    std::uint32_t id = 0;
    std::uint8_t shooter = 0;
};

float breathReach(const Breath& breath);

bool breathTouches(const Breath& breath, const glm::vec3& point);

}
