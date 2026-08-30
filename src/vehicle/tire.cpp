#include "vehicle/tire.h"

namespace anom {

TireParams tire_derive_params(const VehicleConfig& cfg)
{
    const f32 slide_ratio = f_clamp(cfg.tire_slide_mu / f_max(cfg.tire_peak_mu, 0.01f),
                                    0.05f, 0.999f);
    const f32 c = 2.0f - (2.0f / kPi) * std::asin(slide_ratio);
    const f32 peak_arg = std::tan(kPi / (2.0f * c));

    TireParams tp;
    tp.c_long = c;
    tp.c_lat = c;
    tp.b_long = peak_arg / f_max(cfg.tire_peak_slip, 0.01f);
    tp.b_lat = peak_arg / f_max(cfg.tire_peak_angle_deg * kDegToRad, 0.01f);
    tp.peak_mu = cfg.tire_peak_mu;
    return tp;
}

f32 tire_curve(f32 slip, f32 b, f32 c)
{
    return std::sin(c * std::atan(b * slip));
}

// Combined slip. How much grip is left comes from the two slips normalised onto a common
// scale and added; which way it points comes from the way the patch is actually sliding.
//
// Both halves matter. Reading fx and fy off separate curves and scaling the pair back
// inside the circle never lets go, because a locked wheel still reports whatever cornering
// force its slip angle asks for. But taking the direction from the normalised slips is
// just as wrong the other way: slip ratio saturates at one while a slip angle is only a
// few tenths of a radian, so a locked wheel comes out pointing almost straight down the
// car and keeps no cornering force at all, whatever angle it is really travelling at.
TireForces tire_compute(const TireParams& tp, f32 slip_ratio, f32 slip_angle, f32 slip_vel,
                        f32 lat_vel, f32 load, f32 grip_mul, f32 lat_grip_mul)
{
    const f32 sx = slip_ratio * tp.b_long;
    const f32 sy = slip_angle * tp.b_lat;
    const f32 s = std::sqrt(sx * sx + sy * sy);
    if (s < 1e-5f) {
        return TireForces{0.0f, 0.0f};
    }

    const f32 force = tp.peak_mu * grip_mul * load * std::sin(tp.c_long * std::atan(s));

    f32 dx = slip_vel;
    f32 dy = -lat_vel;
    const f32 speed = std::sqrt(dx * dx + dy * dy);
    if (speed > 1e-4f) {
        dx /= speed;
        dy /= speed;
    } else {
        dx = sx / s;
        dy = -sy / s;
    }
    return TireForces{force * dx, force * dy * lat_grip_mul};
}

} // namespace anom
