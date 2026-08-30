#pragma once

#include "carsys/parts.h"
#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

// Order must match kItems in items.cpp; the table is indexed by this enum.
enum ItemKind : u32 {
    ITEM_NONE = 0,
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
    ITEM_COIL,
    ITEM_KIND_COUNT,
};

struct Item {
    ItemKind kind = ITEM_NONE;
    f32 condition = 1.0f;
    i32 aux = 0;
};

std::string_view item_name(ItemKind kind);
std::string_view item_id(ItemKind kind);
ItemKind item_from_id(std::string_view id);
std::string_view item_mesh(ItemKind kind);
f32 item_mass(ItemKind kind);
Vec3 item_cargo_half(ItemKind kind);
Quat item_cargo_rot(ItemKind kind);
Vec3 item_mesh_center(ItemKind kind);
ItemKind item_for_part(PartKind part);
i32 antenna_variant_for_item(ItemKind kind);
ItemKind antenna_item_for_variant(i32 variant);
std::string_view antenna_variant_name(i32 variant);

} // namespace anom
