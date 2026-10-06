#include "carsys/parts.h"

namespace anom {
namespace {

constexpr PartDef kPartDefs[PART_COUNT] = {
    {"engine", false, 120.0f, {0.00f, -0.17f, -1.28f}, {0.28f, 0.16f, 0.30f}, "part_engine", true},
    {"battery", true, 14.0f, {-0.44f, -0.26f, -0.82f}, {0.10f, 0.08f, 0.13f}, "part_battery", true},
    {"alternator", true, 6.0f, {0.34f, -0.22f, -1.06f}, {0.09f, 0.09f, 0.09f}, "part_alternator", true},
    {"radiator", true, 9.0f, {0.00f, -0.20f, -1.80f}, {0.31f, 0.13f, 0.05f}, "part_radiator", true},
    {"fuel tank", false, 40.0f, {0.00f, -0.30f, 1.75f}, {0.30f, 0.11f, 0.21f}, "", false},
    {"headlights", false, 3.0f, {0.00f, 0.03f, -1.98f}, {0.56f, 0.06f, 0.10f}, "", false},
    {"terminal", true, 11.0f, {0.37f, -0.05f, 0.20f}, {0.27f, 0.23f, 0.27f}, "part_computer", false},
    {"antenna", true, 4.0f, {0.35f, 0.515f, 0.50f}, {0.15f, 0.28f, 0.15f}, "", false},
    {"coil", true, 27.0f, {-0.30f, 0.515f, 0.28f}, {0.20f, 0.26f, 0.20f}, "part_coil", false},
    {"tire fl", true, 16.0f, {-0.72f, -0.31f, -1.24f}, {0.14f, 0.31f, 0.31f}, "excel_wheel", false},
    {"tire fr", true, 16.0f, {0.72f, -0.31f, -1.24f}, {0.14f, 0.31f, 0.31f}, "excel_wheel", false},
    {"tire rl", true, 16.0f, {-0.72f, -0.31f, 1.24f}, {0.14f, 0.31f, 0.31f}, "excel_wheel", false},
    {"tire rr", true, 16.0f, {0.72f, -0.31f, 1.24f}, {0.14f, 0.31f, 0.31f}, "excel_wheel", false},
    {"material tank", true, 9.0f, {0.33f, 0.06f, 0.88f}, {0.12f, 0.13f, 0.12f}, "part_tank", false},
    {"printer", true, 15.0f, {0.33f, -0.22f, 0.88f}, {0.22f, 0.15f, 0.18f}, "part_printer", false},
};

} // namespace

const PartDef& part_def(PartKind kind)
{
    return kPartDefs[kind];
}

Quat part_computer_rest_rot()
{
    const Quat yaw = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -kPi * 0.5f - 0.12f);
    const Quat lean = quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, 0.10f);
    const Quat roll = quat_from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, -0.135f);
    return roll * (lean * yaw);
}

bool part_kind_is_tire(PartKind kind)
{
    return kind >= PART_TIRE_FL && kind <= PART_TIRE_RR;
}

void parts_init(PartSlot* parts)
{
    for (u32 i = 0; i < PART_COUNT; i++) {
        parts[i].installed = i != PART_COMPUTER && i != PART_ANTENNA && i != PART_COIL && i != PART_TANK && i != PART_PRINTER;
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
        const f32 dist = distance(local_point, kPartDefs[i].socket_pos);
        const f32 falloff = f_clamp01(1.0f - dist / 1.1f);
        if (falloff <= 0.0f) {
            continue;
        }
        const f32 dmg = severity * falloff
                      * (part_kind_is_tire(static_cast<PartKind>(i)) ? 0.5f : 1.0f);
        parts[i].condition = f_max(parts[i].condition - dmg, 0.0f);
    }
}

} // namespace anom
