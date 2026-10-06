#include "game/fx/wisp_fx.h"

#include "engine/assets/asset_path.h"
#include "engine/render/primitives.h"

#include <glad/glad.h>

namespace ghost::game {
WispFx::WispFx()
    : m_shader(engine::assetPath("shaders/fx/wisp.vert"), engine::assetPath("shaders/fx/wisp.frag")),
      m_box(engine::makeBox(glm::vec3(1.0f))) {}

void WispFx::begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward,
                   const SceneDepthInfo& depth, float time, bool throughWalls) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uCameraPos", cameraPos);
    m_shader.set("uCameraForward", cameraForward);
    glBindTextureUnit(0, depth.texture);
    m_shader.set("uSceneDepth", 0);
    m_shader.set("uNF", glm::vec2(depth.zNear, depth.zFar));
    m_shader.set("uSplit", depth.split);
    m_shader.set("uTime", time);
    m_shader.set("uSteps", 44);
    m_shader.set("uThroughWalls", throughWalls ? 1.0f : 0.0f);
    setLook(0, glm::vec3(0.01f, 0.06f, 0.34f), glm::vec3(0.05f, 0.8f, 1.0f));
}

void WispFx::draw(const glm::vec3& heart, const glm::vec3& cloud, const glm::vec3& tail, float radius, float glow,
                  float heat, float seed, float phase, float flow, float knot) {
    if (glow <= 0.003f || radius <= 0.0f) {
        return;
    }
    const float tailLength = glm::length(tail);
    const glm::vec3 along = tailLength > 1e-4f ? tail / tailLength : glm::vec3(0.0f, 1.0f, 0.0f);

    const float lag = glm::distance(heart, cloud);
    m_shader.set("uBoxCenter", glm::mix(cloud, heart, 0.5f) + along * (radius * 0.6f));
    m_shader.set("uBoxHalf", glm::vec3(radius * 1.9f + lag * 0.5f));
    m_shader.set("uCenter", cloud);
    m_shader.set("uHeart", heart);
    m_shader.set("uPhase", phase);
    m_shader.set("uFlow", flow);
    m_shader.set("uKnot", knot);
    m_shader.set("uRadius", radius);
    m_shader.set("uTail", along);
    m_shader.set("uGlow", glow);
    m_shader.set("uHeat", heat);
    m_shader.set("uSeed", seed);
    m_box.draw();
}

void WispFx::setLook(int form, const glm::vec3& deep, const glm::vec3& hot) {
    m_shader.set("uForm", form);
    m_shader.set("uDeep", deep);
    m_shader.set("uHot", hot);
}

void WispFx::end() {
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}
