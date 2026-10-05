#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <span>

namespace anom {
class Arena;

inline constexpr u32 kTreeVariants = 6;

struct TreeVertex {
    Vec3 pos;
    Vec3 normal;
    Vec2 uv;
    f32 leaf;
};

struct TreeData {
    std::span<const TreeVertex> vertices;
    std::span<const u32> indices;
    u32 bark_index_count = 0;
    u32 trunk_index_count = 0;
    Aabb bounds = aabb_empty();
};

bool tree_generate(u32 variant, Arena& arena, TreeData& out);

}
