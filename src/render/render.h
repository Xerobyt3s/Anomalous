#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "render/camera.h"

b32  r_init(void);
void r_shutdown(void);
void r_hot_reload_poll(f64 now);
void r_begin_frame(const Camera* cam);
void r_end_frame(void);
u32  r_shader(const char* name);
Mat4 r_view_proj(void);
Vec3 r_camera_pos(void);
Vec2 r_viewport_size(void);
b32  r_project_to_screen(Vec3 world, Vec2* out_screen);
