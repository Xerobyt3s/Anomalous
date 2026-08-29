#include "ui/ui.h"
#include "platform/input.h"
#include "render/debug_draw.h"
#include "render/text.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace anom {
namespace {

const u32 kBgColor = dd_rgba(12, 16, 22, 215);
const u32 kBorderColor = dd_rgba(90, 105, 125, 255);
const u32 kTitleColor = dd_rgba(230, 235, 245, 255);
const u32 kLabelColor = dd_rgba(185, 195, 205, 255);
const u32 kTrackColor = dd_rgba(45, 55, 70, 255);
const u32 kHandleColor = dd_rgba(120, 170, 230, 255);
const u32 kHandleHotColor = dd_rgba(170, 210, 255, 255);
const u32 kFieldColor = dd_rgba(8, 10, 14, 235);
const u32 kButtonColor = dd_rgba(38, 48, 62, 255);
const u32 kButtonHotColor = dd_rgba(58, 76, 104, 255);
const u32 kSelectedColor = dd_rgba(52, 88, 130, 255);
const u32 kHoverColor = dd_rgba(34, 42, 54, 255);
const u32 kZeroLineColor = dd_rgba(60, 70, 85, 255);

} // namespace

void Ui::begin_frame(const Input& input, DebugDraw* debug, TextRenderer* text)
{
    input_ = &input;
    debug_ = debug;
    text_ = text;

    if (!input.down(MouseButton::Left)) {
        active_id_ = nullptr;
    }
    if (input.pressed(MouseButton::Left) && !mouse_over_panel(input.mouse_pos())) {
        active_text_ = nullptr;
    }
    if (input.pressed(Key::Escape)) {
        active_text_ = nullptr;
    }

    for (u32 i = 0; i < panel_count_; i++) {
        prev_panels_[i] = panels_[i];
    }
    prev_panel_count_ = panel_count_;
    panel_count_ = 0;
    chars_taken_ = 0;
}

bool Ui::mouse_over_panel(Vec2 mouse) const
{
    for (u32 i = 0; i < prev_panel_count_; i++) {
        if (prev_panels_[i].contains(mouse)) {
            return true;
        }
    }
    return false;
}

bool Ui::mouse_in(f32 x0, f32 y0, f32 x1, f32 y1) const
{
    const Rect r{x0, y0, x1, y1};
    return input_ && r.contains(input_->mouse_pos());
}

void Ui::panel_begin(std::string_view title, f32 x, f32 y, f32 width)
{
    panel_x_ = x;
    panel_width_ = width;
    panel_start_y_ = y;
    cursor_y_ = y + kPadding;
    panel_bg_slot_ = debug_ ? debug_->rect_2d_reserve() : 0;
    in_panel_ = true;

    glyphs(x + kPadding, cursor_y_ + kTextSize, kTextSize + 2.0f, kTitleColor, title);
    cursor_y_ += kRowHeight + 4.0f;
}

void Ui::panel_end()
{
    const f32 x0 = panel_x_;
    const f32 y0 = panel_start_y_;
    const f32 x1 = panel_x_ + panel_width_;
    const f32 y1 = cursor_y_ + kPadding * 0.5f;

    if (debug_) {
        debug_->rect_2d_fill_reserved(panel_bg_slot_, x0, y0, x1, y1, kBgColor);
    }
    stroke(x0, y0, x1, y1, kBorderColor);
    in_panel_ = false;

    if (panel_count_ < kUiMaxPanels) {
        panels_[panel_count_++] = Rect{x0, y0, x1, y1};
    }
}

void Ui::label(const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    glyphs(content_x0(), cursor_y_ + kTextSize, kTextSize, kLabelColor, buf);
    cursor_y_ += kRowHeight;
}

bool Ui::button(std::string_view text)
{
    const f32 x0 = content_x0();
    const f32 x1 = content_x1();
    const f32 y0 = cursor_y_;
    const f32 y1 = y0 + kRowHeight + 2.0f;

    const bool hovering = mouse_in(x0, y0, x1, y1);
    fill(x0, y0, x1, y1, hovering ? kButtonHotColor : kButtonColor);
    stroke(x0, y0, x1, y1, kBorderColor);

    const f32 width = text_width(text, kTextSize);
    glyphs(x0 + (x1 - x0 - width) * 0.5f, y0 + kTextSize + 1.0f, kTextSize, kTitleColor, text);

    cursor_y_ = y1 + 4.0f;
    return hovering && input_->pressed(MouseButton::Left);
}

bool Ui::checkbox(std::string_view text, bool& value)
{
    const f32 x0 = content_x0();
    const f32 box = kTrackHeight + 2.0f;
    const f32 y0 = cursor_y_;
    const f32 y1 = y0 + box;

    const bool hovering = mouse_in(x0, y0, content_x1(), y1);
    fill(x0, y0, x0 + box, y1, kTrackColor);
    if (value) {
        fill(x0 + 3.0f, y0 + 3.0f, x0 + box - 3.0f, y1 - 3.0f, kHandleColor);
    }
    stroke(x0, y0, x0 + box, y1, kBorderColor);
    glyphs(x0 + box + 6.0f, y0 + kTextSize - 1.0f, kTextSize, kLabelColor, text);

    cursor_y_ = y1 + 6.0f;
    if (hovering && input_->pressed(MouseButton::Left)) {
        value = !value;
        return true;
    }
    return false;
}

bool Ui::list_item(std::string_view text, bool selected)
{
    const f32 x0 = content_x0();
    const f32 x1 = content_x1();
    const f32 y0 = cursor_y_;
    const f32 y1 = y0 + kRowHeight;

    const bool hovering = mouse_in(x0, y0, x1, y1);
    if (selected) {
        fill(x0, y0, x1, y1, kSelectedColor);
    } else if (hovering) {
        fill(x0, y0, x1, y1, kHoverColor);
    }
    glyphs(x0 + 4.0f, y0 + kTextSize - 1.0f, kTextSize, kLabelColor, text);

    cursor_y_ = y1 + 1.0f;
    return hovering && input_->pressed(MouseButton::Left);
}

bool Ui::slider(std::string_view text, f32& value, f32 min_value, f32 max_value)
{
    const f32 x0 = content_x0();
    const f32 x1 = content_x1();

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.*s: %.3g", static_cast<int>(text.size()), text.data(),
                  static_cast<f64>(value));
    glyphs(x0, cursor_y_ + kTextSize, kTextSize, kLabelColor, buf);
    cursor_y_ += kRowHeight - 2.0f;

    const f32 track_y0 = cursor_y_;
    const f32 track_y1 = track_y0 + kTrackHeight;
    fill(x0, track_y0, x1, track_y1, kTrackColor);

    const bool hovering = mouse_in(x0, track_y0 - 2.0f, x1, track_y1 + 2.0f);
    if (hovering && input_->pressed(MouseButton::Left) && !active_id_) {
        active_id_ = &value;
    }

    bool changed = false;
    if (active_id_ == &value && input_->down(MouseButton::Left)) {
        const f32 t = f_clamp01((input_->mouse_pos().x - x0) / (x1 - x0));
        const f32 next = min_value + (max_value - min_value) * t;
        if (next != value) {
            value = next;
            changed = true;
        }
    }

    const f32 span = max_value - min_value;
    const f32 t = span > 0.0f ? f_clamp01((value - min_value) / span) : 0.0f;
    const f32 handle_x = x0 + t * (x1 - x0);
    const u32 color = (active_id_ == &value || hovering) ? kHandleHotColor : kHandleColor;
    fill(handle_x - 4.0f, track_y0 - 2.0f, handle_x + 4.0f, track_y1 + 2.0f, color);

    cursor_y_ += kTrackHeight + 8.0f;
    return changed;
}

bool Ui::text_field(std::string_view text, char* buf, u32 cap)
{
    const f32 x0 = content_x0();
    const f32 x1 = content_x1();

    glyphs(x0, cursor_y_ + kTextSize, kTextSize, kLabelColor, text);
    cursor_y_ += kRowHeight - 2.0f;

    const f32 y0 = cursor_y_;
    const f32 y1 = y0 + kRowHeight;
    const bool hovering = mouse_in(x0, y0, x1, y1);
    if (hovering && input_->pressed(MouseButton::Left) && active_text_ != buf) {
        active_text_ = buf;
        chars_taken_ = static_cast<u32>(input_->chars().size());
    }
    const bool focused = active_text_ == buf;

    fill(x0, y0, x1, y1, kFieldColor);
    stroke(x0, y0, x1, y1, focused ? kHandleColor : kBorderColor);

    bool submitted = false;
    if (focused) {
        u32 len = static_cast<u32>(std::strlen(buf));
        if (input_->pressed(Key::Backspace) && len > 0) {
            buf[--len] = 0;
        }
        const std::span<const u32> chars = input_->chars();
        for (u32 i = chars_taken_; i < chars.size(); i++) {
            const u32 cp = chars[i];
            if (cp >= 32 && cp < 127 && len + 1 < cap) {
                buf[len++] = static_cast<char>(cp);
                buf[len] = 0;
            }
        }
        chars_taken_ = static_cast<u32>(chars.size());
        if (input_->pressed(Key::Enter)) {
            submitted = true;
            active_text_ = nullptr;
        }
    }
    glyphs(x0 + 4.0f, y0 + kTextSize - 1.0f, kTextSize, kTitleColor, buf);

    cursor_y_ = y1 + 6.0f;
    return submitted;
}

void Ui::graph(std::string_view text, std::span<const f32> samples, u32 head, f32 min_value,
               f32 max_value, u32 color)
{
    if (samples.size() < 2) {
        return;
    }
    const f32 x0 = content_x0();
    const f32 x1 = content_x1();
    const u32 capacity = static_cast<u32>(samples.size());

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.*s: %.2f", static_cast<int>(text.size()), text.data(),
                  static_cast<f64>(samples[(head + capacity - 1) % capacity]));
    glyphs(x0, cursor_y_ + kTextSize, kTextSize, kLabelColor, buf);
    cursor_y_ += kRowHeight - 2.0f;

    const f32 y0 = cursor_y_;
    const f32 y1 = y0 + kGraphHeight;
    fill(x0, y0, x1, y1, kFieldColor);
    stroke(x0, y0, x1, y1, kTrackColor);

    if (min_value < 0.0f && max_value > 0.0f) {
        const f32 zero_y = y1 - (0.0f - min_value) / (max_value - min_value) * (y1 - y0);
        if (debug_) {
            debug_->line_2d(x0, zero_y, x1, zero_y, kZeroLineColor);
        }
    }

    const f32 range = max_value - min_value > 0.0f ? max_value - min_value : 1.0f;
    f32 prev_x = 0.0f;
    f32 prev_y = 0.0f;
    for (u32 i = 0; i < capacity; i++) {
        const f32 sample = samples[(head + i) % capacity];
        const f32 px = x0 + static_cast<f32>(i) / static_cast<f32>(capacity - 1) * (x1 - x0);
        const f32 py = y1 - f_clamp01((sample - min_value) / range) * (y1 - y0);
        if (i > 0) {
            if (debug_) {
                debug_->line_2d(prev_x, prev_y, px, py, color);
            }
        }
        prev_x = px;
        prev_y = py;
    }

    cursor_y_ += kGraphHeight + 8.0f;
}

void Ui::fill(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    if (debug_) {
        debug_->rect_2d_filled(x0, y0, x1, y1, color);
    }
}

void Ui::stroke(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    if (debug_) {
        debug_->rect_2d(x0, y0, x1, y1, color);
    }
}

void Ui::glyphs(f32 x, f32 y, f32 size, u32 color, std::string_view str)
{
    if (text_) {
        text_->draw(x, y, size, color, str);
    }
}

f32 Ui::text_width(std::string_view str, f32 size) const
{
    return text_ ? text_->measure(str, size) : 0.0f;
}

} // namespace anom
