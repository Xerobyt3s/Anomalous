#include "game/fx/strands.h"

#include "engine/assets/asset_path.h"

#include <glad/glad.h>

namespace ghost::game {
namespace {
constexpr int kFloatsPerVertex = 9;

glm::vec3 normalizeOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float length = glm::length(v);
    return length > 1e-6f ? v / length : fallback;
}

}

Strands::Strands() : m_shader(engine::assetPath("shaders/fx/strand.vert"), engine::assetPath("shaders/fx/strand.frag")) {
    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    m_vao = engine::GlVertexArray(vao);
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 4, GL_FLOAT, GL_FALSE, 3 * sizeof(float));
    glVertexArrayAttribBinding(vao, 1, 0);
    glEnableVertexArrayAttrib(vao, 2);
    glVertexArrayAttribFormat(vao, 2, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float));
    glVertexArrayAttribBinding(vao, 2, 0);
}

void Strands::add(std::span<const Point> points) {
    if (points.size() >= 2) {
        m_pending.emplace_back(points.begin(), points.end());
    }
}

void Strands::draw(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    m_vertices.clear();
    auto push = [&](const glm::vec3& at, const Point& p, float across) {
        m_vertices.insert(m_vertices.end(), {at.x, at.y, at.z, p.color.x, p.color.y, p.color.z, p.alpha,
                                             p.glow, across});
    };
    for (const std::vector<Point>& strand : m_pending) {
        for (std::size_t i = 0; i + 1 < strand.size(); ++i) {
            const Point& a = strand[i];
            const Point& b = strand[i + 1];
            const glm::vec3 along = normalizeOr(b.position - a.position, glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 sideA = normalizeOr(glm::cross(along, cameraPos - a.position), glm::vec3(0.0f, 1.0f, 0.0f)) * (a.width * 0.5f);
            const glm::vec3 sideB = normalizeOr(glm::cross(along, cameraPos - b.position), glm::vec3(0.0f, 1.0f, 0.0f)) * (b.width * 0.5f);
            push(a.position - sideA, a, -1.0f);
            push(a.position + sideA, a, 1.0f);
            push(b.position - sideB, b, -1.0f);
            push(b.position - sideB, b, -1.0f);
            push(a.position + sideA, a, 1.0f);
            push(b.position + sideB, b, 1.0f);
        }
    }
    m_pending.clear();
    if (m_vertices.empty()) {
        return;
    }
    const std::size_t bytes = m_vertices.size() * sizeof(float);
    if (bytes > m_capacity) {
        GLuint vbo = 0;
        glCreateBuffers(1, &vbo);
        m_capacity = bytes * 2;
        glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(m_capacity), nullptr, GL_DYNAMIC_STORAGE_BIT);
        m_vbo = engine::GlBuffer(vbo);
        glVertexArrayVertexBuffer(m_vao.id(), 0, vbo, 0, kFloatsPerVertex * sizeof(float));
    }
    glNamedBufferSubData(m_vbo.id(), 0, static_cast<GLsizeiptr>(bytes), m_vertices.data());
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    glBindVertexArray(m_vao.id());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size() / kFloatsPerVertex));
    glDepthMask(GL_TRUE);
}

}
