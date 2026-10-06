#include "physics/world.h"
#include "core/arena.h"
#include "core/log.h"
#include "physics/gravity_field.h"
#include "physics/heightfield.h"
#include "engine/physics/physics_world.h"
#include "game/ballistics/surface.h"
#include "math/glm_bridge.h"

#include <vector>

namespace anom {
namespace {
constexpr u32 kHeightBlock = 4;
constexpr f32 kGravityWakeSq = 0.05f;
}

Vec3 solid_box_inertia(Vec3 half_extents, f32 mass)
{
    const f32 hx = half_extents.x;
    const f32 hy = half_extents.y;
    const f32 hz = half_extents.z;
    return Vec3{mass / 3.0f * (hy * hy + hz * hz), mass / 3.0f * (hx * hx + hz * hz),
                mass / 3.0f * (hx * hx + hy * hy)};
}

void PhysWorld::init(Arena& arena, const Heightfield* hf)
{
    bodies_.init(arena, kMaxBodies, "bodies");
    descs_.assign(kMaxBodies, BodyDesc{});
    hf_ = hf;
    sync_jolt();
}

void PhysWorld::set_jolt(ghost::engine::PhysicsWorld* jolt)
{
    jolt_ = jolt;
    sync_jolt();
    install_gravity();
    ensure_jolt_bodies();
}

void PhysWorld::install_gravity()
{
    if (!jolt_) {
        return;
    }
    jolt_->setGravityField([this](const glm::vec3& p) { return to_glm(gravity_at(from_glm(p))); });
}

void PhysWorld::sync_jolt()
{
    if (!jolt_) {
        return;
    }
    jolt_->clearStatics();
    const auto ground = static_cast<std::uint64_t>(ghost::game::Surface::Ground);
    if (hf_ && hf_->valid()) {
        const u32 n = hf_->size_x() > hf_->size_z() ? hf_->size_x() : hf_->size_z();
        const u32 padded = ((n + kHeightBlock - 1) / kHeightBlock) * kHeightBlock;
        std::vector<float> samples(static_cast<size_t>(padded) * padded, kNoCollisionHeight);
        for (u32 z = 0; z < hf_->size_z(); z++) {
            for (u32 x = 0; x < hf_->size_x(); x++) {
                samples[static_cast<size_t>(z) * padded + x] = hf_->height_at(x, z);
            }
        }
        const Vec3 origin = hf_->origin();
        jolt_->addHeightfield(samples, padded, glm::vec3(origin.x, 0.0f, origin.z),
                              glm::vec3(hf_->cell_size(), 1.0f, hf_->cell_size()), kHeightBlock, ground);
    }
    const std::span<const StaticTri> tris = statics_.tris();
    if (!tris.empty()) {
        std::vector<glm::vec3> points;
        points.reserve(tris.size() * 3);
        for (const StaticTri& t : tris) {
            points.push_back(to_glm(t.a));
            points.push_back(to_glm(t.b));
            points.push_back(to_glm(t.c));
        }
        jolt_->addStaticMesh(points, ground);
    }
    for (const StaticBox& box : static_boxes_) {
        jolt_->addStaticBox(to_glm(box.center), to_glm(box.half), static_cast<std::uint64_t>(box.surface));
    }
    jolt_->optimize();
}

void PhysWorld::apply_desc(RigidBody& body, const BodyDesc& desc) const
{
    body.inv_mass = 1.0f / desc.mass;
    body.inv_inertia_local = mat3_diag(1.0f / desc.inertia_diag.x, 1.0f / desc.inertia_diag.y,
                                       1.0f / desc.inertia_diag.z);
    body.half_extents = desc.half_extents;
    body.box_offset = desc.box_offset;
    body.friction = desc.friction;
    body.restitution = desc.restitution;
}

u32 PhysWorld::create_jolt_body(const RigidBody& body, const BodyDesc& desc) const
{
    if (!jolt_) {
        return kNoJoltBody;
    }
    ghost::engine::DynamicBodyDesc d;
    d.center = to_glm(body.pos);
    d.rotation = to_glm(body.rot);
    d.halfExtents = to_glm(desc.half_extents);
    d.shapeOffset = to_glm(desc.box_offset);
    d.mass = desc.mass;
    d.inertiaDiagonal = to_glm(desc.inertia_diag);
    d.linearDamping = tuning_.linear_damping;
    d.angularDamping = tuning_.angular_damping;
    d.friction = desc.friction;
    d.restitution = desc.restitution;
    d.linearCast = desc.linear_cast;
    d.allowSleeping = desc.allow_sleeping;
    d.velocity = to_glm(body.vel);
    d.angularVelocity = to_glm(body.angular_vel);
    d.userData = static_cast<std::uint64_t>(ghost::game::Surface::Steel);
    for (u32 i = 0; i < desc.hull_count && i < kBodyHullPoints; i++) {
        d.hull.push_back(to_glm(desc.hull[i]));
    }
    return jolt_->addDynamicBody(d);
}

void PhysWorld::ensure_jolt_bodies()
{
    if (!jolt_) {
        return;
    }
    for (const u32 slot : bodies_.live_indices()) {
        RigidBody& body = *bodies_.at(slot);
        if (body.jolt_id != kNoJoltBody && jolt_->valid(body.jolt_id)) {
            continue;
        }
        body.jolt_id = create_jolt_body(body, descs_[slot]);
        body.read_pos = body.pos;
        body.read_rot = body.rot;
        body.read_vel = body.vel;
        body.read_angular_vel = body.angular_vel;
    }
}

BodyHandle PhysWorld::body_create_box(Vec3 pos, Quat rot, Vec3 half_extents, f32 mass)
{
    BodyDesc desc;
    desc.half_extents = half_extents;
    desc.mass = mass;
    desc.inertia_diag = solid_box_inertia(half_extents, mass);
    return body_create_box(pos, rot, desc);
}

BodyHandle PhysWorld::body_create_box(Vec3 pos, Quat rot, const BodyDesc& desc)
{
    if (!jolt_ && !warned_no_jolt_) {
        warned_no_jolt_ = true;
        log_error("phys: creating bodies without a Jolt world; they will not move");
    }
    const BodyHandle handle = bodies_.alloc();
    RigidBody* body = bodies_.get(handle);
    if (!body) {
        return handle;
    }
    *body = RigidBody{};
    body->pos = pos;
    body->prev_pos = pos;
    body->rot = rot;
    body->prev_rot = rot;
    body->gravity = gravity_at(pos);
    body->jolt_id = kNoJoltBody;
    apply_desc(*body, desc);
    descs_[handle.idx] = desc;
    body->jolt_id = create_jolt_body(*body, desc);
    body->read_pos = pos;
    body->read_rot = rot;
    body->read_vel = body->vel;
    body->read_angular_vel = body->angular_vel;
    return handle;
}

void PhysWorld::body_reshape(BodyHandle handle, const BodyDesc& desc)
{
    RigidBody* body = bodies_.get(handle);
    if (!body) {
        return;
    }
    if (jolt_ && body->jolt_id != kNoJoltBody) {
        jolt_->removeBody(body->jolt_id);
    }
    apply_desc(*body, desc);
    descs_[handle.idx] = desc;
    body->jolt_id = create_jolt_body(*body, desc);
    body->read_pos = body->pos;
    body->read_rot = body->rot;
    body->read_vel = body->vel;
    body->read_angular_vel = body->angular_vel;
    body->asleep = 0;
    body->wake_request = 0;
}

void PhysWorld::body_destroy(BodyHandle handle)
{
    RigidBody* body = bodies_.get(handle);
    if (body && jolt_ && body->jolt_id != kNoJoltBody) {
        jolt_->removeBody(body->jolt_id);
        body->jolt_id = kNoJoltBody;
    }
    bodies_.free(handle);
}

Vec3 PhysWorld::gravity_at(Vec3 p) const
{
    return field_ ? field_->gravity_at(p) : gravity();
}

Vec3 PhysWorld::up_at(Vec3 p) const
{
    return field_ ? field_->up_at(p) : Vec3{0.0f, 1.0f, 0.0f};
}

void PhysWorld::tick(f32 dt)
{
    const std::span<const u32> live = bodies_.live_indices();
    for (const u32 slot : live) {
        RigidBody& body = *bodies_.at(slot);
        body.prev_pos = body.pos;
        body.prev_rot = body.rot;
        const Vec3 g = gravity_at(body.pos);
        if (body.asleep && length_sq(g - body.gravity) > kGravityWakeSq) {
            body.wake_request = 1;
        }
        body.gravity = g;
    }

    if (!jolt_) {
        for (const u32 slot : live) {
            RigidBody& body = *bodies_.at(slot);
            body.force_accum = Vec3{};
            body.torque_accum = Vec3{};
            body.wake_request = 0;
        }
        return;
    }

    for (const u32 slot : live) {
        RigidBody& body = *bodies_.at(slot);
        if (body.jolt_id == kNoJoltBody) {
            continue;
        }
        if (!(body.pos == body.read_pos) || !(body.rot == body.read_rot)) {
            jolt_->setPose(body.jolt_id, to_glm(body.pos), to_glm(body.rot));
        }
        if (!(body.vel == body.read_vel) || !(body.angular_vel == body.read_angular_vel)) {
            jolt_->setVelocities(body.jolt_id, to_glm(body.vel), to_glm(body.angular_vel));
        }
        if (length_sq(body.force_accum) > 0.0f) {
            jolt_->addForce(body.jolt_id, to_glm(body.force_accum));
        }
        if (length_sq(body.torque_accum) > 0.0f) {
            jolt_->addTorque(body.jolt_id, to_glm(body.torque_accum));
        }
        if (body.wake_request) {
            jolt_->activate(body.jolt_id);
        }
    }

    jolt_->step(dt);

    for (const u32 slot : live) {
        RigidBody& body = *bodies_.at(slot);
        body.force_accum = Vec3{};
        body.torque_accum = Vec3{};
        body.wake_request = 0;
        if (body.jolt_id == kNoJoltBody) {
            continue;
        }
        const ghost::engine::BodyState s = jolt_->state(body.jolt_id);
        body.pos = from_glm(s.position);
        body.rot = from_glm(s.rotation);
        body.vel = from_glm(s.velocity);
        body.angular_vel = from_glm(s.angularVelocity);
        body.asleep = s.active ? 0 : 1;
        body.read_pos = body.pos;
        body.read_rot = body.rot;
        body.read_vel = body.vel;
        body.read_angular_vel = body.angular_vel;
    }
}

bool PhysWorld::raycast(Ray ray, f32 max_t, PhysRayHit* out_hit) const
{
    if (!jolt_ || max_t <= 0.0f) {
        return false;
    }
    ghost::engine::RayFilter filter;
    filter.staticOnly = true;
    const std::optional<ghost::engine::RayHit> hit =
        jolt_->raycast(to_glm(ray.origin), to_glm(ray.origin + ray.dir * max_t), filter);
    if (!hit) {
        return false;
    }
    if (out_hit) {
        const f32 t = hit->fraction * max_t;
        Vec3 normal = from_glm(hit->normal);
        if (dot(normal, ray.dir) > 0.0f) {
            normal = -normal;
        }
        out_hit->t = t;
        out_hit->point = ray.origin + ray.dir * t;
        out_hit->normal = normal;
    }
    return true;
}

}
