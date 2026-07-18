#include "carsys/carsys.h"
#include "vehicle/vehicle.h"
#include "vehicle/drivetrain.h"
#include "physics/physics.h"

#define BUMP_START_SPEED 2.5f
#define COLD_CRANK_EXTRA 1.1f
#define IMPACT_ACCEL_THRESHOLD 110.0f
#define IMPACT_SEVERITY_SCALE 0.0022f
#define IMPACT_COOLDOWN 0.25f
#define OVERREV_DMG_PER_S 0.02f
#define CRANK_RPM 350.0f
#define BUMP_START_RPM 250.0f
#define RPM_TO_RAD (2.0f * PI32 / 60.0f)

void carsys_init(CarSys* sys)
{
    CarSys zero = {0};
    *sys = zero;
    parts_init(sys->parts);
    electrics_init(&sys->elec);
    fluids_init(&sys->fluids);
    sys->impact_cooldown = 1.0f;
    sys->handbrake_latched = 1;
    sys->lever_anim = 1.0f;
    sys->floppy_disk = -1;
    sys->tape_inserted = -1;
}

b32 carsys_cargo_add(CarSys* sys, Item item, Vec3 pos)
{
    for (u32 i = 0; i < CARGO_MAX; i++) {
        if (sys->cargo[i].used) {
            continue;
        }
        sys->cargo[i].used = 1;
        sys->cargo[i].item = item;
        sys->cargo[i].pos = pos;
        sys->cargo[i].vel = vec3_zero();
        sys->cargo[i].supported = 1;
        return 1;
    }
    return 0;
}

b32 carsys_cargo_take(CarSys* sys, u32 index, Item* out_item)
{
    if (index >= CARGO_MAX || !sys->cargo[index].used) {
        return 0;
    }
    *out_item = sys->cargo[index].item;
    sys->cargo[index].used = 0;
    return 1;
}

u32 carsys_cargo_count(const CarSys* sys)
{
    u32 count = 0;
    for (u32 i = 0; i < CARGO_MAX; i++) {
        count += sys->cargo[i].used ? 1 : 0;
    }
    return count;
}

static b32 cargo_overlap_xz(Vec3 pos_a, Vec3 half_a, Vec3 pos_b, Vec3 half_b)
{
    return f_abs(pos_a.x - pos_b.x) < half_a.x + half_b.x - 0.005f
        && f_abs(pos_a.z - pos_b.z) < half_a.z + half_b.z - 0.005f;
}

f32 carsys_cargo_place_y(const CarSys* sys, ItemKind kind, f32 x, f32 z, b32* out_ok)
{
    Vec3 half = item_cargo_half(kind);
    Vec3 at = v3(x, 0.0f, z);
    f32 y = TRUNK_FLOOR_Y + half.y;
    for (u32 j = 0; j < CARGO_MAX; j++) {
        const CargoItem* other = &sys->cargo[j];
        if (!other->used) {
            continue;
        }
        Vec3 other_half = item_cargo_half(other->item.kind);
        if (cargo_overlap_xz(at, half, other->pos, other_half)) {
            y = f_max(y, other->pos.y + other_half.y + half.y);
        }
    }
    if (out_ok) {
        *out_ok = y + half.y <= TRUNK_TOP_Y + 0.12f;
    }
    return y;
}

static void carsys_cargo_tick(CarSys* sys, Vec3 apparent, f32 dt)
{
    for (u32 i = 0; i < CARGO_MAX; i++) {
        CargoItem* c = &sys->cargo[i];
        if (!c->used) {
            continue;
        }
        Vec3 half = item_cargo_half(c->item.kind);
        c->vel = vec3_add(c->vel, vec3_scale(apparent, dt));
        c->vel = vec3_scale(c->vel, expf(-(c->supported ? 8.0f : 0.5f) * dt));
        f32 shove = sqrtf(apparent.x * apparent.x + apparent.z * apparent.z);
        if (c->supported && shove < 3.5f && vec3_length(c->vel) < 0.3f) {
            c->vel = vec3_zero();
        }
        c->pos = vec3_add(c->pos, vec3_scale(c->vel, dt));
        f32 lo_x = TRUNK_MIN_X + half.x;
        f32 hi_x = f_max(TRUNK_MAX_X - half.x, lo_x);
        f32 lo_z = TRUNK_MIN_Z + half.z;
        f32 hi_z = f_max(TRUNK_MAX_Z - half.z, lo_z);
        if (c->pos.x < lo_x || c->pos.x > hi_x) {
            c->pos.x = f_clamp(c->pos.x, lo_x, hi_x);
            c->vel.x *= -0.2f;
        }
        if (c->pos.z < lo_z || c->pos.z > hi_z) {
            c->pos.z = f_clamp(c->pos.z, lo_z, hi_z);
            c->vel.z *= -0.2f;
        }
    }

    for (u32 i = 0; i < CARGO_MAX; i++) {
        CargoItem* a = &sys->cargo[i];
        if (!a->used) {
            continue;
        }
        Vec3 half_a = item_cargo_half(a->item.kind);
        for (u32 j = i + 1; j < CARGO_MAX; j++) {
            CargoItem* b = &sys->cargo[j];
            if (!b->used) {
                continue;
            }
            Vec3 half_b = item_cargo_half(b->item.kind);
            if (!cargo_overlap_xz(a->pos, half_a, b->pos, half_b)) {
                continue;
            }
            if (f_abs(a->pos.y - b->pos.y) >= half_a.y + half_b.y - 0.01f) {
                continue;
            }
            f32 pen_x = half_a.x + half_b.x - f_abs(a->pos.x - b->pos.x);
            f32 pen_z = half_a.z + half_b.z - f_abs(a->pos.z - b->pos.z);
            if (pen_x < pen_z) {
                f32 dir = a->pos.x <= b->pos.x ? -1.0f : 1.0f;
                a->pos.x += dir * pen_x * 0.5f;
                b->pos.x -= dir * pen_x * 0.5f;
                a->vel.x *= 0.2f;
                b->vel.x *= 0.2f;
            } else {
                f32 dir = a->pos.z <= b->pos.z ? -1.0f : 1.0f;
                a->pos.z += dir * pen_z * 0.5f;
                b->pos.z -= dir * pen_z * 0.5f;
                a->vel.z *= 0.2f;
                b->vel.z *= 0.2f;
            }
        }
    }

    for (u32 i = 0; i < CARGO_MAX; i++) {
        CargoItem* c = &sys->cargo[i];
        if (!c->used) {
            continue;
        }
        Vec3 half = item_cargo_half(c->item.kind);
        f32 rest_y = TRUNK_FLOOR_Y + half.y;
        for (u32 j = 0; j < CARGO_MAX; j++) {
            const CargoItem* other = &sys->cargo[j];
            if (j == i || !other->used) {
                continue;
            }
            Vec3 other_half = item_cargo_half(other->item.kind);
            if (cargo_overlap_xz(c->pos, half, other->pos, other_half)
                && other->pos.y + other_half.y <= c->pos.y + 0.02f) {
                rest_y = f_max(rest_y, other->pos.y + other_half.y + half.y);
            }
        }
        if (c->pos.y < rest_y) {
            c->pos.y = rest_y;
            c->vel.y *= -0.2f;
        }
        f32 hi_y = f_max(TRUNK_TOP_Y - half.y, rest_y);
        if (c->pos.y > hi_y && c->vel.y > 0.0f) {
            c->pos.y = hi_y;
            c->vel.y *= -0.2f;
        }
        c->supported = c->pos.y <= rest_y + 0.005f;
    }
}

static b32 carsys_can_run(const CarSys* sys)
{
    return sys->parts[PART_ENGINE].condition > 0.02f
        && sys->fluids.fuel > 0.0f;
}

static f32 carsys_crank_time(const CarSys* sys)
{
    f32 cold = f_clamp01((45.0f - sys->fluids.coolant_temp) / 45.0f);
    return CARSYS_CRANK_TIME + COLD_CRANK_EXTRA * cold;
}

b32 carsys_try_start(CarSys* sys, struct Vehicle* veh)
{
    if (sys->engine_on || sys->crank_timer > 0.0f || !carsys_can_run(sys)) {
        return 0;
    }
    sys->key_inserted = 1;
    if (drivetrain_rpm(&veh->train) > BUMP_START_RPM && sys->elec.powered[CONSUMER_IGNITION]) {
        sys->engine_on = 1;
        return 1;
    }
    sys->crank_timer = carsys_crank_time(sys);
    return 1;
}

void carsys_stop_engine(CarSys* sys)
{
    sys->engine_on = 0;
    sys->crank_timer = 0.0f;
}

static void carsys_detect_impacts(CarSys* sys, Vehicle* veh, RigidBody* body, Vec3 dv, f32 dt)
{
    sys->impact_cooldown = f_max(sys->impact_cooldown - dt, 0.0f);
    f32 accel = vec3_length(dv) / dt;
    if (accel <= IMPACT_ACCEL_THRESHOLD || sys->impact_cooldown > 0.0f) {
        return;
    }
    sys->impact_cooldown = IMPACT_COOLDOWN;
    f32 severity = (accel - IMPACT_ACCEL_THRESHOLD) * IMPACT_SEVERITY_SCALE;
    sys->last_impact_severity = severity;
    Vec3 dir_local = quat_rotate_vec3(quat_conjugate(body->rot), vec3_normalize(vec3_negate(dv)));
    Vec3 he = veh->cfg.half_extents;
    Vec3 point = v3(f_clamp(dir_local.x * 2.0f, -1.0f, 1.0f) * he.x,
                    f_clamp(dir_local.y * 2.0f, -1.0f, 1.0f) * he.y * 0.5f,
                    f_clamp(dir_local.z * 2.0f, -1.0f, 1.0f) * he.z);
    Vec3 cfg_point = vec3_add(point, veh->cfg.com_offset);
    parts_apply_impact(sys->parts, cfg_point, severity);
}

void carsys_tick(CarSys* sys, struct Vehicle* veh, struct PhysWorld* world, f32 dt)
{
    RigidBody* body = phys_body(world, veh->body);
    if (!body) {
        return;
    }
    Vec3 dv = vec3_sub(body->vel, sys->prev_vel);
    sys->prev_vel = body->vel;
    carsys_detect_impacts(sys, veh, body, dv, dt);

    Vec3 accel_world = vec3_scale(dv, 1.0f / dt);
    Vec3 apparent = quat_rotate_vec3(quat_conjugate(body->rot),
                                     vec3_sub(v3(0.0f, -9.81f, 0.0f), accel_world));
    carsys_cargo_tick(sys, apparent, dt);

    f32 rpm = drivetrain_rpm(&veh->train);
    f32 idle_rpm = veh->cfg.idle_rpm;
    b32 cranking = sys->crank_timer > 0.0f
                || (sys->key_inserted && sys->crank_request && !sys->engine_on);

    electrics_tick(&sys->elec, sys->parts, rpm, idle_rpm, sys->engine_on, cranking,
                   sys->headlight_switch, sys->deck_play, sys->wiper_mode > 0, dt);
    if (sys->deck_play && !sys->elec.powered[CONSUMER_DECK]) {
        sys->deck_play = 0;
    }

    b32 wipers_run = sys->wiper_mode > 0 && sys->elec.powered[CONSUMER_WIPERS];
    if (wipers_run) {
        f32 cycle = sys->wiper_mode == 1 ? 2.6f : 1.0f;
        sys->wiper_phase += dt / cycle;
        if (sys->wiper_phase >= 1.0f) {
            sys->wiper_phase -= 1.0f;
        }
    } else {
        sys->wiper_phase = f_approach_exp(sys->wiper_phase, 0.0f, 4.0f, dt);
    }
    f32 travel = sys->wiper_mode == 1 ? f_clamp01(sys->wiper_phase * 1.6f)
                                      : sys->wiper_phase;
    f32 prev_sweep = sys->wiper_sweep;
    sys->wiper_sweep = travel < 0.5f ? travel * 2.0f : (1.0f - travel) * 2.0f;
    if (wipers_run) {
        sys->windshield_wet = f_max(sys->windshield_wet
                                    - f_abs(sys->wiper_sweep - prev_sweep) * 0.75f, 0.0f);
    }
    sys->windshield_wet = f_clamp01(sys->windshield_wet + sys->rain_level * 0.05f * dt
                                    - (1.0f - sys->rain_level) * 0.012f * dt);
    sys->glass_wet = f_clamp01(sys->glass_wet + sys->rain_level * 0.05f * dt
                               - (1.0f - sys->rain_level) * 0.010f * dt);

    if (cranking) {
        if (!sys->elec.powered[CONSUMER_STARTER] || !carsys_can_run(sys)) {
            sys->crank_timer = 0.0f;
        } else {
            veh->train.engine_omega = f_max(veh->train.engine_omega, CRANK_RPM * RPM_TO_RAD);
            sys->crank_timer -= dt;
            if (sys->crank_timer <= 0.0f) {
                sys->crank_timer = 0.0f;
                if (sys->elec.powered[CONSUMER_FUEL_PUMP] && sys->elec.powered[CONSUMER_IGNITION]) {
                    sys->engine_on = 1;
                }
            }
        }
    }

    sys->crank_active = 0;
    if (sys->key_inserted && !sys->engine_on && sys->crank_request && carsys_can_run(sys)) {
        f32 roll_speed = vec3_length(body->vel);
        if (roll_speed > BUMP_START_SPEED && sys->elec.powered[CONSUMER_IGNITION]
            && sys->elec.powered[CONSUMER_FUEL_PUMP]) {
            sys->engine_on = 1;
        } else if (sys->elec.powered[CONSUMER_STARTER]) {
            sys->crank_active = 1;
            veh->train.engine_omega = f_max(veh->train.engine_omega, CRANK_RPM * RPM_TO_RAD);
            sys->crank_hold += dt;
            if (sys->crank_hold >= carsys_crank_time(sys)
                && sys->elec.powered[CONSUMER_FUEL_PUMP] && sys->elec.powered[CONSUMER_IGNITION]) {
                sys->engine_on = 1;
                sys->crank_hold = 0.0f;
            }
        }
    }
    if (!sys->crank_request || sys->engine_on) {
        sys->crank_hold = 0.0f;
    }

    if (sys->engine_on) {
        if (!carsys_can_run(sys) || !sys->elec.powered[CONSUMER_FUEL_PUMP]
            || !sys->elec.powered[CONSUMER_IGNITION]) {
            sys->engine_on = 0;
        }
    }

    if (sys->computer_on && sys->parts[PART_COMPUTER].installed
        && sys->elec.battery_charge < 0.02f) {
        sys->computer_on = 0;
    }

    f32 speed = vec3_length(body->vel);
    fluids_tick(&sys->fluids, sys->parts, rpm, veh->cfg.max_rpm, veh->input.throttle,
                sys->engine_on, sys->elec.powered[CONSUMER_FUEL_PUMP], speed, dt);

    if (rpm > veh->cfg.max_rpm * 0.985f) {
        sys->parts[PART_ENGINE].condition =
            f_max(sys->parts[PART_ENGINE].condition - OVERREV_DMG_PER_S * dt, 0.0f);
    }

    sys->hood_open = f_approach_exp(sys->hood_open, sys->hood_target ? 1.0f : 0.0f, 9.0f, dt);
    sys->trunk_open = f_approach_exp(sys->trunk_open, sys->trunk_target ? 1.0f : 0.0f, 9.0f, dt);
    for (u32 i = 0; i < 2; i++) {
        sys->door_open[i] = f_approach_exp(sys->door_open[i], sys->door_target[i] ? 1.0f : 0.0f,
                                           12.0f, dt);
    }
    sys->lever_anim = f_approach_exp(sys->lever_anim, sys->handbrake_latched ? 1.0f : 0.0f,
                                     14.0f, dt);
    sys->cap_anim = f_approach_exp(sys->cap_anim, sys->fuel_cap_open ? 1.0f : 0.0f, 10.0f, dt);

    VehicleEffects* fx = &veh->effects;
    f32 engine_cond = sys->parts[PART_ENGINE].condition;
    fx->engine_power_mul = (0.35f + 0.65f * engine_cond) * fluids_overheat_power_mul(&sys->fluids);
    fx->ignition_ok = sys->engine_on;
    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        const PartSlot* tire = &sys->parts[PART_TIRE_FL + i];
        if (!tire->installed) {
            fx->tire_radius_mul[i] = 0.64f;
            fx->tire_grip_mul[i] = 0.15f;
        } else {
            f32 inflate = f_clamp01(tire->condition / 0.25f);
            fx->tire_radius_mul[i] = f_lerp(0.85f, 1.0f, inflate);
            fx->tire_grip_mul[i] = 0.35f + 0.65f * f_clamp01(tire->condition * 1.4f);
        }
    }
    fx->brake_mul = 1.0f;
    fx->headlights_on = sys->headlight_switch && sys->elec.powered[CONSUMER_HEADLIGHTS]
                     && sys->parts[PART_HEADLIGHTS].condition > 0.1f;
    sys->popup_anim = f_approach_exp(sys->popup_anim, fx->headlights_on ? 1.0f : 0.0f, 6.0f, dt);
}
