#include "game/fx/poncho_cloth.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {

namespace {
constexpr float kTwoPi = 6.2831853f;
constexpr float kMaxStep = 1.0f / 120.0f;
constexpr float kMaxAccel = 60.0f;

glm::vec3 outward(int node) {
    const float a = static_cast<float>(node) / static_cast<float>(PonchoCloth::kNodes) * kTwoPi;
    return glm::vec3(std::sin(a), 0.0f, std::cos(a));
}
}

PonchoTuning parsePonchoTuning(std::string_view json) {
    PonchoTuning t;
    const nlohmann::json doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object() || !doc.contains("poncho") || !doc["poncho"].is_object()) {
        return t;
    }
    const nlohmann::json& p = doc["poncho"];
    t.stiffness = std::max(p.value("stiffness", t.stiffness), 1.0f);
    t.damping = std::max(p.value("damping", t.damping), 0.0f);
    t.inertia = p.value("inertia", t.inertia);
    t.windDrag = p.value("windDrag", t.windDrag);
    t.neighbour = std::max(p.value("neighbour", t.neighbour), 0.0f);
    t.flutter = p.value("flutter", t.flutter);
    t.reach = std::max(p.value("reach", t.reach), 0.01f);
    t.drop = std::max(p.value("drop", t.drop), 0.05f);
    return t;
}

void PonchoCloth::reset() {
    m_offset.fill(glm::vec3(0.0f));
    m_velocity.fill(glm::vec3(0.0f));
    m_primed = 0;
}

void PonchoCloth::step(const glm::vec3& anchor, const glm::mat3& basis, const glm::vec3& gravity, const glm::vec3& wind,
                       float dt) {
    if (!(dt > 0.0f)) {
        return;
    }
    dt = std::min(dt, 0.1f);
    const glm::mat3 toLocal = glm::transpose(basis);
    glm::vec3 velocity(0.0f);
    glm::vec3 accel(0.0f);
    if (m_primed >= 1) {
        velocity = (anchor - m_lastAnchor) / dt;
        if (glm::length(velocity) > 40.0f) {
            velocity = glm::vec3(0.0f);
            m_primed = 0;
        }
    }
    if (m_primed >= 2) {
        accel = (velocity - m_lastVelocity) / dt;
        const float length = glm::length(accel);
        if (length > kMaxAccel) {
            accel *= kMaxAccel / length;
        }
    }
    m_primed = std::min(m_primed + 1, 2);
    m_lastAnchor = anchor;
    m_lastVelocity = velocity;

    const glm::vec3 localAccel = toLocal * accel;
    glm::vec3 air = toLocal * (wind - velocity);
    air.y = 0.0f;
    const glm::vec3 down = glm::length(gravity) > 1e-4f ? toLocal * glm::normalize(gravity) : glm::vec3(0.0f, -1.0f, 0.0f);
    const glm::vec3 sag(down.x, 0.0f, down.z);

    int steps = static_cast<int>(std::ceil(dt / kMaxStep));
    const float h = dt / static_cast<float>(steps);
    while (steps-- > 0) {
        substep(localAccel, air, sag, h);
    }
}

void PonchoCloth::substep(const glm::vec3& accel, const glm::vec3& air, const glm::vec3& sag, float dt) {
    const PonchoTuning& t = m_tuning;
    m_clock += dt;
    const float gust = glm::length(air);
    std::array<glm::vec3, kNodes> next = m_offset;
    for (int i = 0; i < kNodes; ++i) {
        const glm::vec3 out = outward(i);
        const glm::vec3 flat(m_offset[i].x, 0.0f, m_offset[i].z);
        const glm::vec3 around = (m_offset[(i + 1) % kNodes] + m_offset[(i + kNodes - 1) % kNodes]) * 0.5f;
        const float facing = 0.6f + 0.4f * std::max(0.0f, -glm::dot(gust > 1e-4f ? air / gust : glm::vec3(0.0f), out));
        glm::vec3 force = -flat * t.stiffness - m_velocity[i] * t.damping - accel * (t.inertia * t.stiffness) +
                          air * (t.windDrag * t.stiffness * facing) + sag * (t.drop * t.stiffness * 0.5f) +
                          (around - m_offset[i]) * t.neighbour;
        force += out * (std::sin(m_clock * 9.0f + static_cast<float>(i) * 1.7f) * gust * t.flutter * t.stiffness);
        force.y = 0.0f;
        m_velocity[i] += force * dt;
        m_velocity[i].y = 0.0f;
        glm::vec3 moved = flat + m_velocity[i] * dt;
        const float swing = glm::length(moved);
        if (swing > t.reach) {
            moved *= t.reach / swing;
            const glm::vec3 dir = moved / t.reach;
            m_velocity[i] -= dir * std::max(0.0f, glm::dot(m_velocity[i], dir));
        }
        const float lift = glm::dot(moved, moved) / (2.0f * t.drop);
        next[i] = glm::vec3(moved.x, lift, moved.z);
    }
    m_offset = next;
}

}
