#pragma once

#include "engine/render/gl_handle.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

#include <functional>
#include <random>
#include <vector>

namespace ghost::game {
class FireFx {
public:
    FireFx();

    void setUpField(std::function<glm::vec3(const glm::vec3&)> upAt) { m_upAt = std::move(upAt); }
    glm::vec3 upAt(const glm::vec3& at) const { return m_upAt ? m_upAt(at) : glm::vec3(0.0f, 1.0f, 0.0f); }

    void feed(const glm::vec3& center, float radius, float intensity, float dt,
              const glm::vec3& sourceVelocity = glm::vec3(0.0f));

    void jet(const glm::vec3& origin, const glm::vec3& direction, float speed, float spread, float dt);

    void burst(const glm::vec3& center, float radius, int count, float speed);

    void emit(const glm::vec3& position, const glm::vec3& velocity, float size, float life, float heat = 1.0f,
              float drag = 2.0f, float startScale = 0.55f);

    void push(const glm::vec3& center, float radius, float deltaV);

    void update(float dt);

    void draw(const glm::mat4& viewProj, const glm::mat4& invView, float time, const glm::vec3& hotTint, float nearD = -1.0f,
              float farD = 1e30f);

    void reloadShader() { m_shader.reloadIfChanged(); }
    std::size_t count() const { return m_puffs.size(); }
    float& cells() { return m_cells; }

private:
    struct Puff {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float age = 0.0f;
        float life = 1.0f;
        float size = 0.1f;
        float seed = 0.0f;
        float heat = 1.0f;
        float drag = 1.2f;
        float startScale = 0.55f;
        glm::vec3 up{0.0f};
    };

    float random01() { return m_unit(m_rng); }
    void spawn(const Puff& puff);

    std::vector<Puff> m_puffs;
    std::minstd_rand m_rng{777};
    std::uniform_real_distribution<float> m_unit{0.0f, 1.0f};
    float m_time = 0.0f;
    float m_cells = 9.0f;
    std::function<glm::vec3(const glm::vec3&)> m_upAt;

    engine::Shader m_shader;
    engine::GlVertexArray m_vao;
    engine::GlBuffer m_vbo;
    std::size_t m_vboCapacity = 0;
    std::vector<float> m_vertices;
};

}
