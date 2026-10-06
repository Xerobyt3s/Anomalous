#pragma once

#include "engine/render/mesh.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

namespace ghost::game {
class Vortex {
public:
    struct Style {
        float radius = 0.1f;
        float funnel = 0.0f;
        float spin = 6.0f;
        float intensity = 0.5f;
        bool fire = false;
        float seed = 0.0f;
        bool fadeEnds = false;
        bool ghost = false;
    };

    Vortex();

    void begin(const glm::mat4& viewProj, float time);
    void drawAlong(const glm::vec3& from, const glm::vec3& to, const Style& style);
    void end();

    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::Mesh m_tube;
};

}
