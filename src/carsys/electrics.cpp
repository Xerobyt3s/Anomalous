#include "carsys/electrics.h"
#include "math/vmath.h"

namespace anom {
namespace {

constexpr f32 kBatteryCapacityAmpSeconds = 90000.0f;
constexpr f32 kAlternatorMaxAmps = 40.0f;
constexpr f32 kIgnitionAmps = 2.0f;
constexpr f32 kStarterAmps = 90.0f;
constexpr f32 kFuelPumpAmps = 4.0f;
constexpr f32 kHeadlightAmps = 10.0f;
constexpr f32 kDeckAmps = 1.5f;
constexpr f32 kWiperAmps = 3.0f;
constexpr f32 kStarterMinCharge = 0.12f;
constexpr f32 kConsumerMinCharge = 0.02f;

} // namespace

std::string_view consumer_name(Consumer consumer)
{
    switch (consumer) {
    case CONSUMER_IGNITION: return "ignition";
    case CONSUMER_STARTER: return "starter";
    case CONSUMER_FUEL_PUMP: return "fuel pump";
    case CONSUMER_HEADLIGHTS: return "headlights";
    case CONSUMER_DECK: return "tape deck";
    case CONSUMER_WIPERS: return "wipers";
    case CONSUMER_COUNT: break;
    }
    return "?";
}

void Electrics::init()
{
    *this = Electrics{};
}

void Electrics::tick(const PartSlot* parts, const ElectricsInput& in, f32 dt)
{
    const PartSlot& battery = parts[PART_BATTERY];
    const PartSlot& alternator = parts[PART_ALTERNATOR];

    f32 supply = 0.0f;
    if (in.engine_on && alternator.installed && alternator.condition > 0.15f
        && in.rpm > in.idle_rpm * 0.8f) {
        supply = kAlternatorMaxAmps * alternator.condition;
    }
    alternator_amps = supply;

    const bool battery_ok = battery.installed && battery.condition > 0.05f;

    f32 want[CONSUMER_COUNT];
    want[CONSUMER_IGNITION] = (in.engine_on || in.cranking) ? kIgnitionAmps : 0.0f;
    want[CONSUMER_STARTER] = in.cranking ? kStarterAmps : 0.0f;
    want[CONSUMER_FUEL_PUMP] = (in.engine_on || in.cranking) ? kFuelPumpAmps : 0.0f;
    want[CONSUMER_HEADLIGHTS] = in.headlights_switch ? kHeadlightAmps : 0.0f;
    want[CONSUMER_DECK] = in.deck_on ? kDeckAmps : 0.0f;
    want[CONSUMER_WIPERS] = in.wipers_on ? kWiperAmps : 0.0f;

    f32 draw = 0.0f;
    for (u32 i = 0; i < CONSUMER_COUNT; i++) {
        const f32 min_charge = i == CONSUMER_STARTER ? kStarterMinCharge : kConsumerMinCharge;
        const bool battery_can = battery_ok && battery_charge > min_charge;
        const bool alternator_can = supply >= want[i] && want[i] > 0.0f && i != CONSUMER_STARTER;
        powered[i] = fuse_ok[i] && want[i] > 0.0f && (battery_can || alternator_can);
        if (powered[i]) {
            draw += want[i];
        }
    }
    draw_amps = draw;

    if (battery_ok) {
        const f32 net = supply - draw;
        const f32 max_charge = 0.25f + 0.75f * battery.condition;
        battery_charge = f_clamp(battery_charge + net * dt / kBatteryCapacityAmpSeconds, 0.0f,
                                 max_charge);
    }
}

} // namespace anom
