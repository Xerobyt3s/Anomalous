#pragma once

#include "assets/amsh.h"
#include "core/types.h"
#include "math/vmath.h"

namespace anom {

constexpr u32 kNoTexture = 0xFFFFFFFFu;

struct GpuSubmesh {
    u32 first_index = 0;
    u32 index_count = 0;
    u32 texture_slot = 0;
    u32 normal_slot = kNoTexture;
    u32 surface_slot = kNoTexture;
    u32 blend_slot = kNoTexture;
    bool ground = false;
    bool procedural = false;
};

struct GpuMesh {
    u32 vao = 0;
    u32 vbo = 0;
    u32 ebo = 0;
    GpuSubmesh submeshes[kAmshMaxSubmeshes];
    u32 submesh_count = 0;
    Aabb bounds = aabb_empty();
    bool loaded = false;
};

} // namespace anom
