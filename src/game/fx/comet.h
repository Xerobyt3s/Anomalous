#pragma once

#include "engine/render/gl_handle.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

namespace ghost::game {
class Comet {
public:
    Comet();

    void begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, float time);

    void draw(const glm::vec3& head, const glm::vec3& direction, float radius, float tailLength, float fade,
              float brightness, float seed);
    void end();

    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    void quad(const glm::vec3 corners[4], const glm::vec2 uvs[4]);

    engine::Shader m_shader;
    engine::GlVertexArray m_vao;
    engine::GlBuffer m_vbo;
    glm::vec3 m_cameraPos{0.0f};
};

}
