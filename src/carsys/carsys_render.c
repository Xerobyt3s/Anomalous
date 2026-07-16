#include "carsys/carsys_render.h"
#include "carsys/carsys.h"
#include "vehicle/vehicle.h"
#include "physics/physics.h"
#include "assets/assets.h"
#include "render/render.h"

#define HOOD_HINGE_X 0.0f
#define HOOD_HINGE_Y 0.1474f
#define HOOD_HINGE_Z -0.62f
#define HOOD_OPEN_ANGLE 1.15f
#define DOOR_HINGE_X 0.80f
#define DOOR_HINGE_Z -0.55f
#define DOOR_OPEN_ANGLE 1.05f
#define TRUNK_HINGE_Y 0.2038f
#define TRUNK_HINGE_Z 1.42f
#define TRUNK_OPEN_ANGLE 1.35f
#define LEVER_POS_X -0.13f
#define LEVER_POS_Y -0.17f
#define LEVER_POS_Z 0.44f
#define LEVER_ANGLE_REST 0.12f
#define LEVER_ANGLE_SET 0.55f

void carsys_render(const struct CarSys* sys, const struct Vehicle* veh,
                   struct PhysWorld* world, f32 alpha)
{
    RigidBody* body = phys_body(world, veh->body);
    if (!body || !veh->cfg.body_mesh[0]) {
        return;
    }
    Vec3 pos = vec3_lerp(body->prev_pos, body->pos, alpha);
    Quat rot = quat_slerp(body->prev_rot, body->rot, alpha);
    Vec3 one = v3(1.0f, 1.0f, 1.0f);
    Mat4 base = mat4_trs(pos, rot, one);

    Vec3 hinge_local = vec3_sub(v3(HOOD_HINGE_X, HOOD_HINGE_Y, HOOD_HINGE_Z), veh->cfg.com_offset);
    f32 angle = sys ? sys->hood_open * HOOD_OPEN_ANGLE : 0.0f;
    Mat4 hood = mat4_mul(base, mat4_trs(hinge_local,
                                        quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), angle), one));
    r_draw_mesh(asset_mesh("excel_hood"), hood);

    static const PartKind bay_parts[4] = { PART_ENGINE, PART_BATTERY, PART_ALTERNATOR, PART_RADIATOR };
    for (u32 i = 0; i < 4; i++) {
        const PartDef* def = part_def(bay_parts[i]);
        if (sys && !sys->parts[bay_parts[i]].installed) {
            continue;
        }
        Vec3 local = vec3_sub(def->socket_pos, veh->cfg.com_offset);
        Mat4 model = mat4_mul(base, mat4_trs(local, quat_identity(), one));
        r_draw_mesh(asset_mesh(def->mesh), model);
    }

    Vec3 com = veh->cfg.com_offset;
    for (u32 side = 0; side < 2; side++) {
        f32 sign = side == 0 ? -1.0f : 1.0f;
        f32 open = sys ? sys->door_open[side] : 0.0f;
        Vec3 hinge = vec3_sub(v3(sign * DOOR_HINGE_X, 0.0f, DOOR_HINGE_Z), com);
        Quat swing = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), sign * open * DOOR_OPEN_ANGLE);
        Mat4 door = mat4_mul(base, mat4_trs(hinge, swing, one));
        r_draw_mesh(asset_mesh(side == 0 ? "excel_door_l" : "excel_door_r"), door);
    }

    f32 trunk_open = sys ? sys->trunk_open : 0.0f;
    Vec3 trunk_hinge = vec3_sub(v3(0.0f, TRUNK_HINGE_Y, TRUNK_HINGE_Z), com);
    Mat4 lid = mat4_mul(base, mat4_trs(trunk_hinge,
                                       quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f),
                                                            -trunk_open * TRUNK_OPEN_ANGLE), one));
    r_draw_mesh(asset_mesh("excel_trunk_lid"), lid);

    f32 lever_t = sys ? sys->lever_anim : 1.0f;
    Vec3 lever_pos = vec3_sub(v3(LEVER_POS_X, LEVER_POS_Y, LEVER_POS_Z), com);
    f32 lever_angle = f_lerp(LEVER_ANGLE_REST, LEVER_ANGLE_SET, lever_t);
    Mat4 lever = mat4_mul(base, mat4_trs(lever_pos,
                                         quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), lever_angle),
                                         one));
    r_draw_mesh(asset_mesh("excel_lever"), lever);

    if (sys) {
        for (u32 i = 0; i < CARGO_MAX; i++) {
            const CargoItem* c = &sys->cargo[i];
            if (!c->used) {
                continue;
            }
            Vec3 local = vec3_sub(c->pos, com);
            Mat4 model = mat4_mul(base, mat4_trs(local, item_cargo_rot(c->item.kind), one));
            r_draw_mesh(asset_mesh(item_mesh(c->item.kind)), model);
        }
    }
}
