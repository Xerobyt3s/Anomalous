#include "carsys/fluids.h"

#define FUEL_IDLE_PER_S 0.00008f
#define FUEL_LOAD_PER_S 0.0007f
#define FUEL_LEAK_PER_S 0.0012f
#define OIL_LEAK_PER_S 0.0009f
#define OIL_LOW_LEVEL 0.25f
#define OIL_STARVE_DMG_PER_S 0.02f
#define HEAT_IDLE_C_PER_S 1.2f
#define HEAT_LOAD_C_PER_S 3.2f
#define COOL_RADIATOR_BASE 0.035f
#define COOL_RADIATOR_AIRFLOW 0.002f
#define COOL_PASSIVE 0.00075f
#define OVERHEAT_DMG_PER_S_PER_C 0.0012f

void fluids_init(Fluids* fluids)
{
    fluids->fuel = 0.75f;
    fluids->oil = 1.0f;
    fluids->coolant_temp = COOLANT_AMBIENT_C;
}

void fluids_tick(Fluids* fluids, PartSlot* parts, f32 rpm, f32 max_rpm, f32 throttle,
                 b32 engine_on, b32 fuel_pump_powered, f32 speed, f32 dt)
{
    PartSlot* engine = &parts[PART_ENGINE];
    const PartSlot* radiator = &parts[PART_RADIATOR];
    const PartSlot* tank = &parts[PART_FUEL_TANK];

    f32 load = f_clamp01(throttle) * f_clamp01(rpm / f_max(max_rpm, 1.0f));
    if (engine_on && fuel_pump_powered) {
        fluids->fuel -= (FUEL_IDLE_PER_S + FUEL_LOAD_PER_S * load) * dt;
    }
    if (tank->condition < 0.5f && fluids->fuel > 0.0f) {
        fluids->fuel -= FUEL_LEAK_PER_S * (0.5f - tank->condition) * 2.0f * dt;
    }
    fluids->fuel = f_clamp01(fluids->fuel);

    if (engine->condition < 0.5f && engine_on) {
        fluids->oil -= OIL_LEAK_PER_S * (0.5f - engine->condition) * 2.0f * dt;
    }
    if (fluids->oil < OIL_LOW_LEVEL && engine_on) {
        f32 starve = 1.0f - fluids->oil / OIL_LOW_LEVEL;
        engine->condition = f_max(engine->condition - OIL_STARVE_DMG_PER_S * starve * dt, 0.0f);
    }
    fluids->oil = f_clamp01(fluids->oil);

    f32 heat = engine_on ? HEAT_IDLE_C_PER_S + HEAT_LOAD_C_PER_S * load : 0.0f;
    f32 radiator_eff = radiator->installed ? radiator->condition : 0.0f;
    f32 cool_coef = COOL_PASSIVE
                  + radiator_eff * (COOL_RADIATOR_BASE + COOL_RADIATOR_AIRFLOW * f_min(speed, 45.0f));
    f32 cool = (fluids->coolant_temp - COOLANT_AMBIENT_C) * cool_coef;
    fluids->coolant_temp += (heat - cool) * dt;
    fluids->coolant_temp = f_max(fluids->coolant_temp, COOLANT_AMBIENT_C);

    if (fluids->coolant_temp > COOLANT_OVERHEAT_C && engine_on) {
        f32 over = fluids->coolant_temp - COOLANT_OVERHEAT_C;
        engine->condition = f_max(engine->condition - over * OVERHEAT_DMG_PER_S_PER_C * dt, 0.0f);
    }
}

f32 fluids_overheat_power_mul(const Fluids* fluids)
{
    if (fluids->coolant_temp <= COOLANT_OVERHEAT_C) {
        return 1.0f;
    }
    f32 t = f_clamp01((fluids->coolant_temp - COOLANT_OVERHEAT_C)
                      / (COOLANT_MELTDOWN_C - COOLANT_OVERHEAT_C));
    return f_lerp(1.0f, 0.45f, t);
}
