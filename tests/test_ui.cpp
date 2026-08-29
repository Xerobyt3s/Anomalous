#include "test.h"

#include "platform/input.h"
#include "ui/ui.h"

#include <cstring>

using namespace anom;

namespace {

constexpr i32 kMouseLeft = 0;

struct Harness {
    Input input;
    Ui ui;
    bool left_was_down = false;

    void frame(f32 mx, f32 my, bool left_down)
    {
        input.begin_frame();
        input.set_mouse_pos(mx, my);
        if (left_down != left_was_down) {
            input.set_mouse_button(kMouseLeft, left_down);
            left_was_down = left_down;
        }
        ui.begin_frame(input, nullptr, nullptr);
    }

    void key(i32 code, bool down) { input.set_key(code, down); }
    void type(u32 codepoint) { input.push_char(codepoint); }
    void end() { input.finish_frame(); }
};

constexpr f32 kPanelX = 100.0f;
constexpr f32 kPanelY = 50.0f;
constexpr f32 kPanelW = 200.0f;

f32 first_row_center_y()
{
    return kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + Ui::kRowHeight * 0.5f;
}

} // namespace

TEST(ui, a_button_fires_only_when_pressed_over_it)
{
    Harness h;
    const f32 y = first_row_center_y();

    h.frame(kPanelX + 20.0f, y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(h.ui.button("go"));
    h.ui.panel_end();
    h.end();

    h.frame(kPanelX + 20.0f, y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(!h.ui.button("go"));
    h.ui.panel_end();
    h.end();

    h.frame(kPanelX + 20.0f, kPanelY - 40.0f, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(!h.ui.button("go"));
    h.ui.panel_end();
    h.end();
}

TEST(ui, a_checkbox_toggles_on_each_click)
{
    Harness h;
    bool value = false;
    const f32 y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + 4.0f;

    h.frame(kPanelX + 12.0f, y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(h.ui.checkbox("on", value));
    CHECK(value);
    h.ui.panel_end();
    h.end();

    h.frame(kPanelX + 12.0f, y, false);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(!h.ui.checkbox("on", value));
    CHECK(value);
    h.ui.panel_end();
    h.end();

    h.frame(kPanelX + 12.0f, y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(h.ui.checkbox("on", value));
    CHECK(!value);
    h.ui.panel_end();
    h.end();
}

TEST(ui, a_list_item_reports_the_click_that_lands_on_it)
{
    Harness h;
    const f32 first_y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + 4.0f;
    const f32 second_y = first_y + Ui::kRowHeight + 1.0f;

    h.frame(kPanelX + 30.0f, second_y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(!h.ui.list_item("one", false));
    CHECK(h.ui.list_item("two", false));
    CHECK(!h.ui.list_item("three", false));
    h.ui.panel_end();
    h.end();
}

TEST(ui, a_slider_keeps_tracking_once_grabbed)
{
    Harness h;
    f32 value = 0.0f;
    const f32 track_y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + Ui::kRowHeight - 2.0f
                      + Ui::kTrackHeight * 0.5f;
    const f32 x0 = kPanelX + Ui::kPadding;
    const f32 x1 = kPanelX + kPanelW - Ui::kPadding;
    const f32 mid = (x0 + x1) * 0.5f;

    h.frame(mid, track_y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(h.ui.slider("v", value, 0.0f, 10.0f));
    h.ui.panel_end();
    h.end();
    CHECK_NEAR(value, 5.0f, 0.2);

    h.frame(x1 + 60.0f, track_y + 400.0f, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(h.ui.slider("v", value, 0.0f, 10.0f));
    h.ui.panel_end();
    h.end();
    CHECK_NEAR(value, 10.0f, 1e-4);

    h.frame(x0 - 60.0f, track_y, false);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(!h.ui.slider("v", value, 0.0f, 10.0f));
    h.ui.panel_end();
    h.end();
    CHECK_NEAR(value, 10.0f, 1e-4);
}

TEST(ui, only_one_slider_grabs_a_single_press)
{
    Harness h;
    f32 a = 0.0f;
    f32 b = 0.0f;
    const f32 track_y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + Ui::kRowHeight - 2.0f
                      + Ui::kTrackHeight * 0.5f;

    h.frame(kPanelX + kPanelW * 0.5f, track_y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(h.ui.slider("a", a, 0.0f, 10.0f));
    h.ui.slider("b", b, 0.0f, 10.0f);
    h.ui.panel_end();
    h.end();

    CHECK(a > 0.0f);
    CHECK(b == 0.0f);
}

TEST(ui, panel_hit_testing_uses_the_previous_frame_layout)
{
    Harness h;
    CHECK(!h.ui.mouse_over_panel(Vec2{kPanelX + 10.0f, kPanelY + 10.0f}));

    h.frame(0.0f, 0.0f, false);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.label("row");
    h.ui.panel_end();
    h.end();

    h.frame(0.0f, 0.0f, false);
    CHECK(h.ui.mouse_over_panel(Vec2{kPanelX + 10.0f, kPanelY + 10.0f}));
    CHECK(!h.ui.mouse_over_panel(Vec2{kPanelX - 10.0f, kPanelY + 10.0f}));
    CHECK(!h.ui.mouse_over_panel(Vec2{kPanelX + 10.0f, kPanelY + 400.0f}));
    h.end();
}

TEST(ui, a_panel_grows_with_the_rows_it_holds)
{
    Harness h;
    h.frame(0.0f, 0.0f, false);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    const f32 empty = h.ui.cursor_y();
    h.ui.label("a");
    h.ui.label("b");
    const f32 filled = h.ui.cursor_y();
    h.ui.panel_end();
    h.end();

    CHECK_NEAR(filled - empty, Ui::kRowHeight * 2.0f, 1e-4);
}

TEST(ui, a_text_field_takes_focus_and_accepts_typing)
{
    Harness h;
    char buf[32] = {};
    const f32 field_y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + Ui::kRowHeight - 2.0f
                      + Ui::kRowHeight * 0.5f;

    CHECK(!h.ui.text_active());

    h.frame(kPanelX + 40.0f, field_y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();
    CHECK(h.ui.text_active());
    CHECK(std::strlen(buf) == 0);

    h.frame(kPanelX + 40.0f, field_y, false);
    h.type('a');
    h.type('b');
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();
    CHECK(std::strcmp(buf, "ab") == 0);
}

TEST(ui, backspace_and_enter_drive_the_focused_field)
{
    Harness h;
    char buf[32] = {};
    const f32 field_y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + Ui::kRowHeight - 2.0f
                      + Ui::kRowHeight * 0.5f;

    h.frame(kPanelX + 40.0f, field_y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();

    h.frame(kPanelX + 40.0f, field_y, false);
    h.type('x');
    h.type('y');
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();
    CHECK(std::strcmp(buf, "xy") == 0);

    h.frame(kPanelX + 40.0f, field_y, false);
    h.key(static_cast<i32>(Key::Backspace), true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();
    CHECK(std::strcmp(buf, "x") == 0);

    h.frame(kPanelX + 40.0f, field_y, false);
    h.key(static_cast<i32>(Key::Backspace), false);
    h.key(static_cast<i32>(Key::Enter), true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    CHECK(h.ui.text_field("name", buf, sizeof(buf)));
    h.ui.panel_end();
    h.end();
    CHECK(!h.ui.text_active());
}

TEST(ui, a_field_ignores_the_characters_from_the_click_that_focused_it)
{
    Harness h;
    char buf[32] = {};
    const f32 field_y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + Ui::kRowHeight - 2.0f
                      + Ui::kRowHeight * 0.5f;

    h.frame(kPanelX + 40.0f, field_y, true);
    h.type('z');
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();

    CHECK(h.ui.text_active());
    CHECK(std::strlen(buf) == 0);
}

TEST(ui, clicking_outside_every_panel_drops_text_focus)
{
    Harness h;
    char buf[32] = {};
    const f32 field_y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + Ui::kRowHeight - 2.0f
                      + Ui::kRowHeight * 0.5f;

    h.frame(kPanelX + 40.0f, field_y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();
    CHECK(h.ui.text_active());

    h.frame(kPanelX + 40.0f, field_y, false);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();
    CHECK(h.ui.text_active());

    h.frame(kPanelX - 200.0f, field_y, true);
    CHECK(!h.ui.text_active());
    h.end();
}

TEST(ui, escape_drops_text_focus)
{
    Harness h;
    char buf[32] = {};
    const f32 field_y = kPanelY + Ui::kPadding + Ui::kRowHeight + 4.0f + Ui::kRowHeight - 2.0f
                      + Ui::kRowHeight * 0.5f;

    h.frame(kPanelX + 40.0f, field_y, true);
    h.ui.panel_begin("p", kPanelX, kPanelY, kPanelW);
    h.ui.text_field("name", buf, sizeof(buf));
    h.ui.panel_end();
    h.end();
    CHECK(h.ui.text_active());

    h.input.begin_frame();
    h.input.set_mouse_pos(kPanelX + 40.0f, field_y);
    h.input.set_key(static_cast<i32>(Key::Escape), true);
    h.ui.begin_frame(h.input, nullptr, nullptr);
    CHECK(!h.ui.text_active());
    h.end();
}
