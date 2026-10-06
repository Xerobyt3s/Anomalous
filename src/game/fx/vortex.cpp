#include "game/fx/vortex.h"

#include "engine/assets/asset_path.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace ghost::game {
namespace {
engine::Mesh makeTube() {
    constexpr int kSegments = 28;
    constexpr int kRings = 18;
    std::vector<engine::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (int ring = 0; ring <= kRings; ++ring) {
        const float h = static_cast<float>(ring) / kRings;
        for (int seg = 0; seg <= kSegments; ++seg) {
            const float u = static_cast<float>(seg) / kSegments;
            const float a = u * glm::two_pi<float>();
            vertices.push_back({{std::cos(a), h, std::sin(a)}, {std::cos(a), 0.0f, std::sin(a)}, {u, h}});
        }
    }
    const int row = kSegments + 1;
    for (int ring = 0; ring < kRings; ++ring) {
        for (int seg = 0; seg < kSegments; ++seg) {
            const auto i = static_cast<std::uint32_t>(ring * row + seg);
            const auto j = static_cast<std::uint32_t>((ring + 1) * row + seg);
            indices.insert(indices.end(), {i, j, i + 1, i + 1, j, j + 1});
        }
    }
    return engine::Mesh(vertices, indices);
}

}

Vortex::Vortex()
    : m_shader(engine::assetPath("shaders/fx/vortex.vert"), engine::assetPath("shaders/fx/vortex.frag")),
      m_tube(makeTube()) {}

void Vortex::begin(const glm::mat4& viewProj, float time) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uTime", time);
}

void Vortex::drawAlong(const glm::vec3& from, const glm::vec3& to, const Style& style) {
    const glm::vec3 axis = to - from;
    const float length = glm::length(axis);
    if (length < 1e-4f || style.intensity <= 0.001f) {
        return;
    }
    const glm::vec3 a = axis / length;
    const glm::vec3 helper = std::abs(a.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 b = glm::normalize(glm::cross(a, helper));
    const glm::vec3 c = glm::cross(a, b);
    m_shader.set("uBase", from);
    m_shader.set("uAxis", a);
    m_shader.set("uB", b);
    m_shader.set("uC", c);
    m_shader.set("uRadius", style.radius);
    m_shader.set("uHeight", length);
    m_shader.set("uLength", length);
    m_shader.set("uFunnel", style.funnel);
    m_shader.set("uSpin", style.spin);
    m_shader.set("uFlick", style.intensity);
    m_shader.set("uFire", style.ghost ? 2.0f : (style.fire ? 1.0f : 0.0f));
    m_shader.set("uSeed", style.seed);
    m_shader.set("uEndFade", style.fadeEnds ? 1.0f : 0.0f);
    m_tube.draw();
}

void Vortex::end() {
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

}
