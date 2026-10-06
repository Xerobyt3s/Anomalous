#pragma once

#include "engine/render/mesh.h"
#include "engine/render/shader.h"
#include "game/fx/fire_tornado.h"
#include "game/world/fog_field.h"

#include <glm/glm.hpp>

#include <span>

namespace ghost::game {
struct FogStyle {
    float density = 1.7f;
    int steps = 48;
    float noiseScale = 0.55f;
    float churn = 1.0f;
    float brightness = 1.0f;
    glm::vec3 tint{0.84f, 0.9f, 0.93f};
};

struct FogLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{0.0f};
};

class FogVolume {
public:
    FogVolume();

    void draw(std::span<const FogCloud> clouds, std::span<const FogHole> holes, const FogStyle& style,
              const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward,
              const SceneDepthInfo& depth, std::span<const FogLight> lights, const glm::vec3& sunDir, float time);
    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::Mesh m_box;
};

}
