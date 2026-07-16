#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Arena;
struct World;
struct PhysWorld;
struct Terrain;

typedef struct ZoneSpawn {
    Vec3 car_pos;
    f32 car_yaw;
    Vec3 player_pos;
    f32 player_yaw;
} ZoneSpawn;

b32 zone_load(const char* zone_dir, struct Arena* arena, struct World* world,
              struct PhysWorld* phys, struct Terrain* terrain, ZoneSpawn* out_spawn);
