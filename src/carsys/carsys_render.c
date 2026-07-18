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

#define MAX_PUFFS 96

typedef struct Puff {
    Vec3 pos;
    Vec3 vel;
    f32 life;
    f32 max_life;
    f32 size;
    b32 spark;
    b32 used;
} Puff;

static Puff s_puffs[MAX_PUFFS];
static u32 s_puff_next;
static f32 s_smoke_accum;
static u32 s_fx_rng = 0x9E3779B9u;

static f32 fx_rand(void)
{
    s_fx_rng ^= s_fx_rng << 13;
    s_fx_rng ^= s_fx_rng >> 17;
    s_fx_rng ^= s_fx_rng << 5;
    return (f32)(s_fx_rng >> 8) / 16777216.0f;
}

static void puff_spawn(Vec3 pos, Vec3 vel, f32 life, f32 size, b32 spark)
{
    Puff* p = &s_puffs[s_puff_next];
    s_puff_next = (s_puff_next + 1) % MAX_PUFFS;
    p->pos = pos;
    p->vel = vel;
    p->life = life;
    p->max_life = life;
    p->size = size;
    p->spark = spark;
    p->used = 1;
}

static void carsys_render_effects(const struct CarSys* sys, const struct Vehicle* veh,
                                  Mat4 base, f32 dt)
{
    if (sys && sys->fluids.coolant_temp > 105.0f && sys->engine_on) {
        f32 rate = (sys->fluids.coolant_temp - 105.0f) * 0.6f;
        s_smoke_accum += rate * dt;
        while (s_smoke_accum >= 1.0f) {
            s_smoke_accum -= 1.0f;
            Vec3 local = vec3_sub(v3((fx_rand() - 0.5f) * 0.5f, 0.08f, -1.35f + fx_rand() * 0.4f),
                                  veh->cfg.com_offset);
            Vec3 pos = mat4_transform_point(base, local);
            puff_spawn(pos, v3((fx_rand() - 0.5f) * 0.5f, 0.9f + fx_rand() * 0.6f,
                               (fx_rand() - 0.5f) * 0.5f),
                       1.1f + fx_rand() * 0.6f, 0.05f + fx_rand() * 0.05f, 0);
        }
    }

    for (u32 i = 0; i < MAX_PUFFS; i++) {
        Puff* p = &s_puffs[i];
        if (!p->used) {
            continue;
        }
        p->life -= dt;
        if (p->life <= 0.0f) {
            p->used = 0;
            continue;
        }
        p->vel.y += (p->spark ? -9.8f : 0.6f) * dt;
        p->pos = vec3_add(p->pos, vec3_scale(p->vel, dt));
        f32 t = 1.0f - p->life / p->max_life;
        f32 scale = p->spark ? p->size * (1.0f - t)
                  : p->size * (0.6f + 2.2f * t) * f_clamp01(p->life * 5.0f);
        r_draw_mesh(asset_mesh(p->spark ? "warn_amber" : "puff"),
                    mat4_trs(p->pos, quat_identity(), v3(scale, scale, scale)));
    }
}

void carsys_spawn_sparks(Vec3 pos, u32 count)
{
    for (u32 i = 0; i < count; i++) {
        puff_spawn(pos, v3((fx_rand() - 0.5f) * 3.0f, 1.0f + fx_rand() * 2.0f,
                           (fx_rand() - 0.5f) * 3.0f),
                   0.2f + fx_rand() * 0.15f, 0.025f, 1);
    }
}

void carsys_render_glass(const struct CarSys* sys, const struct Vehicle* veh,
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
    Vec3 com = veh->cfg.com_offset;

    r_draw_glass(asset_mesh("excel_glass"),
                 mat4_mul(base, mat4_trs(vec3_scale(com, -1.0f), quat_identity(), one)));
    for (i32 side = 0; side < 2; side++) {
        f32 sign = side == 0 ? -1.0f : 1.0f;
        f32 open = sys ? sys->door_open[side] : 0.0f;
        Vec3 hinge = vec3_sub(v3(sign * DOOR_HINGE_X, 0.0f, DOOR_HINGE_Z), com);
        Quat swing = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), sign * open * DOOR_OPEN_ANGLE);
        Mat4 door = mat4_mul(base, mat4_trs(hinge, swing, one));
        r_draw_glass(asset_mesh(side == 0 ? "excel_door_glass_l" : "excel_door_glass_r"), door);
    }
}

void carsys_render(const struct CarSys* sys, const struct Vehicle* veh,
                   struct PhysWorld* world, f32 alpha, f32 dt, u32 screen_texture)
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

    f32 popup = sys ? sys->popup_anim : 0.0f;
    for (u32 p = 0; p < 2; p++) {
        f32 sign = p == 0 ? -1.0f : 1.0f;
        f32 pod = popup;
        if (p == 1 && sys && sys->parts[PART_HEADLIGHTS].condition < 0.4f) {
            pod = f_min(popup, 0.38f);
        }
        Mat4 pop = mat4_mul(hood, mat4_trs(v3(sign * 0.40f, -0.0934f, -0.88f),
                                           quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f),
                                                                pod * 0.7f), one));
        r_draw_mesh(asset_mesh("excel_popup"), pop);
    }

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
            Quat cargo_rot = item_cargo_rot(c->item.kind);
            Vec3 local = vec3_sub(vec3_sub(c->pos, com),
                                  quat_rotate_vec3(cargo_rot,
                                                   item_mesh_center(c->item.kind)));
            Mat4 model = mat4_mul(base, mat4_trs(local, cargo_rot, one));
            r_draw_mesh(asset_mesh(item_mesh(c->item.kind)), model);
        }

        Mat4 cap = mat4_mul(base, mat4_trs(vec3_sub(v3(0.80f, 0.145f, 1.30f), com),
                                           quat_from_axis_angle(v3(0.0f, 0.0f, 1.0f),
                                                                sys->cap_anim * 1.3f), one));
        r_draw_mesh(asset_mesh("excel_fuelcap"), cap);

        if (sys->key_inserted) {
            Mat4 key = mat4_mul(base, mat4_trs(vec3_sub(v3(-0.22f, 0.05f, -0.25f), com),
                                               quat_identity(), one));
            r_draw_mesh(asset_mesh("part_key"), key);
        }

        Vec3 wiper_axis = vec3_normalize(v3(0.0f, 0.807f, -0.591f));
        f32 wiper_angle = -(0.20f + sys->wiper_sweep * 1.30f);
        static const f32 wiper_px[2] = { -0.38f, 0.10f };
        for (u32 wp = 0; wp < 2; wp++) {
            Mat4 arm = mat4_mul(base,
                                mat4_trs(vec3_sub(v3(wiper_px[wp], 0.150f, -0.60f), com),
                                         quat_from_axis_angle(wiper_axis, wiper_angle), one));
            r_draw_mesh(asset_mesh("part_wiper"), arm);
        }

        r_draw_mesh(asset_mesh("jack_coax"),
                    mat4_mul(base, mat4_trs(vec3_sub(v3(0.35f, 0.515f, 0.36f), com),
                                            quat_identity(), one)));
        r_draw_mesh(asset_mesh("jack_bus"),
                    mat4_mul(base, mat4_trs(vec3_sub(v3(0.32f, -0.02f, -0.75f), com),
                                            quat_identity(), one)));

        Mat4 deck = mat4_mul(base, mat4_trs(vec3_sub(v3(0.12f, -0.045f, -0.295f), com),
                                            quat_identity(), one));
        r_draw_mesh(asset_mesh("part_deck"), deck);
        if (sys->tape_inserted >= 0) {
            r_draw_mesh(asset_mesh("part_cassette"),
                        mat4_mul(deck, mat4_trs(v3(0.0f, 0.006f, 0.052f),
                                                quat_identity(), one)));
        }

        if (sys->parts[PART_ANTENNA].installed) {
            static const char* ant_meshes[3] = { "antenna_whip", "antenna_std", "antenna_array" };
            i32 variant = sys->parts[PART_ANTENNA].variant;
            if (variant < 0 || variant > 2) {
                variant = 1;
            }
            Mat4 ant = mat4_mul(base, mat4_trs(vec3_sub(part_def(PART_ANTENNA)->socket_pos, com),
                                               quat_identity(), one));
            r_draw_mesh(asset_mesh(ant_meshes[variant]), ant);
        }

        if (sys->parts[PART_COMPUTER].installed) {
            const PartDef* cdef = part_def(PART_COMPUTER);
            Mat4 term = mat4_mul(base, mat4_trs(vec3_sub(cdef->socket_pos, com),
                                                part_computer_rest_rot(), one));
            r_draw_mesh(asset_mesh("part_computer"), term);
            if (sys->floppy_disk >= 0) {
                r_draw_mesh(asset_mesh("part_floppy"),
                            mat4_mul(term, mat4_trs(v3(0.0f, -0.119f, 0.223f),
                                                    quat_identity(), one)));
            }
            if (screen_texture) {
                Mat4 screen = mat4_mul(term, mat4_trs(v3(0.0f, 0.047f, 0.170f),
                                                      quat_identity(),
                                                      v3(0.304f, 0.19f, 1.0f)));
                r_draw_lit_quad(screen, screen_texture);
            } else if (sys->computer_on) {
                r_draw_mesh(asset_mesh("computer_glow"), term);
            }
        }

        f32 speed_norm = f_clamp01(f_abs(vehicle_forward_speed((Vehicle*)veh, world)) * 3.6f / 200.0f);
        f32 rpm_norm = f_clamp01(drivetrain_rpm(&veh->train) / 7000.0f);
        f32 fuel_norm = f_clamp01(sys->fluids.fuel);
        f32 temp_norm = f_clamp01((sys->fluids.coolant_temp - 20.0f) / 106.0f);
        static const f32 dial_x[4] = { -0.44f, -0.30f, -0.405f, -0.335f };
        static const f32 dial_y[4] = { 0.064f, 0.064f, 0.006f, 0.006f };
        static const f32 dial_scale[4] = { 1.0f, 1.0f, 0.5f, 0.5f };
        f32 dial_val[4];
        dial_val[0] = speed_norm;
        dial_val[1] = rpm_norm;
        dial_val[2] = fuel_norm;
        dial_val[3] = temp_norm;
        for (u32 d = 0; d < 4; d++) {
            f32 needle_angle = 2.27f - dial_val[d] * 4.54f;
            Vec3 local = vec3_sub(v3(dial_x[d], dial_y[d], -0.305f), com);
            Mat4 needle = mat4_mul(base, mat4_trs(local,
                                                  quat_from_axis_angle(v3(0.0f, 0.0f, 1.0f),
                                                                       needle_angle),
                                                  v3(dial_scale[d], dial_scale[d], dial_scale[d])));
            r_draw_mesh(asset_mesh("excel_needle"), needle);
        }

        b32 warn_on[4];
        warn_on[0] = sys->fluids.coolant_temp > COOLANT_OVERHEAT_C;
        warn_on[1] = sys->fluids.oil < 0.3f;
        warn_on[2] = sys->elec.battery_charge < 0.15f;
        warn_on[3] = sys->handbrake_latched;
        static const b32 warn_amber[4] = { 0, 0, 1, 0 };
        for (u32 wn = 0; wn < 4; wn++) {
            if (!warn_on[wn]) {
                continue;
            }
            Vec3 local = vec3_sub(v3(-0.418f + 0.028f * (f32)wn, 0.030f, -0.304f), com);
            Mat4 model = mat4_mul(base, mat4_trs(local, quat_identity(),
                                                 v3(0.014f, 0.014f, 0.008f)));
            r_draw_mesh(asset_mesh(warn_amber[wn] ? "warn_amber" : "warn_red"), model);
        }

        if (veh->input.brake > 0.05f) {
            r_draw_mesh(asset_mesh("excel_brakelight"),
                        mat4_mul(base, mat4_trs(vec3_negate(com), quat_identity(), one)));
        }
        if (veh->train.gear == -1) {
            r_draw_mesh(asset_mesh("excel_revlight"),
                        mat4_mul(base, mat4_trs(vec3_negate(com), quat_identity(), one)));
        }
    }

    carsys_render_effects(sys, veh, base, dt);
}
