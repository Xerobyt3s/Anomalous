#include "game/fx/fog_volume.h"

#include "engine/assets/asset_path.h"
#include "engine/render/primitives.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ghost::game {
namespace {
constexpr std::size_t kMaxLights = 8;

}

FogVolume::FogVolume()
    : m_shader(engine::assetPath("shaders/fx/fog.vert"), engine::assetPath("shaders/fx/fog.frag")),
      m_box(engine::makeBox(glm::vec3(1.0f))) {}

void FogVolume::draw(std::span<const FogCloud> clouds, std::span<const FogHole> holes, const FogStyle& style,
                     const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward,
                     const SceneDepthInfo& depth, std::span<const FogLight> lights, const glm::vec3& sunDir,
                     float time) {
    if (clouds.empty()) {
        return;
    }

    std::vector<const FogCloud*> order;
    for (const FogCloud& c : clouds) {
        order.push_back(&c);
    }
    std::sort(order.begin(), order.end(), [&](const FogCloud* a, const FogCloud* b) {
        return glm::distance(a->center, cameraPos) > glm::distance(b->center, cameraPos);
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
    m_shader.set("uTime", time);
    m_shader.set("uSteps", style.steps);
    m_shader.set("uDensity", style.density);
    m_shader.set("uNoiseScale", style.noiseScale);
    m_shader.set("uChurn", style.churn);
    m_shader.set("uBrightness", style.brightness);
    m_shader.set("uTint", style.tint);
    m_shader.set("uSunDir", sunDir);

    const std::size_t holeCount = std::min(holes.size(), FogField::kMaxHoles);
    m_shader.set("uHoleCount", static_cast<int>(holeCount));
    for (std::size_t i = 0; i < holeCount; ++i) {
        const FogHole& h = holes[i];
        const float open = h.life > 0.0f ? std::sqrt(std::max(0.0f, 1.0f - h.age / h.life)) : 0.0f;
        const std::string index = std::to_string(i);
        m_shader.set(("uHoleA[" + index + "]").c_str(), h.from);
        m_shader.set(("uHoleReach[" + index + "]").c_str(), h.radius * open);
        m_shader.set(("uHoleB[" + index + "]").c_str(), h.to);
    }
    const std::size_t lightCount = std::min(lights.size(), kMaxLights);
    m_shader.set("uPointCount", static_cast<int>(lightCount));
    for (std::size_t i = 0; i < lightCount; ++i) {
        const std::string index = std::to_string(i);
        m_shader.set(("uPointPos[" + index + "]").c_str(), lights[i].position);
        m_shader.set(("uPointColor[" + index + "]").c_str(), lights[i].color);
    }

    for (const FogCloud* c : order) {
        if (c->lifetime <= 0.0f || c->age >= c->lifetime) {
            continue;
        }

        const float toFunnel =
            glm::distance(glm::vec2(c->center.x, c->center.z), glm::vec2(c->pullPoint.x, c->pullPoint.z));
        const float reach = c->radius * 1.25f + c->pull * toFunnel;
        const float top = c->height * (1.3f + 1.7f * c->pull);
        m_shader.set("uPullPoint", c->pullPoint);
        m_shader.set("uPull", c->pull);
        m_shader.set("uPullReach", toFunnel + c->radius * 1.4f);
        m_shader.set("uPullPhase", c->pullPhase);
        m_shader.set("uBall", c->ball ? 1.0f : 0.0f);
        for (std::size_t r = 0; r < kFogDirections; ++r) {
            m_shader.set(("uReach[" + std::to_string(r) + "]").c_str(), c->reach[r]);
        }
        if (c->ball) {
            m_shader.set("uBoxCenter", c->center);
            m_shader.set("uBoxHalf", glm::vec3(reach, top, reach));
        } else {
            const float down = std::min(c->height * 1.3f, c->reach[3]);
            m_shader.set("uBoxCenter", c->center + glm::vec3(0.0f, (top - down) * 0.5f, 0.0f));
            m_shader.set("uBoxHalf", glm::vec3(reach, (top + down) * 0.5f, reach));
        }
        m_shader.set("uCenter", c->center);
        m_shader.set("uRadius", c->radius);
        m_shader.set("uHeight", c->height);
        m_shader.set("uAge", c->age);
        m_shader.set("uLifetime", c->lifetime);
        m_shader.set("uElectrified", std::min(1.0f, c->electrified / 0.35f));
        m_shader.set("uSeed", static_cast<float>(c->id) * 7.31f);
        m_box.draw();
    }

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}
