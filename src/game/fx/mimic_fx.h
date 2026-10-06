#pragma once

#include "engine/render/mesh.h"
#include "engine/render/shader.h"
#include "game/fx/mimic_body.h"

#include <glm/glm.hpp>

namespace ghost::game {
class MimicFx {
public:
    MimicFx();
    void begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, float depthSplit, float time);
    void draw(const MimicBody& body, float seed, float thrash);
    void end();
    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::Mesh m_box;
};

}
