#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "carsys/parts.h"

typedef enum ItemKind {
    ITEM_NONE,
    ITEM_BATTERY,
    ITEM_ALTERNATOR,
    ITEM_RADIATOR,
    ITEM_TIRE,
    ITEM_JERRYCAN,
    ITEM_OILCAN,
    ITEM_COMPUTER,
    ITEM_ANTENNA_WHIP,
    ITEM_ANTENNA_STD,
    ITEM_ANTENNA_ARRAY,
    ITEM_KEY,
    ITEM_FLOPPY,
    ITEM_CAMERA,
    ITEM_REEL,
    ITEM_CASSETTE,
    ITEM_KIND_COUNT,
} ItemKind;

typedef struct Item {
    ItemKind kind;
    f32 condition;
    i32 aux;
} Item;

const char* item_name(ItemKind kind);
const char* item_id(ItemKind kind);
ItemKind    item_from_id(const char* id);
const char* item_mesh(ItemKind kind);
f32         item_mass(ItemKind kind);
Vec3        item_cargo_half(ItemKind kind);
Quat        item_cargo_rot(ItemKind kind);
Vec3        item_mesh_center(ItemKind kind);
ItemKind    item_for_part(PartKind part);
i32         antenna_variant_for_item(ItemKind kind);
ItemKind    antenna_item_for_variant(i32 variant);
const char* antenna_variant_name(i32 variant);
