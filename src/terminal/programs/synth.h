#pragma once

#include "carsys/carsys.h"
#include "terminal/program.h"

namespace anom {

enum class SynthVerdict : u8 {
    Empty,
    Ready,
    NoPropellant,
    TwoPropellants,
    Unstable,
};

struct SynthPreview {
    SynthVerdict verdict = SynthVerdict::Empty;
    i32 element = -1;
    u32 affordable = 0;
};

SynthPreview synth_preview(const ghost::game::AmmoData& ammo, const u16* tank, const u8* doses, u32 dose_count);

class SynthProgram : public Program {
public:
    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;
    void key_char(const ProgramContext& ctx, const TermView& view, char c) override;

private:
    u8 doses_[kSynthDoses] = {};
    u32 dose_count_ = 0;
    u32 count_ = 6;
};

}
