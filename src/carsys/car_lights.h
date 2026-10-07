#pragma once

#include "carsys/car_layout.h"
#include "carsys/carsys.h"
#include "core/types.h"
#include "math/vmath.h"
#include "render/device.h"
#include "vehicle/vehicle.h"

namespace anom {

inline constexpr u32 kCarLightMax = 5;
inline constexpr f32 kHeadlightBeamPower = 14.0f;
inline constexpr f32 kHeadlightBeamReach = 0.07f;
inline constexpr f32 kHeadlightBeamCone = 0.906f;
inline constexpr f32 kHeadlightFillPower = 2.0f;
inline constexpr f32 kTailLightPower = 1.6f;
inline constexpr f32 kReverseLightPower = 1.2f;

inline u32 car_lights(Vec3 pos, Quat rot, const Vehicle& veh, const CarSys& sys, PointLight* out)
{
    u32 n = 0;
    const Vec3 com = veh.config().com_offset;
    const auto place = [&](Vec3 local, Vec3 color) -> PointLight& {
        out[n] = PointLight{};
        out[n].pos = pos + rotate(rot, local - com);
        out[n].color = color;
        return out[n++];
    };
    if (veh.effects().headlights_on) {
        const f32 lamp = f_clamp01(sys.parts[PART_HEADLIGHTS].condition);
        const Vec3 warm{1.0f, 0.95f, 0.80f};
        const Vec3 aim = rotate(rot, normalize(car_layout().head_aim));
        for (const f32 side : {-1.0f, 1.0f}) {
            PointLight& beam = place(Vec3{car_layout().head_lamp.x * side, car_layout().head_lamp.y, car_layout().head_lamp.z},
                                     warm * (kHeadlightBeamPower * lamp));
            beam.dir = aim;
            beam.cone = kHeadlightBeamCone;
            beam.reach = kHeadlightBeamReach;
        }
        place(car_layout().head_fill, warm * (kHeadlightFillPower * lamp));
    }
    if (veh.input().brake > 0.05f || veh.input().handbrake) {
        place(car_layout().tail_lamp, Vec3{1.0f, 0.08f, 0.03f} * kTailLightPower);
    }
    if (veh.train().gear < 0) {
        place(car_layout().reverse_lamp, Vec3{0.95f, 0.95f, 1.0f} * kReverseLightPower);
    }
    return n;
}

inline u32 merge_lights(const PointLight* car, u32 car_count, const PointLight* extra, u32 extra_count,
                        PointLight* out, u32 max_count)
{
    u32 n = 0;
    for (u32 i = 0; i < car_count && n < max_count; i++) {
        out[n++] = car[i];
    }
    for (u32 i = 0; i < extra_count && n < max_count; i++) {
        out[n++] = extra[i];
    }
    return n;
}

} // namespace anom
