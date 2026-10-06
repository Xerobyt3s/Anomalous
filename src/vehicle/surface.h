#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

inline constexpr f32 kWetGripLossOffroad = 0.40f;
inline constexpr f32 kWetGripLossRoad = 0.24f;

inline f32 surface_grip_mul(f32 road, f32 wetness, f32 grass_grip)
{
    const f32 r = f_clamp01(road);
    const f32 dry = f_lerp(grass_grip, 1.0f, r);
    const f32 wet = 1.0f - f_clamp01(wetness) * f_lerp(kWetGripLossOffroad, kWetGripLossRoad, r);
    return dry * wet;
}

inline f32 surface_rolling_mul(f32 road, f32 offroad_mul)
{
    return f_lerp(offroad_mul, 1.0f, f_clamp01(road));
}

} // namespace anom
