#include "carsys/items.h"

static const struct {
    const char* name;
    const char* mesh;
    f32 mass;
    Vec3 cargo_half;
    f32 cargo_roll;
    Vec3 mesh_center;
} s_items[ITEM_KIND_COUNT] = {
    { "nothing",    "",                0.0f,  { 0.0f, 0.0f, 0.0f },   0.0f, { 0.0f, 0.0f, 0.0f } },
    { "battery",    "part_battery",    14.0f, { 0.10f, 0.08f, 0.13f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "alternator", "part_alternator", 6.0f,  { 0.09f, 0.09f, 0.09f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "radiator",   "part_radiator",   9.0f,  { 0.30f, 0.04f, 0.14f }, -0.5f, { 0.0f, 0.0f, 0.0f } },
    { "tire",       "excel_wheel",     16.0f, { 0.28f, 0.09f, 0.28f }, 0.5f, { 0.0f, 0.0f, 0.0f } },
    { "jerry can",  "part_jerrycan",   12.0f, { 0.16f, 0.19f, 0.07f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "oil can",    "part_oilcan",     5.0f,  { 0.06f, 0.12f, 0.06f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "terminal",   "part_computer",   11.0f, { 0.24f, 0.21f, 0.26f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "whip antenna",  "antenna_whip",  2.0f,  { 0.33f, 0.05f, 0.09f }, 0.5f, { 0.0f, 0.315f, 0.036f } },
    { "antenna",       "antenna_std",   4.0f,  { 0.38f, 0.10f, 0.08f }, 0.5f, { 0.0f, 0.37f, 0.01f } },
    { "array antenna", "antenna_array", 15.0f, { 0.48f, 0.17f, 0.17f }, 0.5f, { 0.0f, 0.475f, 0.0f } },
    { "car key",    "part_key",        0.2f,  { 0.04f, 0.015f, 0.07f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "floppy disk", "part_floppy",    0.3f,  { 0.07f, 0.012f, 0.07f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "camera",     "part_camera",     1.5f,  { 0.12f, 0.08f, 0.075f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "cable reel", "part_reel",       6.0f,  { 0.12f, 0.11f, 0.12f }, 0.0f, { 0.0f, 0.0f, 0.0f } },
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
    if (kind == ITEM_TIRE || antenna_variant_for_item(kind) >= 0) {
        return quat_from_axis_angle(v3(0.0f, 0.0f, 1.0f), PI32 * 0.5f);
    }
    return quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), PI32 * 0.5f);
}

Vec3 item_mesh_center(ItemKind kind)
{
    return s_items[kind].mesh_center;
}

ItemKind item_for_part(PartKind part)
{
    switch (part) {
    case PART_BATTERY: return ITEM_BATTERY;
    case PART_ALTERNATOR: return ITEM_ALTERNATOR;
    case PART_RADIATOR: return ITEM_RADIATOR;
    case PART_COMPUTER: return ITEM_COMPUTER;
    case PART_ANTENNA: return ITEM_ANTENNA_STD;
    default:
        return part_kind_is_tire(part) ? ITEM_TIRE : ITEM_NONE;
    }
}

i32 antenna_variant_for_item(ItemKind kind)
{
    switch (kind) {
    case ITEM_ANTENNA_WHIP: return 0;
    case ITEM_ANTENNA_STD: return 1;
    case ITEM_ANTENNA_ARRAY: return 2;
    default: return -1;
    }
}

ItemKind antenna_item_for_variant(i32 variant)
{
    switch (variant) {
    case 0: return ITEM_ANTENNA_WHIP;
    case 2: return ITEM_ANTENNA_ARRAY;
    default: return ITEM_ANTENNA_STD;
    }
}

const char* antenna_variant_name(i32 variant)
{
    switch (variant) {
    case 0: return "WHIP";
    case 2: return "ARRAY";
    default: return "STANDARD";
    }
}
