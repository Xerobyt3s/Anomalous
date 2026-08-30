#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

enum PartKind : u32 {
    PART_ENGINE = 0,
    PART_BATTERY,
    PART_ALTERNATOR,
    PART_RADIATOR,
    PART_FUEL_TANK,
    PART_HEADLIGHTS,
    PART_COMPUTER,
    PART_ANTENNA,
    PART_COIL,
    PART_TIRE_FL,
    PART_TIRE_FR,
    PART_TIRE_RL,
    PART_TIRE_RR,
    PART_COUNT,
};

struct PartDef {
    std::string_view name;
    bool removable;
    f32 mass;
    Vec3 socket_pos;
    Vec3 socket_half;
    std::string_view mesh;
    bool engine_bay;
};

struct PartSlot {
    bool installed = false;
    f32 condition = 1.0f;
    i32 variant = 0;
};

const PartDef& part_def(PartKind kind);
Quat part_computer_rest_rot();
bool part_kind_is_tire(PartKind kind);
void parts_init(PartSlot* parts);
void parts_apply_impact(PartSlot* parts, Vec3 local_point, f32 severity);

} // namespace anom
