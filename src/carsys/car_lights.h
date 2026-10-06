#pragma once

#include "carsys/carsys.h"
#include "core/types.h"
#include "math/vmath.h"
#include "render/device.h"
#include "vehicle/vehicle.h"

namespace anom {

inline constexpr u32 kCarLightMax = 4;
inline constexpr f32 kHeadlightBeamPower = 9.0f;
inline constexpr f32 kHeadlightFillPower = 3.0f;
inline constexpr f32 kTailLightPower = 1.6f;

inline u32 car_lights(Vec3 pos, Quat rot, const Vehicle& veh, const CarSys& sys, PointLight* out)
{
    u32 n = 0;
    const Vec3 com = veh.config().com_offset;
    const auto place = [&](Vec3 local, Vec3 color) {
        out[n].pos = pos + rotate(rot, local - com);
        out[n].color = color;
        n++;
    };
    if (veh.effects().headlights_on) {
        const f32 lamp = f_clamp01(sys.parts[PART_HEADLIGHTS].condition);
        const Vec3 warm{1.0f, 0.95f, 0.80f};
        place(Vec3{-0.55f, -0.10f, -5.0f}, warm * (kHeadlightBeamPower * lamp));
        place(Vec3{0.55f, -0.10f, -5.0f}, warm * (kHeadlightBeamPower * lamp));
        place(Vec3{0.0f, 0.15f, -2.2f}, warm * (kHeadlightFillPower * lamp));
    }
    if (veh.input().brake > 0.05f || veh.input().handbrake) {
        place(Vec3{0.0f, 0.05f, 2.3f}, Vec3{1.0f, 0.08f, 0.03f} * kTailLightPower);
    }
    return n;
}

inline f32 puff_rate(f32 slide)
{
    return f_max(slide - 2.5f, 0.0f) * 0.9f;
}

} // namespace anom
