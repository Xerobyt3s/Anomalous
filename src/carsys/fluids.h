#pragma once

#include "core/types.h"
#include "carsys/parts.h"

#define COOLANT_AMBIENT_C 20.0f
#define COOLANT_OVERHEAT_C 112.0f
#define COOLANT_MELTDOWN_C 126.0f

typedef struct Fluids {
    f32 fuel;
    f32 oil;
    f32 coolant_temp;
} Fluids;

void fluids_init(Fluids* fluids);
void fluids_tick(Fluids* fluids, PartSlot* parts, f32 rpm, f32 max_rpm, f32 throttle,
                 b32 engine_on, b32 fuel_pump_powered, f32 speed, f32 dt);
f32  fluids_overheat_power_mul(const Fluids* fluids);
