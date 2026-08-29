#pragma once

#include "terminal/program.h"

namespace anom {

class LinkProgram : public Program {
public:
    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    void key_char(const ProgramContext& ctx, const TermView& view, char c) override;

    i32 negotiating() const { return anim_port_; }
    bool denied() const { return deny_ > 0.0f; }

private:
    void draw_port(Screen& s, i32 row, const char* label, i32 state, const char* linked,
                   f32 blink) const;

    i32 anim_port_ = -1;
    f32 anim_t_ = 0.0f;
    f32 deny_ = 0.0f;
};

class DevProgram : public Program {
public:
    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    void key_char(const ProgramContext& ctx, const TermView& view, char c) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;

private:
    void request_time(const ProgramContext& ctx, f32 tod);

    f32 tod_ = 0.0f;
    bool warp_ = false;
    f32 rain_ = 0.0f;
    f32 wet_ = 0.0f;
    i32 wmode_ = 0;
};

} // namespace anom
