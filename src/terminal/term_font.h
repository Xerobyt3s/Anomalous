#pragma once

#include "core/types.h"

#define TERM_FONT_SLOT_W 16
#define TERM_FONT_SLOT_H 12
#define TERM_FONT_ATLAS_COLS 32
#define TERM_FONT_ATLAS_ROWS 42
#define TERM_FONT_BLOCK 95
#define TERM_FONT_WIDE_CONT 0xFFFFu

b32  term_font_init(void);
void term_font_shutdown(void);
u32  term_font_texture(void);
u32  term_font_slot(u32 codepoint, b32* out_wide);
b32  term_font_cp_wide(u32 codepoint);
