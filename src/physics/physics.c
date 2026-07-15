#include "physics/physics.h"
#include "physics/heightfield.h"
#include "physics/collide.h"
#include "core/arena.h"

#define PHYS_GRAVITY_Y -9.81f
#define PHYS_LINEAR_DAMPING 0.02f
#define PHYS_ANGULAR_DAMPING 0.08f
#define PHYS_PUSHOUT_BETA 0.5f
#define PHYS_RESTITUTION_MIN_SPEED 1.0f

void phys_init(PhysWorld* world, struct Arena* arena, const struct Heightfield* hf)
{
    pool_init(&world->bodies, arena, sizeof(RigidBody), PHYS_MAX_BODIES);
    world->hf = hf;
    world->gravity = v3(0.0f, PHYS_GRAVITY_Y, 0.0f);
    world->contact_count = 0;
}

BodyHandle phys_body_create_box(PhysWorld* world, Vec3 pos, Quat rot, Vec3 half_extents, f32 mass)
{
    BodyHandle handle = pool_alloc(&world->bodies);
    RigidBody* body = pool_get(&world->bodies, handle);
    if (!body) {
        return handle;
    }
    body->pos = pos;
    body->prev_pos = pos;
    body->rot = rot;
    body->prev_rot = rot;
    body->vel = vec3_zero();
    body->angular_vel = vec3_zero();
    body->inv_mass = 1.0f / mass;
    f32 hx = half_extents.x, hy = half_extents.y, hz = half_extents.z;
    f32 ix = mass / 3.0f * (hy * hy + hz * hz);
    f32 iy = mass / 3.0f * (hx * hx + hz * hz);
    f32 iz = mass / 3.0f * (hx * hx + hy * hy);
    body->inv_inertia_local = mat3_diag(1.0f / ix, 1.0f / iy, 1.0f / iz);
    body->half_extents = half_extents;

    f32 min_half = f_min(hx, f_min(hy, hz));
    f32 r = f_max(min_half * 0.4f, 0.05f);
    body->sphere_radius = r;
    body->sphere_count = 8;
    f32 ox = f_max(hx - r, 0.0f);
    f32 oy = f_max(hy - r, 0.0f);
    f32 oz = f_max(hz - r, 0.0f);
    for (u32 i = 0; i < 8; i++) {
        body->sphere_offsets[i] = v3((i & 1) ? ox : -ox,
                                     (i & 2) ? oy : -oy,
                                     (i & 4) ? oz : -oz);
    }
    body->restitution = 0.15f;
    body->friction = 0.6f;
    return handle;
}

void phys_body_destroy(PhysWorld* world, BodyHandle handle)
{
    pool_free(&world->bodies, handle);
}

RigidBody* phys_body(PhysWorld* world, BodyHandle handle)
{
    return pool_get(&world->bodies, handle);
}

Mat3 body_inv_inertia_world(const RigidBody* body)
{
    Mat3 r = quat_to_mat3(body->rot);
    return mat3_mul(r, mat3_mul(body->inv_inertia_local, mat3_transpose(r)));
}

Vec3 body_velocity_at_point(const RigidBody* body, Vec3 point)
{
    return vec3_add(body->vel, vec3_cross(body->angular_vel, vec3_sub(point, body->pos)));
}

void body_apply_force_at_point(RigidBody* body, Vec3 force, Vec3 point)
{
    body->force_accum = vec3_add(body->force_accum, force);
    body->torque_accum = vec3_add(body->torque_accum, vec3_cross(vec3_sub(point, body->pos), force));
}

void body_apply_impulse_at_point(RigidBody* body, Vec3 impulse, Vec3 point)
{
    body->vel = vec3_add(body->vel, vec3_scale(impulse, body->inv_mass));
    Vec3 angular_impulse = vec3_cross(vec3_sub(point, body->pos), impulse);
    body->angular_vel = vec3_add(body->angular_vel, mat3_mul_vec3(body_inv_inertia_world(body), angular_impulse));
}

static void resolve_contact(RigidBody* body, const SphereContact* contact)
{
    Vec3 n = contact->normal;
    body->pos = vec3_add(body->pos, vec3_scale(n, contact->depth * PHYS_PUSHOUT_BETA));

    Vec3 r = vec3_sub(contact->point, body->pos);
    Vec3 v_point = vec3_add(body->vel, vec3_cross(body->angular_vel, r));
    f32 vn = vec3_dot(v_point, n);
    if (vn >= 0.0f) {
        return;
    }

    Mat3 inv_inertia = body_inv_inertia_world(body);
    Vec3 rxn = vec3_cross(r, n);
    f32 effective_mass = body->inv_mass + vec3_dot(n, vec3_cross(mat3_mul_vec3(inv_inertia, rxn), r));
    if (effective_mass < 1e-8f) {
        return;
    }
    f32 e = (-vn > PHYS_RESTITUTION_MIN_SPEED) ? body->restitution : 0.0f;
    f32 j = -(1.0f + e) * vn / effective_mass;
    body->vel = vec3_add(body->vel, vec3_scale(n, j * body->inv_mass));
    body->angular_vel = vec3_add(body->angular_vel, mat3_mul_vec3(inv_inertia, vec3_scale(rxn, j)));

    v_point = vec3_add(body->vel, vec3_cross(body->angular_vel, r));
    Vec3 vt = vec3_sub(v_point, vec3_scale(n, vec3_dot(v_point, n)));
    f32 vt_len = vec3_length(vt);
    if (vt_len < 1e-5f) {
        return;
    }
    Vec3 t = vec3_scale(vt, -1.0f / vt_len);
    Vec3 rxt = vec3_cross(r, t);
    f32 effective_mass_t = body->inv_mass + vec3_dot(t, vec3_cross(mat3_mul_vec3(inv_inertia, rxt), r));
    if (effective_mass_t < 1e-8f) {
        return;
    }
    f32 jt = f_min(vt_len / effective_mass_t, body->friction * j);
    body->vel = vec3_add(body->vel, vec3_scale(t, jt * body->inv_mass));
    body->angular_vel = vec3_add(body->angular_vel, mat3_mul_vec3(inv_inertia, vec3_scale(rxt, jt)));
}

void phys_tick(PhysWorld* world, f32 dt)
{
    world->contact_count = 0;

    for (u32 idx = 0; idx < world->bodies.capacity; idx++) {
        RigidBody* body = pool_at(&world->bodies, idx);
        if (!body) {
            continue;
        }
        body->prev_pos = body->pos;
        body->prev_rot = body->rot;

        Vec3 accel = vec3_add(world->gravity, vec3_scale(body->force_accum, body->inv_mass));
        body->vel = vec3_add(body->vel, vec3_scale(accel, dt));
        body->angular_vel = vec3_add(body->angular_vel,
                                     vec3_scale(mat3_mul_vec3(body_inv_inertia_world(body), body->torque_accum), dt));
        body->vel = vec3_scale(body->vel, 1.0f / (1.0f + PHYS_LINEAR_DAMPING * dt));
        body->angular_vel = vec3_scale(body->angular_vel, 1.0f / (1.0f + PHYS_ANGULAR_DAMPING * dt));
        body->force_accum = vec3_zero();
        body->torque_accum = vec3_zero();

        body->pos = vec3_add(body->pos, vec3_scale(body->vel, dt));
        body->rot = quat_integrate(body->rot, body->angular_vel, dt);
    }

    for (u32 idx = 0; idx < world->bodies.capacity; idx++) {
        RigidBody* body = pool_at(&world->bodies, idx);
        if (!body) {
            continue;
        }
        Mat3 rot = quat_to_mat3(body->rot);
        for (u32 i = 0; i < body->sphere_count; i++) {
            Sphere sphere;
            sphere.center = vec3_add(body->pos, mat3_mul_vec3(rot, body->sphere_offsets[i]));
            sphere.radius = body->sphere_radius;
            SphereContact contact;
            if (!collide_sphere_heightfield(world->hf, sphere, &contact)) {
                continue;
            }
            resolve_contact(body, &contact);
            if (world->contact_count < PHYS_MAX_CONTACTS) {
                PhysContact* record = &world->contacts[world->contact_count++];
                record->point = contact.point;
                record->normal = contact.normal;
                record->depth = contact.depth;
            }
        }
    }
}

b32 phys_raycast(const PhysWorld* world, Ray ray, f32 max_t, PhysRayHit* out_hit)
{
    f32 t;
    Vec3 normal;
    if (!heightfield_raycast(world->hf, ray, max_t, &t, &normal)) {
        return 0;
    }
    if (out_hit) {
        out_hit->t = t;
        out_hit->point = vec3_add(ray.origin, vec3_scale(ray.dir, t));
        out_hit->normal = normal;
    }
    return 1;
}
