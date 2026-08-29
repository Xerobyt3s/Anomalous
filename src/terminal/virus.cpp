#include "terminal/virus.h"
#include "math/vmath.h"
#include "terminal/fs.h"
#include "terminal/screen.h"

#include <cmath>

namespace anom {
namespace {

constexpr const char* kTaunts[3] = {
    "?SYN ?SYN ?SYN CARRIER LOST\n",
    "I CAN SEE THE ROAD FROM HERE\n",
    "SECTOR 0 SECTOR 0 SECTOR 0\n",
};

} // namespace

f32 Virus::next_rand()
{
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<f32>(rng_ >> 8) / 16777216.0f;
}

void Virus::infect()
{
    if (active_) {
        return;
    }
    active_ = true;
    rng_ = 0xBADC0DEu;
    burst_ = 0.45f;
    next_beep_ = 5.0f;
    next_corrupt_ = 70.0f;
}

void Virus::cure()
{
    *this = Virus{};
}

f32 Virus::lie(f32 value, f32 blink, f32 salt) const
{
    if (!active_) {
        return value;
    }
    const f32 w = std::sin(blink * (1.7f + salt * 0.9f) + salt * 13.7f);
    return value * (0.25f + 1.3f * f_abs(w));
}

void Virus::corrupt_file(Screen& screen, Fs& fs, bool in_shell)
{
    FsDrive& drive = fs.drive(kFsDriveA);
    i32 candidates[kFsDriveNodes];
    i32 count = 0;
    for (i32 i = 1; i < kFsDriveNodes; i++) {
        const FsNode& node = drive.nodes[i];
        if (node.used && !node.is_dir && !node.corrupted) {
            candidates[count++] = i;
        }
    }
    if (count == 0) {
        return;
    }

    FsNode& victim = drive.nodes[candidates[static_cast<i32>(next_rand()
                                                            * static_cast<f32>(count))
                                            % count]];
    victim.corrupted = true;
    burst_ = 0.6f + next_rand() * 0.6f;
    screen.click();
    if (in_shell) {
        screen.printf("WRITE FAULT ON DRIVE A: -- %s\n", victim.name.c_str());
        if (next_rand() < 0.35f) {
            screen.print(kTaunts[static_cast<u32>(next_rand() * 2.999f)]);
        }
    }
}

void Virus::tick(Screen& screen, Fs& fs, f32 dt, bool visible, bool in_shell)
{
    if (!active_) {
        return;
    }

    if (burst_ > 0.0f) {
        burst_ -= dt;
        if (visible) {
            const i32 n = 8 + static_cast<i32>(next_rand() * 30.0f);
            for (i32 i = 0; i < n; i++) {
                const i32 r = static_cast<i32>(next_rand() * static_cast<f32>(kTermRows))
                            % static_cast<i32>(kTermRows);
                const i32 c = static_cast<i32>(next_rand() * static_cast<f32>(kTermCols))
                            % static_cast<i32>(kTermCols);
                const u16 glyph = static_cast<u16>(33 + static_cast<i32>(next_rand() * 91.0f));
                screen.grid_put(r, c, glyph, next_rand() < 0.3f ? TC_RED : TC_BRIGHT);
            }
        }
    }

    next_beep_ -= dt;
    if (next_beep_ <= 0.0f) {
        next_beep_ = 1.5f + next_rand() * 6.0f;
        screen.click();
    }

    next_corrupt_ -= dt;
    if (next_corrupt_ <= 0.0f) {
        next_corrupt_ = 60.0f + next_rand() * 60.0f;
        corrupt_file(screen, fs, in_shell);
    }
}

} // namespace anom
