#pragma once

#include "core/fixed_string.h"
#include "core/handle.h"
#include "core/pool.h"
#include "core/types.h"
#include "math/vmath.h"
#include "physics/body.h"

namespace anom {
class Arena;

inline constexpr u32 kMaxEntities = 4096;
inline constexpr u32 kNoEntityBody = 0xFFFFFFFFu;

inline constexpr u32 kEntityFlagCollides = 1u << 0;
inline constexpr u32 kEntityFlagInteractable = 1u << 1;
inline constexpr u32 kEntityFlagTower = 1u << 2;

enum class EntityKind : u32 {
    StaticMesh,
    Tree,
    Building,
    Vehicle,
    PartPickup,
    Trigger,
    Gravity,
    Island,
    IslandLink,
};

struct Entity {
    EntityKind kind;
    u32 flags;
    Vec3 pos;
    Quat rot;
    f32 scale;
    Vec3 half;
    FixedString<32> mesh_name;
    u32 aux_kind;
    f32 aux_value;
    u32 aux_data;
    u32 body;
};

using EntityHandle = Handle<Entity>;

class World {
public:
    void init(Arena& arena);

    EntityHandle spawn(EntityKind kind, Vec3 pos, Quat rot, f32 scale,
                       std::string_view mesh_name, u32 flags);
    Entity* entity(EntityHandle handle) { return entities_.get(handle); }
    const Entity* entity(EntityHandle handle) const { return entities_.get(handle); }
    void despawn(EntityHandle handle) { entities_.free(handle); }
    void clear() { entities_.clear(); }

    Pool<Entity>& entities() { return entities_; }
    const Pool<Entity>& entities() const { return entities_; }
    u32 count() const { return entities_.count(); }

private:
    Pool<Entity> entities_;
};

}
