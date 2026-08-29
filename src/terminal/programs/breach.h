#pragma once

#include "terminal/program.h"

namespace anom {

inline constexpr i32 kBreachGrid = 5;
inline constexpr u32 kBreachBuf = 7;
inline constexpr u32 kBreachTarget = 3;
inline constexpr f32 kBreachTime = 30.0f;

class BreachProgram : public Program {
public:
    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;

    i32 result() const { return result_; }
    u32 buffer_len() const { return buf_len_; }
    void seed(u32 value) { seed_ = value; }

private:
    u32 next_rand();
    void cell(i32 slot, i32& out_r, i32& out_c) const;
    bool line_unused(i32& out_first) const;
    bool target_hit() const;
    void move(i32 dir);
    void pick(const ProgramContext& ctx);

    u8 grid_[kBreachGrid][kBreachGrid] = {};
    bool used_[kBreachGrid][kBreachGrid] = {};
    u8 buffer_[kBreachBuf] = {};
    u8 target_[kBreachTarget] = {};
    u32 buf_len_ = 0;
    i32 axis_ = 0;
    i32 line_ = 0;
    i32 cursor_ = 0;
    f32 timer_ = 0.0f;
    i32 result_ = 0;
    f32 result_time_ = 0.0f;
    u32 rng_ = 0;
    u32 seed_ = 0x1337C0DEu;
};

} // namespace anom
