#pragma once

#include "core/types.h"

namespace anom {

inline constexpr u32 kAmshMagic = 0x48534D41u;
inline constexpr u32 kAmshVersion = 1u;
inline constexpr u32 kAmshMaterialNameMax = 32;
inline constexpr u32 kAmshMaxSubmeshes = 16;

struct AmshHeader {
    u32 magic;
    u32 version;
    u32 vertex_count;
    u32 index_count;
    u32 submesh_count;
};

struct AmshVertex {
    f32 pos[3];
    f32 normal[3];
    f32 uv[2];
};

struct AmshSubmesh {
    u32 first_index;
    u32 index_count;
    char material[kAmshMaterialNameMax];
};

static_assert(sizeof(AmshHeader) == 20);
static_assert(sizeof(AmshVertex) == 32);
static_assert(sizeof(AmshSubmesh) == 40);

} // namespace anom
