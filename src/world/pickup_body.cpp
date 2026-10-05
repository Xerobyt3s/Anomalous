#include "world/pickup_body.h"
#include "engine/physics/physics_world.h"
#include "game/ballistics/surface.h"
#include "math/glm_bridge.h"
#include "physics/world.h"
#include "world/entity.h"

namespace anom {
u32 pickup_body_create(PhysWorld& phys, Vec3 pos, Quat rot, Vec3 half, f32 mass, Vec3 vel)
{
    ghost::engine::PhysicsWorld* jolt = phys.jolt();
    if (!jolt) {
        return kNoEntityBody;
    }
    return jolt->addDynamicBox(to_glm(pos), to_glm(rot), to_glm(half), mass,
                               static_cast<std::uint64_t>(ghost::game::Surface::Steel), to_glm(vel));
}

bool pickup_has_body(const PhysWorld& phys, const Entity& e)
{
    return e.body != kNoEntityBody && phys.jolt() && phys.jolt()->valid(e.body);
}

bool pickup_body_state(const PhysWorld& phys, const Entity& e, PickupState& out)
{
    if (!pickup_has_body(phys, e)) {
        return false;
    }
    const ghost::engine::BodyState s = phys.jolt()->state(e.body);
    out.pos = from_glm(s.position);
    out.rot = from_glm(s.rotation);
    out.vel = from_glm(s.velocity);
    out.angular_vel = from_glm(s.angularVelocity);
    out.active = s.active;
    return true;
}

void pickup_set_state(PhysWorld& phys, const Entity& e, const PickupState& state)
{
    if (!pickup_has_body(phys, e)) {
        return;
    }
    ghost::engine::BodyState s;
    s.position = to_glm(state.pos);
    s.rotation = to_glm(state.rot);
    s.velocity = to_glm(state.vel);
    s.angularVelocity = to_glm(state.angular_vel);
    s.active = state.active;
    phys.jolt()->setState(e.body, s);
}

void pickup_freeze(PhysWorld& phys, const Entity& e, Vec3 pos, Quat rot)
{
    PickupState s;
    s.pos = pos;
    s.rot = rot;
    s.active = true;
    pickup_set_state(phys, e, s);
}

void pickup_body_destroy(PhysWorld& phys, Entity& e)
{
    if (pickup_has_body(phys, e)) {
        phys.jolt()->removeBody(e.body);
    }
    e.body = kNoEntityBody;
}

}
