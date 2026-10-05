#include "game/fx/lightning.h"

#include "engine/assets/asset_path.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace ghost::game {
namespace {
constexpr int kFloatsPerVertex = 10;

using Rng = std::minstd_rand;

float unit(Rng& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

glm::vec3 perpendicular(const glm::vec3& dir, Rng& rng) {
    const glm::vec3 helper = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 a = glm::normalize(glm::cross(dir, helper));
    const glm::vec3 b = glm::cross(dir, a);
    const float angle = unit(rng) * glm::two_pi<float>();
    return a * std::cos(angle) + b * std::sin(angle);
}

std::vector<glm::vec3> displacedLine(const glm::vec3& from, const glm::vec3& to, int levels, float jaggedness,
                                     Rng& rng, float straightStart = 0.0f) {
    std::vector<glm::vec3> points{from, to};
    std::vector<float> along{0.0f, 1.0f};
    for (int level = 0; level < levels; ++level) {
        std::vector<glm::vec3> next;
        std::vector<float> nextAlong;
        next.reserve(points.size() * 2);
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            const glm::vec3 a = points[i];
            const glm::vec3 b = points[i + 1];
            const glm::vec3 segment = b - a;
            const float length = glm::length(segment);
            glm::vec3 mid = (a + b) * 0.5f;
            const float t = (along[i] + along[i + 1]) * 0.5f;
            if (length > 1e-5f) {
                mid += perpendicular(segment / length, rng) * (length * jaggedness * (unit(rng) * 2.0f - 1.0f));
            }
            next.push_back(a);
            next.push_back(mid);
            nextAlong.push_back(along[i]);
            nextAlong.push_back(t);
        }
        next.push_back(points.back());
        nextAlong.push_back(1.0f);
        points = std::move(next);
        along = std::move(nextAlong);
    }
    if (straightStart > 0.0f) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const glm::vec3 onLine = glm::mix(from, to, along[i]);
            points[i] = onLine + (points[i] - onLine) * glm::smoothstep(0.0f, straightStart, along[i]);
        }
    }
    return points;
}

void addBranch(Bolt& bolt, const glm::vec3& start, const glm::vec3& direction, float length, float startReach,
               float reachSpan, float startWidth, int levels, const BoltParams& params, Rng& rng, int depth) {
    BoltPath path;
    path.branch = true;
    path.points = displacedLine(start, start + direction * length, std::max(levels, 2), params.jaggedness * 1.3f, rng);
    const std::size_t count = path.points.size();
    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        path.width.push_back(startWidth * (1.0f - t));
        path.reach.push_back(startReach + reachSpan * t);
    }
    const glm::vec3 forkPoint = path.points[count / 2];
    const glm::vec3 forkDir = glm::normalize(path.points[count / 2 + 1] - path.points[count / 2 - 1]);
    bolt.paths.push_back(std::move(path));

    if (depth < 2 && unit(rng) < params.subBranchChance) {
        const float angle = glm::radians(20.0f + 25.0f * unit(rng));
        const glm::vec3 dir = glm::normalize(forkDir * std::cos(angle) + perpendicular(forkDir, rng) * std::sin(angle));
        addBranch(bolt, forkPoint, dir, length * 0.45f, startReach + reachSpan * 0.5f, reachSpan * 0.45f,
                  startWidth * 0.5f, levels - 1, params, rng, depth + 1);
    }
}

}

Bolt buildBolt(const glm::vec3& from, const glm::vec3& to, std::uint32_t seed, const BoltParams& params) {
    Rng rng(seed * 2654435761u + 12345u);
    Bolt bolt;

    BoltPath main;
    main.points = displacedLine(from, to, params.levels, params.jaggedness, rng, params.straightStart);
    const std::size_t count = main.points.size();
    for (std::size_t i = 0; i < count; ++i) {
        main.width.push_back(1.0f);
        main.reach.push_back(static_cast<float>(i) / static_cast<float>(count - 1));
    }
    bolt.paths.push_back(main);

    const float total = glm::distance(from, to);
    for (int b = 0; b < params.branches && count > 8; ++b) {
        const float t = 0.08f + 0.72f * unit(rng);
        const auto index = std::clamp<std::size_t>(static_cast<std::size_t>(t * static_cast<float>(count - 1)), 1, count - 2);
        const glm::vec3 along = glm::normalize(main.points[index + 1] - main.points[index - 1]);
        const float angle = glm::radians(15.0f + 25.0f * unit(rng));
        const glm::vec3 dir = glm::normalize(along * std::cos(angle) + perpendicular(along, rng) * std::sin(angle));
        const float remaining = total * (1.0f - main.reach[index]);
        const float length = remaining * params.branchLength * (0.5f + 0.5f * unit(rng));
        addBranch(bolt, main.points[index], dir, length, main.reach[index], length / std::max(total, 1e-3f), 0.55f,
                  params.levels - 2, params, rng, 1);
    }
    return bolt;
}

Lightning::Lightning()
    : m_shader(engine::assetPath("shaders/fx/bolt.vert"), engine::assetPath("shaders/fx/bolt.frag")) {
    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    m_vao = engine::GlVertexArray(vao);
    const int sizes[3] = {3, 3, 4};
    GLuint offset = 0;
    for (GLuint i = 0; i < 3; ++i) {
        glEnableVertexArrayAttrib(vao, i);
        glVertexArrayAttribFormat(vao, i, sizes[i], GL_FLOAT, GL_FALSE, offset);
        glVertexArrayAttribBinding(vao, i, 0);
        offset += static_cast<GLuint>(sizes[i]) * sizeof(float);
    }
}

void Lightning::begin(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uCameraPos", cameraPos);
}

void Lightning::draw(const Bolt& bolt, const BoltStyle& style) {
    if (style.intensity <= 0.002f) {
        return;
    }
    m_vertices.clear();
    for (const BoltPath& path : bolt.paths) {
        const std::size_t count = path.points.size();
        auto tangentAt = [&](std::size_t i) {
            const glm::vec3 d = path.points[std::min(i + 1, count - 1)] - path.points[i > 0 ? i - 1 : 0];
            const float length = glm::length(d);
            return length > 1e-6f ? d / length : glm::vec3(0.0f, 1.0f, 0.0f);
        };
        auto push = [&](std::size_t i, float side) {
            const glm::vec3& p = path.points[i];
            const glm::vec3 t = tangentAt(i);
            m_vertices.insert(m_vertices.end(), {p.x, p.y, p.z, t.x, t.y, t.z, side * path.width[i], side,
                                                 path.reach[i], path.branch ? 1.0f : 0.0f});
        };
        for (std::size_t i = 0; i + 1 < count; ++i) {
            push(i, -1.0f);
            push(i, 1.0f);
            push(i + 1, -1.0f);
            push(i + 1, -1.0f);
            push(i, 1.0f);
            push(i + 1, 1.0f);
        }
    }
    if (m_vertices.empty()) {
        return;
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

    m_shader.set("uColor", style.color);
    m_shader.set("uBranchIntensity", style.branchIntensity);
    m_shader.set("uReveal", style.reveal);
    m_shader.set("uJitter", style.jitter);
    m_shader.set("uJitterSeed", style.jitterSeed);
    m_shader.set("uStartShift", style.startShift);
    m_shader.set("uStartWidth", style.startWidth);
    glBindVertexArray(m_vao.id());
    const auto vertexCount = static_cast<GLsizei>(m_vertices.size() / kFloatsPerVertex);

    m_shader.set("uWidth", style.width * style.haloScale);
    m_shader.set("uIntensity", style.intensity * 0.35f);
    m_shader.set("uCore", 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    m_shader.set("uWidth", style.width);
    m_shader.set("uIntensity", style.intensity);
    m_shader.set("uCore", 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

void Lightning::end() {
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

}
