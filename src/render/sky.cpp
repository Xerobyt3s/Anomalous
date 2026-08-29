#include "render/sky.h"

namespace anom {
namespace {

constexpr f32 kPlanetRadius = 6360.0f;
constexpr f32 kAtmosphereRadius = 6460.0f;
constexpr f32 kAtmosphereThickness = kAtmosphereRadius - kPlanetRadius;

constexpr Vec3 kBetaRayleigh{5.802e-3f, 13.558e-3f, 33.1e-3f};
constexpr f32 kBetaMieScatter = 10.0e-3f;
constexpr f32 kBetaMieAbsorb = 4.4e-3f;
constexpr Vec3 kBetaOzone{0.650e-3f, 1.881e-3f, 0.085e-3f};

constexpr f32 kHeightRayleigh = 8.0f;
constexpr f32 kHeightMie = 1.2f;
constexpr f32 kOzoneCentre = 25.0f;
constexpr f32 kOzoneHalfWidth = 15.0f;

constexpr f32 kMieG = 0.76f;
constexpr Vec3 kSunIrradiance{20.0f, 20.0f, 20.0f};
constexpr Vec3 kGroundAlbedo{0.10f, 0.09f, 0.07f};

constexpr i32 kViewSteps = 32;
constexpr i32 kLightSteps = 8;

constexpr f32 kMultipleScatterStrength = 0.15f;
constexpr f32 kMultipleScatterAltitude = 30.0f;
constexpr f32 kCloudAltitude = 6.0f;

Vec3 vec_exp(Vec3 v)
{
    return {std::exp(v.x), std::exp(v.y), std::exp(v.z)};
}

Vec2 ray_sphere(f32 altitude, f32 cos_zenith, f32 sphere_altitude)
{
    const f32 radius = kPlanetRadius + altitude;
    const f32 b = radius * cos_zenith;
    const f32 c = (altitude - sphere_altitude)
                * (2.0f * kPlanetRadius + altitude + sphere_altitude);
    f32 discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return {-1.0f, -1.0f};
    }
    discriminant = std::sqrt(discriminant);
    return {-b - discriminant, -b + discriminant};
}

Vec2 ray_sphere_at(Vec3 position, Vec3 direction, f32 sphere_altitude)
{
    const f32 altitude = length(position) - kPlanetRadius;
    const f32 cos_zenith = dot(normalize(position), direction);
    return ray_sphere(altitude, cos_zenith, sphere_altitude);
}

Vec3 atmosphere_density(f32 altitude)
{
    return {std::exp(-altitude / kHeightRayleigh),
            std::exp(-altitude / kHeightMie),
            f_max(0.0f, 1.0f - f_abs(altitude - kOzoneCentre) / kOzoneHalfWidth)};
}

Vec3 extinction_coefficients(Vec3 density)
{
    return kBetaRayleigh * density.x + Vec3{1.0f, 1.0f, 1.0f}
                                           * ((kBetaMieScatter + kBetaMieAbsorb) * density.y)
         + kBetaOzone * density.z;
}

Vec3 atmosphere_transmittance(Vec3 position, Vec3 direction, f32 ray_length)
{
    if (ray_length <= 0.0f) {
        return {1.0f, 1.0f, 1.0f};
    }
    Vec3 optical_depth{0.0f, 0.0f, 0.0f};
    f32 previous = 0.0f;
    for (i32 i = 0; i < kLightSteps; i++) {
        const f32 fraction = static_cast<f32>(i + 1) / static_cast<f32>(kLightSteps);
        const f32 next = ray_length * fraction * fraction;
        const f32 segment = next - previous;
        const Vec3 sample = position + direction * (previous + segment * 0.5f);
        const f32 altitude = length(sample) - kPlanetRadius;
        optical_depth += extinction_coefficients(atmosphere_density(altitude)) * segment;
        previous = next;
    }
    return vec_exp(-optical_depth);
}

Vec3 sun_transmittance(Vec3 position, Vec3 sun_toward)
{
    if (ray_sphere_at(position, sun_toward, 0.0f).x > 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }
    const f32 to_edge = ray_sphere_at(position, sun_toward, kAtmosphereThickness).y;
    return atmosphere_transmittance(position, sun_toward, to_edge);
}

f32 sunlit_air_fraction(Vec3 sun_toward)
{
    const f32 depression = f_max(-sun_toward.y, 0.0f);
    const f32 shadow_height = kPlanetRadius * depression * depression * 0.5f;
    return std::exp(-shadow_height / kHeightRayleigh);
}

Vec3 multiple_scatter_fill(Vec3 sun_toward)
{
    const Vec3 high_altitude{0.0f, kPlanetRadius + kMultipleScatterAltitude, 0.0f};
    const f32 strength = sun_toward.y >= 0.0f
                             ? kMultipleScatterStrength
                             : kMultipleScatterStrength * sunlit_air_fraction(sun_toward);
    return hadamard(kSunIrradiance, sun_transmittance(high_altitude, sun_toward))
         * (strength * (1.0f / (4.0f * kPi)));
}

f32 rayleigh_phase(f32 cos_theta)
{
    return (3.0f / (16.0f * kPi)) * (1.0f + cos_theta * cos_theta);
}

f32 henyey_greenstein(f32 cos_theta, f32 g)
{
    const f32 gg = g * g;
    const f32 denom = 1.0f + gg - 2.0f * g * cos_theta;
    return (1.0f - gg) / (4.0f * kPi * denom * std::sqrt(f_max(denom, 1e-4f)));
}

Vec3 atmosphere_radiance(Vec3 origin, Vec3 direction, Vec3 sun_toward)
{
    const f32 origin_altitude = length(origin) - kPlanetRadius;
    const Vec2 atmosphere_hit = ray_sphere(origin_altitude, direction.y, kAtmosphereThickness);
    if (atmosphere_hit.y < 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }

    const f32 ray_start = f_max(atmosphere_hit.x, 0.0f);
    f32 ray_end = atmosphere_hit.y;

    const Vec2 planet_hit = ray_sphere(origin_altitude, direction.y, 0.0f);
    if (planet_hit.x > 0.0f) {
        ray_end = planet_hit.x;
    }

    const f32 span = ray_end - ray_start;
    Vec3 optical_depth{0.0f, 0.0f, 0.0f};
    Vec3 scattered_rayleigh{0.0f, 0.0f, 0.0f};
    Vec3 scattered_mie{0.0f, 0.0f, 0.0f};
    Vec3 scattered_multiple{0.0f, 0.0f, 0.0f};
    f32 previous = ray_start;

    for (i32 i = 0; i < kViewSteps; i++) {
        const f32 fraction = static_cast<f32>(i + 1) / static_cast<f32>(kViewSteps);
        const f32 next = ray_start + span * fraction * fraction;
        const f32 segment = next - previous;

        const Vec3 position = origin + direction * (previous + segment * 0.5f);
        const f32 altitude = length(position) - kPlanetRadius;
        const Vec3 density = atmosphere_density(altitude) * segment;

        optical_depth += extinction_coefficients(density);
        const Vec3 view_transmittance = vec_exp(-optical_depth);
        const Vec3 combined = hadamard(view_transmittance,
                                       sun_transmittance(position, sun_toward));

        scattered_rayleigh += combined * density.x;
        scattered_mie += combined * density.y;
        scattered_multiple += hadamard(view_transmittance,
                                       kBetaRayleigh * density.x
                                           + Vec3{1.0f, 1.0f, 1.0f} * (kBetaMieScatter * density.y));
        previous = next;
    }

    const f32 cos_theta = dot(direction, sun_toward);
    Vec3 radiance = hadamard(hadamard(scattered_rayleigh, kBetaRayleigh)
                                     * rayleigh_phase(cos_theta)
                                 + scattered_mie
                                       * (kBetaMieScatter * henyey_greenstein(cos_theta, kMieG)),
                             kSunIrradiance);
    radiance += hadamard(scattered_multiple, multiple_scatter_fill(sun_toward));
    return radiance;
}

Vec3 origin_for_height(f32 camera_height_metres)
{
    return Vec3{0.0f, kPlanetRadius + 0.2f + camera_height_metres * 0.001f, 0.0f};
}

} // namespace

SkyLighting compute_sky_lighting(Vec3 sun_toward, f32 camera_height_metres)
{
    const Vec3 origin = origin_for_height(camera_height_metres);
    const Vec3 up{0.0f, 1.0f, 0.0f};

    SkyLighting lighting;
    lighting.sun_color = hadamard(kSunIrradiance, sun_transmittance(origin, sun_toward));
    lighting.ambient_zenith = atmosphere_radiance(origin, up, sun_toward);

    Vec3 horizon_sum{0.0f, 0.0f, 0.0f};
    constexpr i32 kHorizonSamples = 8;
    const f32 horizon_elevation = 4.0f * kDegToRad;
    for (i32 i = 0; i < kHorizonSamples; i++) {
        const f32 azimuth = (static_cast<f32>(i) / kHorizonSamples) * kTau;
        const Vec3 direction{std::cos(azimuth) * std::cos(horizon_elevation),
                             std::sin(horizon_elevation),
                             std::sin(azimuth) * std::cos(horizon_elevation)};
        horizon_sum += atmosphere_radiance(origin, normalize(direction), sun_toward);
    }
    lighting.ambient_horizon = horizon_sum * (1.0f / static_cast<f32>(kHorizonSamples));

    const f32 sun_elevation_cosine = f_max(sun_toward.y, 0.0f);
    lighting.ambient_ground =
        hadamard(kGroundAlbedo,
                 lighting.sun_color * (sun_elevation_cosine / kPi)
                     + (lighting.ambient_horizon + lighting.ambient_zenith) * 0.5f);

    const Vec3 cloud_origin{0.0f, kPlanetRadius + kCloudAltitude, 0.0f};
    lighting.cloud_sun_color = hadamard(kSunIrradiance,
                                       sun_transmittance(cloud_origin, sun_toward));

    return lighting;
}

Vec3 probe_sky_radiance(Vec3 sun_toward, f32 elevation_degrees, f32 azimuth_from_sun_degrees,
                        f32 camera_height_metres)
{
    const Vec3 origin = origin_for_height(camera_height_metres);

    Vec2 sun_bearing{0.0f, 1.0f};
    const Vec2 flat{sun_toward.x, sun_toward.z};
    if (length(flat) > 1e-5f) {
        sun_bearing = normalize(flat);
    }

    const f32 azimuth = azimuth_from_sun_degrees * kDegToRad;
    const f32 elevation = elevation_degrees * kDegToRad;
    const Vec2 rotated{sun_bearing.x * std::cos(azimuth) - sun_bearing.y * std::sin(azimuth),
                       sun_bearing.x * std::sin(azimuth) + sun_bearing.y * std::cos(azimuth)};
    const Vec3 direction = normalize(Vec3{rotated.x * std::cos(elevation),
                                          std::sin(elevation),
                                          rotated.y * std::cos(elevation)});
    return atmosphere_radiance(origin, direction, sun_toward);
}

Vec3 sun_direction_for_time(f32 time_of_day)
{
    const f32 angle = (time_of_day - 0.25f) * kTau;
    return normalize(Vec3{std::cos(angle) * 0.35f, std::sin(angle), -0.30f});
}

} // namespace anom
