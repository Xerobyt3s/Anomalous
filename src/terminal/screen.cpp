#include "terminal/screen.h"
#include "core/utf8.h"
#include "terminal/term_font.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace anom {

void Screen::reset()
{
    *this = Screen{};
    grid_clear();
}

void Screen::print(std::string_view text)
{
    for (const char c : text) {
        const u32 next = (pend_head_ + 1) % kTermPendingMax;
        if (next == pend_tail_) {
            return;
        }
        pending_[pend_head_] = c;
        pend_head_ = next;
    }
}

void Screen::printf(const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    print(buf);
}

void Screen::commit_line()
{
    std::memcpy(lines_[line_head_], out_line_, sizeof(out_line_));
    line_head_ = (line_head_ + 1) % kTermLines;
    if (line_count_ < kTermLines) {
        line_count_++;
    }
    out_line_[0] = 0;
    out_len_ = 0;
}

void Screen::reveal(f32 dt)
{
    reveal_accum_ += kTermCharsPerSecond * dt;
    while (reveal_accum_ >= 1.0f && pend_tail_ != pend_head_) {
        reveal_accum_ -= 1.0f;
        const char c = pending_[pend_tail_];
        pend_tail_ = (pend_tail_ + 1) % kTermPendingMax;

        if (c == '\n' || out_len_ >= kTermCols) {
            commit_line();
        }
        if (c != '\n') {
            out_line_[out_len_++] = c;
            out_line_[out_len_] = 0;
            if (c != ' ') {
                click_pending_ = true;
            }
        }
    }
}

void Screen::flush_pending()
{
    while (pend_tail_ != pend_head_) {
        reveal_accum_ = 1.0f;
        reveal(0.0f);
    }
    reveal_accum_ = 0.0f;
}

void Screen::grid_clear()
{
    for (u32 row = 0; row < kTermRows; row++) {
        for (u32 col = 0; col < kTermCols; col++) {
            glyphs_[row][col] = ' ';
            colors_[row][col] = TC_GREEN;
        }
    }
}

void Screen::grid_put(i32 row, i32 col, u16 glyph, u8 color)
{
    if (row < 0 || row >= static_cast<i32>(kTermRows) || col < 0
        || col >= static_cast<i32>(kTermCols)) {
        return;
    }
    glyphs_[row][col] = glyph;
    colors_[row][col] = color;
}

void Screen::grid_text(i32 row, i32 col, u8 color, const char* fmt, ...)
{
    char buf[kTermCols * 3 + 1];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    std::string_view cursor(buf);
    i32 c = col;
    while (c < static_cast<i32>(kTermCols)) {
        u32 cp = utf8_next(cursor);
        if (cp == 0) {
            break;
        }
        if (cp > 0xFFFDu) {
            cp = 0xFFFDu;
        }
        const bool wide = term_font_cp_wide(cp);
        grid_put(row, c, static_cast<u16>(cp), color);
        if (wide) {
            grid_put(row, c + 1, kTermFontWideCont, color);
        }
        c += wide ? 2 : 1;
    }
}

void Screen::grid_bar(i32 row, i32 col, i32 width, f32 frac, u8 color)
{
    grid_text(row, col, TC_DIM, "[");
    for (i32 i = 0; i < width; i++) {
        const bool fill = static_cast<f32>(i) / static_cast<f32>(width) < frac;
        grid_put(row, col + 1 + i, fill ? '#' : '.', fill ? color : TC_DIM);
    }
    grid_text(row, col + 1 + width, TC_DIM, "]");
}

void Screen::grid_title(std::string_view title)
{
    for (u32 c = 0; c < kTermCols; c++) {
        colors_[0][c] = TC_BG;
    }
    grid_text(0, 1, TC_BG, "%.*s", static_cast<int>(title.size()), title.data());
    grid_text(0, static_cast<i32>(kTermCols) - 8, TC_BG, "Q: BACK");
}

void Screen::grid_block(i32 row, i32 col, i32 max_rows, u8 color, std::string_view text,
                        i32 budget)
{
    i32 r = 0;
    i32 c = 0;
    for (const char ch : text) {
        if (r >= max_rows || row + r >= static_cast<i32>(kTermRows)) {
            return;
        }
        if (ch == '\n') {
            r++;
            c = 0;
            continue;
        }
        if (budget >= 0) {
            if (budget == 0) {
                return;
            }
            budget--;
        }
        grid_put(row + r, col + c, static_cast<u8>(ch), color);
        c++;
    }
}

std::string_view Screen::line(u32 index) const
{
    if (index >= line_count_) {
        return {};
    }
    const u32 slot = (line_head_ + kTermLines - line_count_ + index) % kTermLines;
    return lines_[slot];
}

void Screen::draw_scrollback(i32 first_row, i32 rows, u8 color)
{
    const i32 shown = rows - 1;
    const i32 start = static_cast<i32>(line_count_) - shown;
    for (i32 i = 0; i < shown; i++) {
        const i32 index = start + i;
        if (index < 0) {
            continue;
        }
        const std::string_view text = line(static_cast<u32>(index));
        grid_text(first_row + i, 0, color, "%.*s", static_cast<int>(text.size()), text.data());
    }
    grid_text(first_row + shown, 0, color, "%.*s", static_cast<int>(out_len_), out_line_);
}

void Screen::tick_blink(f32 dt)
{
    blink_ += dt;
    if (blink_ >= 1.0f) {
        blink_ -= 1.0f;
    }
}

} // namespace anom
