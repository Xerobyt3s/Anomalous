#pragma once

#include "engine/render/gl_handle.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace ghost::game {
struct BoltParams {
    int levels = 6;
    float jaggedness = 0.16f;
    int branches = 4;
    float branchLength = 0.3f;
    float subBranchChance = 0.5f;
    float straightStart = 0.0f;
};

struct BoltPath {
    std::vector<glm::vec3> points;
    std::vector<float> width;
    std::vector<float> reach;
    bool branch = false;
};

struct Bolt {
    std::vector<BoltPath> paths;
};

Bolt buildBolt(const glm::vec3& from, const glm::vec3& to, std::uint32_t seed, const BoltParams& params = {});

struct BoltStyle {
    float width = 0.03f;
    float haloScale = 7.0f;
    glm::vec3 color{0.6f, 0.7f, 1.0f};
    float intensity = 1.0f;
    float branchIntensity = 1.0f;
    float reveal = 1.0f;
    float jitter = 0.0f;
    float jitterSeed = 0.0f;
    glm::vec3 startShift{0.0f};
    float startWidth = 1.0f;
};

class Lightning {
public:
    Lightning();

    void begin(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void draw(const Bolt& bolt, const BoltStyle& style);
    void end();

    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::GlVertexArray m_vao;
    engine::GlBuffer m_vbo;
    std::size_t m_vboCapacity = 0;
    std::vector<float> m_vertices;
};

}
