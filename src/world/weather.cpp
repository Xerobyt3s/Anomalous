#include "world/weather.h"
#include "math/vmath.h"

namespace anom {
namespace {

constexpr f32 kSnowFall = 0.8f;
constexpr f32 kSnowSettle = 0.05f;
constexpr f32 kSnowBuildSeconds = 75.0f;
constexpr f32 kSnowMeltSeconds = 240.0f;
constexpr f32 kSnowRainMelt = 3.0f;
constexpr f32 kSnowWind = 0.35f;

} // namespace

const char* weather_mode_name(WeatherMode mode)
{
    switch (mode) {
    case WeatherMode::Auto: return "AUTO";
    case WeatherMode::Clear: return "CLEAR";
    case WeatherMode::Drizzle: return "DRIZZLE";
    case WeatherMode::Rain: return "RAIN";
    case WeatherMode::Snow: return "SNOW";
    case WeatherMode::Count: break;
    }
    return "?";
}

void Weather::init(u64 seed)
{
    *this = Weather{};
    rng_.seed(seed);
    mode_ = WeatherMode::Auto;
    next_shift_ = rng_.range(90.0f, 240.0f);
}

void Weather::pick_target()
{
    const f32 roll = rng_.next_f32();
    if (roll < 0.52f) {
        rain_target_ = 0.0f;
    } else if (roll < 0.74f) {
        rain_target_ = rng_.range(0.18f, 0.40f);
    } else {
        rain_target_ = rng_.range(0.55f, 1.0f);
    }
}

void Weather::set_mode(WeatherMode mode)
{
    mode_ = mode;
    next_shift_ = rng_.range(120.0f, 300.0f);
}

void Weather::tick(f32 dt)
{
    snow_target_ = 0.0f;
    switch (mode_) {
    case WeatherMode::Clear:
        rain_target_ = 0.0f;
        break;
    case WeatherMode::Drizzle:
        rain_target_ = 0.30f;
        break;
    case WeatherMode::Rain:
        rain_target_ = 0.85f;
        break;
    case WeatherMode::Snow:
        rain_target_ = 0.0f;
        snow_target_ = kSnowFall;
        break;
    default:
        next_shift_ -= dt;
        if (next_shift_ <= 0.0f) {
            next_shift_ = rng_.range(150.0f, 420.0f);
            pick_target();
        }
        break;
    }

    rain_ = f_approach_exp(rain_, rain_target_, 0.016f, dt);
    snow_ = f_approach_exp(snow_, snow_target_, 0.016f, dt);
    if (snow_ > kSnowSettle) {
        snow_cover_ = f_min(snow_cover_ + snow_ * dt / kSnowBuildSeconds, 1.0f);
    } else {
        const f32 melt = (1.0f + kSnowRainMelt * rain_) * dt / kSnowMeltSeconds;
        snow_cover_ = f_max(snow_cover_ - melt, 0.0f);
    }
    const f32 rain_overcast = rain_target_ > 0.01f ? 0.55f + rain_target_ * 0.45f : rain_ * 2.0f;
    const f32 snow_overcast = snow_target_ > 0.01f ? 0.75f + snow_target_ * 0.25f : snow_ * 2.0f;
    const f32 overcast_target = f_clamp01(f_max(rain_overcast, snow_overcast));
    overcast_ = f_approach_exp(overcast_, overcast_target, 0.020f, dt);

    if (rain_ > wetness_) {
        wetness_ = f_approach_exp(wetness_, rain_, 0.030f, dt);
    } else {
        wetness_ = f_max(wetness_ - 0.0035f * dt, 0.0f);
    }

    wind_phase_ += dt * 0.11f;
    if (wind_phase_ > 100.0f * kPi) {
        wind_phase_ -= 100.0f * kPi;
    }
    wind_ = 0.15f + rain_ * 0.45f + snow_ * kSnowWind + 0.12f * std::sin(wind_phase_)
          + 0.06f * std::sin(wind_phase_ * 2.7f);
}

} // namespace anom
