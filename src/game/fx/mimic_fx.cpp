#include "game/fx/mimic_fx.h"

#include "engine/assets/asset_path.h"
#include "engine/render/primitives.h"

#include <glad/glad.h>

#include <string>

namespace ghost::game {
MimicFx::MimicFx()
    : m_shader(engine::assetPath("shaders/fx/blob.vert"), engine::assetPath("shaders/fx/mimic.frag")),
      m_box(engine::makeBox(glm::vec3(1.0f))) {}

void MimicFx::begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, float depthSplit, float time) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uCameraPos", cameraPos);
    m_shader.set("uSplit", depthSplit);
    m_shader.set("uTime", time);
}

void MimicFx::draw(const MimicBody& body, float seed, float thrash) {
    const glm::vec3 center = body.center();
    if (body.scale() < 0.01f) {
        return;
    }
    glm::vec3 lo = center;
    glm::vec3 hi = center;
    for (const glm::vec3& p : body.points()) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const glm::vec3 margin(0.32f);
    m_shader.set("uBoxCenter", (lo + hi) * 0.5f);
    m_shader.set("uBoxHalf", (hi - lo) * 0.5f + margin);
    m_shader.set("uCenter", center);
    m_shader.set("uScale", body.scale());
    m_shader.set("uSeed", seed);
    m_shader.set("uThrash", thrash);
    m_shader.set("uPoints", std::span<const glm::vec3>(body.points()));
    for (int i = 0; i < MimicBody::kTentacles; ++i) {
        m_shader.set(("uThick[" + std::to_string(i) + "]").c_str(), body.thickness(i));
    }
    m_box.draw();
}

void MimicFx::end() {
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
}

}
