#pragma once

#include "core/types.h"

#pragma warning(push, 0)
#include <stb_truetype.h>
#pragma warning(pop)

b32  fontchain_init(void);
i32  fontchain_find(u32 codepoint, i32* out_glyph);
const stbtt_fontinfo* fontchain_face(i32 face);
u32  utf8_next(const char** p);
