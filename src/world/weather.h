#pragma once

#include "core/types.h"
#include "core/rng.h"

typedef enum WeatherMode {
    WEATHER_AUTO,
    WEATHER_CLEAR,
    WEATHER_DRIZZLE,
    WEATHER_RAIN,
    WEATHER_MODE_COUNT,
} WeatherMode;

typedef struct Weather {
    Rng rng;
    WeatherMode mode;
    f32 rain;
    f32 rain_target;
    f32 overcast;
    f32 wetness;
    f32 wind;
    f32 wind_phase;
    f32 next_shift;
} Weather;

void weather_init(Weather* w, u64 seed);
void weather_tick(Weather* w, f32 dt);
void weather_set_mode(Weather* w, WeatherMode mode);
const char* weather_mode_name(WeatherMode mode);
