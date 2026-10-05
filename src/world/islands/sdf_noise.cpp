#include "world/islands/sdf_noise.h"

#include <cmath>

namespace anom {
namespace {

f32 fade(f32 t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

f32 lattice(i32 x, i32 y, i32 z, u32 seed)
{
    return static_cast<f32>(noise_hash(x, y, z, seed) >> 8) * (1.0f / 16777216.0f);
}

} // namespace

u32 noise_hash(i32 x, i32 y, i32 z, u32 seed)
{
    u32 h = seed * 0x27D4EB2Du;
    h ^= static_cast<u32>(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<u32>(y) * 0xC2B2AE35u;
    h = (h << 17) | (h >> 15);
    h ^= static_cast<u32>(z) * 0x165667B1u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

f32 value_noise2(f32 x, f32 z, u32 seed)
{
    const f32 fx = std::floor(x);
    const f32 fz = std::floor(z);
    const i32 ix = static_cast<i32>(fx);
    const i32 iz = static_cast<i32>(fz);
    const f32 u = fade(x - fx);
    const f32 v = fade(z - fz);
    const f32 a = lattice(ix, 0, iz, seed);
    const f32 b = lattice(ix + 1, 0, iz, seed);
    const f32 c = lattice(ix, 0, iz + 1, seed);
    const f32 d = lattice(ix + 1, 0, iz + 1, seed);
    return f_lerp(f_lerp(a, b, u), f_lerp(c, d, u), v);
}

f32 value_noise3(Vec3 p, u32 seed)
{
    const f32 fx = std::floor(p.x);
    const f32 fy = std::floor(p.y);
    const f32 fz = std::floor(p.z);
    const i32 ix = static_cast<i32>(fx);
    const i32 iy = static_cast<i32>(fy);
    const i32 iz = static_cast<i32>(fz);
    const f32 u = fade(p.x - fx);
    const f32 v = fade(p.y - fy);
    const f32 w = fade(p.z - fz);
    const f32 c000 = lattice(ix, iy, iz, seed);
    const f32 c100 = lattice(ix + 1, iy, iz, seed);
    const f32 c010 = lattice(ix, iy + 1, iz, seed);
    const f32 c110 = lattice(ix + 1, iy + 1, iz, seed);
    const f32 c001 = lattice(ix, iy, iz + 1, seed);
    const f32 c101 = lattice(ix + 1, iy, iz + 1, seed);
    const f32 c011 = lattice(ix, iy + 1, iz + 1, seed);
    const f32 c111 = lattice(ix + 1, iy + 1, iz + 1, seed);
    const f32 x00 = f_lerp(c000, c100, u);
    const f32 x10 = f_lerp(c010, c110, u);
    const f32 x01 = f_lerp(c001, c101, u);
    const f32 x11 = f_lerp(c011, c111, u);
    return f_lerp(f_lerp(x00, x10, v), f_lerp(x01, x11, v), w);
}

f32 noise_fbm2(f32 x, f32 z, u32 seed, u32 octaves)
{
    f32 total = 0.0f;
    f32 amp = 1.0f;
    f32 norm = 0.0f;
    for (u32 o = 0; o < octaves; o++) {
        total += amp * value_noise2(x, z, seed + o * 1013u);
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        z *= 2.03f;
    }
    return total / norm;
}

f32 noise_fbm3(Vec3 p, u32 seed, u32 octaves)
{
    f32 total = 0.0f;
    f32 amp = 1.0f;
    f32 norm = 0.0f;
    for (u32 o = 0; o < octaves; o++) {
        total += amp * value_noise3(p, seed + o * 1013u);
        norm += amp;
        amp *= 0.5f;
        p = p * 2.03f;
    }
    return total / norm;
}

f32 noise_ridged3(Vec3 p, u32 seed, u32 octaves)
{
    f32 total = 0.0f;
    f32 amp = 1.0f;
    f32 norm = 0.0f;
    f32 weight = 1.0f;
    for (u32 o = 0; o < octaves; o++) {
        f32 n = 1.0f - f_abs(value_noise3(p, seed + o * 2029u) * 2.0f - 1.0f);
        n *= n * weight;
        weight = f_clamp01(n * 1.6f);
        total += amp * n;
        norm += amp;
        amp *= 0.5f;
        p = p * 2.07f;
    }
    return total / norm;
}

Vec3 noise_warp3(Vec3 p, u32 seed, f32 amount)
{
    const Vec3 offset{value_noise3(p, seed) - 0.5f, value_noise3(p, seed + 71u) - 0.5f,
                      value_noise3(p, seed + 151u) - 0.5f};
    return p + offset * (2.0f * amount);
}

f32 smooth_min(f32 a, f32 b, f32 k)
{
    const f32 h = f_clamp01(0.5f + 0.5f * (b - a) / k);
    return f_lerp(b, a, h) - k * h * (1.0f - h);
}

f32 smooth_max(f32 a, f32 b, f32 k)
{
    return -smooth_min(-a, -b, k);
}

f32 smoothstep01(f32 lo, f32 hi, f32 x)
{
    const f32 t = f_clamp01((x - lo) / (hi - lo));
    return t * t * (3.0f - 2.0f * t);
}

Vec3 rng_unit_vector(Rng& rng)
{
    const f32 z = rng.range(-1.0f, 1.0f);
    const f32 a = rng.range(0.0f, kTau);
    const f32 r = std::sqrt(f_max(1.0f - z * z, 0.0f));
    return Vec3{r * std::cos(a), z, r * std::sin(a)};
}

u32 seed_from_name(std::string_view name, u32 salt)
{
    u32 h = 2166136261u ^ salt;
    for (char c : name) {
        h ^= static_cast<u8>(c);
        h *= 16777619u;
    }
    return h;
}

} // namespace anom
