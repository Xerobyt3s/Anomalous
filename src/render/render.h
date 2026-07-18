#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "render/camera.h"

struct GpuMesh;

b32  r_init(void);
void r_shutdown(void);
void r_hot_reload_poll(f64 now);
void r_set_environment(Vec3 sun_dir, Vec3 sun_color, f32 ambient, Vec3 fog_color, f32 fog_density);
void r_set_headlights(Vec3 pos_left, Vec3 pos_right, Vec3 dir, f32 intensity);
void r_set_point_light(u32 index, Vec3 pos, Vec3 color, f32 radius);
void r_set_weather(f32 overcast, f32 wetness);
void r_set_windshield(f32 wet, f32 wiper_sweep, f32 glass_wet);
void r_draw_rain(f32 intensity, f32 wind, Vec3 cam_vel, f32 time);
void r_begin_frame(const Camera* cam);
void r_post_process(f32 time);
void r_draw_sky(f32 time);
b32  r_shadow_begin(Vec3 focus);
void r_shadow_end(void);
b32  r_shadow_pass_active(void);
Mat4 r_shadow_matrix(void);
void r_end_frame(void);
void r_draw_mesh(const struct GpuMesh* mesh, Mat4 model);
void r_draw_glass(const struct GpuMesh* mesh, Mat4 model);
void r_scene_grab(void);
void r_draw_lit_quad(Mat4 model, u32 gl_texture);
void r_blit_texture(f32 x, f32 y, f32 w, f32 h, u32 gl_texture, f32 alpha);
void r_set_screen_fx(f32 power_seconds, f32 burn, f32 shake, f32 pixelate);
u32  r_shader(const char* name);
Mat4 r_view_proj(void);
Vec3 r_camera_pos(void);
Vec2 r_viewport_size(void);
b32  r_read_backbuffer_rgb(u8* out, i32 out_w, i32 out_h);
b32  r_video_begin(const struct Camera* cam);
u32  r_video_end(void);
const Frustum* r_frustum(void);
b32  r_project_to_screen(Vec3 world, Vec2* out_screen);
