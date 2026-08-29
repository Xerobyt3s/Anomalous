#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

struct SkyLighting {
    Vec3 sun_color{0.0f, 0.0f, 0.0f};
    Vec3 ambient_zenith{0.0f, 0.0f, 0.0f};
    Vec3 ambient_horizon{0.0f, 0.0f, 0.0f};
    Vec3 ambient_ground{0.0f, 0.0f, 0.0f};
    Vec3 cloud_sun_color{0.0f, 0.0f, 0.0f};
};

SkyLighting compute_sky_lighting(Vec3 sun_toward, f32 camera_height_metres);

Vec3 probe_sky_radiance(Vec3 sun_toward, f32 elevation_degrees, f32 azimuth_from_sun_degrees,
                        f32 camera_height_metres);

Vec3 sun_direction_for_time(f32 time_of_day);

} // namespace anom
