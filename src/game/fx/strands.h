#pragma once

#include "engine/render/gl_handle.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace ghost::game {
class Strands {
public:
    struct Point {
        glm::vec3 position{0.0f};
        float width = 0.003f;
        glm::vec3 color{1.0f};
        float alpha = 1.0f;
        float glow = 0.0f;
    };

    Strands();
    void add(std::span<const Point> points);
    void draw(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::GlVertexArray m_vao;
    engine::GlBuffer m_vbo;
    std::size_t m_capacity = 0;
    std::vector<std::vector<Point>> m_pending;
    std::vector<float> m_vertices;
};

}
