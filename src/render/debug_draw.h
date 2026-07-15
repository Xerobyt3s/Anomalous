#pragma once

#include "core/types.h"
#include "math/vmath.h"

#define DD_WHITE   0xFFFFFFFFu
#define DD_GRAY    0xFF808080u
#define DD_DARK    0xFF404040u
#define DD_RED     0xFF3030F0u
#define DD_GREEN   0xFF30F030u
#define DD_BLUE    0xFFF06030u
#define DD_YELLOW  0xFF30E0F0u
#define DD_CYAN    0xFFF0E030u
#define DD_MAGENTA 0xFFF030F0u
#define DD_ORANGE  0xFF3090F0u

static inline u32 dd_rgba(u8 r, u8 g, u8 b, u8 a)
{
    return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

b32  dd_init(void);
void dd_shutdown(void);
void dd_begin_frame(void);
void dd_line(Vec3 a, Vec3 b, u32 color);
void dd_ray(Vec3 origin, Vec3 dir, f32 length, u32 color);
void dd_arrow(Vec3 from, Vec3 to, f32 head_size, u32 color);
void dd_aabb(Aabb box, u32 color);
void dd_obb(Vec3 center, Quat rot, Vec3 half_extents, u32 color);
void dd_sphere(Vec3 center, f32 radius, u32 color);
void dd_circle(Vec3 center, Vec3 normal, f32 radius, u32 color);
void dd_cross(Vec3 p, f32 size, u32 color);
void dd_grid(Vec3 center, f32 extent, f32 step, u32 color);
void dd_text_3d(Vec3 pos, f32 size, u32 color, const char* fmt, ...);
void dd_text_2d(f32 x, f32 y, f32 size, u32 color, const char* fmt, ...);
void dd_flush(void);
