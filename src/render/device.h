#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "render/asset_cache.h"
#include "render/camera.h"
#include "render/gpu_mesh.h"
#include "render/shader.h"
#include "render/sky.h"

namespace anom {

class Arena;
class FileWatcher;

struct Environment {
    Vec3 sun_dir{-0.45f, -0.8f, -0.35f};
    f32 fog_density = 0.0028f;
    f32 exposure = 1.40f;
    f32 time_of_day = 0.5f;
};

struct RetroFx {
    bool enabled = true;
    f32 quantise_bits = 6.0f;
    f32 pixel_scale = 3.0f;
    f32 near_distance = 2.5f;
    f32 far_distance = 12.0f;
};

struct ScreenFx {
    f32 power_seconds = 0.0f;
    f32 burn = 0.0f;
    f32 shake = 0.0f;
    f32 pixelate = 1.0f;
};

struct DrawStats {
    u32 meshes_submitted = 0;
    u32 meshes_culled = 0;
    u32 draw_calls = 0;
    u32 program_binds = 0;
    u32 vao_binds = 0;
    u32 texture_binds = 0;
};

class RenderDevice {
public:
    static constexpr i32 kShadowSize = 2048;
    static constexpr i32 kBloomMips = 6;
    static constexpr u32 kPointLights = 4;
    static constexpr i32 kVideoWidth = 320;
    static constexpr i32 kVideoHeight = 200;

    bool init(FileWatcher& watcher, Arena& scratch);
    void shutdown();

    ShaderLibrary& shaders() { return shaders_; }
    AssetCache& assets() { return assets_; }

    void set_environment(const Environment& env);
    void set_retro_fx(const RetroFx& fx) { retro_ = fx; }
    const RetroFx& retro_fx() const { return retro_; }
    const SkyLighting& sky_lighting() const { return lighting_; }
    void set_headlights(Vec3 left, Vec3 right, Vec3 dir, f32 intensity);
    void set_point_light(u32 index, Vec3 pos, Vec3 color, f32 radius);
    void set_weather(f32 overcast, f32 wetness);
    void set_windshield(f32 wet, f32 wiper_sweep, f32 glass_wet);
    void set_screen_fx(const ScreenFx& fx) { screen_fx_ = fx; }
    void set_time(f32 seconds) { time_seconds_ = seconds; }

    void begin_frame(const Camera& cam, i32 fb_width, i32 fb_height);
    void end_frame();

    bool shadow_begin(Vec3 focus);
    void shadow_end();
    bool shadow_pass_active() const { return shadow_pass_; }

    bool video_begin(const Camera& cam);
    u32 video_end();
    bool read_backbuffer_rgb(Arena& scratch, u8* out, i32 out_w, i32 out_h);
    Mat4 shadow_matrix() const { return shadow_mat_; }

    void draw_sky(f32 time);
    void draw_mesh(const GpuMesh* mesh, const Mat4& model);
    void draw_glass(const GpuMesh* mesh, const Mat4& model, f32 time);
    void draw_rain(f32 intensity, f32 wind, Vec3 cam_vel, f32 time);
    void scene_grab();
    void post_process(f32 time);
    void blit_texture(f32 x, f32 y, f32 w, f32 h, u32 gl_texture, f32 alpha, f32 time);
    void draw_lit_quad(const Mat4& model, u32 gl_texture, f32 time);

    Mat4 view_proj() const { return view_proj_; }
    Vec3 camera_pos() const { return cam_pos_; }
    Vec2 viewport() const { return viewport_; }
    const Frustum& frustum() const { return frustum_; }
    bool project_to_screen(Vec3 world, Vec2& out_screen) const;

    const DrawStats& stats() const { return stats_; }

    void use_program(u32 program);
    void bind_vao(u32 vao);
    void bind_texture0(u32 texture);
    void bind_texture(u32 unit, u32 texture);
    void set_cull(bool enabled);
    void reset_state_cache();

private:
    struct StateCache {
        u32 program = 0;
        u32 vao = 0;
        u32 texture0 = 0;
        i32 cull = -1;
    };

    void quad_init();
    void view_setup(const Camera& cam, f32 width, f32 height);
    void scene_target_ensure(i32 width, i32 height);
    bool bloom_render();
    void draw_quad();

    ShaderLibrary shaders_;
    AssetCache assets_;

    Environment env_;
    RetroFx retro_;
    SkyLighting lighting_;
    Vec3 lighting_sun_dir_{0.0f, 0.0f, 0.0f};
    ScreenFx screen_fx_;
    f32 time_seconds_ = 0.0f;
    DrawStats stats_;
    StateCache state_;

    u32 video_fbo_ = 0;
    u32 video_tex_ = 0;
    u32 video_depth_ = 0;
    bool video_broken_ = false;

    u32 camera_ubo_ = 0;
    u32 quad_vao_ = 0;
    u32 quad_vbo_ = 0;
    u32 quad_ebo_ = 0;

    Mat4 view_proj_ = mat4_identity();
    Vec3 cam_pos_{0.0f, 0.0f, 0.0f};
    Vec2 viewport_{1.0f, 1.0f};
    Frustum frustum_{};

    Vec3 sky_fwd_{0.0f, 0.0f, -1.0f};
    Vec3 sky_right_{1.0f, 0.0f, 0.0f};
    Vec3 sky_up_{0.0f, 1.0f, 0.0f};

    Vec3 spot_pos_[2]{};
    Vec3 spot_dir_{0.0f, 0.0f, -1.0f};
    f32 spot_intensity_ = 0.0f;

    Vec3 point_pos_[kPointLights]{};
    Vec3 point_color_[kPointLights]{};
    f32 point_radius_[kPointLights]{};

    f32 weather_overcast_ = 0.0f;
    f32 weather_wetness_ = 0.0f;
    f32 shield_wet_ = 0.0f;
    f32 shield_wiper_ = 0.0f;
    f32 shield_incar_ = 0.0f;
    f32 shield_rain_ = 0.0f;

    u32 scene_fbo_ = 0;
    u32 scene_color_ = 0;
    u32 scene_depth_ = 0;
    u32 scene_copy_ = 0;
    i32 scene_w_ = 0;
    i32 scene_h_ = 0;
    bool scene_broken_ = false;

    u32 bloom_fbo_ = 0;
    u32 bloom_tex_ = 0;
    i32 bloom_w_[kBloomMips]{};
    i32 bloom_h_[kBloomMips]{};
    i32 bloom_count_ = 0;

    u32 shadow_fbo_ = 0;
    u32 shadow_tex_ = 0;
    bool shadow_broken_ = false;
    bool shadow_pass_ = false;
    f32 shadow_strength_ = 0.0f;
    Mat4 shadow_mat_ = mat4_identity();
};

} // namespace anom
