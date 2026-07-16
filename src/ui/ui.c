#include "ui/ui.h"
#include "platform/platform.h"
#include "render/debug_draw.h"
#include "render/text.h"
#include "core/arena.h"

#include <stdio.h>
#include <stdarg.h>

#define UI_PADDING 8.0f
#define UI_ROW_HEIGHT 20.0f
#define UI_TEXT_SIZE 15.0f
#define UI_TRACK_HEIGHT 12.0f
#define UI_GRAPH_HEIGHT 56.0f
#define UI_BG_COLOR dd_rgba(12, 16, 22, 215)
#define UI_BORDER_COLOR dd_rgba(90, 105, 125, 255)
#define UI_TITLE_COLOR dd_rgba(230, 235, 245, 255)
#define UI_LABEL_COLOR dd_rgba(185, 195, 205, 255)
#define UI_TRACK_COLOR dd_rgba(45, 55, 70, 255)
#define UI_HANDLE_COLOR dd_rgba(120, 170, 230, 255)
#define UI_HANDLE_HOT_COLOR dd_rgba(170, 210, 255, 255)

typedef struct UiState {
    const GameInput* input;
    const void* active_id;
    f32 panel_x;
    f32 panel_y;
    f32 panel_width;
    f32 cursor_y;
    f32 panel_start_y;
    u32 panel_bg_slot;
    b32 in_panel;
} UiState;

static UiState s_ui;

void ui_begin_frame(const struct GameInput* input)
{
    s_ui.input = input;
    if (!input->mouse_down[MOUSE_LEFT]) {
        s_ui.active_id = 0;
    }
}

void ui_panel_begin(const char* title, f32 x, f32 y, f32 width)
{
    s_ui.panel_x = x;
    s_ui.panel_y = y;
    s_ui.panel_width = width;
    s_ui.panel_start_y = y;
    s_ui.cursor_y = y + UI_PADDING;
    s_ui.panel_bg_slot = dd_rect_2d_reserve();
    s_ui.in_panel = 1;

    text_draw(x + UI_PADDING, s_ui.cursor_y + UI_TEXT_SIZE, UI_TEXT_SIZE + 2.0f, UI_TITLE_COLOR, title);
    s_ui.cursor_y += UI_ROW_HEIGHT + 4.0f;
}

static b32 ui_mouse_in(f32 x0, f32 y0, f32 x1, f32 y1)
{
    const GameInput* in = s_ui.input;
    return in->mouse_x >= x0 && in->mouse_x <= x1 && in->mouse_y >= y0 && in->mouse_y <= y1;
}

void ui_label(const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    text_draw(s_ui.panel_x + UI_PADDING, s_ui.cursor_y + UI_TEXT_SIZE, UI_TEXT_SIZE, UI_LABEL_COLOR, buf);
    s_ui.cursor_y += UI_ROW_HEIGHT;
}

b32 ui_slider_f32(const char* label, f32* value, f32 min_val, f32 max_val)
{
    f32 x0 = s_ui.panel_x + UI_PADDING;
    f32 x1 = s_ui.panel_x + s_ui.panel_width - UI_PADDING;

    char buf[128];
    snprintf(buf, sizeof(buf), "%s: %.3g", label, (f64)*value);
    text_draw(x0, s_ui.cursor_y + UI_TEXT_SIZE, UI_TEXT_SIZE, UI_LABEL_COLOR, buf);
    s_ui.cursor_y += UI_ROW_HEIGHT - 2.0f;

    f32 track_y0 = s_ui.cursor_y;
    f32 track_y1 = track_y0 + UI_TRACK_HEIGHT;
    dd_rect_2d_filled(x0, track_y0, x1, track_y1, UI_TRACK_COLOR);

    const GameInput* in = s_ui.input;
    b32 hovering = ui_mouse_in(x0, track_y0 - 2.0f, x1, track_y1 + 2.0f);
    if (hovering && in->mouse_pressed[MOUSE_LEFT] && !s_ui.active_id) {
        s_ui.active_id = value;
    }

    b32 changed = 0;
    if (s_ui.active_id == value && in->mouse_down[MOUSE_LEFT]) {
        f32 t = f_clamp01((in->mouse_x - x0) / (x1 - x0));
        f32 next = min_val + (max_val - min_val) * t;
        if (next != *value) {
            *value = next;
            changed = 1;
        }
    }

    f32 t = (max_val - min_val) > 0.0f ? f_clamp01((*value - min_val) / (max_val - min_val)) : 0.0f;
    f32 handle_x = x0 + t * (x1 - x0);
    u32 handle_color = (s_ui.active_id == value || hovering) ? UI_HANDLE_HOT_COLOR : UI_HANDLE_COLOR;
    dd_rect_2d_filled(handle_x - 4.0f, track_y0 - 2.0f, handle_x + 4.0f, track_y1 + 2.0f, handle_color);

    s_ui.cursor_y += UI_TRACK_HEIGHT + 8.0f;
    return changed;
}

void ui_graph(const char* label, const f32* samples, u32 capacity, u32 head, f32 min_val, f32 max_val, u32 color)
{
    f32 x0 = s_ui.panel_x + UI_PADDING;
    f32 x1 = s_ui.panel_x + s_ui.panel_width - UI_PADDING;

    f32 latest = samples[(head + capacity - 1) % capacity];
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: %.2f", label, (f64)latest);
    text_draw(x0, s_ui.cursor_y + UI_TEXT_SIZE, UI_TEXT_SIZE, UI_LABEL_COLOR, buf);
    s_ui.cursor_y += UI_ROW_HEIGHT - 2.0f;

    f32 y0 = s_ui.cursor_y;
    f32 y1 = y0 + UI_GRAPH_HEIGHT;
    dd_rect_2d_filled(x0, y0, x1, y1, dd_rgba(8, 10, 14, 235));
    dd_rect_2d(x0, y0, x1, y1, UI_TRACK_COLOR);

    if (min_val < 0.0f && max_val > 0.0f) {
        f32 zero_y = y1 - (0.0f - min_val) / (max_val - min_val) * (y1 - y0);
        dd_line_2d(x0, zero_y, x1, zero_y, dd_rgba(60, 70, 85, 255));
    }

    f32 range = max_val - min_val;
    if (range <= 0.0f) {
        range = 1.0f;
    }
    f32 prev_x = 0.0f, prev_y = 0.0f;
    for (u32 i = 0; i < capacity; i++) {
        f32 sample = samples[(head + i) % capacity];
        f32 px = x0 + (f32)i / (f32)(capacity - 1) * (x1 - x0);
        f32 py = y1 - f_clamp01((sample - min_val) / range) * (y1 - y0);
        if (i > 0) {
            dd_line_2d(prev_x, prev_y, px, py, color);
        }
        prev_x = px;
        prev_y = py;
    }

    s_ui.cursor_y += UI_GRAPH_HEIGHT + 8.0f;
}

void ui_panel_end(void)
{
    f32 x0 = s_ui.panel_x;
    f32 y0 = s_ui.panel_start_y;
    f32 x1 = s_ui.panel_x + s_ui.panel_width;
    f32 y1 = s_ui.cursor_y + UI_PADDING * 0.5f;
    dd_rect_2d_fill_reserved(s_ui.panel_bg_slot, x0, y0, x1, y1, UI_BG_COLOR);
    dd_rect_2d(x0, y0, x1, y1, UI_BORDER_COLOR);
    s_ui.in_panel = 0;
}
