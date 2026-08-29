#pragma once

#include "core/types.h"

namespace anom {

class Rng {
public:
    Rng() = default;
    explicit Rng(u64 seed) { this->seed(seed); }

    void seed(u64 value) { state_ = value * 0x9E3779B97F4A7C15ull + 1; }

    u32 next_u32()
    {
        u64 x = state_;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state_ = x;
        return static_cast<u32>((x * 0x2545F4914F6CDD1Dull) >> 32);
    }

    f32 next_f32() { return static_cast<f32>(next_u32() >> 8) * (1.0f / 16777216.0f); }

    f32 range(f32 lo, f32 hi) { return lo + (hi - lo) * next_f32(); }

    u32 range_u32(u32 lo, u32 hi) { return lo + next_u32() % (hi - lo + 1); }

    u64 state() const { return state_; }

private:
    u64 state_ = 0;
};

} // namespace anom
