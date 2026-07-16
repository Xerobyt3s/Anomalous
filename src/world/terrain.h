#pragma once

#include "core/types.h"
#include "physics/heightfield.h"

struct Arena;
struct Config;

typedef struct Terrain {
    Heightfield hf;
    u8* roadmask;
    u32 mask_size;
} Terrain;

b32 terrain_load(Terrain* terrain, struct Arena* arena, const char* zone_dir, const struct Config* cfg);
f32 terrain_road_amount(const Terrain* terrain, f32 x, f32 z);
