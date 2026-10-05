#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {
class PhysWorld;
struct Entity;

struct PickupState {
    Vec3 pos{};
    Quat rot = quat_identity();
    Vec3 vel{};
    Vec3 angular_vel{};
    bool active = false;
};

u32 pickup_body_create(PhysWorld& phys, Vec3 pos, Quat rot, Vec3 half, f32 mass, Vec3 vel);
bool pickup_has_body(const PhysWorld& phys, const Entity& e);
bool pickup_body_state(const PhysWorld& phys, const Entity& e, PickupState& out);
void pickup_set_state(PhysWorld& phys, const Entity& e, const PickupState& state);
void pickup_freeze(PhysWorld& phys, const Entity& e, Vec3 pos, Quat rot);
void pickup_body_destroy(PhysWorld& phys, Entity& e);

}
