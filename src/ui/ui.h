#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"

#include <span>
#include <string_view>

namespace anom {

class DebugDraw;
class Input;
class TextRenderer;

inline constexpr u32 kUiMaxPanels = 8;

class Ui {
public:
    static constexpr f32 kPadding = 8.0f;
    static constexpr f32 kRowHeight = 20.0f;
    static constexpr f32 kTextSize = 15.0f;
    static constexpr f32 kTrackHeight = 12.0f;
    static constexpr f32 kGraphHeight = 56.0f;

    void begin_frame(const Input& input, DebugDraw* debug, TextRenderer* text);

    void panel_begin(std::string_view title, f32 x, f32 y, f32 width);
    void panel_end();

    void label(const char* fmt, ...);
    bool button(std::string_view text);
    bool checkbox(std::string_view text, bool& value);
    bool list_item(std::string_view text, bool selected);
    bool slider(std::string_view text, f32& value, f32 min_value, f32 max_value);
    bool text_field(std::string_view text, char* buf, u32 cap);
    void graph(std::string_view text, std::span<const f32> samples, u32 head, f32 min_value,
               f32 max_value, u32 color);

    bool text_active() const { return active_text_ != nullptr; }
    bool mouse_over_panel(Vec2 mouse) const;
    f32 cursor_y() const { return cursor_y_; }

private:
    struct Rect {
        f32 x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;

        bool contains(Vec2 p) const
        {
            return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
        }
    };

    bool mouse_in(f32 x0, f32 y0, f32 x1, f32 y1) const;
    void fill(f32 x0, f32 y0, f32 x1, f32 y1, u32 color);
    void stroke(f32 x0, f32 y0, f32 x1, f32 y1, u32 color);
    void glyphs(f32 x, f32 y, f32 size, u32 color, std::string_view str);
    f32 text_width(std::string_view str, f32 size) const;
    f32 content_x0() const { return panel_x_ + kPadding; }
    f32 content_x1() const { return panel_x_ + panel_width_ - kPadding; }

    const Input* input_ = nullptr;
    DebugDraw* debug_ = nullptr;
    TextRenderer* text_ = nullptr;

    const void* active_id_ = nullptr;
    const void* active_text_ = nullptr;
    u32 chars_taken_ = 0;

    f32 panel_x_ = 0.0f;
    f32 panel_width_ = 0.0f;
    f32 panel_start_y_ = 0.0f;
    f32 cursor_y_ = 0.0f;
    u32 panel_bg_slot_ = 0;
    bool in_panel_ = false;

    Rect panels_[kUiMaxPanels];
    u32 panel_count_ = 0;
    Rect prev_panels_[kUiMaxPanels];
    u32 prev_panel_count_ = 0;
};

} // namespace anom
