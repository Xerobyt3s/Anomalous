#include "carsys/electrics.h"

#define BATTERY_CAPACITY_AMP_SECONDS 90000.0f
#define ALTERNATOR_MAX_AMPS 40.0f
#define IGNITION_AMPS 2.0f
#define STARTER_AMPS 90.0f
#define FUEL_PUMP_AMPS 4.0f
#define HEADLIGHT_AMPS 10.0f
#define DECK_AMPS 1.5f
#define STARTER_MIN_CHARGE 0.12f
#define CONSUMER_MIN_CHARGE 0.02f

static const char* s_consumer_names[CONSUMER_COUNT] = {
    "ignition", "starter", "fuel pump", "headlights", "tape deck",
};

const char* consumer_name(Consumer consumer)
{
    return s_consumer_names[consumer];
}

void electrics_init(Electrics* elec)
{
    elec->battery_charge = 0.9f;
    for (u32 i = 0; i < CONSUMER_COUNT; i++) {
        elec->fuse_ok[i] = 1;
        elec->powered[i] = 0;
    }
    elec->alternator_amps = 0.0f;
    elec->draw_amps = 0.0f;
}

void electrics_tick(Electrics* elec, const PartSlot* parts, f32 rpm, f32 idle_rpm,
                    b32 engine_on, b32 cranking, b32 headlights_switch, b32 deck_on, f32 dt)
{
    const PartSlot* battery = &parts[PART_BATTERY];
    const PartSlot* alternator = &parts[PART_ALTERNATOR];

    f32 supply = 0.0f;
    if (engine_on && alternator->installed && alternator->condition > 0.15f
        && rpm > idle_rpm * 0.8f) {
        supply = ALTERNATOR_MAX_AMPS * alternator->condition;
    }
    elec->alternator_amps = supply;

    b32 battery_ok = battery->installed && battery->condition > 0.05f;
    f32 want[CONSUMER_COUNT];
    want[CONSUMER_IGNITION] = (engine_on || cranking) ? IGNITION_AMPS : 0.0f;
    want[CONSUMER_STARTER] = cranking ? STARTER_AMPS : 0.0f;
    want[CONSUMER_FUEL_PUMP] = (engine_on || cranking) ? FUEL_PUMP_AMPS : 0.0f;
    want[CONSUMER_HEADLIGHTS] = headlights_switch ? HEADLIGHT_AMPS : 0.0f;
    want[CONSUMER_DECK] = deck_on ? DECK_AMPS : 0.0f;

    f32 draw = 0.0f;
    for (u32 i = 0; i < CONSUMER_COUNT; i++) {
        f32 min_charge = i == CONSUMER_STARTER ? STARTER_MIN_CHARGE : CONSUMER_MIN_CHARGE;
        b32 battery_can = battery_ok && elec->battery_charge > min_charge;
        b32 alternator_can = supply >= want[i] && want[i] > 0.0f && i != CONSUMER_STARTER;
        elec->powered[i] = elec->fuse_ok[i] && want[i] > 0.0f && (battery_can || alternator_can);
        if (elec->powered[i]) {
            draw += want[i];
        }
    }
    elec->draw_amps = draw;

    if (battery_ok) {
        f32 net = supply - draw;
        f32 max_charge = 0.25f + 0.75f * battery->condition;
        elec->battery_charge = f_clamp(elec->battery_charge + net * dt / BATTERY_CAPACITY_AMP_SECONDS,
                                       0.0f, max_charge);
    }
}
