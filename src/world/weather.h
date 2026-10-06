#pragma once

#include "core/rng.h"
#include "core/types.h"
#include "math/vmath.h"

namespace anom {

enum class WeatherMode : u32 {
    Auto,
    Clear,
    Drizzle,
    Rain,
    Snow,
    Count,
};

inline constexpr f32 kWindSpeed = 7.0f;
inline constexpr Vec3 kWindDir{0.86f, 0.0f, 0.51f};

const char* weather_mode_name(WeatherMode mode);

class Weather {
public:
    void init(u64 seed);
    void tick(f32 dt);
    void set_mode(WeatherMode mode);

    WeatherMode mode() const { return mode_; }
    f32 rain() const { return rain_; }
    f32 rain_target() const { return rain_target_; }
    f32 overcast() const { return overcast_; }
    f32 wetness() const { return wetness_; }
    f32 wind() const { return wind_; }
    Vec3 wind_velocity() const;
    f32 snow() const { return snow_; }
    f32 snow_cover() const { return snow_cover_; }

private:
    void pick_target();

    Rng rng_;
    WeatherMode mode_ = WeatherMode::Auto;
    f32 rain_ = 0.0f;
    f32 rain_target_ = 0.0f;
    f32 overcast_ = 0.0f;
    f32 wetness_ = 0.0f;
    f32 wind_ = 0.0f;
    f32 snow_ = 0.0f;
    f32 snow_target_ = 0.0f;
    f32 snow_cover_ = 0.0f;
    f32 wind_phase_ = 0.0f;
    f32 next_shift_ = 0.0f;
};

} // namespace anom
