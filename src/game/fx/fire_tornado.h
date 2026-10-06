#pragma once

#include "engine/render/mesh.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

#include <array>
#include <span>

namespace ghost::game {
inline constexpr int kTornadoGroundGrid = 7;
inline constexpr std::size_t kTornadoGroundCells = kTornadoGroundGrid * kTornadoGroundGrid;

glm::mat3 tornadoBasis(const glm::vec3& axis);

struct TornadoInstance {
    glm::vec3 base{0.0f};
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float height = 5.0f;
    float baseRadius = 0.34f;
    float topRadius = 1.8f;
    float grow = 1.0f;
    float thick = 1.0f;
    float rope = 0.0f;
    float intensity = 1.0f;
    std::array<float, kTornadoGroundCells> ground{};
    glm::vec3 groundCenter{0.0f};
    float groundSpan = 6.0f;
    float seed = 0.0f;
};

struct TornadoStyle {
    float spin = 4.5f;
    float twist = 5.0f;
    float rise = 1.8f;
    float erosion = 1.0f;
    float bend = 0.45f;
    float density = 6.0f;
    float heat = 1.15f;
    int steps = 48;
};

struct SceneDepthInfo {
    unsigned texture = 0;
    float zNear = 0.01f;
    float zFar = 100.0f;
    float split = 0.0f;
};

class FireTornado {
public:
    FireTornado();

    void draw(std::span<const TornadoInstance> tornadoes, const TornadoStyle& style, const glm::mat4& viewProj,
              const glm::vec3& cameraPos, const glm::vec3& cameraForward, const SceneDepthInfo& depth, float time);
    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::Mesh m_box;
};

}
