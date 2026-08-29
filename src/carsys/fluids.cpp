#include "carsys/fluids.h"
#include "math/vmath.h"

namespace anom {
namespace {

constexpr f32 kFuelIdlePerS = 0.00008f;
constexpr f32 kFuelLoadPerS = 0.0007f;
constexpr f32 kFuelLeakPerS = 0.0012f;
constexpr f32 kOilLeakPerS = 0.0009f;
constexpr f32 kOilLowLevel = 0.25f;
constexpr f32 kOilStarveDmgPerS = 0.02f;
constexpr f32 kHeatIdleCPerS = 1.2f;
constexpr f32 kHeatLoadCPerS = 3.2f;
constexpr f32 kCoolRadiatorBase = 0.035f;
constexpr f32 kCoolRadiatorAirflow = 0.002f;
constexpr f32 kCoolPassive = 0.00075f;
constexpr f32 kOverheatDmgPerSPerC = 0.0012f;

} // namespace

void Fluids::init()
{
    *this = Fluids{};
}

void Fluids::tick(PartSlot* parts, const FluidsInput& in, f32 dt)
{
    PartSlot& engine = parts[PART_ENGINE];
    const PartSlot& radiator = parts[PART_RADIATOR];
    const PartSlot& tank = parts[PART_FUEL_TANK];

    const f32 load = f_clamp01(in.throttle) * f_clamp01(in.rpm / f_max(in.max_rpm, 1.0f));
    if (in.engine_on && in.fuel_pump_powered) {
        fuel -= (kFuelIdlePerS + kFuelLoadPerS * load) * dt;
    }
    if (tank.condition < 0.5f && fuel > 0.0f) {
        fuel -= kFuelLeakPerS * (0.5f - tank.condition) * 2.0f * dt;
    }
    fuel = f_clamp01(fuel);

    if (engine.condition < 0.5f && in.engine_on) {
        oil -= kOilLeakPerS * (0.5f - engine.condition) * 2.0f * dt;
    }
    if (oil < kOilLowLevel && in.engine_on) {
        const f32 starve = 1.0f - oil / kOilLowLevel;
        engine.condition = f_max(engine.condition - kOilStarveDmgPerS * starve * dt, 0.0f);
    }
    oil = f_clamp01(oil);

    const f32 heat = in.engine_on ? kHeatIdleCPerS + kHeatLoadCPerS * load : 0.0f;
    const f32 radiator_eff = radiator.installed ? radiator.condition : 0.0f;
    const f32 cool_coef = kCoolPassive
                        + radiator_eff * (kCoolRadiatorBase
                                          + kCoolRadiatorAirflow * f_min(in.speed, 45.0f));
    const f32 cool = (coolant_temp - kCoolantAmbientC) * cool_coef;
    coolant_temp += (heat - cool) * dt;
    coolant_temp = f_max(coolant_temp, kCoolantAmbientC);

    if (coolant_temp > kCoolantOverheatC && in.engine_on) {
        const f32 over = coolant_temp - kCoolantOverheatC;
        engine.condition = f_max(engine.condition - over * kOverheatDmgPerSPerC * dt, 0.0f);
    }
}

f32 Fluids::overheat_power_mul() const
{
    if (coolant_temp <= kCoolantOverheatC) {
        return 1.0f;
    }
    const f32 t = f_clamp01((coolant_temp - kCoolantOverheatC)
                            / (kCoolantMeltdownC - kCoolantOverheatC));
    return f_lerp(1.0f, 0.45f, t);
}

} // namespace anom
