#pragma once

#include "terminal/program.h"

namespace anom {

class BootProgram : public Program {
public:
    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;

    bool done() const { return timer_ >= kBootTime; }

private:
    static constexpr f32 kBootTime = 4.8f;

    f32 timer_ = 0.0f;
};

class ShellProgram : public Program {
public:
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;
    void key_char(const ProgramContext& ctx, const TermView& view, char c) override;
};

} // namespace anom
