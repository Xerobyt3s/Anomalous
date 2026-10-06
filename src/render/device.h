#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "engine/render/post_process.h"
#include "render/asset_cache.h"
#include "render/camera.h"
#include "render/gpu_mesh.h"
#include "render/shader_set.h"
#include "render/sky.h"

#include <optional>

namespace anom {

class Arena;
class FileWatcher;

struct Environment {
    Vec3 sun_dir{-0.45f, -0.8f, -0.35f};
    f32 fog_density = 0.0028f;
    f32 exposure = 1.10f;
    f32 time_of_day = 0.5f;
};

struct PointLight {
    Vec3 pos{};
    Vec3 color{};
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
    u32 instanced_submeshes = 0;
    u32 program_binds = 0;
    u32 vao_binds = 0;
    u32 texture_binds = 0;
};

class RenderDevice {
public:
    static constexpr i32 kShadowSize = 2048;
    static constexpr f32 kHazeChromaPixels = 7.0f;
    static constexpr f32 kHazeShimmerScreen = 0.0025f;
    static constexpr u32 kMaxPointLights = 8;
    static constexpr u32 kMaxMeshDraws = 2048;
    static constexpr i32 kVideoWidth = 320;
    static constexpr i32 kVideoHeight = 200;

    bool init(FileWatcher& watcher, Arena& scratch);
    void shutdown();

    ShaderSet& shaders() { return shaders_; }
    AssetCache& assets() { return assets_; }

    void set_environment(const Environment& env);
    ghost::engine::PostProcess* post() { return post_ ? &*post_ : nullptr; }
    const SkyLighting& sky_lighting() const { return lighting_; }
    void set_point_lights(const PointLight* lights, u32 count);
    void set_weather(f32 overcast, f32 wetness);
    void set_snow(f32 cover, f32 fall, f32 wind);
    void set_windshield(f32 wet, f32 wiper_sweep, f32 glass_wet);
    void set_travel_warp(f32 warp, f32 flash) { warp_ = warp; warp_flash_ = flash; }
    void set_chroma(f32 pixels, f32 seed) { chroma_ = pixels; chroma_seed_ = seed; }
    void set_fall_rotation(Quat rot) { fall_rot_ = rot; }
    void set_haze(const Vec4* spheres, u32 count, f32 density, f32 chroma, f32 shimmer, Vec3 tint, f32 margin)
    {
        for (u32 i = 0; i < 4; i++) {
            haze_[i] = i < count ? spheres[i] : Vec4{};
        }
        haze_params_ = Vec4{static_cast<f32>(count < 4 ? count : 4), density, chroma, shimmer};
        haze_tint_ = Vec4{tint.x, tint.y, tint.z, margin};
    }
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
    void flush_meshes();
    u32 copy_scene_depth();
    void draw_island_haze(f32 near_d, f32 far_d, u32 depth_texture);
    void bind_procedural_ground();
    void draw_glass(const GpuMesh* mesh, const Mat4& model, f32 time);
    void draw_rain(f32 intensity, f32 wind, Vec3 cam_vel, f32 time);
    void draw_snow(f32 intensity, f32 wind, f32 time);
    void scene_grab();
    void begin_viewmodel();
    void end_viewmodel();
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
    void set_projection(const Mat4& proj);
    bool render_clouds();
    void view_setup(const Camera& cam, f32 width, f32 height);
    void draw_quad();

    ShaderSet shaders_;
    AssetCache assets_;

    Environment env_;
    std::optional<ghost::engine::PostProcess> post_;
    ghost::engine::GlTexture cloud_tex_;
    ghost::engine::GlFramebuffer cloud_fbo_;
    glm::ivec2 cloud_size_{0};
    bool video_active_ = false;
    SkyLighting lighting_;
    Vec3 lighting_sun_dir_{0.0f, 0.0f, 0.0f};
    ScreenFx screen_fx_;
    f32 time_seconds_ = 0.0f;
    DrawStats stats_;
    StateCache state_;

    struct MeshDraw {
        const GpuMesh* mesh = nullptr;
        Mat4 model = mat4_identity();
    };

    MeshDraw mesh_queue_[kMaxMeshDraws];
    Mat4 instance_staging_[kMaxMeshDraws];
    u32 mesh_queue_count_ = 0;
    u32 instance_vbo_ = 0;
    u32 instance_cursor_ = 0;

    u32 video_fbo_ = 0;
    u32 video_tex_ = 0;
    u32 video_depth_ = 0;
    bool video_broken_ = false;

    u32 camera_ubo_ = 0;
    u32 quad_vao_ = 0;
    u32 quad_vbo_ = 0;
    u32 quad_ebo_ = 0;

    Mat4 view_proj_ = mat4_identity();
    Mat4 view_ = mat4_identity();
    Mat4 proj_ = mat4_identity();
    Mat4 viewmodel_proj_ = mat4_identity();
    Vec3 cam_pos_{0.0f, 0.0f, 0.0f};
    Vec2 viewport_{1.0f, 1.0f};
    Frustum frustum_{};

    Vec3 sky_fwd_{0.0f, 0.0f, -1.0f};
    Vec3 sky_right_{1.0f, 0.0f, 0.0f};
    Vec3 sky_up_{0.0f, 1.0f, 0.0f};

    PointLight point_lights_[kMaxPointLights]{};
    u32 point_count_ = 0;

    f32 weather_overcast_ = 0.0f;
    f32 weather_wetness_ = 0.0f;
    f32 snow_cover_ = 0.0f;
    f32 snow_fall_ = 0.0f;
    f32 wind_ = 0.0f;
    f32 shield_wet_ = 0.0f;
    f32 shield_wiper_ = 0.0f;
    f32 shield_incar_ = 0.0f;
    f32 shield_rain_ = 0.0f;
    f32 warp_ = 0.0f;
    f32 warp_flash_ = 0.0f;
    f32 chroma_ = 0.0f;
    f32 chroma_seed_ = 0.0f;
    Quat fall_rot_ = quat_identity();
    Vec4 haze_[4] = {};
    Vec4 haze_params_{};
    Vec4 haze_tint_{};
    u32 procedural_slots_[6] = {kNoTexture, kNoTexture, kNoTexture, kNoTexture, kNoTexture, kNoTexture};


    u32 shadow_fbo_ = 0;
    u32 shadow_tex_ = 0;
    bool shadow_broken_ = false;
    bool shadow_pass_ = false;
    f32 shadow_strength_ = 0.0f;
    Mat4 shadow_mat_ = mat4_identity();
};

} // namespace anom
