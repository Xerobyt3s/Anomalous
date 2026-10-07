#pragma once

#include <glm/glm.hpp>

#include <array>
#include <string_view>

namespace ghost::game {

struct PonchoTuning {
    float stiffness = 26.0f;
    float damping = 3.2f;
    float inertia = 0.055f;
    float windDrag = 0.045f;
    float neighbour = 9.0f;
    float flutter = 0.018f;
    float reach = 0.34f;
    float drop = 0.55f;
};

PonchoTuning parsePonchoTuning(std::string_view json);

class PonchoCloth {
public:
    static constexpr int kNodes = 12;

    void reset();
    void step(const glm::vec3& anchor, const glm::mat3& basis, const glm::vec3& gravity, const glm::vec3& wind, float dt);
    const std::array<glm::vec3, kNodes>& offsets() const { return m_offset; }
    void setTuning(const PonchoTuning& tuning) { m_tuning = tuning; }

private:
    void substep(const glm::vec3& accel, const glm::vec3& air, const glm::vec3& sag, float dt);

    PonchoTuning m_tuning;
    std::array<glm::vec3, kNodes> m_offset{};
    std::array<glm::vec3, kNodes> m_velocity{};
    glm::vec3 m_lastAnchor{0.0f};
    glm::vec3 m_lastVelocity{0.0f};
    float m_clock = 0.0f;
    int m_primed = 0;
};

}
