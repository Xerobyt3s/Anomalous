#pragma once

#include <glm/glm.hpp>

#include <span>

namespace ghost::game {
struct VortexParams {
    float radius = 0.0f;
    float coreRadius = 1.4f;
    float height = 5.0f;
    float inflowSpeed = 8.5f;
    float swirlSpeed = 7.0f;
    float liftSpeed = 7.5f;
    float outflowSpeed = 11.0f;
    float rampIn = 0.8f;
    float rampOut = 1.5f;
    float objectCoupling = 6.0f;
    float playerCoupling = 1.0f;
};

struct WindVortex {
    glm::vec3 base{0.0f};
    float strength = 1.0f;
    VortexParams params;
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
};

float vortexStrength(const VortexParams& params, float age, float lifetime);

glm::vec3 airVelocity(const WindVortex& vortex, const glm::vec3& position);
glm::vec3 airVelocity(std::span<const WindVortex> vortices, const glm::vec3& position);

}
