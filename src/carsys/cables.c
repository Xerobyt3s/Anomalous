#include "carsys/cables.h"
#include "world/terrain.h"
#include "assets/assets.h"
#include "render/render.h"

#include <string.h>

#define CABLE_ITERS 8
#define CABLE_RADIUS 0.014f
#define CABLE_BOX_COUNT 14

typedef struct CarBox {
    Vec3 center;
    Vec3 half;
} CarBox;

static const CarBox s_car_boxes[CABLE_BOX_COUNT] = {
    { { 0.0f, -0.17f, 0.00f }, { 0.80f, 0.31f, 2.18f } },
    { { 0.0f, 0.075f, 0.10f }, { 0.79f, 0.095f, 0.68f } },
    { { 0.0f, 0.06f, -1.45f }, { 0.72f, 0.06f, 0.73f } },
    { { 0.0f, 0.12f, -0.66f }, { 0.70f, 0.04f, 0.12f } },
    { { 0.0f, 0.22f, -0.48f }, { 0.60f, 0.07f, 0.14f } },
    { { 0.0f, 0.38f, -0.28f }, { 0.57f, 0.09f, 0.14f } },
    { { 0.0f, 0.49f, 0.25f },  { 0.55f, 0.045f, 0.32f } },
    { { 0.0f, 0.42f, 0.72f },  { 0.54f, 0.06f, 0.18f } },
    { { 0.0f, 0.30f, 0.95f },  { 0.58f, 0.06f, 0.20f } },
    { { 0.0f, 0.13f, 1.70f },  { 0.68f, 0.075f, 0.49f } },
    { { -0.72f, -0.255f, -1.24f }, { 0.10f, 0.31f, 0.31f } },
    { { 0.72f, -0.255f, -1.24f },  { 0.10f, 0.31f, 0.31f } },
    { { -0.72f, -0.255f, 1.24f },  { 0.10f, 0.31f, 0.31f } },
    { { 0.72f, -0.255f, 1.24f },   { 0.10f, 0.31f, 0.31f } },
};

void cable_reset(Cable* cable)
{
    cable->state = CABLE_STOWED;
    cable->linked = 0;
    cable->sim_init = 0;
    cable->let_out = 0.0f;
}

static void cable_collide(Vec3* p, const struct Terrain* terrain, Vec3 car_pos, Quat car_rot)
{
    f32 floor_y = heightfield_sample(&terrain->hf, p->x, p->z) + CABLE_RADIUS + 0.005f;
    if (p->y < floor_y) {
        p->y = floor_y;
    }
    Quat inv = quat_conjugate(car_rot);
    Vec3 local = quat_rotate_vec3(inv, vec3_sub(*p, car_pos));
    for (u32 b = 0; b < CABLE_BOX_COUNT; b++) {
        Vec3 d = vec3_sub(local, s_car_boxes[b].center);
        Vec3 half = vec3_add(s_car_boxes[b].half, v3(CABLE_RADIUS, CABLE_RADIUS, CABLE_RADIUS));
        if (f_abs(d.x) >= half.x || f_abs(d.y) >= half.y || f_abs(d.z) >= half.z) {
            continue;
        }
        f32 px = half.x - f_abs(d.x);
        f32 py = half.y - f_abs(d.y);
        f32 pz = half.z - f_abs(d.z);
        if (px < py && px < pz) {
            local.x = s_car_boxes[b].center.x + (d.x >= 0.0f ? half.x : -half.x);
        } else if (py < pz) {
            local.y = s_car_boxes[b].center.y + (d.y >= 0.0f ? half.y : -half.y);
        } else {
            local.z = s_car_boxes[b].center.z + (d.z >= 0.0f ? half.z : -half.z);
        }
    }
    *p = vec3_add(car_pos, quat_rotate_vec3(car_rot, local));
}

void cable_sim(Cable* cable, Vec3 root, const Vec3* end, const struct Terrain* terrain,
               Vec3 car_pos, Quat car_rot, f32 dt)
{
    dt = f_min(dt, 1.0f / 30.0f);
    f32 need = end ? vec3_distance(root, *end) * 1.10f + 0.35f : 1.0f;
    need = f_clamp(need, 0.7f, CABLE_LENGTH);
    if (!cable->sim_init) {
        for (u32 i = 0; i < CABLE_POINTS; i++) {
            f32 f = (f32)i / (f32)(CABLE_POINTS - 1);
            Vec3 target = end ? *end : vec3_add(root, v3(0.0f, -0.3f, 0.0f));
            cable->p[i] = vec3_lerp(root, target, f);
            cable->prev[i] = cable->p[i];
        }
        cable->let_out = need;
        cable->sim_init = 1;
    }
    if (cable->let_out < need) {
        f32 pay_out = f_max((need - cable->let_out) * 10.0f, 1.5f) * dt;
        cable->let_out = f_min(cable->let_out + pay_out, need);
    } else {
        cable->let_out = f_max(cable->let_out - 1.8f * dt, need);
    }
    f32 seg = cable->let_out / (f32)(CABLE_POINTS - 1);
    f32 grav = 9.8f * dt * dt;
    for (u32 i = 1; i < CABLE_POINTS; i++) {
        b32 pinned = end && i == CABLE_POINTS - 1;
        if (pinned) {
            continue;
        }
        Vec3 vel = vec3_scale(vec3_sub(cable->p[i], cable->prev[i]), 0.976f);
        cable->prev[i] = cable->p[i];
        cable->p[i] = vec3_add(cable->p[i], vel);
        cable->p[i].y -= grav;
    }
    cable->p[0] = root;
    if (end) {
        cable->p[CABLE_POINTS - 1] = *end;
    }

    for (u32 iter = 0; iter < CABLE_ITERS; iter++) {
        for (u32 i = 0; i + 1 < CABLE_POINTS; i++) {
            Vec3 delta = vec3_sub(cable->p[i + 1], cable->p[i]);
            f32 len = vec3_length(delta);
            if (len < 1e-6f) {
                continue;
            }
            f32 diff = (len - seg) / len;
            b32 pin_a = i == 0;
            b32 pin_b = end && i + 1 == CABLE_POINTS - 1;
            if (pin_a && pin_b) {
                continue;
            }
            f32 wa = pin_a ? 0.0f : (pin_b ? 1.0f : 0.5f);
            f32 wb = pin_b ? 0.0f : (pin_a ? 1.0f : 0.5f);
            cable->p[i] = vec3_add(cable->p[i], vec3_scale(delta, diff * wa));
            cable->p[i + 1] = vec3_sub(cable->p[i + 1], vec3_scale(delta, diff * wb));
        }
        for (u32 i = 1; i < CABLE_POINTS; i++) {
            if (end && i == CABLE_POINTS - 1) {
                continue;
            }
            cable_collide(&cable->p[i], terrain, car_pos, car_rot);
        }
    }
}

static Quat quat_y_to(Vec3 dir)
{
    Vec3 up = v3(0.0f, 1.0f, 0.0f);
    f32 d = vec3_dot(up, dir);
    if (d > 0.9999f) {
        return quat_identity();
    }
    if (d < -0.9999f) {
        return quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), PI32);
    }
    Vec3 axis = vec3_normalize(vec3_cross(up, dir));
    return quat_from_axis_angle(axis, acosf(f_clamp(d, -1.0f, 1.0f)));
}

void cable_render(const Cable* cable)
{
    if (cable->state == CABLE_STOWED || !cable->sim_init) {
        return;
    }
    const GpuMesh* seg = asset_mesh("cable_seg");
    for (u32 i = 0; i + 1 < CABLE_POINTS; i++) {
        Vec3 delta = vec3_sub(cable->p[i + 1], cable->p[i]);
        f32 len = vec3_length(delta);
        if (len < 1e-5f) {
            continue;
        }
        Vec3 dir = vec3_scale(delta, 1.0f / len);
        Mat4 model = mat4_trs(cable->p[i], quat_y_to(dir),
                              v3(CABLE_RADIUS, len * 1.02f, CABLE_RADIUS));
        r_draw_mesh(seg, model);
    }
    if (cable->state == CABLE_DRAGGED) {
        Vec3 delta = vec3_sub(cable->p[CABLE_POINTS - 1], cable->p[CABLE_POINTS - 2]);
        f32 len = vec3_length(delta);
        Quat rot = len > 1e-5f
                   ? quat_mul(quat_y_to(vec3_scale(delta, 1.0f / len)),
                              quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), -PI32 * 0.5f))
                   : quat_identity();
        r_draw_mesh(asset_mesh("cable_plug"), mat4_trs(cable->p[CABLE_POINTS - 1], rot,
                                                       v3(1.0f, 1.0f, 1.0f)));
    }
}
