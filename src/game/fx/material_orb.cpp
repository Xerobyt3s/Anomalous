#include "game/fx/material_orb.h"

#include "engine/assets/asset_path.h"
#include "engine/render/primitives.h"

#include <glad/glad.h>

#include <string>

namespace ghost::game {
MaterialOrb::MaterialOrb()
    : m_shader(engine::assetPath("shaders/fx/blob.vert"), engine::assetPath("shaders/fx/orb.frag")),
      m_box(engine::makeBox(glm::vec3(1.0f))) {}

void MaterialOrb::begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, float depthSplit, float time) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uCameraPos", cameraPos);
    m_shader.set("uSplit", depthSplit);
    m_shader.set("uTime", time);
    m_shader.set("uEnergy", 1.0f);
}

void MaterialOrb::draw(const glm::vec3& center, float radius, Kind kind, float seed, const Suck* suck) {
    const float reach = radius * (kind == Kind::StormOrb ? 1.05f : 1.7f);
    glm::vec3 boxCenter = center;
    glm::vec3 boxHalf(reach);
    if (suck) {
        const glm::vec3 lo = glm::min(center, suck->target) - glm::vec3(reach);
        const glm::vec3 hi = glm::max(center, suck->target) + glm::vec3(reach);
        boxCenter = (lo + hi) * 0.5f;
        boxHalf = (hi - lo) * 0.5f;
    }
    m_shader.set("uBoxCenter", boxCenter);
    m_shader.set("uBoxHalf", boxHalf);
    m_shader.set("uCenter", center);
    m_shader.set("uRadius", radius);
    m_shader.set("uKind", static_cast<int>(kind));
    m_shader.set("uSeed", seed);
    m_shader.set("uTarget", suck ? suck->target : center);
    m_shader.set("uStream", suck ? suck->stream : 0.0f);
    for (std::size_t i = 0; i < 5; ++i) {
        m_shader.set(("uDetach[" + std::to_string(i) + "]").c_str(), suck ? suck->detach[i] : 0.0f);
    }
    m_box.draw();
}

void MaterialOrb::drawBall(const glm::vec3& center, float radius, float seed, float energy) {
    const float reach = radius * 1.9f;
    m_shader.set("uBoxCenter", center);
    m_shader.set("uBoxHalf", glm::vec3(reach));
    m_shader.set("uCenter", center);
    m_shader.set("uRadius", radius);
    m_shader.set("uKind", static_cast<int>(Kind::BallLightning));
    m_shader.set("uSeed", seed);
    m_shader.set("uEnergy", energy);
    m_box.draw();
}

void MaterialOrb::end() {
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

}
