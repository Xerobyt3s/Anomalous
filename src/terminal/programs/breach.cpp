#include "terminal/programs/breach.h"
#include "terminal/screen.h"

#include <cmath>

namespace anom {
namespace {

constexpr u8 kBreachBytes[6] = {0x1C, 0x55, 0xBD, 0xE9, 0x7A, 0xFF};

void hex_byte(u8 v, char* out)
{
    static const char* kHex = "0123456789ABCDEF";
    out[0] = kHex[(v >> 4) & 0xF];
    out[1] = kHex[v & 0xF];
    out[2] = 0;
}

} // namespace

u32 BreachProgram::next_rand()
{
    rng_ = rng_ * 1664525u + 1013904223u;
    return rng_ >> 8;
}

void BreachProgram::cell(i32 slot, i32& out_r, i32& out_c) const
{
    if (axis_ == 0) {
        out_r = line_;
        out_c = slot;
    } else {
        out_r = slot;
        out_c = line_;
    }
}

bool BreachProgram::line_unused(i32& out_first) const
{
    bool any = false;
    for (i32 s = 0; s < kBreachGrid; s++) {
        i32 r = 0;
        i32 c = 0;
        cell(s, r, c);
        if (!used_[r][c]) {
            if (!any) {
                out_first = s;
            }
            any = true;
        }
    }
    return any;
}

bool BreachProgram::target_hit() const
{
    if (buf_len_ < kBreachTarget) {
        return false;
    }
    for (u32 start = 0; start + kBreachTarget <= buf_len_; start++) {
        bool ok = true;
        for (u32 k = 0; k < kBreachTarget; k++) {
            if (buffer_[start + k] != target_[k]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

void BreachProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;

    for (i32 r = 0; r < kBreachGrid; r++) {
        for (i32 c = 0; c < kBreachGrid; c++) {
            used_[r][c] = false;
        }
    }
    buf_len_ = 0;

    seed_ = seed_ * 2654435761u + 40503u;
    rng_ = seed_;
    for (i32 r = 0; r < kBreachGrid; r++) {
        for (i32 c = 0; c < kBreachGrid; c++) {
            grid_[r][c] = kBreachBytes[next_rand() % 6];
        }
    }

    bool sim_used[kBreachGrid][kBreachGrid] = {};
    i32 axis = 0;
    i32 line = 0;
    for (u32 t = 0; t < kBreachTarget; t++) {
        i32 choices[kBreachGrid];
        i32 n = 0;
        for (i32 s = 0; s < kBreachGrid; s++) {
            const i32 r = axis == 0 ? line : s;
            const i32 c = axis == 0 ? s : line;
            if (!sim_used[r][c]) {
                choices[n++] = s;
            }
        }
        if (n == 0) {
            target_[t] = kBreachBytes[next_rand() % 6];
            continue;
        }
        const i32 pick_slot = choices[next_rand() % static_cast<u32>(n)];
        const i32 r = axis == 0 ? line : pick_slot;
        const i32 c = axis == 0 ? pick_slot : line;
        target_[t] = grid_[r][c];
        sim_used[r][c] = true;
        if (axis == 0) {
            axis = 1;
            line = c;
        } else {
            axis = 0;
            line = r;
        }
    }

    axis_ = 0;
    line_ = 0;
    cursor_ = 0;
    timer_ = kBreachTime;
    result_ = 0;
    result_time_ = 0.0f;
}

void BreachProgram::move(i32 dir)
{
    if (result_ != 0) {
        return;
    }
    cursor_ += dir;
    cursor_ = cursor_ < 0 ? 0 : (cursor_ >= kBreachGrid ? kBreachGrid - 1 : cursor_);
}

void BreachProgram::pick(const ProgramContext& ctx)
{
    if (result_ != 0) {
        return;
    }
    i32 r = 0;
    i32 c = 0;
    cell(cursor_, r, c);
    if (used_[r][c]) {
        return;
    }
    if (buf_len_ < kBreachBuf) {
        buffer_[buf_len_++] = grid_[r][c];
    }
    used_[r][c] = true;

    if (target_hit()) {
        result_ = 1;
        result_time_ = 0.0f;
        ctx.request->breach_open = true;
        return;
    }

    if (axis_ == 0) {
        axis_ = 1;
        line_ = c;
    } else {
        axis_ = 0;
        line_ = r;
    }

    i32 first = 0;
    const bool any = line_unused(first);
    cursor_ = first;
    if (!any || buf_len_ >= kBreachBuf) {
        result_ = -1;
        result_time_ = 0.0f;
    }
}

void BreachProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    (void)view;
    if (result_ == 0) {
        timer_ -= dt;
        if (timer_ <= 0.0f) {
            timer_ = 0.0f;
            result_ = -1;
            result_time_ = 0.0f;
        }
    } else {
        result_time_ += dt;
    }

    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("ICE BREAK -- RELAY R-4");

    char hex[3];
    s.grid_text(2, 2, TC_BRIGHT, "REQUIRED SEQUENCE");
    for (u32 i = 0; i < kBreachTarget; i++) {
        hex_byte(target_[i], hex);
        s.grid_text(2, 22 + static_cast<i32>(i) * 4, TC_AMBER, "%s", hex);
    }

    s.grid_text(3, 2, TC_DIM, "BUFFER");
    for (u32 i = 0; i < kBreachBuf; i++) {
        if (i < buf_len_) {
            hex_byte(buffer_[i], hex);
            s.grid_text(3, 22 + static_cast<i32>(i) * 4, TC_GREEN, "%s", hex);
        } else {
            s.grid_text(3, 22 + static_cast<i32>(i) * 4, TC_DIM, "--");
        }
    }

    for (i32 r = 0; r < kBreachGrid; r++) {
        for (i32 c = 0; c < kBreachGrid; c++) {
            const bool on_line = axis_ == 0 ? r == line_ : c == line_;
            const bool is_cursor = axis_ == 0 ? (r == line_ && c == cursor_)
                                              : (c == line_ && r == cursor_);
            const i32 row = 6 + r * 2;
            const i32 col = 20 + c * 5;
            if (used_[r][c]) {
                s.grid_text(row, col, TC_DIM, "##");
                continue;
            }
            hex_byte(grid_[r][c], hex);
            u8 color = on_line ? TC_GREEN : TC_DIM;
            if (is_cursor && result_ == 0) {
                if (std::fmod(ctx.blink, 0.5f) < 0.28f) {
                    s.grid_text(row, col - 1, TC_BRIGHT, "[%s]", hex);
                    continue;
                }
                color = TC_BRIGHT;
            }
            s.grid_text(row, col, color, "%s", hex);
        }
    }

    const i32 last_row = static_cast<i32>(kTermRows) - 1;
    if (result_ == 0) {
        const i32 bar = static_cast<i32>(timer_ / kBreachTime * 30.0f);
        const u8 tcol = timer_ < 8.0f ? TC_RED : TC_AMBER;
        s.grid_text(5, 2, tcol, "TRACE");
        for (i32 i = 0; i < 30; i++) {
            s.grid_put(5, 10 + i, i < bar ? '=' : '.', i < bar ? tcol : TC_DIM);
        }
        s.grid_text(last_row, 1, TC_DIM,
                    axis_ == 0 ? "< > MOVE   ENTER SELECT   Q ABORT"
                               : "^ v MOVE   ENTER SELECT   Q ABORT");
    } else if (result_ == 1) {
        if (std::fmod(ctx.blink, 0.6f) < 0.4f) {
            s.grid_text(last_row - 3, static_cast<i32>(kTermCols) / 2 - 8, TC_BRIGHT,
                        "ACCESS GRANTED");
        }
        s.grid_text(last_row, 1, TC_DIM, "LOCK DISENGAGED   ENTER / Q TO EXIT");
    } else {
        s.grid_text(last_row - 3, static_cast<i32>(kTermCols) / 2 - 12, TC_RED,
                    "TRACE DETECTED -- LOCKED OUT");
        s.grid_text(last_row, 1, TC_DIM, "RUN BREACH TO RETRY   ENTER / Q TO EXIT");
    }
}

bool BreachProgram::key(const ProgramContext& ctx, const TermView& view, TermKey key)
{
    (void)view;
    if (result_ != 0) {
        if (key != TermKey::Enter && key != TermKey::Quit && key != TermKey::Escape) {
            return false;
        }
        if (result_ == 1 && key == TermKey::Enter) {
            ctx.screen->print("LOCK DISENGAGED. PORT OPEN. RUN LINK.\n");
        }
        return true;
    }
    switch (key) {
    case TermKey::Left:
        if (axis_ == 0) {
            move(-1);
        }
        break;
    case TermKey::Right:
        if (axis_ == 0) {
            move(1);
        }
        break;
    case TermKey::Up:
        if (axis_ == 1) {
            move(-1);
        }
        break;
    case TermKey::Down:
        if (axis_ == 1) {
            move(1);
        }
        break;
    case TermKey::Enter:
        pick(ctx);
        break;
    case TermKey::Quit:
    case TermKey::Escape:
        return true;
    default:
        break;
    }
    return false;
}

} // namespace anom
