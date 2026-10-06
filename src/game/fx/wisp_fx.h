#pragma once

#include "engine/render/mesh.h"
#include "engine/render/shader.h"
#include "game/fx/fire_tornado.h"

#include <glm/glm.hpp>

namespace ghost::game {
class WispFx {
public:
    WispFx();

    void begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward,
               const SceneDepthInfo& depth, float time, bool throughWalls = false);

    void draw(const glm::vec3& heart, const glm::vec3& cloud, const glm::vec3& tail, float radius, float glow, float heat,
              float seed, float phase, float flow, float knot = 1.0f);

    void setLook(int form, const glm::vec3& deep, const glm::vec3& hot);
    void end();

    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::Mesh m_box;
};

}
