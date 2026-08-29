#pragma once

#include "carsys/parts.h"
#include "core/types.h"

namespace anom {

inline constexpr f32 kCoolantAmbientC = 20.0f;
inline constexpr f32 kCoolantOverheatC = 112.0f;
inline constexpr f32 kCoolantMeltdownC = 126.0f;

struct FluidsInput {
    f32 rpm = 0.0f;
    f32 max_rpm = 6500.0f;
    f32 throttle = 0.0f;
    bool engine_on = false;
    bool fuel_pump_powered = false;
    f32 speed = 0.0f;
};

struct Fluids {
    f32 fuel = 0.75f;
    f32 oil = 1.0f;
    f32 coolant_temp = kCoolantAmbientC;

    void init();
    void tick(PartSlot* parts, const FluidsInput& in, f32 dt);
    f32 overheat_power_mul() const;
};

} // namespace anom
