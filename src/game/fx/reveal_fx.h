#pragma once

#include "engine/render/gl_handle.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

#include <vector>

namespace ghost::game {
struct FireRingPoint {
    glm::vec3 ground{0.0f};
    glm::vec3 outward{1.0f, 0.0f, 0.0f};
    float along = 0.0f;
};

class RevealFx {
public:
    RevealFx();

    void drawRing(const std::vector<FireRingPoint>& points, float height, float trail, float intensity,
                  const glm::mat4& viewProj, float time);

    void beginMask();
    void endMask();
    void outline(const glm::vec3& color, float intensity, float seed, float time);

    void dim(float amount);

    void reloadShaders() {
        m_ring.reloadIfChanged();
        m_outline.reloadIfChanged();
    }

private:
    engine::Shader m_ring;
    engine::Shader m_outline;
    engine::GlVertexArray m_ringVao;
    engine::GlBuffer m_ringVbo;
    std::size_t m_ringCapacity = 0;
    std::vector<float> m_vertices;

    engine::GlVertexArray m_screenVao;
    engine::GlFramebuffer m_maskFbo;
    engine::GlTexture m_mask;
    glm::ivec2 m_maskSize{0};
    int m_sceneFbo = 0;
};

}
