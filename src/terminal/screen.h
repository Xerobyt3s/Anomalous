#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

inline constexpr u32 kTermCols = 70;
inline constexpr u32 kTermRows = 24;
inline constexpr u32 kTermCellW = 8;
inline constexpr u32 kTermCellH = 14;
inline constexpr u32 kTermTexW = 640;
inline constexpr u32 kTermTexH = 400;
inline constexpr u32 kTermOriginX = 40;
inline constexpr u32 kTermOriginY = 32;
inline constexpr u32 kTermLines = 32;
inline constexpr u32 kTermPendingMax = 4096;
inline constexpr f32 kTermCharsPerSecond = 620.0f;

enum TermColor : u8 {
    TC_BG = 0,
    TC_GREEN,
    TC_BRIGHT,
    TC_AMBER,
    TC_RED,
    TC_DIM,
};

class Screen {
public:
    void reset();

    void print(std::string_view text);
    void printf(const char* fmt, ...);
    bool idle() const { return pend_head_ == pend_tail_; }
    void flush_pending();
    void reveal(f32 dt);

    void grid_clear();
    void grid_text(i32 row, i32 col, u8 color, const char* fmt, ...);
    void grid_put(i32 row, i32 col, u16 glyph, u8 color);
    void grid_bar(i32 row, i32 col, i32 width, f32 frac, u8 color);
    void grid_title(std::string_view title);
    void grid_block(i32 row, i32 col, i32 max_rows, u8 color, std::string_view text,
                    i32 budget);

    void draw_scrollback(i32 first_row, i32 rows, u8 color);

    u16 glyph(u32 row, u32 col) const { return glyphs_[row][col]; }
    u8 color(u32 row, u32 col) const { return colors_[row][col]; }

    std::string_view line(u32 index) const;
    u32 line_count() const { return line_count_; }
    std::string_view out_line() const { return {out_line_, out_len_}; }

    void click() { click_pending_ = true; }
    bool take_click() { const bool c = click_pending_; click_pending_ = false; return c; }
    void tick_blink(f32 dt);
    bool blink_on() const { return blink_ < 0.5f; }

private:
    void commit_line();

    u16 glyphs_[kTermRows][kTermCols] = {};
    u8 colors_[kTermRows][kTermCols] = {};

    char lines_[kTermLines][kTermCols + 1] = {};
    u32 line_head_ = 0;
    u32 line_count_ = 0;

    char out_line_[kTermCols + 1] = {};
    u32 out_len_ = 0;

    char pending_[kTermPendingMax] = {};
    u32 pend_head_ = 0;
    u32 pend_tail_ = 0;
    f32 reveal_accum_ = 0.0f;

    f32 blink_ = 0.0f;
    bool click_pending_ = false;
};

} // namespace anom
