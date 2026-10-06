#pragma once

#include "engine/render/mesh.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

#include <array>

namespace ghost::game {
class MaterialOrb {
public:
    enum class Kind { StormOrb = 1, HazeBubble = 2, WindCloud = 3, BallLightning = 4 };

    struct Suck {
        glm::vec3 target{0.0f};
        std::array<float, 5> detach{};
        float stream = 0.0f;
    };

    MaterialOrb();
    void begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, float depthSplit, float time);
    void draw(const glm::vec3& center, float radius, Kind kind, float seed, const Suck* suck = nullptr);

    void drawBall(const glm::vec3& center, float radius, float seed, float energy);
    void end();
    void reloadShader() { m_shader.reloadIfChanged(); }

private:
    engine::Shader m_shader;
    engine::Mesh m_box;
};

}
