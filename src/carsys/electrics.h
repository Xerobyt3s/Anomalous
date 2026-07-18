#pragma once

#include "core/types.h"
#include "carsys/parts.h"

typedef enum Consumer {
    CONSUMER_IGNITION,
    CONSUMER_STARTER,
    CONSUMER_FUEL_PUMP,
    CONSUMER_HEADLIGHTS,
    CONSUMER_DECK,
    CONSUMER_COUNT,
} Consumer;

typedef struct Electrics {
    f32 battery_charge;
    b32 fuse_ok[CONSUMER_COUNT];
    b32 powered[CONSUMER_COUNT];
    f32 alternator_amps;
    f32 draw_amps;
} Electrics;

const char* consumer_name(Consumer consumer);
void electrics_init(Electrics* elec);
void electrics_tick(Electrics* elec, const PartSlot* parts, f32 rpm, f32 idle_rpm,
                    b32 engine_on, b32 cranking, b32 headlights_switch, b32 deck_on, f32 dt);
