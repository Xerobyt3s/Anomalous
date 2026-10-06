#include "game/fx/fire_fx.h"

#include "engine/assets/asset_path.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ghost::game {
namespace {
constexpr std::size_t kMaxPuffs = 4000;
constexpr int kFloatsPerVertex = 9;

glm::vec3 anyPerpendicular(const glm::vec3& v) {
    const glm::vec3 helper = std::abs(v.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    return glm::normalize(glm::cross(v, helper));
}

}

FireFx::FireFx() : m_shader(engine::assetPath("shaders/fx/fire.vert"), engine::assetPath("shaders/fx/fire.frag")) {
    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    m_vao = engine::GlVertexArray(vao);
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float));
    glVertexArrayAttribBinding(vao, 1, 0);
    glEnableVertexArrayAttrib(vao, 2);
    glVertexArrayAttribFormat(vao, 2, 4, GL_FLOAT, GL_FALSE, 5 * sizeof(float));
    glVertexArrayAttribBinding(vao, 2, 0);
}

void FireFx::spawn(const Puff& puff) {
    if (m_puffs.size() < kMaxPuffs) {
        m_puffs.push_back(puff);
        if (glm::dot(puff.up, puff.up) < 1e-6f) {
            m_puffs.back().up = upAt(puff.position);
        }
    }
}

void FireFx::feed(const glm::vec3& center, float radius, float intensity, float dt, const glm::vec3& sourceVelocity) {
    const glm::vec3 up = upAt(center);
    const float expected = dt * (30.0f + 120.0f * radius) * intensity;
    int count = static_cast<int>(expected);
    if (random01() < expected - static_cast<float>(count)) {
        ++count;
    }
    const glm::vec3 side = anyPerpendicular(up);
    const glm::vec3 side2 = glm::cross(up, side);
    for (int i = 0; i < count; ++i) {
        const float a = random01() * glm::two_pi<float>();
        const float r = std::sqrt(random01()) * 0.45f * radius;
        Puff p;
        p.position = center + (side * std::cos(a) + side2 * std::sin(a)) * r;
        p.velocity = sourceVelocity * 0.6f + side * ((random01() - 0.5f) * 0.24f) +
                     side2 * ((random01() - 0.5f) * 0.24f) + up * (0.7f + random01() * 0.7f);
        p.life = 0.6f + random01() * 0.6f;
        p.size = radius * (1.5f + random01() * 0.8f);
        p.seed = random01() * 100.0f;
        p.heat = std::min(1.0f, 0.6f + 0.4f * intensity);
        p.up = up;
        spawn(p);
    }
}

void FireFx::jet(const glm::vec3& origin, const glm::vec3& direction, float speed, float spread, float dt) {
    const float expected = 260.0f * dt;
    int count = static_cast<int>(expected);
    if (random01() < expected - static_cast<float>(count)) {
        ++count;
    }
    const glm::vec3 dir = glm::normalize(direction);
    const glm::vec3 side = anyPerpendicular(dir);
    const glm::vec3 side2 = glm::cross(dir, side);
    for (int i = 0; i < count; ++i) {
        const float a = random01() * glm::two_pi<float>();
        const float s = random01() * spread;
        Puff p;
        p.position = origin;
        p.velocity = glm::normalize(dir + (side * std::cos(a) + side2 * std::sin(a)) * s) * speed *
                     (0.85f + 0.15f * random01());
        p.life = 0.45f + random01() * 0.4f;
        p.size = 0.25f + random01() * 0.2f;
        p.seed = random01() * 100.0f;
        p.heat = 1.0f;
        p.drag = 2.6f;
        p.startScale = 1.0f;
        spawn(p);
    }
}

void FireFx::burst(const glm::vec3& center, float radius, int count, float speed) {
    const glm::vec3 up = upAt(center);
    const glm::vec3 side = anyPerpendicular(up);
    const glm::vec3 side2 = glm::cross(up, side);
    for (int i = 0; i < count; ++i) {
        const glm::vec3 dir = glm::normalize(side * (random01() - 0.5f) + up * (random01() * 0.8f + 0.2f) + side2 * (random01() - 0.5f));
        Puff p;
        p.position = center;
        p.velocity = dir * speed * (0.4f + 0.6f * random01());
        p.life = 0.5f + random01() * 0.5f;
        p.size = radius * (1.2f + random01() * 0.8f);
        p.seed = random01() * 100.0f;
        p.heat = 1.0f;
        p.drag = 2.0f;
        p.up = up;
        spawn(p);
    }
}

void FireFx::emit(const glm::vec3& position, const glm::vec3& velocity, float size, float life, float heat, float drag,
                  float startScale) {
    Puff p;
    p.position = position;
    p.velocity = velocity;
    p.size = size;
    p.life = life;
    p.heat = heat;
    p.drag = drag;
    p.startScale = startScale;
    p.seed = random01() * 100.0f;
    spawn(p);
}

void FireFx::push(const glm::vec3& center, float radius, float deltaV) {
    for (Puff& p : m_puffs) {
        const glm::vec3 d = p.position - center;
        const float distance = glm::length(d);
        if (distance < radius && distance > 1e-4f) {
            p.velocity += d / distance * deltaV * (1.0f - distance / radius);
        }
    }
}

void FireFx::update(float dt) {
    m_time += dt;
    for (Puff& p : m_puffs) {
        p.age += dt;
        const glm::vec3 side = anyPerpendicular(p.up);
        const glm::vec3 side2 = glm::cross(p.up, side);
        p.velocity += p.up * (1.6f * dt);
        p.velocity += side * (std::sin(m_time * 2.7f + p.seed) * 0.5f * dt);
        p.velocity += side2 * (std::cos(m_time * 2.1f + p.seed * 1.3f) * 0.5f * dt);
        p.velocity *= std::max(0.0f, 1.0f - p.drag * dt);
        p.position += p.velocity * dt;
    }
    std::erase_if(m_puffs, [](const Puff& p) { return p.age >= p.life; });
}

void FireFx::draw(const glm::mat4& viewProj, const glm::mat4& invView, float time, const glm::vec3& hotTint, float nearD, float farD) {
    if (m_puffs.empty()) {
        return;
    }

    const glm::vec3 eye = glm::vec3(invView[3]);
    const glm::vec3 forward = -glm::vec3(invView[2]);
    std::vector<std::size_t> order;
    order.reserve(m_puffs.size());
    for (std::size_t i = 0; i < m_puffs.size(); ++i) {
        const float distance = glm::distance(m_puffs[i].position, eye);
        if (distance > nearD && distance <= farD) {
            order.push_back(i);
        }
    }
    if (order.empty()) {
        return;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return glm::dot(m_puffs[a].position - eye, forward) > glm::dot(m_puffs[b].position - eye, forward);
    });

    static constexpr float kCorners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    m_vertices.clear();
    m_vertices.reserve(m_puffs.size() * 6 * kFloatsPerVertex);
    for (std::size_t index : order) {
        const Puff& p = m_puffs[index];
        const float age01 = std::clamp(p.age / p.life, 0.0f, 1.0f);
        const float size = p.size * (p.startScale + 0.9f * age01);
        for (const auto& corner : kCorners) {
            m_vertices.insert(m_vertices.end(), {p.position.x, p.position.y, p.position.z, corner[0], corner[1], size,
                                                 age01, p.seed, p.heat});
        }
    }

    const std::size_t bytes = m_vertices.size() * sizeof(float);
    if (bytes > m_vboCapacity) {
        GLuint vbo = 0;
        glCreateBuffers(1, &vbo);
        m_vbo = engine::GlBuffer(vbo);
        m_vboCapacity = std::max(bytes * 2, std::size_t(64 * 1024));
        glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(m_vboCapacity), nullptr, GL_DYNAMIC_STORAGE_BIT);
        glVertexArrayVertexBuffer(m_vao.id(), 0, vbo, 0, kFloatsPerVertex * sizeof(float));
    }
    glNamedBufferSubData(m_vbo.id(), 0, static_cast<GLsizeiptr>(bytes), m_vertices.data());

    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uRight", glm::vec3(invView[0]));
    m_shader.set("uUp", glm::vec3(invView[1]));
    m_shader.set("uTime", time);
    m_shader.set("uCells", m_cells);
    m_shader.set("uHotTint", hotTint);
    glBindVertexArray(m_vao.id());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size() / kFloatsPerVertex));
    glBindVertexArray(0);
}

}
