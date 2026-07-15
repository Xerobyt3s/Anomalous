#pragma once

#include "core/types.h"
#include "math/vmath.h"

b32  text_init(const char* ttf_path);
void text_shutdown(void);
void text_begin_frame(void);
void text_draw(f32 x, f32 y, f32 size, u32 color, const char* str);
f32  text_measure(const char* str, f32 size);
f32  text_line_height(f32 size);
void text_flush(void);
