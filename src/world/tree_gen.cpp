#include "world/tree_gen.h"
#include "core/arena.h"

#include <cmath>

namespace anom {
namespace {
constexpr u32 kSides = 5;
constexpr i32 kMaxDepth = 5;
constexpr u32 kMaxVertices = 16384;
constexpr u32 kMaxIndices = 32768;

constexpr f32 kTrunkLength = 3.1f;
constexpr f32 kTrunkRadius = 0.30f;
constexpr f32 kLeafClusterSize = 1.15f;

struct Builder {
    TreeVertex* vertices = nullptr;
    u32 vertex_count = 0;
    u32* bark = nullptr;
    u32 bark_count = 0;
    u32* leaf = nullptr;
    u32 leaf_count = 0;
    u32 trunk_count = 0;
    u32 rng = 0;
    Aabb bounds = aabb_empty();

    f32 random()
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<f32>(rng & 0xFFFFFFu) / 16777216.0f;
    }

    static Vec3 perpendicular(Vec3 v)
    {
        const Vec3 helper = f_abs(v.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
        return normalize(cross(v, helper));
    }

    void push_vertex(Vec3 pos, Vec3 normal, Vec2 uv, f32 is_leaf)
    {
        if (vertex_count >= kMaxVertices) {
            return;
        }
        vertices[vertex_count++] = TreeVertex{pos, normal, uv, is_leaf};
        bounds = expand(bounds, pos);
    }

    void push_bark(u32 index)
    {
        if (bark_count < kMaxIndices) {
            bark[bark_count++] = index;
        }
    }

    void push_leaf(u32 index)
    {
        if (leaf_count < kMaxIndices) {
            leaf[leaf_count++] = index;
        }
    }

    void add_segment(Vec3 start, Vec3 end, f32 start_radius, f32 end_radius)
    {
        const Vec3 axis = normalize(end - start);
        const Vec3 right = perpendicular(axis);
        const Vec3 forward = cross(axis, right);

        const u32 base = vertex_count;
        for (u32 i = 0; i <= kSides; i++) {
            const f32 angle = static_cast<f32>(i) / static_cast<f32>(kSides) * kTau;
            const Vec3 offset = right * std::cos(angle) + forward * std::sin(angle);
            const f32 u = static_cast<f32>(i) / static_cast<f32>(kSides);
            push_vertex(start + offset * start_radius, offset, Vec2{u, 0.0f}, 0.0f);
            push_vertex(end + offset * end_radius, offset, Vec2{u, 1.0f}, 0.0f);
        }
        for (u32 i = 0; i < kSides; i++) {
            const u32 a = base + i * 2;
            push_bark(a);
            push_bark(a + 1);
            push_bark(a + 2);
            push_bark(a + 2);
            push_bark(a + 1);
            push_bark(a + 3);
        }
    }

    void add_leaf_cluster(Vec3 centre, f32 size)
    {
        for (u32 card = 0; card < 3; card++) {
            const f32 yaw = random() * kTau;
            const f32 tilt = (random() - 0.5f) * 1.1f;
            const Vec3 right{std::cos(yaw), tilt, std::sin(yaw)};
            const Vec3 up = normalize(cross(right, Vec3{std::sin(yaw), 0.0f, -std::cos(yaw)}));
            const Vec3 axis = normalize(right);
            const Vec3 offset{(random() - 0.5f) * size * 2.4f, (random() - 0.5f) * size * 1.5f,
                              (random() - 0.5f) * size * 2.4f};
            const Vec3 c = centre + offset;
            const f32 half = size * (0.55f + random() * 0.5f);
            const Vec3 normal = normalize(cross(axis, up));

            const u32 base = vertex_count;
            push_vertex(c - axis * half - up * half, normal, Vec2{0.0f, 0.0f}, 1.0f);
            push_vertex(c + axis * half - up * half, normal, Vec2{1.0f, 0.0f}, 1.0f);
            push_vertex(c - axis * half + up * half, normal, Vec2{0.0f, 1.0f}, 1.0f);
            push_vertex(c + axis * half + up * half, normal, Vec2{1.0f, 1.0f}, 1.0f);
            push_leaf(base);
            push_leaf(base + 1);
            push_leaf(base + 2);
            push_leaf(base + 2);
            push_leaf(base + 1);
            push_leaf(base + 3);
        }
    }

    void branch(Vec3 start, Vec3 direction, f32 length, f32 radius, i32 depth)
    {
        const Vec3 droop{0.0f, -0.22f * static_cast<f32>(kMaxDepth - depth), 0.0f};
        const Vec3 end = start + normalize(direction + droop * 0.12f) * length;
        add_segment(start, end, radius, radius * 0.68f);
        if (depth == kMaxDepth) {
            trunk_count = bark_count;
        }

        if (depth <= 0) {
            add_leaf_cluster(end, kLeafClusterSize);
            return;
        }

        const u32 children = random() < 0.35f ? 3 : 2;
        for (u32 i = 0; i < children; i++) {
            const f32 yaw = random() * kTau;
            const f32 spread = 0.42f + random() * 0.45f;
            const Vec3 side{std::cos(yaw) * spread, 0.0f, std::sin(yaw) * spread};
            branch(end, normalize(direction + side), length * (0.68f + random() * 0.14f),
                   radius * 0.66f, depth - 1);
        }
    }
};

}

bool tree_generate(u32 variant, Arena& arena, TreeData& out)
{
    Builder b;
    b.vertices = arena.push_array<TreeVertex>(kMaxVertices);
    b.bark = arena.push_array<u32>(kMaxIndices);
    b.leaf = arena.push_array<u32>(kMaxIndices);
    if (!b.vertices || !b.bark || !b.leaf) {
        return false;
    }
    b.rng = 12345u + variant * 7919u;

    b.branch(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, kTrunkLength, kTrunkRadius,
             kMaxDepth);

    u32* indices = arena.push_array<u32>(b.bark_count + b.leaf_count);
    if (!indices) {
        return false;
    }
    for (u32 i = 0; i < b.bark_count; i++) {
        indices[i] = b.bark[i];
    }
    for (u32 i = 0; i < b.leaf_count; i++) {
        indices[b.bark_count + i] = b.leaf[i];
    }

    out.vertices = {b.vertices, b.vertex_count};
    out.indices = {indices, b.bark_count + b.leaf_count};
    out.bark_index_count = b.bark_count;
    out.trunk_index_count = b.trunk_count;
    out.bounds = b.bounds;
    return true;
}

}
