#include "carsys/items.h"

static const struct {
    const char* name;
    const char* mesh;
    f32 mass;
    Vec3 cargo_half;
    f32 cargo_roll;
} s_items[ITEM_KIND_COUNT] = {
    { "nothing",    "",                0.0f,  { 0.0f, 0.0f, 0.0f },   0.0f },
    { "battery",    "part_battery",    14.0f, { 0.10f, 0.08f, 0.13f }, 0.0f },
    { "alternator", "part_alternator", 6.0f,  { 0.09f, 0.09f, 0.09f }, 0.0f },
    { "radiator",   "part_radiator",   9.0f,  { 0.30f, 0.04f, 0.14f }, -0.5f },
    { "tire",       "excel_wheel",     16.0f, { 0.28f, 0.09f, 0.28f }, 0.5f },
    { "jerry can",  "part_jerrycan",   12.0f, { 0.16f, 0.19f, 0.07f }, 0.0f },
    { "oil can",    "part_oilcan",     5.0f,  { 0.06f, 0.12f, 0.06f }, 0.0f },
    { "car key",    "part_key",        0.2f,  { 0.04f, 0.015f, 0.07f }, 0.0f },
};

const char* item_name(ItemKind kind)
{
    return s_items[kind].name;
}

const char* item_mesh(ItemKind kind)
{
    return s_items[kind].mesh;
}

f32 item_mass(ItemKind kind)
{
    return s_items[kind].mass;
}

Vec3 item_cargo_half(ItemKind kind)
{
    return s_items[kind].cargo_half;
}

Quat item_cargo_rot(ItemKind kind)
{
    if (s_items[kind].cargo_roll == 0.0f) {
        return quat_identity();
    }
    if (kind == ITEM_TIRE) {
        return quat_from_axis_angle(v3(0.0f, 0.0f, 1.0f), PI32 * 0.5f);
    }
    return quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), PI32 * 0.5f);
}

ItemKind item_for_part(PartKind part)
{
    switch (part) {
    case PART_BATTERY: return ITEM_BATTERY;
    case PART_ALTERNATOR: return ITEM_ALTERNATOR;
    case PART_RADIATOR: return ITEM_RADIATOR;
    default:
        return part_kind_is_tire(part) ? ITEM_TIRE : ITEM_NONE;
    }
}
