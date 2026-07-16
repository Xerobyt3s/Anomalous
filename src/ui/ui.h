#pragma once

#include "core/types.h"

struct GameInput;

void ui_begin_frame(const struct GameInput* input);
void ui_panel_begin(const char* title, f32 x, f32 y, f32 width);
void ui_label(const char* fmt, ...);
b32  ui_slider_f32(const char* label, f32* value, f32 min_val, f32 max_val);
void ui_graph(const char* label, const f32* samples, u32 capacity, u32 head, f32 min_val, f32 max_val, u32 color);
void ui_panel_end(void);
