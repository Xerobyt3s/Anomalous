#pragma once

#include "core/types.h"
#include "vehicle/vehicle_config.h"

struct Wheel;

typedef struct Drivetrain {
    f32 engine_omega;
    i32 gear;
    f32 shift_timer;
    f32 shift_lockout;
    b32 shifting;
} Drivetrain;

void drivetrain_init(Drivetrain* train, const VehicleConfig* cfg);
f32  drivetrain_rpm(const Drivetrain* train);
f32  drivetrain_ratio(const Drivetrain* train, const VehicleConfig* cfg);
f32  drivetrain_torque_curve(const VehicleConfig* cfg, f32 rpm);
void drivetrain_tick(Drivetrain* train, const VehicleConfig* cfg, struct Wheel* wheels,
                     f32 throttle, f32 power_mul, f32 dt);
