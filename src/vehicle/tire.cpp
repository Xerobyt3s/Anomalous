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

TireForces tire_compute(const TireParams& tp, f32 slip_ratio, f32 slip_angle, f32 load,
                        f32 grip_mul, f32 lat_grip_mul)
{
    const f32 limit = tp.peak_mu * load * grip_mul;

    f32 fx = tp.peak_mu * load * tire_curve(slip_ratio, tp.b_long, tp.c_long) * grip_mul;
    f32 fy = -tp.peak_mu * load * tire_curve(slip_angle, tp.b_lat, tp.c_lat) * grip_mul
           * lat_grip_mul;

    const f32 magnitude = std::sqrt(fx * fx + fy * fy);
    if (magnitude > limit && magnitude > 1e-6f) {
        const f32 scale = limit / magnitude;
        fx *= scale;
        fy *= scale;
    }
    return TireForces{fx, fy};
}

} // namespace anom
