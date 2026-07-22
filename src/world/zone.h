#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Arena;
struct World;
struct PhysWorld;
struct Terrain;

#define ZONE_MAX_PICKUPS 64

typedef struct ZoneSpawn {
    Vec3 car_pos;
    f32 car_yaw;
    Vec3 player_pos;
    f32 player_yaw;
    b32 tower_present;
    f32 tower_x;
    f32 tower_z;
    f32 tower_yaw_deg;
} ZoneSpawn;

typedef struct ZonePickup {
    char item[32];
    f32 x, z;
    f32 yaw_deg;
    f32 condition;
    i32 aux;
} ZonePickup;

typedef struct ZonePickups {
    ZonePickup items[ZONE_MAX_PICKUPS];
    u32 count;
} ZonePickups;

b32 zone_load(const char* zone_dir, struct Arena* arena, struct World* world,
              struct PhysWorld* phys, struct Terrain* terrain, ZoneSpawn* out_spawn,
              ZonePickups* out_pickups);
b32 zone_save(const char* zone_dir, struct World* world, struct PhysWorld* phys,
              const struct Terrain* terrain);
b32 zone_reload(const char* zone_dir, struct Arena* arena, struct World* world,
                struct PhysWorld* phys, const struct Terrain* terrain,
                ZonePickups* out_pickups);
