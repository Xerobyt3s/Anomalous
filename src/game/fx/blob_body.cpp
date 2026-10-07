#include "game/fx/blob_body.h"

#include "engine/assets/asset_path.h"
#include "engine/render/primitives.h"

#include <glad/glad.h>

#include <string>

namespace ghost::game {
BlobBody::BlobBody()
    : m_shader(engine::assetPath("shaders/fx/blob.vert"), engine::assetPath("shaders/fx/blob.frag")),
      m_box(engine::makeBox(glm::vec3(1.0f))) {}

void BlobBody::begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& lightDir, float depthSplit,
                     float time) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uCameraPos", cameraPos);
    m_shader.set("uSplit", depthSplit);
    m_shader.set("uLightDir", glm::normalize(lightDir));
    m_shader.set("uSunColor", glm::vec3(3.0f, 2.9f, 2.7f));
    m_shader.set("uTime", time);
}

void BlobBody::draw(const BodyPose& pose, float headRadius, const glm::vec3& color, float seed, float dissolve, bool cowboy,
                    std::span<const glm::vec3> hem) {
    const float reach = pose.bodyRadii.x + pose.bodyRadii.y + 0.05f;
    glm::vec3 low = glm::min(pose.body - glm::vec3(reach), pose.head - glm::vec3(headRadius + 0.04f));
    glm::vec3 high = glm::max(pose.body + glm::vec3(reach), pose.head + glm::vec3(headRadius + 0.04f));
    auto include = [&](const glm::vec3& point) {
        low = glm::min(low, point - glm::vec3(0.2f));
        high = glm::max(high, point + glm::vec3(0.2f));
    };
    for (int a = 0; a < 2; ++a) {
        for (int i = 0; i < kArmPoints; ++i) {
            const glm::vec3& point = pose.arms[static_cast<std::size_t>(a)][static_cast<std::size_t>(i)];
            include(point);
            m_shader.set(("uArms[" + std::to_string(a * kArmPoints + i) + "]").c_str(), point);
        }
    }
    for (int l = 0; l < 2; ++l) {
        const Limb& leg = pose.legs[static_cast<std::size_t>(l)];
        const glm::vec3 points[3] = {leg.root, leg.joint, leg.end};
        for (int i = 0; i < 3; ++i) {
            include(points[i]);
            m_shader.set(("uLegs[" + std::to_string(l * 3 + i) + "]").c_str(), points[i]);
        }
    }
    for (int l = 0; l < 2; ++l) {
        m_shader.set(("uFootForward[" + std::to_string(l) + "]").c_str(), pose.footForward[static_cast<std::size_t>(l)]);
    }
    if (cowboy) {
        const glm::vec3 crown = pose.head + pose.headUp * (headRadius * 0.6f + 0.1f);
        low = glm::min(low, crown - glm::vec3(0.45f));
        high = glm::max(high, crown + glm::vec3(0.45f));
        low = glm::min(low, pose.body - glm::vec3(pose.bodyRadii.x + 0.4f));
        high = glm::max(high, pose.body + glm::vec3(pose.bodyRadii.x + 0.4f));
    }
    m_shader.set("uCowboy", cowboy ? 1.0f : 0.0f);
    if (cowboy) {
        glm::vec3 ring[12]{};
        for (std::size_t i = 0; i < hem.size() && i < 12; ++i) {
            ring[i] = hem[i];
        }
        m_shader.set("uHem", std::span<const glm::vec3>(ring, 12));
    }
    m_shader.set("uBoxCenter", (low + high) * 0.5f);
    m_shader.set("uBoxHalf", (high - low) * 0.5f);
    m_shader.set("uBody", pose.body);
    m_shader.set("uBodyRadii", pose.bodyRadii);
    m_shader.set("uBodyBasis", pose.bodyBasis);
    m_shader.set("uStir", pose.stir);
    m_shader.set("uBlink", pose.blink);
    m_shader.set("uSeed", seed);
    m_shader.set("uDissolve", dissolve);
    m_shader.set("uHead", pose.head);
    m_shader.set("uHeadRadius", headRadius);
    m_shader.set("uNeck", pose.neck);
    m_shader.set("uHeadForward", pose.headForward);
    m_shader.set("uHeadUp", pose.headUp);
    m_shader.set("uSkin", color);
    m_box.draw();
}

void BlobBody::end() {
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
}

}
