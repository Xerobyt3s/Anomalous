#pragma once

#include "carsys/parts.h"
#include "core/types.h"

#include <string_view>

namespace anom {

enum Consumer : u32 {
    CONSUMER_IGNITION = 0,
    CONSUMER_STARTER,
    CONSUMER_FUEL_PUMP,
    CONSUMER_HEADLIGHTS,
    CONSUMER_DECK,
    CONSUMER_WIPERS,
    CONSUMER_COUNT,
};

std::string_view consumer_name(Consumer consumer);

struct ElectricsInput {
    f32 rpm = 0.0f;
    f32 idle_rpm = 850.0f;
    bool engine_on = false;
    bool cranking = false;
    bool headlights_switch = false;
    bool deck_on = false;
    bool wipers_on = false;
};

struct Electrics {
    f32 battery_charge = 0.9f;
    bool fuse_ok[CONSUMER_COUNT] = {true, true, true, true, true, true};
    bool powered[CONSUMER_COUNT] = {};
    f32 alternator_amps = 0.0f;
    f32 draw_amps = 0.0f;

    void init();
    void tick(const PartSlot* parts, const ElectricsInput& in, f32 dt);
};

} // namespace anom
