#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Heightfield;

b32  terrain_render_init(const struct Heightfield* hf, const u8* roadmask, u32 mask_size);
void terrain_render_draw(void);
void terrain_render_draw_scrub(Vec3 cam_pos, f32 time);
void terrain_render_shutdown(void);
u32  terrain_render_chunks_drawn(void);
