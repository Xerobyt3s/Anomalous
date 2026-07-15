#pragma once

#include "core/types.h"

typedef struct Rng {
    u64 state;
} Rng;

static inline void rng_seed(Rng* rng, u64 seed)
{
    rng->state = seed * 0x9E3779B97F4A7C15ull + 1;
}

static inline u32 rng_next_u32(Rng* rng)
{
    u64 x = rng->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->state = x;
    return (u32)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

static inline f32 rng_f32(Rng* rng)
{
    return (f32)(rng_next_u32(rng) >> 8) * (1.0f / 16777216.0f);
}

static inline f32 rng_range(Rng* rng, f32 lo, f32 hi)
{
    return lo + (hi - lo) * rng_f32(rng);
}

static inline u32 rng_range_u32(Rng* rng, u32 lo, u32 hi)
{
    return lo + rng_next_u32(rng) % (hi - lo + 1);
}
