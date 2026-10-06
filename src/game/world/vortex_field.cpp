#include "game/world/vortex_field.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
float vortexStrength(const VortexParams& params, float age, float lifetime) {
    const float in = params.rampIn > 0.0f ? glm::smoothstep(0.0f, params.rampIn, age) : 1.0f;
    const float out = params.rampOut > 0.0f ? std::clamp((lifetime - age) / params.rampOut, 0.0f, 1.0f) : 1.0f;
    return in * out;
}

glm::vec3 airVelocity(const WindVortex& vortex, const glm::vec3& position) {
    const VortexParams& p = vortex.params;
    if (p.radius <= 0.0f || vortex.strength <= 0.0f) {
        return glm::vec3(0.0f);
    }
    const glm::vec3 axis = vortex.axis;
    const glm::vec3 offset = position - vortex.base;
    const float h = glm::dot(offset, axis);
    if (h < -0.5f || h > p.height * 1.1f) {
        return glm::vec3(0.0f);
    }
    const glm::vec3 radial = offset - axis * h;
    const float d = glm::length(radial);
    if (d >= p.radius) {
        return glm::vec3(0.0f);
    }

    const float reach = std::pow(1.0f - d / p.radius, 1.3f);
    const float top = 1.0f - glm::smoothstep(p.height * 0.9f, p.height * 1.1f, h);

    const float core = std::max(p.coreRadius, 1e-3f);
    const float outsideCore = std::min(d / core, 1.0f);

    glm::vec3 velocity{0.0f};
    if (d > 1e-4f) {
        const glm::vec3 outward = radial / d;
        const glm::vec3 around = glm::cross(axis, outward);
        velocity += around * (p.swirlSpeed * reach * outsideCore);

        const float upper = glm::smoothstep(p.height * 0.5f, p.height * 0.85f, h);
        velocity -= outward * (p.inflowSpeed * reach * outsideCore * (1.0f - upper));
        velocity += outward * (p.outflowSpeed * upper * (1.0f - glm::smoothstep(core, core * 3.0f, d)));
    }

    velocity += axis * (p.liftSpeed * (1.0f - glm::smoothstep(core, core * 1.5f, d)));
    return velocity * (top * vortex.strength);
}

glm::vec3 airVelocity(std::span<const WindVortex> vortices, const glm::vec3& position) {
    glm::vec3 sum{0.0f};
    for (const WindVortex& v : vortices) {
        sum += airVelocity(v, position);
    }
    return sum;
}

}
