#pragma once

#include "core/types.h"
#include "math/vmath.h"

typedef enum PartKind {
    PART_ENGINE,
    PART_BATTERY,
    PART_ALTERNATOR,
    PART_RADIATOR,
    PART_FUEL_TANK,
    PART_HEADLIGHTS,
    PART_COMPUTER,
    PART_ANTENNA,
    PART_TIRE_FL,
    PART_TIRE_FR,
    PART_TIRE_RL,
    PART_TIRE_RR,
    PART_COUNT,
} PartKind;

typedef struct PartDef {
    const char* name;
    b32 removable;
    f32 mass;
    Vec3 socket_pos;
    Vec3 socket_half;
    const char* mesh;
    b32 engine_bay;
} PartDef;

typedef struct PartSlot {
    b32 installed;
    f32 condition;
    i32 variant;
} PartSlot;

const PartDef* part_def(PartKind kind);
Quat part_computer_rest_rot(void);
b32  part_kind_is_tire(PartKind kind);
void parts_init(PartSlot* parts);
void parts_apply_impact(PartSlot* parts, Vec3 local_point, f32 severity);
