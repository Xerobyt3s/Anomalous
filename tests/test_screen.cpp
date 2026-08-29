#include "test.h"

#include "terminal/screen.h"
#include "terminal/term_font.h"

using namespace anom;

namespace {

Screen fresh()
{
    Screen s;
    s.reset();
    return s;
}

std::string_view row_text(const Screen& s, u32 row, char* buf, u32 cols)
{
    u32 n = 0;
    for (u32 c = 0; c < cols; c++) {
        buf[n++] = static_cast<char>(s.glyph(row, c));
    }
    while (n > 0 && buf[n - 1] == ' ') {
        n--;
    }
    buf[n] = 0;
    return {buf, n};
}

} // namespace

TEST(screen, a_fresh_screen_is_blank_green)
{
    Screen s = fresh();
    CHECK(s.idle());
    CHECK(s.line_count() == 0);
    CHECK(s.out_line().empty());
    for (u32 c = 0; c < kTermCols; c++) {
        CHECK(s.glyph(0, c) == ' ');
        CHECK(s.color(0, c) == TC_GREEN);
    }
}

TEST(screen, printed_text_is_revealed_over_time)
{
    Screen s = fresh();
    s.print("HELLO");
    CHECK(!s.idle());
    CHECK(s.out_line().empty());

    s.reveal(2.0f / kTermCharsPerSecond);
    CHECK(s.out_line() == "HE");
    CHECK(!s.idle());

    s.reveal(10.0f / kTermCharsPerSecond);
    CHECK(s.out_line() == "HELLO");
    CHECK(s.idle());
}

TEST(screen, a_newline_commits_the_line)
{
    Screen s = fresh();
    s.print("ONE\nTWO\n");
    s.flush_pending();

    CHECK(s.line_count() == 2);
    CHECK(s.line(0) == "ONE");
    CHECK(s.line(1) == "TWO");
    CHECK(s.out_line().empty());
}

TEST(screen, a_long_line_wraps_at_the_column_limit)
{
    Screen s = fresh();
    for (u32 i = 0; i < kTermCols + 5; i++) {
        s.print("X");
    }
    s.flush_pending();

    CHECK(s.line_count() == 1);
    CHECK(s.line(0).size() == kTermCols);
    CHECK(s.out_line().size() == 5);
}

TEST(screen, scrollback_drops_its_oldest_lines)
{
    Screen s = fresh();
    for (u32 i = 0; i < kTermLines + 4; i++) {
        s.printf("LINE%u\n", i);
    }
    s.flush_pending();

    CHECK(s.line_count() == kTermLines);
    CHECK(s.line(0) == "LINE4");
    CHECK(s.line(kTermLines - 1) == "LINE35");
}

TEST(screen, the_pending_queue_refuses_to_overflow)
{
    Screen s = fresh();
    for (u32 i = 0; i < kTermPendingMax * 2; i++) {
        s.print("A");
    }
    CHECK(!s.idle());

    s.flush_pending();
    CHECK(s.idle());
    CHECK(s.line_count() > 0);
}

TEST(screen, revealing_a_visible_character_arms_the_key_click)
{
    Screen s = fresh();
    CHECK(!s.take_click());

    s.print(" ");
    s.flush_pending();
    CHECK(!s.take_click());

    s.print("A");
    s.flush_pending();
    CHECK(s.take_click());
    CHECK(!s.take_click());
}

TEST(screen, grid_text_writes_cells_and_clips)
{
    Screen s = fresh();
    s.grid_text(2, 3, TC_AMBER, "AB");

    CHECK(s.glyph(2, 3) == 'A');
    CHECK(s.glyph(2, 4) == 'B');
    CHECK(s.color(2, 3) == TC_AMBER);
    CHECK(s.glyph(2, 5) == ' ');

    s.grid_text(2, static_cast<i32>(kTermCols) - 1, TC_RED, "XY");
    CHECK(s.glyph(2, kTermCols - 1) == 'X');

    s.grid_text(-4, 0, TC_RED, "OFF");
    s.grid_text(static_cast<i32>(kTermRows) + 2, 0, TC_RED, "OFF");
}

TEST(screen, a_wide_glyph_claims_a_continuation_cell)
{
    Screen s = fresh();
    CHECK(term_font_cp_wide(0x4E00));
    CHECK(!term_font_cp_wide('A'));

    s.grid_text(1, 0, TC_GREEN, "\xE4\xB8\x80" "Z");
    CHECK(s.glyph(1, 0) == 0x4E00);
    CHECK(s.glyph(1, 1) == kTermFontWideCont);
    CHECK(s.glyph(1, 2) == 'Z');
}

TEST(screen, grid_clear_resets_every_cell)
{
    Screen s = fresh();
    s.grid_text(5, 5, TC_RED, "DIRTY");
    s.grid_clear();

    CHECK(s.glyph(5, 5) == ' ');
    CHECK(s.color(5, 5) == TC_GREEN);
}

TEST(screen, a_bar_fills_in_proportion)
{
    Screen s = fresh();
    s.grid_bar(3, 0, 10, 0.5f, TC_GREEN);

    CHECK(s.glyph(3, 0) == '[');
    CHECK(s.glyph(3, 1) == '#');
    CHECK(s.glyph(3, 5) == '#');
    CHECK(s.glyph(3, 6) == '.');
    CHECK(s.glyph(3, 11) == ']');
    CHECK(s.color(3, 1) == TC_GREEN);
    CHECK(s.color(3, 6) == TC_DIM);
}

TEST(screen, a_title_bar_inverts_the_top_row)
{
    Screen s = fresh();
    s.grid_title("STATUS");

    CHECK(s.glyph(0, 1) == 'S');
    for (u32 c = 0; c < kTermCols; c++) {
        CHECK(s.color(0, c) == TC_BG);
    }
    CHECK(s.glyph(0, kTermCols - 8) == 'Q');
}

TEST(screen, scrollback_draws_the_tail_plus_the_open_line)
{
    Screen s = fresh();
    s.print("ALPHA\nBETA\nGAMMA");
    s.flush_pending();

    s.grid_clear();
    s.draw_scrollback(0, 3, TC_GREEN);

    char buf[kTermCols + 1];
    CHECK(row_text(s, 0, buf, kTermCols) == "ALPHA");
    CHECK(row_text(s, 1, buf, kTermCols) == "BETA");
    CHECK(row_text(s, 2, buf, kTermCols) == "GAMMA");
}

TEST(screen, the_cursor_blink_cycles)
{
    Screen s = fresh();
    CHECK(s.blink_on());

    s.tick_blink(0.6f);
    CHECK(!s.blink_on());

    s.tick_blink(0.6f);
    CHECK(s.blink_on());
}
