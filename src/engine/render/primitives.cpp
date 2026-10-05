#include "engine/render/primitives.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ghost::engine {
Mesh makeBox(const glm::vec3& halfExtents) {
    struct Face {
        glm::vec3 normal, u, v;
    };

    const std::array<Face, 6> faces{{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (const Face& f : faces) {
        const auto base = static_cast<std::uint32_t>(vertices.size());
        const std::array<glm::vec2, 4> corners{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
        for (const glm::vec2& c : corners) {
            const glm::vec3 p = f.normal + f.u * c.x + f.v * c.y;
            vertices.push_back({p * halfExtents, f.normal, c * 0.5f + 0.5f});
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return Mesh(vertices, indices);
}

Mesh makeCylinder(int segments) {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    const float step = 6.28318530718f / static_cast<float>(segments);

    for (int i = 0; i <= segments; ++i) {
        const float a = step * static_cast<float>(i);
        const glm::vec3 n{std::cos(a), 0.0f, std::sin(a)};
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        vertices.push_back({n, n, {u, 0.0f}});
        vertices.push_back({n + glm::vec3(0.0f, 1.0f, 0.0f), n, {u, 1.0f}});
    }
    for (int i = 0; i < segments; ++i) {
        const auto b = static_cast<std::uint32_t>(i * 2);
        indices.insert(indices.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
    }

    for (int cap = 0; cap < 2; ++cap) {
        const float y = static_cast<float>(cap);
        const glm::vec3 n{0.0f, cap ? 1.0f : -1.0f, 0.0f};
        const auto center = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({{0.0f, y, 0.0f}, n, {0.5f, 0.5f}});
        for (int i = 0; i <= segments; ++i) {
            const float a = step * static_cast<float>(i);
            vertices.push_back({{std::cos(a), y, std::sin(a)}, n, {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}});
        }
        for (int i = 0; i < segments; ++i) {
            const auto a = center + 1 + static_cast<std::uint32_t>(i);
            if (cap) {
                indices.insert(indices.end(), {center, a + 1, a});
            } else {
                indices.insert(indices.end(), {center, a, a + 1});
            }
        }
    }
    return Mesh(vertices, indices);
}

Mesh makeRing(float innerRadius, float outerRadius, int segments) {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    const glm::vec3 forward{0.0f, 0.0f, 1.0f};
    for (int i = 0; i <= segments; ++i) {
        const float a = 6.28318530718f * static_cast<float>(i) / static_cast<float>(segments);
        const glm::vec3 dir{std::cos(a), std::sin(a), 0.0f};
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        vertices.push_back({dir * innerRadius, forward, {u, 0.0f}});
        vertices.push_back({dir * outerRadius, forward, {u, 1.0f}});
    }
    for (int i = 0; i < segments; ++i) {
        const auto b = static_cast<std::uint32_t>(i * 2);
        indices.insert(indices.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
    }
    return Mesh(vertices, indices);
}

Mesh makeQuad() {
    const glm::vec3 forward{0.0f, 0.0f, 1.0f};
    const std::array<Vertex, 4> vertices{{
        {{-1.0f, -1.0f, 0.0f}, forward, {0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, forward, {1.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}, forward, {1.0f, 1.0f}},
        {{-1.0f, 1.0f, 0.0f}, forward, {0.0f, 1.0f}},
    }};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    return Mesh(vertices, indices);
}

Mesh makePlane(float height, float halfSize) {
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const std::array<Vertex, 4> vertices{{
        {{-halfSize, height, -halfSize}, up, {0.0f, 0.0f}},
        {{halfSize, height, -halfSize}, up, {1.0f, 0.0f}},
        {{halfSize, height, halfSize}, up, {1.0f, 1.0f}},
        {{-halfSize, height, halfSize}, up, {0.0f, 1.0f}},
    }};
    const std::array<std::uint32_t, 6> indices{0, 2, 1, 0, 3, 2};
    return Mesh(vertices, indices);
}

}
