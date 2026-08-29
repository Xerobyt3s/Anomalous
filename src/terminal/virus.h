#pragma once

#include "core/types.h"

namespace anom {

class Fs;
class Screen;

class Virus {
public:
    void infect();
    void cure();
    bool active() const { return active_; }
    bool bursting() const { return burst_ > 0.0f; }

    f32 lie(f32 value, f32 blink, f32 salt) const;
    void tick(Screen& screen, Fs& fs, f32 dt, bool visible, bool in_shell);

private:
    f32 next_rand();
    void corrupt_file(Screen& screen, Fs& fs, bool in_shell);

    bool active_ = false;
    u32 rng_ = 0;
    f32 burst_ = 0.0f;
    f32 next_beep_ = 0.0f;
    f32 next_corrupt_ = 0.0f;
};

} // namespace anom
