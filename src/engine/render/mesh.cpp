#include "engine/render/mesh.h"

#include <cstddef>
#include <cstdint>

namespace ghost::engine {
Mesh::Mesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    if (vertices.empty() || indices.empty()) {
        return;
    }

    GLuint vbo = 0;
    glCreateBuffers(1, &vbo);
    m_vbo = GlBuffer(vbo);
    glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), 0);

    GLuint ebo = 0;
    glCreateBuffers(1, &ebo);
    m_ebo = GlBuffer(ebo);
    glNamedBufferStorage(ebo, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data(), 0);

    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    m_vao = GlVertexArray(vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao, ebo);

    auto attribute = [vao](GLuint location, GLint components, std::size_t offset) {
        glEnableVertexArrayAttrib(vao, location);
        glVertexArrayAttribFormat(vao, location, components, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offset));
        glVertexArrayAttribBinding(vao, location, 0);
    };
    attribute(0, 3, offsetof(Vertex, position));
    attribute(1, 3, offsetof(Vertex, normal));
    attribute(2, 2, offsetof(Vertex, uv));

    m_indexCount = static_cast<GLsizei>(indices.size());
}

void Mesh::draw() const {
    if (m_indexCount == 0) {
        return;
    }
    glBindVertexArray(m_vao.id());
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

void Mesh::drawRange(GLsizei first, GLsizei count) const {
    if (count <= 0) {
        return;
    }
    glBindVertexArray(m_vao.id());
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT,
                   reinterpret_cast<const void*>(static_cast<std::uintptr_t>(first) * sizeof(std::uint32_t)));
}

std::vector<Mesh> uploadMeshes(const ModelData& model) {
    std::vector<Mesh> meshes;
    meshes.reserve(model.meshes.size());
    for (const MeshData& data : model.meshes) {
        meshes.emplace_back(data.vertices, data.indices);
    }
    return meshes;
}

}
