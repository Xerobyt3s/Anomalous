#include "game/fx/fire_tornado.h"

#include <cmath>

#include "engine/assets/asset_path.h"
#include "engine/render/primitives.h"

#include <glad/glad.h>

#include <algorithm>
#include <vector>

namespace ghost::game {
glm::mat3 tornadoBasis(const glm::vec3& axis) {
    const float length = glm::length(axis);
    const glm::vec3 up = length > 1e-4f ? axis / length : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 helper = std::abs(up.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(up, helper));
    return glm::mat3(right, up, glm::cross(right, up));
}

FireTornado::FireTornado()
    : m_shader(engine::assetPath("shaders/fx/tornado.vert"), engine::assetPath("shaders/fx/tornado.frag")),
      m_box(engine::makeBox(glm::vec3(1.0f))) {}

void FireTornado::draw(std::span<const TornadoInstance> tornadoes, const TornadoStyle& style,
                       const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward,
                       const SceneDepthInfo& depth, float time) {
    if (tornadoes.empty()) {
        return;
    }

    std::vector<const TornadoInstance*> order;
    for (const TornadoInstance& t : tornadoes) {
        order.push_back(&t);
    }
    std::sort(order.begin(), order.end(), [&](const TornadoInstance* a, const TornadoInstance* b) {
        return glm::distance(a->base, cameraPos) > glm::distance(b->base, cameraPos);
    });

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
    m_shader.set("uSpin", style.spin);
    m_shader.set("uTwist", style.twist);
    m_shader.set("uRise", style.rise);
    m_shader.set("uErosion", style.erosion);
    m_shader.set("uBend", style.bend);
    m_shader.set("uDensity", style.density);
    m_shader.set("uHeat", style.heat);
    m_shader.set("uSteps", style.steps);
    m_shader.set("uTime", time);

    for (const TornadoInstance* t : order) {
        if (t->intensity <= 0.003f || t->grow <= 0.0f) {
            continue;
        }

        const float reach = t->topRadius * 1.9f + style.bend * 3.2f;

        float low = 0.0f;
        for (const float g : t->ground) {
            low = g > -100.0f ? std::min(low, std::max(g, -3.0f)) : low;
        }
        const float top = t->height * 1.2f;
        const glm::mat3 basis = tornadoBasis(t->axis);
        m_shader.set("uBasis", basis);
        m_shader.set("uBoxCenter", t->base + basis[1] * ((top + low) * 0.5f - 0.05f));
        m_shader.set("uBoxHalf", glm::vec3(reach, (top - low) * 0.5f + 0.1f, reach));
        for (std::size_t g = 0; g < kTornadoGroundCells; ++g) {
            m_shader.set(("uGround[" + std::to_string(g) + "]").c_str(), t->ground[g]);
        }
        m_shader.set("uGroundCenter", t->groundCenter);
        m_shader.set("uGroundSpan", t->groundSpan);
        m_shader.set("uBase", t->base);
        m_shader.set("uHeight", t->height);
        m_shader.set("uBaseRadius", t->baseRadius);
        m_shader.set("uTopRadius", t->topRadius);
        m_shader.set("uGrow", t->grow);
        m_shader.set("uThick", t->thick);
        m_shader.set("uRope", t->rope);
        m_shader.set("uIntensity", t->intensity);
        m_shader.set("uSeed", t->seed);
        m_box.draw();
    }

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}
