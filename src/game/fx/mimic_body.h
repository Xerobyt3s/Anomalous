#pragma once

#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <optional>

namespace ghost::game {
class MimicBody {
public:
    static constexpr int kTentacles = 8;
    static constexpr int kPoints = 8;
    static constexpr float kLength = 1.1f;

    enum class Mode { Hidden, Reveal, Walk, Idle, Air, Lash, Tumble };

    struct Input {
        glm::vec3 center{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
        float bodyRadius = 0.25f;
        float lift = 0.0f;
        Mode mode = Mode::Walk;
        float progress = 0.0f;

        glm::vec3 target{0.0f};
        float seed = 0.0f;
        float violence = 1.0f;
    };
    using Raycast = std::function<std::optional<glm::vec3>(const glm::vec3& from, const glm::vec3& to)>;

    void update(float dt, const Input& in, const Raycast& raycast);

    const std::array<glm::vec3, kTentacles * kPoints>& points() const { return m_points; }

    glm::vec3 center() const { return m_center; }

    float scale() const { return m_scale; }
    float out(int tentacle) const { return m_out[static_cast<std::size_t>(tentacle)]; }

    float thickness(int tentacle) const;
    float linkLength(int tentacle) const;
    bool planted(int tentacle) const { return m_feet[static_cast<std::size_t>(tentacle)].planted; }
    glm::vec3 foot(int tentacle) const { return m_feet[static_cast<std::size_t>(tentacle)].at; }
    int steps() const { return m_steps; }

private:
    struct Foot {
        glm::vec3 at{0.0f};
        glm::vec3 from{0.0f};
        float stepping = -1.0f;
        float stepTime = 0.1f;
        float lift = 0.1f;
        bool planted = false;
    };

    float restLength(int tentacle) const;

    std::array<glm::vec3, kTentacles * kPoints> m_points{};
    std::array<glm::vec3, kTentacles * kPoints> m_previous{};
    std::array<Foot, kTentacles> m_feet{};
    std::array<float, kTentacles> m_out{};
    std::array<float, kTentacles> m_stretch{};
    glm::vec3 m_center{0.0f};
    glm::vec3 m_centerVelocity{0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_velocity{0.0f};
    std::array<glm::vec3, kTentacles> m_wantTip{};
    std::array<bool, kTentacles> m_free{};
    float m_time = 0.0f;
    float m_gait = 0.0f;
    float m_scale = 0.0f;
    float m_seed = 0.0f;
    float m_posed = 0.0f;
    bool m_wasAir = false;
    bool m_started = false;
    int m_steps = 0;
};

}
