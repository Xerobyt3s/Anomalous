#include "player/player.h"
#include "physics/physics.h"
#include "physics/collide.h"
#include "physics/heightfield.h"
#include "vehicle/vehicle.h"
#include "render/camera.h"
#include "render/debug_draw.h"

#define PLAYER_RADIUS 0.35f
#define PLAYER_HEIGHT 1.75f
#define PLAYER_EYE_HEIGHT 1.62f
#define PLAYER_STEP_HEIGHT 0.3f
#define PLAYER_WALK_SPEED 4.2f
#define PLAYER_RUN_SPEED 6.8f
#define PLAYER_GROUND_ACCEL 45.0f
#define PLAYER_AIR_ACCEL 10.0f
#define PLAYER_JUMP_SPEED 4.4f
#define PLAYER_SNAP_DOWN 0.3f
#define PLAYER_WALKABLE_NY 0.64f
#define PLAYER_SPHERES 3
#define PLAYER_ENTER_TIME 0.45f
#define PLAYER_EXIT_TIME 0.4f
#define PLAYER_EXIT_MAX_SPEED 1.5f
#define PLAYER_ENTER_RANGE 2.2f
#define PLAYER_ENTER_MAX_CAR_SPEED 2.0f
#define PLAYER_LOOK_SENSITIVITY 0.0022f
#define PLAYER_LOOK_YAW_LIMIT 2.4f
#define PLAYER_LOOK_PITCH_LIMIT 1.0f
#define PLAYER_PITCH_LIMIT (89.0f * DEG_TO_RAD)
#define PLAYER_COCKPIT_LAG_RATE 18.0f
#define PLAYER_COCKPIT_LAG_MAX 0.15f
#define PLAYER_EXIT_CLEARANCE 0.03f

static f32 smooth01(f32 t)
{
    t = f_clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static f32 angle_lerp(f32 a, f32 b, f32 t)
{
    return f_wrap_angle(a + f_wrap_angle(b - a) * t);
}

static void sphere_centers(Vec3 foot, Vec3 out[PLAYER_SPHERES])
{
    f32 bottom = PLAYER_STEP_HEIGHT + PLAYER_RADIUS;
    f32 top = PLAYER_HEIGHT - PLAYER_RADIUS;
    out[0] = vec3_add(foot, v3(0.0f, bottom, 0.0f));
    out[1] = vec3_add(foot, v3(0.0f, (bottom + top) * 0.5f, 0.0f));
    out[2] = vec3_add(foot, v3(0.0f, top, 0.0f));
}

static b32 sphere_vs_car(const RigidBody* car, Vec3 center, f32 radius, SphereContact* out_contact)
{
    Vec3 local = quat_rotate_vec3(quat_conjugate(car->rot), vec3_sub(center, car->pos));
    Vec3 he = car->half_extents;
    Vec3 clamped = v3(f_clamp(local.x, -he.x, he.x),
                      f_clamp(local.y, -he.y, he.y),
                      f_clamp(local.z, -he.z, he.z));
    Vec3 delta = vec3_sub(local, clamped);
    f32 dist_sq = vec3_length_sq(delta);
    if (dist_sq > radius * radius) {
        return 0;
    }
    if (dist_sq > 1e-8f) {
        f32 dist = sqrtf(dist_sq);
        out_contact->normal = quat_rotate_vec3(car->rot, vec3_scale(delta, 1.0f / dist));
        out_contact->depth = radius - dist;
        out_contact->point = vec3_add(car->pos, quat_rotate_vec3(car->rot, clamped));
        return 1;
    }
    f32 pen_x = he.x - f_abs(local.x);
    f32 pen_y = he.y - f_abs(local.y);
    f32 pen_z = he.z - f_abs(local.z);
    Vec3 axis;
    f32 pen;
    if (pen_x <= pen_y && pen_x <= pen_z) {
        axis = v3(local.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
        pen = pen_x;
    } else if (pen_y <= pen_z) {
        axis = v3(0.0f, local.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
        pen = pen_y;
    } else {
        axis = v3(0.0f, 0.0f, local.z >= 0.0f ? 1.0f : -1.0f);
        pen = pen_z;
    }
    out_contact->normal = quat_rotate_vec3(car->rot, axis);
    out_contact->depth = pen + radius;
    out_contact->point = center;
    return 1;
}

static b32 deepest_contact(struct PhysWorld* phys, const RigidBody* car, Vec3 foot, f32 radius,
                           SphereContact* out_contact)
{
    Vec3 centers[PLAYER_SPHERES];
    sphere_centers(foot, centers);
    b32 found = 0;
    SphereContact best = {0};
    for (u32 s = 0; s < PLAYER_SPHERES; s++) {
        Sphere sphere;
        sphere.center = centers[s];
        sphere.radius = radius;
        SphereContact contact;
        if (collide_sphere_heightfield(phys->hf, sphere, &contact)) {
            if (!found || contact.depth > best.depth) {
                best = contact;
                found = 1;
            }
        }
        SphereContact statics[4];
        u32 count = collide_sphere_statics(&phys->statics, sphere, statics, 4);
        for (u32 i = 0; i < count; i++) {
            if (!found || statics[i].depth > best.depth) {
                best = statics[i];
                found = 1;
            }
        }
        if (car && sphere_vs_car(car, centers[s], radius, &contact)) {
            if (!found || contact.depth > best.depth) {
                best = contact;
                found = 1;
            }
        }
    }
    if (found && out_contact) {
        *out_contact = best;
    }
    return found;
}

static void player_resolve_collisions(Player* p, struct PhysWorld* phys, const RigidBody* car)
{
    for (u32 iter = 0; iter < 8; iter++) {
        SphereContact contact;
        if (!deepest_contact(phys, car, p->pos, PLAYER_RADIUS, &contact) || contact.depth < 1e-4f) {
            break;
        }
        p->pos = vec3_add(p->pos, vec3_scale(contact.normal, contact.depth));
        f32 into = vec3_dot(p->vel, contact.normal);
        if (into < 0.0f) {
            p->vel = vec3_sub(p->vel, vec3_scale(contact.normal, into));
        }
    }
}

static void player_ground_snap(Player* p, struct PhysWorld* phys, b32 was_grounded)
{
    p->grounded = 0;
    if (p->vel.y > 0.01f) {
        return;
    }
    f32 probe_up = PLAYER_STEP_HEIGHT + 0.05f;
    Ray ray;
    ray.origin = vec3_add(p->pos, v3(0.0f, probe_up, 0.0f));
    ray.dir = v3(0.0f, -1.0f, 0.0f);
    f32 max_t = probe_up + (was_grounded ? PLAYER_SNAP_DOWN : 0.02f);
    PhysRayHit hit;
    if (!phys_raycast(phys, ray, max_t, &hit)) {
        return;
    }
    if (hit.normal.y < PLAYER_WALKABLE_NY) {
        return;
    }
    p->pos.y = ray.origin.y - hit.t;
    p->vel.y = 0.0f;
    p->grounded = 1;
}

static void player_move_on_foot(Player* p, struct PhysWorld* phys, const RigidBody* car,
                                PlayerCommand cmd, f32 dt)
{
    Vec3 forward = v3(sinf(p->yaw), 0.0f, -cosf(p->yaw));
    Vec3 right = v3(cosf(p->yaw), 0.0f, sinf(p->yaw));
    Vec3 wish = vec3_add(vec3_scale(right, cmd.move_x), vec3_scale(forward, cmd.move_z));
    f32 wish_len = vec3_length(wish);
    if (wish_len > 1.0f) {
        wish = vec3_scale(wish, 1.0f / wish_len);
    }
    f32 target_speed = cmd.run ? PLAYER_RUN_SPEED : PLAYER_WALK_SPEED;

    b32 was_grounded = p->grounded;
    if (p->grounded) {
        Vec3 hvel = v3(p->vel.x, 0.0f, p->vel.z);
        Vec3 goal = vec3_scale(wish, target_speed);
        Vec3 delta = vec3_sub(goal, hvel);
        f32 delta_len = vec3_length(delta);
        f32 max_change = PLAYER_GROUND_ACCEL * dt;
        if (delta_len > max_change) {
            delta = vec3_scale(delta, max_change / delta_len);
        }
        hvel = vec3_add(hvel, delta);
        p->vel.x = hvel.x;
        p->vel.z = hvel.z;
        if (cmd.jump) {
            p->vel.y = PLAYER_JUMP_SPEED;
            p->grounded = 0;
            was_grounded = 0;
        }
    } else {
        p->vel = vec3_add(p->vel, vec3_scale(wish, PLAYER_AIR_ACCEL * dt));
    }
    p->vel.y += phys->gravity.y * dt;
    p->pos = vec3_add(p->pos, vec3_scale(p->vel, dt));

    player_resolve_collisions(p, phys, car);
    player_ground_snap(p, phys, was_grounded);

    f32 surface = heightfield_sample(phys->hf, p->pos.x, p->pos.z);
    if (p->pos.y < surface - 1.0f) {
        p->pos.y = surface;
        p->vel.y = 0.0f;
    }
}

static Vec3 seat_eye_world(const Vehicle* veh, const RigidBody* body)
{
    return vec3_add(body->pos, quat_rotate_vec3(body->rot, veh->cfg.seat_eye));
}

static Vec3 seat_foot_world(const Vehicle* veh, const RigidBody* body)
{
    return vec3_sub(seat_eye_world(veh, body), v3(0.0f, PLAYER_EYE_HEIGHT, 0.0f));
}

static f32 body_yaw(const RigidBody* body)
{
    Vec3 fwd = quat_rotate_vec3(body->rot, v3(0.0f, 0.0f, -1.0f));
    return atan2f(fwd.x, -fwd.z);
}

static f32 body_pitch(const RigidBody* body)
{
    Vec3 fwd = quat_rotate_vec3(body->rot, v3(0.0f, 0.0f, -1.0f));
    return asinf(f_clamp(fwd.y, -1.0f, 1.0f));
}

static b32 player_fits(struct PhysWorld* phys, const RigidBody* car, Vec3 foot)
{
    SphereContact contact;
    if (!deepest_contact(phys, car, foot, PLAYER_RADIUS, &contact)) {
        return 1;
    }
    return contact.depth <= PLAYER_EXIT_CLEARANCE;
}

static b32 player_probe_exit(struct PhysWorld* phys, const Vehicle* veh, Vec3* out_foot)
{
    RigidBody* body = phys_body(phys, veh->body);
    if (!body) {
        return 0;
    }
    Vec3 he = body->half_extents;
    f32 side = veh->cfg.seat_eye.x < 0.0f ? -1.0f : 1.0f;
    f32 out_x = he.x + PLAYER_RADIUS + 0.45f;
    f32 out_z = he.z + PLAYER_RADIUS + 0.6f;
    Vec3 candidates[4];
    candidates[0] = v3(side * out_x, 0.0f, veh->cfg.seat_eye.z);
    candidates[1] = v3(-side * out_x, 0.0f, veh->cfg.seat_eye.z);
    candidates[2] = v3(0.0f, 0.0f, out_z);
    candidates[3] = v3(0.0f, 0.0f, -out_z);
    for (u32 i = 0; i < 4; i++) {
        Vec3 world = vec3_add(body->pos, quat_rotate_vec3(body->rot, candidates[i]));
        Ray ray;
        ray.origin = vec3_add(world, v3(0.0f, 1.5f, 0.0f));
        ray.dir = v3(0.0f, -1.0f, 0.0f);
        PhysRayHit hit;
        if (!phys_raycast(phys, ray, 4.0f, &hit)) {
            continue;
        }
        if (hit.normal.y < PLAYER_WALKABLE_NY) {
            continue;
        }
        Vec3 foot = v3(world.x, ray.origin.y - hit.t, world.z);
        if (player_fits(phys, body, foot)) {
            if (out_foot) {
                *out_foot = foot;
            }
            return 1;
        }
    }
    return 0;
}

void player_init(Player* p, Vec3 pos, f32 yaw)
{
    Player zero = {0};
    *p = zero;
    p->state = PLAYER_ON_FOOT;
    p->pos = pos;
    p->prev_pos = pos;
    p->yaw = yaw;
}

b32 player_driving(const Player* p)
{
    return p->state == PLAYER_DRIVING;
}

b32 player_can_enter(const Player* p, struct PhysWorld* phys, const struct Vehicle* veh)
{
    if (p->state != PLAYER_ON_FOOT || !veh) {
        return 0;
    }
    RigidBody* body = phys_body(phys, veh->body);
    if (!body || vec3_length(body->vel) > PLAYER_ENTER_MAX_CAR_SPEED) {
        return 0;
    }
    Vec3 he = body->half_extents;
    Vec3 waist = vec3_add(p->pos, v3(0.0f, 0.9f, 0.0f));
    for (i32 side = -1; side <= 1; side += 2) {
        Vec3 anchor_local = v3((f32)side * (he.x + 0.4f), 0.0f, veh->cfg.seat_eye.z);
        Vec3 anchor = vec3_add(body->pos, quat_rotate_vec3(body->rot, anchor_local));
        if (vec3_distance(waist, anchor) < PLAYER_ENTER_RANGE) {
            return 1;
        }
    }
    return 0;
}

b32 player_can_exit(const Player* p, struct PhysWorld* phys, const struct Vehicle* veh)
{
    if (p->state != PLAYER_DRIVING || !veh) {
        return 0;
    }
    RigidBody* body = phys_body(phys, veh->body);
    if (!body || vec3_length(body->vel) > PLAYER_EXIT_MAX_SPEED) {
        return 0;
    }
    return player_probe_exit(phys, veh, 0);
}

void player_tick(Player* p, struct PhysWorld* phys, struct Vehicle* veh, PlayerCommand cmd, f32 dt)
{
    p->prev_pos = p->pos;
    RigidBody* body = veh ? phys_body(phys, veh->body) : 0;

    switch (p->state) {
    case PLAYER_ON_FOOT: {
        if (cmd.interact && player_can_enter(p, phys, veh)) {
            p->state = PLAYER_ENTERING;
            p->transition_t = 0.0f;
            p->transition_eye = vec3_add(p->pos, v3(0.0f, PLAYER_EYE_HEIGHT, 0.0f));
            p->transition_yaw = p->yaw;
            p->transition_pitch = p->pitch;
            p->look_yaw = 0.0f;
            p->look_pitch = 0.0f;
            p->vel = vec3_zero();
            p->grounded = 0;
        } else {
            player_move_on_foot(p, phys, body, cmd, dt);
        }
        break;
    }
    case PLAYER_ENTERING: {
        p->transition_t += dt / PLAYER_ENTER_TIME;
        if (body) {
            p->pos = seat_foot_world(veh, body);
        }
        if (p->transition_t >= 1.0f) {
            p->transition_t = 1.0f;
            p->state = PLAYER_DRIVING;
            p->cockpit_eye_valid = 0;
        }
        break;
    }
    case PLAYER_DRIVING: {
        if (body) {
            p->pos = seat_foot_world(veh, body);
        }
        if (cmd.interact && body && vec3_length(body->vel) <= PLAYER_EXIT_MAX_SPEED) {
            Vec3 foot;
            if (player_probe_exit(phys, veh, &foot)) {
                p->state = PLAYER_EXITING;
                p->transition_t = 0.0f;
                p->exit_pos = foot;
                p->transition_eye = p->cockpit_eye_valid ? p->cockpit_eye : seat_eye_world(veh, body);
                p->yaw = f_wrap_angle(body_yaw(body) + p->look_yaw);
                p->pitch = f_clamp(body_pitch(body) + p->look_pitch, -PLAYER_PITCH_LIMIT, PLAYER_PITCH_LIMIT);
            }
        }
        break;
    }
    case PLAYER_EXITING: {
        p->transition_t += dt / PLAYER_EXIT_TIME;
        f32 s = smooth01(p->transition_t);
        Vec3 from = body ? seat_foot_world(veh, body) : p->exit_pos;
        p->pos = vec3_lerp(from, p->exit_pos, s);
        if (p->transition_t >= 1.0f) {
            p->state = PLAYER_ON_FOOT;
            p->pos = p->exit_pos;
            p->prev_pos = p->exit_pos;
            p->vel = vec3_zero();
            p->grounded = 0;
            player_ground_snap(p, phys, 1);
        }
        break;
    }
    }
}

void player_look(Player* p, f32 dx, f32 dy)
{
    if (p->state == PLAYER_ON_FOOT) {
        p->yaw = f_wrap_angle(p->yaw + dx * PLAYER_LOOK_SENSITIVITY);
        p->pitch = f_clamp(p->pitch - dy * PLAYER_LOOK_SENSITIVITY, -PLAYER_PITCH_LIMIT, PLAYER_PITCH_LIMIT);
    } else if (p->state == PLAYER_DRIVING) {
        p->look_yaw = f_clamp(p->look_yaw + dx * PLAYER_LOOK_SENSITIVITY,
                              -PLAYER_LOOK_YAW_LIMIT, PLAYER_LOOK_YAW_LIMIT);
        p->look_pitch = f_clamp(p->look_pitch - dy * PLAYER_LOOK_SENSITIVITY,
                                -PLAYER_LOOK_PITCH_LIMIT, PLAYER_LOOK_PITCH_LIMIT);
    }
}

void player_camera(Player* p, struct PhysWorld* phys, const struct Vehicle* veh, f32 alpha, f32 dt,
                   struct Camera* cam)
{
    RigidBody* body = veh ? phys_body(phys, veh->body) : 0;

    if (p->state == PLAYER_ON_FOOT || !body) {
        Vec3 foot = vec3_lerp(p->prev_pos, p->pos, alpha);
        cam->pos = vec3_add(foot, v3(0.0f, PLAYER_EYE_HEIGHT, 0.0f));
        cam->yaw = p->yaw;
        cam->pitch = p->pitch;
        p->cockpit_eye_valid = 0;
        return;
    }

    Vec3 body_pos = vec3_lerp(body->prev_pos, body->pos, alpha);
    Quat body_rot = quat_slerp(body->prev_rot, body->rot, alpha);
    Vec3 seat_eye = vec3_add(body_pos, quat_rotate_vec3(body_rot, veh->cfg.seat_eye));
    Vec3 fwd = quat_rotate_vec3(body_rot, v3(0.0f, 0.0f, -1.0f));
    f32 car_yaw = atan2f(fwd.x, -fwd.z);
    f32 car_pitch = asinf(f_clamp(fwd.y, -1.0f, 1.0f));

    if (p->state == PLAYER_ENTERING) {
        f32 s = smooth01(p->transition_t);
        cam->pos = vec3_lerp(p->transition_eye, seat_eye, s);
        cam->yaw = angle_lerp(p->transition_yaw, car_yaw, s);
        cam->pitch = p->transition_pitch + (car_pitch - p->transition_pitch) * s;
        p->cockpit_eye_valid = 0;
        return;
    }
    if (p->state == PLAYER_EXITING) {
        f32 s = smooth01(p->transition_t);
        Vec3 exit_eye = vec3_add(p->exit_pos, v3(0.0f, PLAYER_EYE_HEIGHT, 0.0f));
        cam->pos = vec3_lerp(p->transition_eye, exit_eye, s);
        cam->yaw = p->yaw;
        cam->pitch = p->pitch;
        p->cockpit_eye_valid = 0;
        return;
    }

    if (!p->cockpit_eye_valid) {
        p->cockpit_eye = seat_eye;
        p->cockpit_eye_valid = 1;
    }
    p->cockpit_eye = v3(f_approach_exp(p->cockpit_eye.x, seat_eye.x, PLAYER_COCKPIT_LAG_RATE, dt),
                        f_approach_exp(p->cockpit_eye.y, seat_eye.y, PLAYER_COCKPIT_LAG_RATE, dt),
                        f_approach_exp(p->cockpit_eye.z, seat_eye.z, PLAYER_COCKPIT_LAG_RATE, dt));
    Vec3 offset = vec3_sub(p->cockpit_eye, seat_eye);
    f32 offset_len = vec3_length(offset);
    if (offset_len > PLAYER_COCKPIT_LAG_MAX) {
        offset = vec3_scale(offset, PLAYER_COCKPIT_LAG_MAX / offset_len);
        p->cockpit_eye = vec3_add(seat_eye, offset);
    }
    cam->pos = p->cockpit_eye;
    cam->yaw = f_wrap_angle(car_yaw + p->look_yaw);
    cam->pitch = f_clamp(car_pitch + p->look_pitch, -PLAYER_PITCH_LIMIT, PLAYER_PITCH_LIMIT);
}

void player_debug_draw(const Player* p, f32 alpha)
{
    Vec3 foot = vec3_lerp(p->prev_pos, p->pos, alpha);
    Vec3 centers[PLAYER_SPHERES];
    sphere_centers(foot, centers);
    u32 color = p->grounded ? DD_CYAN : DD_ORANGE;
    for (u32 s = 0; s < PLAYER_SPHERES; s++) {
        dd_sphere(centers[s], PLAYER_RADIUS, color);
    }
    dd_line(foot, vec3_add(foot, v3(0.0f, PLAYER_HEIGHT, 0.0f)), color);
}
