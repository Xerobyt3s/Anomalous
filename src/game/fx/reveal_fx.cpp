#include "game/fx/reveal_fx.h"

#include "engine/assets/asset_path.h"

#include <glad/glad.h>

#include <algorithm>

namespace ghost::game {
namespace {
constexpr int kFloatsPerVertex = 6;

}

RevealFx::RevealFx()
    : m_ring(engine::assetPath("shaders/fx/fire_ring.vert"), engine::assetPath("shaders/fx/fire_ring.frag")),
      m_outline(engine::assetPath("shaders/post/fullscreen.vert"), engine::assetPath("shaders/fx/outline.frag")) {
    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    m_ringVao = engine::GlVertexArray(vao);
    const int sizes[3] = {3, 2, 1};
    GLuint offset = 0;
    for (GLuint i = 0; i < 3; ++i) {
        glEnableVertexArrayAttrib(vao, i);
        glVertexArrayAttribFormat(vao, i, sizes[i], GL_FLOAT, GL_FALSE, offset);
        glVertexArrayAttribBinding(vao, i, 0);
        offset += static_cast<GLuint>(sizes[i]) * sizeof(float);
    }
    GLuint screen = 0;
    glCreateVertexArrays(1, &screen);
    m_screenVao = engine::GlVertexArray(screen);
}

void RevealFx::drawRing(const std::vector<FireRingPoint>& points, float height, float trail, float intensity,
                        const glm::mat4& viewProj, float time) {
    if (points.size() < 3 || intensity <= 0.003f) {
        return;
    }
    m_vertices.clear();
    auto push = [&](const glm::vec3& p, float u, float v, float part) {
        m_vertices.insert(m_vertices.end(), {p.x, p.y, p.z, u, v, part});
    };
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const FireRingPoint& a = points[i];
        const FireRingPoint& b = points[i + 1];

        const glm::vec3 a0 = a.ground + up * 0.02f, b0 = b.ground + up * 0.02f;
        const glm::vec3 a1 = a0 + up * height + a.outward * (height * 0.35f);
        const glm::vec3 b1 = b0 + up * height + b.outward * (height * 0.35f);
        push(a0, a.along, 0.0f, 0.0f);
        push(b0, b.along, 0.0f, 0.0f);
        push(a1, a.along, 1.0f, 0.0f);
        push(a1, a.along, 1.0f, 0.0f);
        push(b0, b.along, 0.0f, 0.0f);
        push(b1, b.along, 1.0f, 0.0f);

        const glm::vec3 a2 = a0 - a.outward * trail, b2 = b0 - b.outward * trail;
        const glm::vec3 a3 = a0 + a.outward * 0.25f, b3 = b0 + b.outward * 0.25f;
        push(a3, a.along, 0.0f, 1.0f);
        push(b3, b.along, 0.0f, 1.0f);
        push(a2, a.along, 1.0f, 1.0f);
        push(a2, a.along, 1.0f, 1.0f);
        push(b3, b.along, 0.0f, 1.0f);
        push(b2, b.along, 1.0f, 1.0f);
    }

    const std::size_t bytes = m_vertices.size() * sizeof(float);
    if (bytes > m_ringCapacity) {
        GLuint vbo = 0;
        glCreateBuffers(1, &vbo);
        m_ringVbo = engine::GlBuffer(vbo);
        m_ringCapacity = std::max(bytes * 2, std::size_t(64 * 1024));
        glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(m_ringCapacity), nullptr, GL_DYNAMIC_STORAGE_BIT);
        glVertexArrayVertexBuffer(m_ringVao.id(), 0, vbo, 0, kFloatsPerVertex * sizeof(float));
    }
    glNamedBufferSubData(m_ringVbo.id(), 0, static_cast<GLsizeiptr>(bytes), m_vertices.data());

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    m_ring.use();
    m_ring.set("uViewProj", viewProj);
    m_ring.set("uTime", time);
    m_ring.set("uIntensity", intensity);
    glBindVertexArray(m_ringVao.id());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size() / kFloatsPerVertex));
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void RevealFx::beginMask() {
    GLint viewport[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_sceneFbo);
    const glm::ivec2 size{viewport[2], viewport[3]};
    if (size != m_maskSize) {
        GLuint texture = 0;
        glCreateTextures(GL_TEXTURE_2D, 1, &texture);
        glTextureStorage2D(texture, 1, GL_RGBA8, size.x, size.y);
        glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_mask = engine::GlTexture(texture);
        GLuint fbo = 0;
        glCreateFramebuffers(1, &fbo);
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, texture, 0);
        m_maskFbo = engine::GlFramebuffer(fbo);
        m_maskSize = size;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_maskFbo.id());
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearNamedFramebufferfv(m_maskFbo.id(), GL_COLOR, 0, clear);
}

void RevealFx::endMask() { glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_sceneFbo)); }

void RevealFx::outline(const glm::vec3& color, float intensity, float seed, float time) {
    if (intensity <= 0.003f) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    m_outline.use();
    glBindTextureUnit(0, m_mask.id());
    m_outline.set("uMask", 0);
    m_outline.set("uColor", color);
    m_outline.set("uIntensity", intensity);
    m_outline.set("uSeed", seed);
    m_outline.set("uTime", time);
    glBindVertexArray(m_screenVao.id());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void RevealFx::dim(float amount) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    m_outline.use();
    m_outline.set("uDim", amount);
    glBindVertexArray(m_screenVao.id());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    m_outline.set("uDim", 0.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}
