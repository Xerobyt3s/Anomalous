#include "physics/world.h"
#include "core/arena.h"
#include "core/log.h"
#include "physics/collide.h"
#include "physics/gravity_field.h"
#include "physics/heightfield.h"
#include "engine/physics/physics_world.h"
#include "game/ballistics/surface.h"
#include "math/glm_bridge.h"

#include <vector>

#include <algorithm>

namespace anom {
namespace {
constexpr u32 kHeightBlock = 4;
constexpr f32 kGravityWakeSq = 0.05f;

u64 static_key(u32 slot, u32 sphere, u32 feature)
{
    return (static_cast<u64>(slot) << 40) | (static_cast<u64>(sphere) << 32) | feature;
}

void tangent_basis(Vec3 n, Vec3& t0, Vec3& t1)
{
    const Vec3 ref = f_abs(n.y) < 0.99f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    t0 = normalize(cross(n, ref));
    t1 = cross(n, t0);
}

}

void PhysWorld::init(Arena& arena, const Heightfield* hf)
{
    bodies_.init(arena, kMaxBodies, "bodies");
    hf_ = hf;
    contacts_ = arena.push_array<PhysContact>(kMaxContacts);
    previous_ = arena.push_array<PhysContact>(kMaxContacts);
    contact_count_ = 0;
    previous_count_ = 0;
    sync_jolt();
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
    jolt_->optimize();
}

BodyHandle PhysWorld::body_create_box(Vec3 pos, Quat rot, Vec3 half_extents, f32 mass)
{
    const BodyHandle handle = bodies_.alloc();
    RigidBody* body = bodies_.get(handle);
    if (!body) {
        return handle;
    }

    body->pos = pos;
    body->prev_pos = pos;
    body->rot = rot;
    body->prev_rot = rot;
    body->inv_mass = 1.0f / mass;

    const f32 hx = half_extents.x;
    const f32 hy = half_extents.y;
    const f32 hz = half_extents.z;
    const f32 ix = mass / 3.0f * (hy * hy + hz * hz);
    const f32 iy = mass / 3.0f * (hx * hx + hz * hz);
    const f32 iz = mass / 3.0f * (hx * hx + hy * hy);
    body->inv_inertia_local = mat3_diag(1.0f / ix, 1.0f / iy, 1.0f / iz);
    body->half_extents = half_extents;

    const f32 min_half = f_min(hx, f_min(hy, hz));
    const f32 r = f_max(min_half * 0.4f, 0.05f);
    body->sphere_radius = r;
    body->sphere_count = 8;
    const f32 ox = f_max(hx - r, 0.0f);
    const f32 oy = f_max(hy - r, 0.0f);
    const f32 oz = f_max(hz - r, 0.0f);
    for (u32 i = 0; i < 8; i++) {
        body->sphere_offsets[i] = Vec3{(i & 1) ? ox : -ox, (i & 2) ? oy : -oy,
                                       (i & 4) ? oz : -oz};
    }
    body->restitution = 0.15f;
    body->friction = 0.6f;
    return handle;
}

void PhysWorld::body_destroy(BodyHandle handle)
{
    const RigidBody* dead = bodies_.get(handle);
    if (dead) {
        const Vec3 dead_pos = dead->pos;
        for (const u32 slot : bodies_.live_indices()) {
            RigidBody* other = bodies_.at(slot);
            if (other && other != dead && distance_sq(other->pos, dead_pos) < 4.0f) {
                body_wake(*other);
            }
        }
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

void PhysWorld::integrate_velocities(f32 dt)
{
    for (const u32 slot : bodies_.live_indices()) {
        RigidBody& body = *bodies_.at(slot);
        body.prev_pos = body.pos;
        body.prev_rot = body.rot;
        const Vec3 g = gravity_at(body.pos);
        if (body.asleep && length_sq(g - body.gravity) > kGravityWakeSq) {
            body_wake(body);
        }
        body.gravity = g;
        if (body.asleep) {
            body.force_accum = Vec3{0.0f, 0.0f, 0.0f};
            body.torque_accum = Vec3{0.0f, 0.0f, 0.0f};
            continue;
        }

        const Vec3 accel = g + body.force_accum * body.inv_mass;
        body.vel += accel * dt;
        body.angular_vel += body_inv_inertia_world(body) * body.torque_accum * dt;
        body.vel *= 1.0f / (1.0f + tuning_.linear_damping * dt);
        body.angular_vel *= 1.0f / (1.0f + tuning_.angular_damping * dt);
        body.force_accum = Vec3{0.0f, 0.0f, 0.0f};
        body.torque_accum = Vec3{0.0f, 0.0f, 0.0f};
    }
}

void PhysWorld::integrate_positions(f32 dt)
{
    for (const u32 slot : bodies_.live_indices()) {
        RigidBody& body = *bodies_.at(slot);
        if (body.asleep) {
            continue;
        }
        body.pos += body.vel * dt;
        body.rot = quat_integrate(body.rot, body.angular_vel, dt);
    }
}

void PhysWorld::add_contact(const PhysContact& contact)
{
    if (contact_count_ >= kMaxContacts) {
        return;
    }
    PhysContact& stored = contacts_[contact_count_++];
    stored = contact;
    tangent_basis(stored.normal, stored.tangent[0], stored.tangent[1]);
}

void PhysWorld::collect_static_contacts(u32 slot)
{
    RigidBody& body = *bodies_.at(slot);
    if (body.asleep) {
        return;
    }

    for (u32 i = 0; i < body.sphere_count; i++) {
        Sphere sphere;
        sphere.center = body_sphere_center(body, i);
        sphere.radius = body.sphere_radius;

        SphereContact hits[4];
        u32 count = 0;
        if (hf_ && collide_sphere_heightfield(*hf_, sphere, hits[0])) {
            count = 1;
        }
        count += collide_sphere_statics(statics_, sphere, hits + count,
                                        static_cast<u32>(std::size(hits)) - count);

        for (u32 c = 0; c < count; c++) {
            PhysContact contact{};
            contact.key = static_key(slot, i, hits[c].feature);
            contact.body_a = kStaticBody;
            contact.body_b = slot;
            contact.point = hits[c].point;
            contact.normal = hits[c].normal;
            contact.depth = hits[c].depth;
            add_contact(contact);
        }
    }
}

void PhysWorld::collect_contacts()
{
    contact_count_ = 0;

    const std::span<const u32> live = bodies_.live_indices();
    stats_.live_bodies = static_cast<u32>(live.size());

    for (const u32 slot : live) {
        collect_static_contacts(slot);
    }

    std::sort(contacts_, contacts_ + contact_count_,
              [](const PhysContact& x, const PhysContact& y) { return x.key < y.key; });
    stats_.contacts = contact_count_;
}

void PhysWorld::warm_start()
{
    stats_.warm_started = 0;
    if (!tuning_.warm_start || previous_count_ == 0) {
        return;
    }

    u32 prev = 0;
    for (u32 i = 0; i < contact_count_; i++) {
        while (prev < previous_count_ && previous_[prev].key < contacts_[i].key) {
            prev++;
        }
        if (prev >= previous_count_) {
            break;
        }
        if (previous_[prev].key != contacts_[i].key) {
            continue;
        }

        PhysContact& c = contacts_[i];
        c.normal_impulse = previous_[prev].normal_impulse;
        c.tangent_impulse[0] = previous_[prev].tangent_impulse[0];
        c.tangent_impulse[1] = previous_[prev].tangent_impulse[1];
        stats_.warm_started++;

        const Vec3 impulse = c.normal * c.normal_impulse
                           + c.tangent[0] * c.tangent_impulse[0]
                           + c.tangent[1] * c.tangent_impulse[1];
        if (c.body_a != kStaticBody) {
            if (RigidBody* a = bodies_.at(c.body_a)) {
                body_apply_impulse_at_point(*a, -impulse, c.point);
            }
        }
        if (RigidBody* b = bodies_.at(c.body_b)) {
            body_apply_impulse_at_point(*b, impulse, c.point);
        }
    }
}

void PhysWorld::solve(f32 dt)
{
    const f32 inv_dt = dt > 0.0f ? 1.0f / dt : 0.0f;

    for (u32 iter = 0; iter < tuning_.iterations; iter++) {
        for (u32 i = 0; i < contact_count_; i++) {
            PhysContact& c = contacts_[i];
            RigidBody* a = c.body_a == kStaticBody ? nullptr : bodies_.at(c.body_a);
            RigidBody* b = bodies_.at(c.body_b);
            if (!b) {
                continue;
            }

            const f32 inv_mass_a = a ? a->inv_mass : 0.0f;
            const f32 inv_sum = inv_mass_a + b->inv_mass;
            if (inv_sum < 1e-8f) {
                continue;
            }

            const Vec3 rb = c.point - b->pos;
            const Vec3 ra = a ? c.point - a->pos : Vec3{0.0f, 0.0f, 0.0f};
            const Mat3 inv_ib = body_inv_inertia_world(*b);
            const Mat3 inv_ia = a ? body_inv_inertia_world(*a) : mat3_diag(0.0f, 0.0f, 0.0f);

            Vec3 rel = body_velocity_at_point(*b, c.point);
            if (a) {
                rel -= body_velocity_at_point(*a, c.point);
            }

            const Vec3 rbn = cross(rb, c.normal);
            const Vec3 ran = cross(ra, c.normal);
            f32 eff = inv_sum + dot(c.normal, cross(inv_ib * rbn, rb));
            if (a) {
                eff += dot(c.normal, cross(inv_ia * ran, ra));
            }
            if (eff < 1e-8f) {
                continue;
            }

            const f32 vn = dot(rel, c.normal);
            const f32 penetration = f_max(c.depth - tuning_.slop, 0.0f);
            const f32 bias = f_min(tuning_.baumgarte * penetration * inv_dt,
                                   tuning_.max_correction_speed);
            const f32 restitution = -vn > tuning_.restitution_min_speed
                                        ? (a ? f_min(a->restitution, b->restitution)
                                             : b->restitution)
                                        : 0.0f;

            f32 lambda = (-(1.0f + restitution) * vn + bias) / eff;
            const f32 old_normal = c.normal_impulse;
            c.normal_impulse = f_max(old_normal + lambda, 0.0f);
            lambda = c.normal_impulse - old_normal;

            const Vec3 impulse = c.normal * lambda;
            if (a) {
                body_apply_impulse_at_point(*a, -impulse, c.point);
            }
            body_apply_impulse_at_point(*b, impulse, c.point);

            const f32 mu = a ? 0.5f * (a->friction + b->friction) : b->friction;
            const f32 max_friction = mu * c.normal_impulse;

            f32 proposed[2];
            for (u32 axis = 0; axis < 2; axis++) {
                const Vec3 t = c.tangent[axis];
                rel = body_velocity_at_point(*b, c.point);
                if (a) {
                    rel -= body_velocity_at_point(*a, c.point);
                }

                const Vec3 rbt = cross(rb, t);
                const Vec3 rat = cross(ra, t);
                f32 eff_t = inv_sum + dot(t, cross(inv_ib * rbt, rb));
                if (a) {
                    eff_t += dot(t, cross(inv_ia * rat, ra));
                }
                if (eff_t < 1e-8f) {
                    proposed[axis] = c.tangent_impulse[axis];
                    continue;
                }
                proposed[axis] = c.tangent_impulse[axis] - dot(rel, t) / eff_t;
            }

            const f32 magnitude = std::sqrt(proposed[0] * proposed[0]
                                            + proposed[1] * proposed[1]);
            if (magnitude > max_friction && magnitude > 1e-8f) {
                const f32 scale = max_friction / magnitude;
                proposed[0] *= scale;
                proposed[1] *= scale;
            }

            Vec3 friction_impulse{0.0f, 0.0f, 0.0f};
            for (u32 axis = 0; axis < 2; axis++) {
                friction_impulse += c.tangent[axis] * (proposed[axis] - c.tangent_impulse[axis]);
                c.tangent_impulse[axis] = proposed[axis];
            }
            if (a) {
                body_apply_impulse_at_point(*a, -friction_impulse, c.point);
            }
            body_apply_impulse_at_point(*b, friction_impulse, c.point);
        }
    }
}

void PhysWorld::update_sleep(f32 dt)
{
    for (const u32 slot : bodies_.live_indices()) {
        RigidBody& body = *bodies_.at(slot);
        if (body.asleep) {
            continue;
        }
        if (length_sq(body.vel) < tuning_.sleep_linear
            && length_sq(body.angular_vel) < tuning_.sleep_angular) {
            body.sleep_timer += dt;
            if (body.sleep_timer > tuning_.sleep_time) {
                body.asleep = 1;
                body.vel = Vec3{0.0f, 0.0f, 0.0f};
                body.angular_vel = Vec3{0.0f, 0.0f, 0.0f};
            }
        } else {
            body.sleep_timer = 0.0f;
        }
    }
}

void PhysWorld::tick(f32 dt)
{
    integrate_velocities(dt);
    collect_contacts();
    warm_start();
    solve(dt);
    integrate_positions(dt);
    update_sleep(dt);

    std::swap(contacts_, previous_);
    previous_count_ = contact_count_;
    contact_count_ = 0;
}

bool PhysWorld::raycast(Ray ray, f32 max_t, PhysRayHit* out_hit) const
{
    f32 best_t = max_t;
    Vec3 best_normal{0.0f, 1.0f, 0.0f};
    bool found = false;

    f32 t = 0.0f;
    Vec3 normal{0.0f, 1.0f, 0.0f};
    if (hf_ && hf_->raycast(ray, best_t, &t, &normal)) {
        best_t = t;
        best_normal = normal;
        found = true;
    }
    if (statics_.raycast(ray, best_t, &t, &normal)) {
        best_t = t;
        best_normal = normal;
        found = true;
    }

    if (found && out_hit) {
        out_hit->t = best_t;
        out_hit->point = ray.origin + ray.dir * best_t;
        out_hit->normal = best_normal;
    }
    return found;
}

}
