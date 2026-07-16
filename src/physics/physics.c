#include "physics/physics.h"
#include "physics/heightfield.h"
#include "physics/collide.h"
#include "core/arena.h"

#define PHYS_GRAVITY_Y -9.81f
#define PHYS_LINEAR_DAMPING 0.02f
#define PHYS_ANGULAR_DAMPING 0.08f
#define PHYS_PUSHOUT_BETA 0.5f
#define PHYS_RESTITUTION_MIN_SPEED 1.0f

#define STATIC_GRID_CELL 8.0f
#define STATIC_RAY_EPS 1e-4f
#define STATIC_T_INF 1e30f

void phys_init(PhysWorld* world, struct Arena* arena, const struct Heightfield* hf)
{
    pool_init(&world->bodies, arena, sizeof(RigidBody), PHYS_MAX_BODIES);
    world->hf = hf;
    world->gravity = v3(0.0f, PHYS_GRAVITY_Y, 0.0f);
    world->contact_count = 0;
    StaticGrid zero = {0};
    world->statics = zero;
}

void phys_statics_reserve(PhysWorld* world, struct Arena* arena, u32 max_tris)
{
    world->statics.tris = arena_push_array(arena, StaticTri, max_tris);
    world->statics.tri_capacity = max_tris;
    world->statics.tri_count = 0;
    world->statics.built = 0;
}

void phys_add_static_tri(PhysWorld* world, Vec3 a, Vec3 b, Vec3 c)
{
    StaticGrid* grid = &world->statics;
    ASSERT(grid->tris && grid->tri_count < grid->tri_capacity && !grid->built);
    if (!grid->tris || grid->tri_count >= grid->tri_capacity) {
        return;
    }
    StaticTri* tri = &grid->tris[grid->tri_count++];
    tri->a = a;
    tri->b = b;
    tri->c = c;
}

static void statics_tri_cell_range(const StaticGrid* grid, const StaticTri* tri,
                                   i32* out_x0, i32* out_x1, i32* out_z0, i32* out_z1)
{
    Vec3 lo = vec3_min(tri->a, vec3_min(tri->b, tri->c));
    Vec3 hi = vec3_max(tri->a, vec3_max(tri->b, tri->c));
    *out_x0 = (i32)f_clamp((lo.x - grid->origin.x) / grid->cell_size, 0.0f, (f32)(grid->cells_x - 1));
    *out_x1 = (i32)f_clamp((hi.x - grid->origin.x) / grid->cell_size, 0.0f, (f32)(grid->cells_x - 1));
    *out_z0 = (i32)f_clamp((lo.z - grid->origin.z) / grid->cell_size, 0.0f, (f32)(grid->cells_z - 1));
    *out_z1 = (i32)f_clamp((hi.z - grid->origin.z) / grid->cell_size, 0.0f, (f32)(grid->cells_z - 1));
}

void phys_statics_build(PhysWorld* world, struct Arena* arena)
{
    StaticGrid* grid = &world->statics;
    if (!grid->tri_count) {
        grid->built = 0;
        return;
    }

    Aabb bounds = aabb_empty();
    for (u32 i = 0; i < grid->tri_count; i++) {
        bounds = aabb_expand(bounds, grid->tris[i].a);
        bounds = aabb_expand(bounds, grid->tris[i].b);
        bounds = aabb_expand(bounds, grid->tris[i].c);
    }
    grid->cell_size = STATIC_GRID_CELL;
    grid->origin = v3(bounds.min.x - 0.5f, 0.0f, bounds.min.z - 0.5f);
    grid->min_y = bounds.min.y - 0.5f;
    grid->max_y = bounds.max.y + 0.5f;
    grid->cells_x = (u32)((bounds.max.x - grid->origin.x) / grid->cell_size) + 2;
    grid->cells_z = (u32)((bounds.max.z - grid->origin.z) / grid->cell_size) + 2;

    u32 cell_count = grid->cells_x * grid->cells_z;
    u32* counts = arena_push_array(arena, u32, cell_count + 1);
    for (u32 i = 0; i <= cell_count; i++) {
        counts[i] = 0;
    }
    u64 total_refs = 0;
    for (u32 i = 0; i < grid->tri_count; i++) {
        i32 x0, x1, z0, z1;
        statics_tri_cell_range(grid, &grid->tris[i], &x0, &x1, &z0, &z1);
        for (i32 z = z0; z <= z1; z++) {
            for (i32 x = x0; x <= x1; x++) {
                counts[(u32)z * grid->cells_x + (u32)x]++;
                total_refs++;
            }
        }
    }
    grid->cell_first = arena_push_array(arena, u32, cell_count + 1);
    u32 running = 0;
    for (u32 i = 0; i < cell_count; i++) {
        grid->cell_first[i] = running;
        running += counts[i];
        counts[i] = grid->cell_first[i];
    }
    grid->cell_first[cell_count] = running;
    grid->cell_tris = arena_push_array(arena, u32, total_refs ? total_refs : 1);
    for (u32 i = 0; i < grid->tri_count; i++) {
        i32 x0, x1, z0, z1;
        statics_tri_cell_range(grid, &grid->tris[i], &x0, &x1, &z0, &z1);
        for (i32 z = z0; z <= z1; z++) {
            for (i32 x = x0; x <= x1; x++) {
                grid->cell_tris[counts[(u32)z * grid->cells_x + (u32)x]++] = i;
            }
        }
    }
    grid->built = 1;
}

static b32 statics_raycast(const StaticGrid* grid, Ray ray, f32 max_t, f32* out_t, Vec3* out_normal)
{
    if (!grid->built) {
        return 0;
    }
    Aabb bounds;
    bounds.min = v3(grid->origin.x, grid->min_y, grid->origin.z);
    bounds.max = v3(grid->origin.x + (f32)grid->cells_x * grid->cell_size, grid->max_y,
                    grid->origin.z + (f32)grid->cells_z * grid->cell_size);
    f32 t_cur = 0.0f;
    if (!aabb_contains_point(bounds, ray.origin)) {
        if (!ray_vs_aabb(ray, bounds, max_t, &t_cur)) {
            return 0;
        }
        t_cur += STATIC_RAY_EPS;
    }

    Vec3 p = vec3_add(ray.origin, vec3_scale(ray.dir, t_cur));
    i32 max_ix = (i32)grid->cells_x - 1;
    i32 max_iz = (i32)grid->cells_z - 1;
    i32 ix = (i32)f_clamp((p.x - grid->origin.x) / grid->cell_size, 0.0f, (f32)max_ix);
    i32 iz = (i32)f_clamp((p.z - grid->origin.z) / grid->cell_size, 0.0f, (f32)max_iz);
    i32 step_x = ray.dir.x > 0.0f ? 1 : -1;
    i32 step_z = ray.dir.z > 0.0f ? 1 : -1;
    f32 t_max_x = STATIC_T_INF, t_max_z = STATIC_T_INF;
    f32 t_delta_x = STATIC_T_INF, t_delta_z = STATIC_T_INF;
    if (f_abs(ray.dir.x) > 1e-9f) {
        f32 boundary = grid->origin.x + (f32)(ix + (step_x > 0 ? 1 : 0)) * grid->cell_size;
        t_max_x = t_cur + (boundary - p.x) / ray.dir.x;
        t_delta_x = grid->cell_size / f_abs(ray.dir.x);
    }
    if (f_abs(ray.dir.z) > 1e-9f) {
        f32 boundary = grid->origin.z + (f32)(iz + (step_z > 0 ? 1 : 0)) * grid->cell_size;
        t_max_z = t_cur + (boundary - p.z) / ray.dir.z;
        t_delta_z = grid->cell_size / f_abs(ray.dir.z);
    }

    f32 best_t = max_t;
    Vec3 best_normal = v3(0.0f, 1.0f, 0.0f);
    b32 found = 0;
    while (ix >= 0 && ix <= max_ix && iz >= 0 && iz <= max_iz && t_cur <= best_t) {
        u32 cell = (u32)iz * grid->cells_x + (u32)ix;
        for (u32 ref = grid->cell_first[cell]; ref < grid->cell_first[cell + 1]; ref++) {
            const StaticTri* tri = &grid->tris[grid->cell_tris[ref]];
            RayHitTri hit;
            if (ray_vs_triangle(ray, tri->a, tri->b, tri->c, best_t, &hit) && hit.t < best_t) {
                best_t = hit.t;
                Vec3 n = vec3_normalize(vec3_cross(vec3_sub(tri->b, tri->a), vec3_sub(tri->c, tri->a)));
                if (vec3_dot(n, ray.dir) > 0.0f) {
                    n = vec3_negate(n);
                }
                best_normal = n;
                found = 1;
            }
        }
        if (t_max_x < t_max_z) {
            t_cur = t_max_x;
            t_max_x += t_delta_x;
            ix += step_x;
        } else {
            t_cur = t_max_z;
            t_max_z += t_delta_z;
            iz += step_z;
        }
    }
    if (found) {
        if (out_t) {
            *out_t = best_t;
        }
        if (out_normal) {
            *out_normal = best_normal;
        }
    }
    return found;
}

u32 collide_sphere_statics(const StaticGrid* grid, Sphere sphere, SphereContact* out_contacts, u32 max_contacts)
{
    if (!grid->built || !max_contacts) {
        return 0;
    }
    f32 r = sphere.radius;
    i32 max_ix = (i32)grid->cells_x - 1;
    i32 max_iz = (i32)grid->cells_z - 1;
    i32 x0 = (i32)f_clamp((sphere.center.x - r - grid->origin.x) / grid->cell_size, 0.0f, (f32)max_ix);
    i32 x1 = (i32)f_clamp((sphere.center.x + r - grid->origin.x) / grid->cell_size, 0.0f, (f32)max_ix);
    i32 z0 = (i32)f_clamp((sphere.center.z - r - grid->origin.z) / grid->cell_size, 0.0f, (f32)max_iz);
    i32 z1 = (i32)f_clamp((sphere.center.z + r - grid->origin.z) / grid->cell_size, 0.0f, (f32)max_iz);

    u32 count = 0;
    for (i32 z = z0; z <= z1; z++) {
        for (i32 x = x0; x <= x1; x++) {
            u32 cell = (u32)z * grid->cells_x + (u32)x;
            for (u32 ref = grid->cell_first[cell]; ref < grid->cell_first[cell + 1]; ref++) {
                const StaticTri* tri = &grid->tris[grid->cell_tris[ref]];
                Vec3 cp = closest_point_on_triangle(sphere.center, tri->a, tri->b, tri->c);
                Vec3 delta = vec3_sub(sphere.center, cp);
                f32 dist_sq = vec3_length_sq(delta);
                if (dist_sq > r * r) {
                    continue;
                }
                f32 dist = sqrtf(dist_sq);
                Vec3 n;
                if (dist > 1e-6f) {
                    n = vec3_scale(delta, 1.0f / dist);
                } else {
                    n = vec3_normalize(vec3_cross(vec3_sub(tri->b, tri->a), vec3_sub(tri->c, tri->a)));
                }
                SphereContact contact;
                contact.point = cp;
                contact.normal = n;
                contact.depth = r - dist;
                if (count < max_contacts) {
                    out_contacts[count++] = contact;
                } else {
                    u32 shallowest = 0;
                    for (u32 i = 1; i < max_contacts; i++) {
                        if (out_contacts[i].depth < out_contacts[shallowest].depth) {
                            shallowest = i;
                        }
                    }
                    if (contact.depth > out_contacts[shallowest].depth) {
                        out_contacts[shallowest] = contact;
                    }
                }
            }
        }
    }
    return count;
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
            SphereContact contacts[4];
            u32 contact_count = 0;
            if (collide_sphere_heightfield(world->hf, sphere, &contacts[0])) {
                contact_count = 1;
            }
            contact_count += collide_sphere_statics(&world->statics, sphere,
                                                    contacts + contact_count,
                                                    ARRAY_COUNT(contacts) - contact_count);
            for (u32 c = 0; c < contact_count; c++) {
                resolve_contact(body, &contacts[c]);
                if (world->contact_count < PHYS_MAX_CONTACTS) {
                    PhysContact* record = &world->contacts[world->contact_count++];
                    record->point = contacts[c].point;
                    record->normal = contacts[c].normal;
                    record->depth = contacts[c].depth;
                }
            }
        }
    }
}

b32 phys_raycast(const PhysWorld* world, Ray ray, f32 max_t, PhysRayHit* out_hit)
{
    f32 best_t = max_t;
    Vec3 best_normal = v3(0.0f, 1.0f, 0.0f);
    b32 found = 0;

    f32 t;
    Vec3 normal;
    if (heightfield_raycast(world->hf, ray, best_t, &t, &normal)) {
        best_t = t;
        best_normal = normal;
        found = 1;
    }
    if (statics_raycast(&world->statics, ray, best_t, &t, &normal)) {
        best_t = t;
        best_normal = normal;
        found = 1;
    }
    if (found && out_hit) {
        out_hit->t = best_t;
        out_hit->point = vec3_add(ray.origin, vec3_scale(ray.dir, best_t));
        out_hit->normal = best_normal;
    }
    return found;
}
