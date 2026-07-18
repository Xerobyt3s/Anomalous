#include "world/weather.h"
#include "math/vmath.h"

static const char* s_mode_names[WEATHER_MODE_COUNT] = {
    "AUTO", "CLEAR", "DRIZZLE", "RAIN",
};

const char* weather_mode_name(WeatherMode mode)
{
    return s_mode_names[mode];
}

void weather_init(Weather* w, u64 seed)
{
    Weather zero = {0};
    *w = zero;
    rng_seed(&w->rng, seed);
    w->mode = WEATHER_AUTO;
    w->next_shift = rng_range(&w->rng, 90.0f, 240.0f);
}

static void weather_pick_target(Weather* w)
{
    f32 roll = rng_f32(&w->rng);
    if (roll < 0.52f) {
        w->rain_target = 0.0f;
    } else if (roll < 0.74f) {
        w->rain_target = rng_range(&w->rng, 0.18f, 0.40f);
    } else {
        w->rain_target = rng_range(&w->rng, 0.55f, 1.0f);
    }
}

void weather_set_mode(Weather* w, WeatherMode mode)
{
    w->mode = mode;
    w->next_shift = rng_range(&w->rng, 120.0f, 300.0f);
}

void weather_tick(Weather* w, f32 dt)
{
    switch (w->mode) {
    case WEATHER_CLEAR:
        w->rain_target = 0.0f;
        break;
    case WEATHER_DRIZZLE:
        w->rain_target = 0.30f;
        break;
    case WEATHER_RAIN:
        w->rain_target = 0.85f;
        break;
    default:
        w->next_shift -= dt;
        if (w->next_shift <= 0.0f) {
            w->next_shift = rng_range(&w->rng, 150.0f, 420.0f);
            weather_pick_target(w);
        }
        break;
    }

    w->rain = f_approach_exp(w->rain, w->rain_target, 0.016f, dt);
    f32 overcast_target = f_clamp01(w->rain_target > 0.01f ? 0.55f + w->rain_target * 0.45f
                                                           : w->rain * 2.0f);
    w->overcast = f_approach_exp(w->overcast, overcast_target, 0.020f, dt);

    if (w->rain > w->wetness) {
        w->wetness = f_approach_exp(w->wetness, w->rain, 0.030f, dt);
    } else {
        w->wetness = f_max(w->wetness - 0.0035f * dt, 0.0f);
    }

    w->wind_phase += dt * 0.11f;
    if (w->wind_phase > 100.0f * PI32) {
        w->wind_phase -= 100.0f * PI32;
    }
    w->wind = 0.15f + w->rain * 0.45f + 0.12f * sinf(w->wind_phase)
            + 0.06f * sinf(w->wind_phase * 2.7f);
}
