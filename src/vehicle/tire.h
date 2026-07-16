#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "vehicle/vehicle_config.h"

typedef struct TireParams {
    f32 b_long;
    f32 c_long;
    f32 b_lat;
    f32 c_lat;
    f32 peak_mu;
} TireParams;

typedef struct TireForces {
    f32 fx;
    f32 fy;
} TireForces;

TireParams tire_derive_params(const VehicleConfig* cfg);
f32        tire_curve(f32 slip, f32 b, f32 c);
TireForces tire_compute(const TireParams* tp, f32 slip_ratio, f32 slip_angle, f32 load,
                        f32 grip_mul, f32 lat_grip_mul);
