#pragma once

#include "engine/render/mesh.h"
#include "engine/render/shader.h"
#include "game/player/body_rig.h"

#include <glm/glm.hpp>

#include <span>

namespace ghost::game {
class BlobBody {
public:
    BlobBody();

    void begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& lightDir, float depthSplit, float time);

    void draw(const BodyPose& pose, float headRadius, const glm::vec3& color, float seed, float dissolve = 0.0f,
              bool cowboy = false, std::span<const glm::vec3> hem = {});
    void end();

    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::Mesh m_box;
};

}
