#include "game/fx/comet.h"

#include "engine/assets/asset_path.h"

#include <array>
#include <cmath>

namespace ghost::game {
namespace {
constexpr int kFloatsPerVertex = 5;
constexpr float kHeadExtent = 3.4f;

glm::vec3 normalizeOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float length = glm::length(v);
    return length > 1e-5f ? v / length : fallback;
}

}

Comet::Comet() : m_shader(engine::assetPath("shaders/fx/comet.vert"), engine::assetPath("shaders/fx/comet.frag")) {
    GLuint vao = 0;
    GLuint vbo = 0;
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);
    m_vao = engine::GlVertexArray(vao);
    m_vbo = engine::GlBuffer(vbo);
    glNamedBufferStorage(vbo, 6 * kFloatsPerVertex * sizeof(float), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, kFloatsPerVertex * sizeof(float));
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float));
    glVertexArrayAttribBinding(vao, 1, 0);
}

void Comet::begin(const glm::mat4& viewProj, const glm::vec3& cameraPos, float time) {
    m_cameraPos = cameraPos;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uTime", time);
    glBindVertexArray(m_vao.id());
}

void Comet::quad(const glm::vec3 corners[4], const glm::vec2 uvs[4]) {
    std::array<float, 6 * kFloatsPerVertex> data{};
    const int order[6] = {0, 1, 2, 2, 1, 3};
    for (int i = 0; i < 6; ++i) {
        const glm::vec3& p = corners[order[i]];
        const glm::vec2& uv = uvs[order[i]];
        const auto base = static_cast<std::size_t>(i * kFloatsPerVertex);
        data[base] = p.x;
        data[base + 1] = p.y;
        data[base + 2] = p.z;
        data[base + 3] = uv.x;
        data[base + 4] = uv.y;
    }
    glNamedBufferSubData(m_vbo.id(), 0, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Comet::draw(const glm::vec3& head, const glm::vec3& direction, float radius, float tailLength, float fade,
                 float brightness, float seed) {
    if (fade <= 0.003f || radius <= 0.0f) {
        return;
    }
    const glm::vec3 toCamera = normalizeOr(m_cameraPos - head, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 right = normalizeOr(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), toCamera), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 up = glm::cross(toCamera, right);
    const glm::vec3 dir = normalizeOr(direction, glm::vec3(0.0f, 0.0f, -1.0f));

    m_shader.set("uFade", fade);
    m_shader.set("uBrightness", brightness);
    m_shader.set("uSeed", seed);

    const glm::vec3 across = glm::cross(dir, toCamera);
    const float acrossLength = glm::length(across);
    if (tailLength > 0.02f && acrossLength > 0.02f) {
        const glm::vec3 side = across / acrossLength * (radius * 1.15f);
        const glm::vec3 end = head - dir * tailLength;
        const glm::vec3 corners[4] = {head - side, head + side, end - side, end + side};
        const glm::vec2 uvs[4] = {{0.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}};
        m_shader.set("uPart", 1);
        m_shader.set("uStretch", tailLength / radius);

        m_shader.set("uTailFade", std::min(1.0f, acrossLength * 6.0f));
        quad(corners, uvs);
    }

    const glm::vec3 r = right * (radius * kHeadExtent);
    const glm::vec3 u = up * (radius * kHeadExtent);
    const glm::vec3 corners[4] = {head - r - u, head + r - u, head - r + u, head + r + u};
    const glm::vec2 uvs[4] = {{-kHeadExtent, -kHeadExtent}, {kHeadExtent, -kHeadExtent}, {-kHeadExtent, kHeadExtent},
                              {kHeadExtent, kHeadExtent}};
    m_shader.set("uPart", 0);
    m_shader.set("uLead", glm::vec2(glm::dot(dir, right), glm::dot(dir, up)));
    quad(corners, uvs);
}

void Comet::end() {
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}
