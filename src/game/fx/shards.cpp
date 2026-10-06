#include "game/fx/shards.h"

#include "engine/assets/asset_path.h"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <string>

namespace ghost::game {
namespace {
constexpr int kInstanceFloats = 9;

std::vector<float> crystalMesh() {
    const glm::vec3 tip{0.0f, 0.0f, 0.62f};
    const glm::vec3 tail{0.0f, 0.0f, -0.38f};
    const std::array<glm::vec3, 4> ring{glm::vec3(0.5f, 0.0f, -0.12f), glm::vec3(0.0f, 0.38f, -0.16f), glm::vec3(-0.5f, 0.0f, -0.12f),
                                        glm::vec3(0.0f, -0.38f, -0.16f)};
    std::vector<float> mesh;
    auto face = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        for (const glm::vec3& p : {a, b, c}) {
            mesh.insert(mesh.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        }
    };
    for (std::size_t i = 0; i < 4; ++i) {
        face(ring[i], ring[(i + 1) % 4], tip);
        face(ring[(i + 1) % 4], ring[i], tail);
    }
    return mesh;
}

}

Shards::Shards() : m_shader(engine::assetPath("shaders/fx/shard.vert"), engine::assetPath("shaders/fx/shard.frag")) {
    const std::vector<float> mesh = crystalMesh();
    m_vertexCount = static_cast<int>(mesh.size() / 6);
    GLuint vao = 0;
    GLuint vbo = 0;
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);
    m_vao = engine::GlVertexArray(vao);
    m_mesh = engine::GlBuffer(vbo);
    glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(mesh.size() * sizeof(float)), mesh.data(), 0);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, 6 * sizeof(float));
    for (GLuint attribute = 0; attribute < 2; ++attribute) {
        glEnableVertexArrayAttrib(vao, attribute);
        glVertexArrayAttribFormat(vao, attribute, 3, GL_FLOAT, GL_FALSE, attribute * 3 * sizeof(float));
        glVertexArrayAttribBinding(vao, attribute, 0);
    }

    for (GLuint attribute = 2; attribute < 5; ++attribute) {
        glEnableVertexArrayAttrib(vao, attribute);
        glVertexArrayAttribFormat(vao, attribute, 3, GL_FLOAT, GL_FALSE, (attribute - 2) * 3 * sizeof(float));
        glVertexArrayAttribBinding(vao, attribute, 1);
    }
    glVertexArrayBindingDivisor(vao, 1, 1);
}

void Shards::begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& lightDir, const glm::vec3& sunColor,
                   std::span<const Light> lights) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uCameraPos", cameraPos);
    m_shader.set("uLightDir", lightDir);
    m_shader.set("uSunColor", sunColor);
    const std::size_t count = std::min<std::size_t>(lights.size(), 8);
    m_shader.set("uPointCount", static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        const std::string index = std::to_string(i);
        m_shader.set(("uPointPos[" + index + "]").c_str(), lights[i].position);
        m_shader.set(("uPointColor[" + index + "]").c_str(), lights[i].color);
    }
    glBindVertexArray(m_vao.id());
}

void Shards::draw(std::span<const ShardFlock::Shard> shards) {
    if (shards.empty()) {
        return;
    }
    m_data.clear();
    for (const ShardFlock::Shard& s : shards) {
        const float shown = std::max(s.fade, 0.0f);
        m_data.insert(m_data.end(), {s.position.x, s.position.y, s.position.z, s.axis.x, s.axis.y, s.axis.z, s.length * shown,
                                     s.width * shown, s.roll});
    }
    const std::size_t bytes = m_data.size() * sizeof(float);
    if (bytes > m_capacity) {
        GLuint buffer = 0;
        glCreateBuffers(1, &buffer);
        m_capacity = bytes * 2;
        glNamedBufferStorage(buffer, static_cast<GLsizeiptr>(m_capacity), nullptr, GL_DYNAMIC_STORAGE_BIT);
        m_instances = engine::GlBuffer(buffer);
        glVertexArrayVertexBuffer(m_vao.id(), 1, buffer, 0, kInstanceFloats * sizeof(float));
    }
    glNamedBufferSubData(m_instances.id(), 0, static_cast<GLsizeiptr>(bytes), m_data.data());
    glDrawArraysInstanced(GL_TRIANGLES, 0, m_vertexCount, static_cast<GLsizei>(shards.size()));
}

void Shards::end() { glBindVertexArray(0); }

}
