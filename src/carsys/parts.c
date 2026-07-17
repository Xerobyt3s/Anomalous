#include "carsys/parts.h"

static const PartDef s_part_defs[PART_COUNT] = {
    { "engine",     0, 120.0f, { 0.00f, -0.17f, -1.28f }, { 0.28f, 0.16f, 0.30f }, "part_engine",   1 },
    { "battery",    1,  14.0f, { -0.44f, -0.26f, -0.82f }, { 0.10f, 0.08f, 0.13f }, "part_battery",  1 },
    { "alternator", 1,   6.0f, { 0.34f, -0.22f, -1.06f }, { 0.09f, 0.09f, 0.09f }, "part_alternator", 1 },
    { "radiator",   1,   9.0f, { 0.00f, -0.20f, -1.80f }, { 0.31f, 0.13f, 0.05f }, "part_radiator", 1 },
    { "fuel tank",  0,  40.0f, { 0.00f, -0.30f, 1.75f },  { 0.30f, 0.11f, 0.21f }, "",              0 },
    { "headlights", 0,   3.0f, { 0.00f, 0.03f, -1.98f },  { 0.56f, 0.06f, 0.10f }, "",              0 },
    { "terminal",   1,  11.0f, { 0.37f, -0.05f, 0.20f },  { 0.27f, 0.23f, 0.27f }, "part_computer", 0 },
    { "antenna",    1,   4.0f, { 0.35f, 0.515f, 0.50f },  { 0.15f, 0.28f, 0.15f }, "",              0 },
    { "tire fl",    1,  16.0f, { -0.72f, -0.31f, -1.24f }, { 0.14f, 0.31f, 0.31f }, "excel_wheel",  0 },
    { "tire fr",    1,  16.0f, { 0.72f, -0.31f, -1.24f },  { 0.14f, 0.31f, 0.31f }, "excel_wheel",  0 },
    { "tire rl",    1,  16.0f, { -0.72f, -0.31f, 1.24f },  { 0.14f, 0.31f, 0.31f }, "excel_wheel",  0 },
    { "tire rr",    1,  16.0f, { 0.72f, -0.31f, 1.24f },   { 0.14f, 0.31f, 0.31f }, "excel_wheel",  0 },
};

const PartDef* part_def(PartKind kind)
{
    return &s_part_defs[kind];
}

Quat part_computer_rest_rot(void)
{
    Quat yaw = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), -PI32 * 0.5f - 0.12f);
    Quat lean = quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), 0.10f);
    Quat roll = quat_from_axis_angle(v3(0.0f, 0.0f, 1.0f), -0.135f);
    return quat_mul(roll, quat_mul(lean, yaw));
}

b32 part_kind_is_tire(PartKind kind)
{
    return kind >= PART_TIRE_FL && kind <= PART_TIRE_RR;
}

void parts_init(PartSlot* parts)
{
    for (u32 i = 0; i < PART_COUNT; i++) {
        parts[i].installed = i != PART_COMPUTER && i != PART_ANTENNA;
        parts[i].condition = 1.0f;
        parts[i].variant = 0;
    }
}

void parts_apply_impact(PartSlot* parts, Vec3 local_point, f32 severity)
{
    for (u32 i = 0; i < PART_COUNT; i++) {
        if (!parts[i].installed) {
            continue;
        }
        f32 dist = vec3_distance(local_point, s_part_defs[i].socket_pos);
        f32 falloff = f_clamp01(1.0f - dist / 1.1f);
        if (falloff <= 0.0f) {
            continue;
        }
        f32 dmg = severity * falloff * (part_kind_is_tire((PartKind)i) ? 0.5f : 1.0f);
        parts[i].condition = f_max(parts[i].condition - dmg, 0.0f);
    }
}
