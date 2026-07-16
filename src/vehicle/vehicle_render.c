#include "vehicle/vehicle_render.h"
#include "vehicle/vehicle.h"
#include "physics/physics.h"
#include "assets/assets.h"
#include "render/render.h"

void vehicle_render(const struct Vehicle* v, struct PhysWorld* world, f32 alpha)
{
    RigidBody* body = phys_body(world, v->body);
    if (!body || !v->cfg.body_mesh[0]) {
        return;
    }
    Vec3 pos = vec3_lerp(body->prev_pos, body->pos, alpha);
    Quat rot = quat_slerp(body->prev_rot, body->rot, alpha);
    Vec3 one = v3(1.0f, 1.0f, 1.0f);

    Mat4 base = mat4_trs(pos, rot, one);
    Mat4 model = mat4_mul(base, mat4_trs(vec3_negate(v->cfg.com_offset), quat_identity(), one));
    r_draw_mesh(asset_mesh(v->cfg.body_mesh), model);

    if (!v->cfg.wheel_mesh[0]) {
        return;
    }
    const struct GpuMesh* wheel_mesh = asset_mesh(v->cfg.wheel_mesh);
    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        const Wheel* w = &v->wheels[i];
        f32 drop = v->cfg.wheels[i].travel - w->compression;
        Vec3 local = vec3_sub(w->attach_local, v3(0.0f, drop, 0.0f));
        Vec3 center = vec3_add(pos, quat_rotate_vec3(rot, local));
        Quat q = quat_mul(rot, quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), w->steer_rad));
        q = quat_mul(q, quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), w->spin_angle));
        if (v->cfg.wheels[i].pos.x > 0.0f) {
            q = quat_mul(q, quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), PI32));
        }
        f32 mul = v->effects.tire_radius_mul[i];
        r_draw_mesh(wheel_mesh, mat4_trs(center, q, v3(1.0f, mul, mul)));
    }
}
