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
    ITEM_KIND_COUNT,
} ItemKind;

typedef struct Item {
    ItemKind kind;
    f32 condition;
} Item;

const char* item_name(ItemKind kind);
const char* item_mesh(ItemKind kind);
f32         item_mass(ItemKind kind);
Vec3        item_cargo_half(ItemKind kind);
Quat        item_cargo_rot(ItemKind kind);
ItemKind    item_for_part(PartKind part);
