#pragma once

#include "engine/render/gl_handle.h"
#include "engine/render/shader.h"
#include "game/fx/shard_flock.h"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace ghost::game {
class Shards {
public:
    struct Light {
        glm::vec3 position{0.0f};
        glm::vec3 color{0.0f};
    };

    Shards();
    void begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& lightDir, const glm::vec3& sunColor,
               std::span<const Light> lights);
    void draw(std::span<const ShardFlock::Shard> shards);
    void end();
    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::GlVertexArray m_vao;
    engine::GlBuffer m_mesh;
    engine::GlBuffer m_instances;
    std::size_t m_capacity = 0;
    int m_vertexCount = 0;
    std::vector<float> m_data;
};

}
