#pragma once

#include "engine/assets/model.h"
#include "engine/render/gl_handle.h"

#include <cstdint>
#include <span>
#include <vector>

namespace ghost::engine {
class Mesh {
public:
    Mesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);

    void draw() const;
    void drawRange(GLsizei first, GLsizei count) const;
    GLuint vao() const { return m_vao.id(); }
    GLsizei indexCount() const { return m_indexCount; }

private:
    GlVertexArray m_vao;
    GlBuffer m_vbo;
    GlBuffer m_ebo;
    GLsizei m_indexCount = 0;
};

std::vector<Mesh> uploadMeshes(const ModelData& model);

}
