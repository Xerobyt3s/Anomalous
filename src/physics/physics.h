#pragma once

#include "core/types.h"
#include "core/pool.h"
#include "math/vmath.h"

struct Arena;
struct Heightfield;

#define PHYS_MAX_BODIES 256
#define PHYS_MAX_SPHERES_PER_BODY 8
#define PHYS_MAX_CONTACTS 512

typedef Handle BodyHandle;

typedef struct RigidBody {
    Vec3 pos;
    Vec3 prev_pos;
    Quat rot;
    Quat prev_rot;
    Vec3 vel;
    Vec3 angular_vel;
    Vec3 force_accum;
    Vec3 torque_accum;
    f32 inv_mass;
    Mat3 inv_inertia_local;
    Vec3 half_extents;
    Vec3 sphere_offsets[PHYS_MAX_SPHERES_PER_BODY];
    u32 sphere_count;
    f32 sphere_radius;
    f32 restitution;
    f32 friction;
    Vec3 box_offset;
    f32 sleep_timer;
    b32 asleep;
} RigidBody;

typedef struct PhysContact {
    Vec3 point;
    Vec3 normal;
    f32 depth;
    Handle body;
} PhysContact;

typedef struct PhysRayHit {
    f32 t;
    Vec3 point;
    Vec3 normal;
} PhysRayHit;

typedef struct StaticTri {
    Vec3 a;
    Vec3 b;
    Vec3 c;
} StaticTri;

typedef struct StaticGrid {
    StaticTri* tris;
    u32 tri_count;
    u32 tri_capacity;
    u32* cell_first;
    u32* cell_tris;
    u32 cells_x;
    u32 cells_z;
    Vec3 origin;
    f32 cell_size;
    f32 min_y;
    f32 max_y;
    b32 built;
} StaticGrid;

typedef struct PhysWorld {
    Pool bodies;
    const struct Heightfield* hf;
    StaticGrid statics;
    Vec3 gravity;
    PhysContact contacts[PHYS_MAX_CONTACTS];
    u32 contact_count;
} PhysWorld;

void       phys_init(PhysWorld* world, struct Arena* arena, const struct Heightfield* hf);
void       phys_statics_reserve(PhysWorld* world, struct Arena* arena, u32 max_tris);
void       phys_add_static_tri(PhysWorld* world, Vec3 a, Vec3 b, Vec3 c);
void       phys_statics_build(PhysWorld* world, struct Arena* arena);
BodyHandle phys_body_create_box(PhysWorld* world, Vec3 pos, Quat rot, Vec3 half_extents, f32 mass);
void       phys_body_destroy(PhysWorld* world, BodyHandle handle);
RigidBody* phys_body(PhysWorld* world, BodyHandle handle);
void       phys_tick(PhysWorld* world, f32 dt);
b32        phys_raycast(const PhysWorld* world, Ray ray, f32 max_t, PhysRayHit* out_hit);

void body_apply_force_at_point(RigidBody* body, Vec3 force, Vec3 point);
void body_apply_impulse_at_point(RigidBody* body, Vec3 impulse, Vec3 point);
Vec3 body_velocity_at_point(const RigidBody* body, Vec3 point);
Mat3 body_inv_inertia_world(const RigidBody* body);
