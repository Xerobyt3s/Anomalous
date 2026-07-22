#include "ui/ui.h"
#include "platform/platform.h"
#include "render/debug_draw.h"
#include "render/text.h"
#include "core/arena.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

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

#define UI_MAX_PANELS 8

typedef struct UiRect {
    f32 x0, y0, x1, y1;
} UiRect;

typedef struct UiState {
    const GameInput* input;
    const void* active_id;
    const void* active_text;
    f32 panel_x;
    f32 panel_y;
    f32 panel_width;
    f32 cursor_y;
    f32 panel_start_y;
    u32 panel_bg_slot;
    b32 in_panel;
    UiRect panels[UI_MAX_PANELS];
    u32 panel_count;
    UiRect prev_panels[UI_MAX_PANELS];
    u32 prev_panel_count;
} UiState;

static UiState s_ui;

void ui_begin_frame(const struct GameInput* input)
{
    s_ui.input = input;
    if (!input->mouse_down[MOUSE_LEFT]) {
        s_ui.active_id = 0;
    }
    if (input->mouse_pressed[MOUSE_LEFT] && !ui_mouse_over_panel(input)) {
        s_ui.active_text = 0;
    }
    if (input->key_pressed[KEY_ESCAPE]) {
        s_ui.active_text = 0;
    }
    memcpy(s_ui.prev_panels, s_ui.panels, sizeof(s_ui.panels));
    s_ui.prev_panel_count = s_ui.panel_count;
    s_ui.panel_count = 0;
}

b32 ui_text_active(void)
{
    return s_ui.active_text != 0;
}

b32 ui_mouse_over_panel(const struct GameInput* input)
{
    for (u32 i = 0; i < s_ui.prev_panel_count; i++) {
        const UiRect* r = &s_ui.prev_panels[i];
        if (input->mouse_x >= r->x0 && input->mouse_x <= r->x1
            && input->mouse_y >= r->y0 && input->mouse_y <= r->y1) {
            return 1;
        }
    }
    return 0;
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

b32 ui_button(const char* label)
{
    f32 x0 = s_ui.panel_x + UI_PADDING;
    f32 x1 = s_ui.panel_x + s_ui.panel_width - UI_PADDING;
    f32 y0 = s_ui.cursor_y;
    f32 y1 = y0 + UI_ROW_HEIGHT + 2.0f;

    const GameInput* in = s_ui.input;
    b32 hovering = ui_mouse_in(x0, y0, x1, y1);
    u32 fill = hovering ? dd_rgba(58, 76, 104, 255) : dd_rgba(38, 48, 62, 255);
    dd_rect_2d_filled(x0, y0, x1, y1, fill);
    dd_rect_2d(x0, y0, x1, y1, UI_BORDER_COLOR);
    f32 tw = text_measure(label, UI_TEXT_SIZE);
    text_draw(x0 + (x1 - x0 - tw) * 0.5f, y0 + UI_TEXT_SIZE + 1.0f, UI_TEXT_SIZE, UI_TITLE_COLOR, label);

    s_ui.cursor_y = y1 + 4.0f;
    return hovering && in->mouse_pressed[MOUSE_LEFT];
}

b32 ui_checkbox(const char* label, b32* value)
{
    f32 x0 = s_ui.panel_x + UI_PADDING;
    f32 box = UI_TRACK_HEIGHT + 2.0f;
    f32 y0 = s_ui.cursor_y;
    f32 y1 = y0 + box;

    const GameInput* in = s_ui.input;
    b32 hovering = ui_mouse_in(x0, y0, s_ui.panel_x + s_ui.panel_width - UI_PADDING, y1);
    dd_rect_2d_filled(x0, y0, x0 + box, y1, UI_TRACK_COLOR);
    if (*value) {
        dd_rect_2d_filled(x0 + 3.0f, y0 + 3.0f, x0 + box - 3.0f, y1 - 3.0f, UI_HANDLE_COLOR);
    }
    dd_rect_2d(x0, y0, x0 + box, y1, UI_BORDER_COLOR);
    text_draw(x0 + box + 6.0f, y0 + UI_TEXT_SIZE - 1.0f, UI_TEXT_SIZE, UI_LABEL_COLOR, label);

    s_ui.cursor_y = y1 + 6.0f;
    b32 changed = 0;
    if (hovering && in->mouse_pressed[MOUSE_LEFT]) {
        *value = !*value;
        changed = 1;
    }
    return changed;
}

b32 ui_list_item(const char* label, b32 selected)
{
    f32 x0 = s_ui.panel_x + UI_PADDING;
    f32 x1 = s_ui.panel_x + s_ui.panel_width - UI_PADDING;
    f32 y0 = s_ui.cursor_y;
    f32 y1 = y0 + UI_ROW_HEIGHT;

    const GameInput* in = s_ui.input;
    b32 hovering = ui_mouse_in(x0, y0, x1, y1);
    if (selected) {
        dd_rect_2d_filled(x0, y0, x1, y1, dd_rgba(52, 88, 130, 255));
    } else if (hovering) {
        dd_rect_2d_filled(x0, y0, x1, y1, dd_rgba(34, 42, 54, 255));
    }
    text_draw(x0 + 4.0f, y0 + UI_TEXT_SIZE - 1.0f, UI_TEXT_SIZE, UI_LABEL_COLOR, label);

    s_ui.cursor_y = y1 + 1.0f;
    return hovering && in->mouse_pressed[MOUSE_LEFT];
}

b32 ui_text_field(const char* label, char* buf, u32 cap)
{
    f32 x0 = s_ui.panel_x + UI_PADDING;
    f32 x1 = s_ui.panel_x + s_ui.panel_width - UI_PADDING;

    text_draw(x0, s_ui.cursor_y + UI_TEXT_SIZE, UI_TEXT_SIZE, UI_LABEL_COLOR, label);
    s_ui.cursor_y += UI_ROW_HEIGHT - 2.0f;

    f32 y0 = s_ui.cursor_y;
    f32 y1 = y0 + UI_ROW_HEIGHT;
    const GameInput* in = s_ui.input;
    b32 hovering = ui_mouse_in(x0, y0, x1, y1);
    if (hovering && in->mouse_pressed[MOUSE_LEFT]) {
        if (s_ui.active_text != buf) {
            while (platform_next_char() != 0) {
            }
        }
        s_ui.active_text = buf;
    }
    b32 focused = s_ui.active_text == buf;

    dd_rect_2d_filled(x0, y0, x1, y1, dd_rgba(8, 10, 14, 235));
    dd_rect_2d(x0, y0, x1, y1, focused ? UI_HANDLE_COLOR : UI_BORDER_COLOR);

    b32 submitted = 0;
    if (focused) {
        u32 len = (u32)strlen(buf);
        if (in->key_pressed[KEY_BACKSPACE] && len > 0) {
            buf[len - 1] = 0;
            len--;
        }
        u32 ch;
        while ((ch = platform_next_char()) != 0) {
            if (ch >= 32 && ch < 127 && len + 1 < cap) {
                buf[len++] = (char)ch;
                buf[len] = 0;
            }
        }
        if (in->key_pressed[KEY_ENTER]) {
            submitted = 1;
            s_ui.active_text = 0;
        }
    }
    text_draw(x0 + 4.0f, y0 + UI_TEXT_SIZE - 1.0f, UI_TEXT_SIZE, UI_TITLE_COLOR, buf);

    s_ui.cursor_y = y1 + 6.0f;
    return submitted;
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

    if (s_ui.panel_count < UI_MAX_PANELS) {
        UiRect* r = &s_ui.panels[s_ui.panel_count++];
        r->x0 = x0;
        r->y0 = y0;
        r->x1 = x1;
        r->y1 = y1;
    }
}
