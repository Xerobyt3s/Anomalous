#pragma once

#include "core/rng.h"
#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

u32 noise_hash(i32 x, i32 y, i32 z, u32 seed);
f32 value_noise2(f32 x, f32 z, u32 seed);
f32 value_noise3(Vec3 p, u32 seed);
f32 noise_fbm2(f32 x, f32 z, u32 seed, u32 octaves);
f32 noise_fbm3(Vec3 p, u32 seed, u32 octaves);
f32 noise_ridged3(Vec3 p, u32 seed, u32 octaves);
Vec3 noise_warp3(Vec3 p, u32 seed, f32 amount);

f32 smooth_min(f32 a, f32 b, f32 k);
f32 smooth_max(f32 a, f32 b, f32 k);
f32 smoothstep01(f32 lo, f32 hi, f32 x);

Vec3 rng_unit_vector(Rng& rng);
u32 seed_from_name(std::string_view name, u32 salt);

} // namespace anom
