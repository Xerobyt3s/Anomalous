#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

class Arena;
class GravityField;
class Heightfield;
class PhysWorld;
class Terrain;
class World;

inline constexpr u32 kZoneMaxPickups = 64;
inline constexpr i32 kZoneMaxGhostsPerSpawn = 8;
inline constexpr u32 kZoneMaxStaticTris = 131072;
inline constexpr f32 kZoneEscapeMarginXZ = 200.0f;
inline constexpr f32 kZoneEscapeMarginY = 400.0f;

enum class SceneKind : u8 {
    Zone,
    Arena,
    Range,
};

struct ZoneScene {
    SceneKind kind = SceneKind::Zone;
    FixedString<32> name;
    bool car = true;
};

std::string_view scene_kind_name(SceneKind kind);
u8 surface_from_name(std::string_view name);
std::string_view surface_name(u8 surface);

struct ZoneSpawn {
    Vec3 car_pos;
    f32 car_yaw = 0.0f;
    Vec3 player_pos;
    f32 player_yaw = 0.0f;
    bool tower_present = false;
    f32 tower_x = 0.0f;
    f32 tower_z = 0.0f;
    f32 tower_yaw_deg = 0.0f;
    ZoneScene scene;
};

struct ZonePickup {
    FixedString<32> item;
    f32 x = 0.0f;
    f32 z = 0.0f;
    f32 yaw_deg = 0.0f;
    f32 condition = 1.0f;
    i32 aux = 0;
};

struct ZonePickups {
    ZonePickup items[kZoneMaxPickups];
    u32 count = 0;
};

bool zone_load(std::string_view zone_dir, Arena& arena, Arena& scratch, World& world,
               PhysWorld& phys, Terrain& terrain, ZoneSpawn& out_spawn,
               ZonePickups* out_pickups);

bool zone_reload(std::string_view zone_dir, Arena& arena, Arena& scratch, World& world,
                 PhysWorld& phys, const Terrain& terrain, ZonePickups* out_pickups);

void zone_gravity(const World& world, GravityField& out);
bool zone_out_of_bounds(const Heightfield& hf, Vec3 p);
void zone_add_entity_collision(const World& world, PhysWorld& phys, Arena& scratch);

bool zone_save(std::string_view zone_dir, Arena& scratch, const World& world,
               const PhysWorld& phys, const Terrain& terrain);

} // namespace anom
