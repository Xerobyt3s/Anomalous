#pragma once

#include "core/types.h"

struct GameInput;

void ui_begin_frame(const struct GameInput* input);
void ui_panel_begin(const char* title, f32 x, f32 y, f32 width);
void ui_label(const char* fmt, ...);
b32  ui_slider_f32(const char* label, f32* value, f32 min_val, f32 max_val);
void ui_graph(const char* label, const f32* samples, u32 capacity, u32 head, f32 min_val, f32 max_val, u32 color);
b32  ui_button(const char* label);
b32  ui_checkbox(const char* label, b32* value);
b32  ui_list_item(const char* label, b32 selected);
b32  ui_text_field(const char* label, char* buf, u32 cap);
b32  ui_text_active(void);
b32  ui_mouse_over_panel(const struct GameInput* input);
void ui_panel_end(void);
