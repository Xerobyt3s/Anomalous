#include "render/device.h"
#include "render/haze.h"
#include "world/weather.h"
#include "math/glm_bridge.h"
#include "assets/watcher.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/gl_loader.h"

#include <algorithm>

#include <cstddef>
#include <exception>

namespace anom {
namespace {

constexpr f32 kSnowFlakes = 60000.0f;
constexpr f32 kSnowWindSpeed = kWindSpeed;
constexpr Vec2 kSnowWindDir{kWindDir.x, kWindDir.z};
constexpr f32 kOvercastSunBlock = 0.7f;
constexpr f32 kSceneNear = 0.05f;
constexpr f32 kSceneFar = 2000.0f;
constexpr f32 kViewmodelNear = 0.01f;
constexpr f32 kViewmodelFar = 10.0f;
constexpr i32 kCloudDivisor = 2;

struct CameraUbo {
    Mat4 view;
    Mat4 proj;
    Mat4 view_proj;
    Vec4 cam_pos;
    Vec4 viewport;
    Vec4 sun_dir;
    Vec4 sun_color_ambient;
    Vec4 fog_color_density;
    Mat4 shadow_mat;
    Vec4 shadow_params;
    Vec4 point_pos[RenderDevice::kMaxPointLights];
    Vec4 point_color[RenderDevice::kMaxPointLights];
    Vec4 light_params;
    Vec4 sky_ambient;
    Vec4 ground_ambient;
    Mat4 inv_view_proj;
    Vec4 ambient_horizon;
    Vec4 exposure_params;
    Vec4 time_params;
    Vec4 cloud_sun_color;
    Vec4 weather;
    Vec4 haze[4];
    Vec4 haze_params;
    Vec4 haze_tint;
    Vec4 point_dir[RenderDevice::kMaxPointLights];
};

} // namespace

void RenderDevice::set_environment(const Environment& env)
{
    env_ = env;
    const Vec3 sun_toward = -normalize(env_.sun_dir);
    if (distance_sq(sun_toward, lighting_sun_dir_) > 1e-8f) {
        lighting_sun_dir_ = sun_toward;
        lighting_ = compute_sky_lighting(sun_toward, 0.0f);
    }
}

bool RenderDevice::init(FileWatcher& watcher, Arena& scratch)
{
    glCreateBuffers(1, &instance_vbo_);
    glNamedBufferStorage(instance_vbo_, kMaxMeshDraws * sizeof(Mat4), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    assets_.init(watcher, scratch);
    assets_.set_instance_buffer(instance_vbo_);

    glCreateBuffers(1, &camera_ubo_);
    glNamedBufferStorage(camera_ubo_, sizeof(CameraUbo), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, camera_ubo_);
    quad_init();
    try {
        post_.emplace(kSceneNear, kSceneFar);
        post_->applyPreset(ghost::engine::PostProcess::GradePreset::Daylight);
    } catch (const std::exception& e) {
        log_error("render: post process unavailable: %s", e.what());
    }
    return true;
}

void RenderDevice::shutdown()
{
    assets_.shutdown();
    shaders_.clear();

    if (shadow_tex_) { glDeleteTextures(1, &shadow_tex_); }
    if (shadow_fbo_) { glDeleteFramebuffers(1, &shadow_fbo_); }
    if (video_fbo_) {
        glDeleteTextures(1, &video_tex_);
        glDeleteTextures(1, &video_depth_);
        glDeleteFramebuffers(1, &video_fbo_);
    }
    post_.reset();
    if (quad_vao_) {
        glDeleteVertexArrays(1, &quad_vao_);
        glDeleteBuffers(1, &quad_vbo_);
        glDeleteBuffers(1, &quad_ebo_);
    }
    if (camera_ubo_) { glDeleteBuffers(1, &camera_ubo_); }
    if (instance_vbo_) { glDeleteBuffers(1, &instance_vbo_); }
}

void RenderDevice::use_program(u32 program)
{
    if (state_.program == program) {
        return;
    }
    state_.program = program;
    stats_.program_binds++;
    glUseProgram(program);
}

void RenderDevice::bind_vao(u32 vao)
{
    if (state_.vao == vao) {
        return;
    }
    state_.vao = vao;
    stats_.vao_binds++;
    glBindVertexArray(vao);
}

void RenderDevice::bind_texture0(u32 texture)
{
    if (state_.texture0 == texture) {
        return;
    }
    state_.texture0 = texture;
    stats_.texture_binds++;
    glBindTextureUnit(0, texture);
}

void RenderDevice::bind_texture(u32 unit, u32 texture)
{
    if (unit == 0) {
        bind_texture0(texture);
        return;
    }
    stats_.texture_binds++;
    glBindTextureUnit(unit, texture);
}

void RenderDevice::set_cull(bool enabled)
{
    const i32 want = enabled ? 1 : 0;
    if (state_.cull == want) {
        return;
    }
    state_.cull = want;
    if (enabled) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void RenderDevice::reset_state_cache()
{
    state_ = StateCache{};
}

void RenderDevice::quad_init()
{
    const f32 verts[4][8] = {
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        {0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
        {-0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
    };
    const u32 indices[6] = {0, 1, 2, 0, 2, 3};

    glCreateBuffers(1, &quad_vbo_);
    glNamedBufferStorage(quad_vbo_, sizeof(verts), verts, 0);
    glCreateBuffers(1, &quad_ebo_);
    glNamedBufferStorage(quad_ebo_, sizeof(indices), indices, 0);
    glCreateVertexArrays(1, &quad_vao_);
    glVertexArrayVertexBuffer(quad_vao_, 0, quad_vbo_, 0, 8 * sizeof(f32));
    glVertexArrayElementBuffer(quad_vao_, quad_ebo_);
    for (u32 i = 0; i < 3; i++) {
        glEnableVertexArrayAttrib(quad_vao_, i);
        glVertexArrayAttribFormat(quad_vao_, i, i == 2 ? 2 : 3, GL_FLOAT, GL_FALSE,
                                  (i == 0 ? 0 : (i == 1 ? 3 : 6)) * sizeof(f32));
        glVertexArrayAttribBinding(quad_vao_, i, 0);
    }
}

void RenderDevice::draw_quad()
{
    bind_vao(quad_vao_);
    stats_.draw_calls++;
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
}

void RenderDevice::set_point_lights(const PointLight* lights, u32 count)
{
    point_count_ = count < kMaxPointLights ? count : kMaxPointLights;
    for (u32 i = 0; i < point_count_; i++) {
        point_lights_[i] = lights[i];
    }
}

void RenderDevice::set_weather(f32 overcast, f32 wetness)
{
    weather_overcast_ = overcast;
    weather_wetness_ = wetness;
}

void RenderDevice::set_snow(f32 cover, f32 fall, f32 wind)
{
    snow_cover_ = cover;
    snow_fall_ = fall;
    wind_ = wind;
}

void RenderDevice::set_windshield(f32 wet, f32 wiper_sweep, f32 glass_wet)
{
    shield_wet_ = wet;
    shield_wiper_ = wiper_sweep;
    shield_incar_ = glass_wet > 0.0f ? 1.0f : 0.0f;
    shield_rain_ = glass_wet;
}

void RenderDevice::view_setup(const Camera& cam, f32 width, f32 height)
{
    viewport_ = Vec2{width, height};
    const f32 aspect = width / height;

    CameraUbo ubo;
    ubo.view = cam.view();
    ubo.proj = cam.proj(aspect);
    ubo.view_proj = ubo.proj * ubo.view;
    view_ = ubo.view;
    proj_ = ubo.proj;
    viewmodel_proj_ = mat4_perspective(cam.fov_y, aspect, kViewmodelNear, kViewmodelFar);
    ubo.inv_view_proj = inverse(ubo.view_proj);
    ubo.cam_pos = vec4_from_vec3(cam.pos, 1.0f);
    ubo.viewport = Vec4{viewport_.x, viewport_.y, 1.0f / viewport_.x, 1.0f / viewport_.y};
    ubo.sun_dir = vec4_from_vec3(normalize(env_.sun_dir), 0.0f);

    const f32 ambient_scalar = f_clamp01(0.25f + luminance(lighting_.ambient_zenith) * 4.0f);
    const f32 sun_through = 1.0f - kOvercastSunBlock * f_clamp01(weather_overcast_);
    ubo.sun_color_ambient = vec4_from_vec3(lighting_.sun_color * sun_through, ambient_scalar);

    const Vec3 fog_color = lighting_.ambient_horizon;
    ubo.fog_color_density = vec4_from_vec3(fog_color, env_.fog_density);
    ubo.exposure_params = Vec4{env_.exposure, env_.time_of_day, 0.0f, 0.0f};
    ubo.time_params = Vec4{time_seconds_, 0.0f, 0.0f, 0.0f};
    ubo.cloud_sun_color = vec4_from_vec3(lighting_.cloud_sun_color, 0.0f);
    ubo.weather = Vec4{snow_cover_, snow_fall_, wind_, 0.0f};
    for (u32 i = 0; i < 4; i++) {
        ubo.haze[i] = haze_[i];
    }
    ubo.haze_params = haze_params_;
    ubo.haze_tint = haze_tint_;

    for (u32 i = 0; i < kMaxPointLights; i++) {
        const bool on = i < point_count_;
        ubo.point_pos[i] = on ? vec4_from_vec3(point_lights_[i].pos, 1.0f) : Vec4{};
        ubo.point_color[i] = on ? vec4_from_vec3(point_lights_[i].color, point_lights_[i].reach) : Vec4{};
        ubo.point_dir[i] = on ? vec4_from_vec3(point_lights_[i].dir, point_lights_[i].cone) : Vec4{0.0f, 0.0f, 0.0f, -2.0f};
    }
    ubo.light_params = Vec4{static_cast<f32>(point_count_), 0.0f, 0.0f, 0.0f};
    ubo.shadow_mat = shadow_mat_;
    ubo.shadow_params = Vec4{shadow_strength_, 1.0f / static_cast<f32>(kShadowSize),
                             weather_wetness_, weather_overcast_};

    const f32 gray = f_clamp01(weather_overcast_ * 0.75f);
    const Vec3 grey_target{luminance(lighting_.ambient_zenith),
                           luminance(lighting_.ambient_zenith),
                           luminance(lighting_.ambient_zenith)};
    ubo.sky_ambient = vec4_from_vec3(lerp(lighting_.ambient_zenith, grey_target, gray), 0.0f);
    ubo.ambient_horizon = vec4_from_vec3(lerp(lighting_.ambient_horizon, grey_target, gray), 0.0f);
    ubo.ground_ambient = vec4_from_vec3(lighting_.ambient_ground, 0.0f);

    glNamedBufferSubData(camera_ubo_, 0, sizeof(CameraUbo), &ubo);
    if (shadow_tex_) {
        glBindTextureUnit(7, shadow_tex_);
    }

    view_proj_ = ubo.view_proj;
    cam_pos_ = cam.pos;
    frustum_ = frustum_from_view_proj(view_proj_);

    const f32 tan_h = std::tan(cam.fov_y * 0.5f);
    sky_fwd_ = cam.forward();
    const Vec3 cam_right = cam.right();
    const Vec3 cam_up = normalize(cross(cam_right, sky_fwd_));
    sky_right_ = cam_right * (tan_h * aspect);
    sky_up_ = cam_up * tan_h;

    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glClearColor(fog_color.x, fog_color.y, fog_color.z, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RenderDevice::begin_frame(const Camera& cam, i32 fb_width, i32 fb_height)
{
    if (fb_width <= 0 || fb_height <= 0) {
        fb_width = 1;
        fb_height = 1;
    }
    stats_ = DrawStats{};
    instance_cursor_ = 0;
    reset_state_cache();
    if (post_) {
        post_->resize(glm::ivec2(fb_width, fb_height));
        post_->beginScene();
        post_->setCamera(to_glm(cam.view()), to_glm(cam.proj(static_cast<f32>(fb_width) / static_cast<f32>(fb_height))));
        ghost::engine::PostProcess::useWorldDepthRange();
    }
    view_setup(cam, static_cast<f32>(fb_width), static_cast<f32>(fb_height));
}

void RenderDevice::end_frame()
{
    flush_meshes();
    use_program(0);
    bind_vao(0);
}

bool RenderDevice::video_begin(const Camera& cam)
{
    flush_meshes();
    if (video_broken_) {
        return false;
    }
    if (!video_fbo_) {
        glCreateTextures(GL_TEXTURE_2D, 1, &video_tex_);
        glTextureStorage2D(video_tex_, 1, GL_RGBA8, kVideoWidth, kVideoHeight);
        glTextureParameteri(video_tex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(video_tex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(video_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(video_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCreateTextures(GL_TEXTURE_2D, 1, &video_depth_);
        glTextureStorage2D(video_depth_, 1, GL_DEPTH_COMPONENT24, kVideoWidth, kVideoHeight);
        glCreateFramebuffers(1, &video_fbo_);
        glNamedFramebufferTexture(video_fbo_, GL_COLOR_ATTACHMENT0, video_tex_, 0);
        glNamedFramebufferTexture(video_fbo_, GL_DEPTH_ATTACHMENT, video_depth_, 0);
        if (glCheckNamedFramebufferStatus(video_fbo_, GL_FRAMEBUFFER)
            != GL_FRAMEBUFFER_COMPLETE) {
            log_error("render: video framebuffer incomplete");
            video_broken_ = true;
            return false;
        }
    }
    reset_state_cache();
    video_active_ = true;
    ghost::engine::PostProcess::resetDepthRange();
    glBindFramebuffer(GL_FRAMEBUFFER, video_fbo_);
    view_setup(cam, static_cast<f32>(kVideoWidth), static_cast<f32>(kVideoHeight));
    return true;
}

u32 RenderDevice::video_end()
{
    video_active_ = false;
    flush_meshes();
    use_program(0);
    bind_vao(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return video_tex_;
}

bool RenderDevice::read_backbuffer_rgb(Arena& scratch, u8* out, i32 out_w, i32 out_h)
{
    const i32 w = static_cast<i32>(viewport_.x);
    const i32 h = static_cast<i32>(viewport_.y);
    if (w <= 0 || h <= 0 || out_w <= 0 || out_h <= 0) {
        return false;
    }

    ArenaScope temp(scratch);
    u8* rgba = scratch.push_array<u8>(static_cast<u64>(w) * static_cast<u64>(h) * 4);
    if (!rgba) {
        return false;
    }
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    for (i32 oy = 0; oy < out_h; oy++) {
        const i32 sy0 = oy * h / out_h;
        const i32 sy1 = (oy + 1) * h / out_h > sy0 ? (oy + 1) * h / out_h : sy0 + 1;
        for (i32 ox = 0; ox < out_w; ox++) {
            const i32 sx0 = ox * w / out_w;
            const i32 sx1 = (ox + 1) * w / out_w > sx0 ? (ox + 1) * w / out_w : sx0 + 1;
            u32 sum[3] = {0, 0, 0};
            for (i32 sy = sy0; sy < sy1; sy++) {
                const u8* row = &rgba[static_cast<u64>(h - 1 - sy) * static_cast<u64>(w) * 4];
                for (i32 sx = sx0; sx < sx1; sx++) {
                    const u8* p = &row[static_cast<u64>(sx) * 4];
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                }
            }
            const u32 n = static_cast<u32>((sx1 - sx0) * (sy1 - sy0));
            u8* dst = &out[(static_cast<u64>(oy) * static_cast<u64>(out_w)
                            + static_cast<u64>(ox)) * 3];
            dst[0] = static_cast<u8>(sum[0] / n);
            dst[1] = static_cast<u8>(sum[1] / n);
            dst[2] = static_cast<u8>(sum[2] / n);
        }
    }
    return true;
}

bool RenderDevice::shadow_begin(Vec3 focus)
{
    if (shadow_broken_) {
        return false;
    }
    ghost::engine::PostProcess::resetDepthRange();
    if (!shadow_fbo_) {
        glCreateTextures(GL_TEXTURE_2D, 1, &shadow_tex_);
        glTextureStorage2D(shadow_tex_, 1, GL_DEPTH_COMPONENT32F, kShadowSize, kShadowSize);
        glTextureParameteri(shadow_tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(shadow_tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(shadow_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTextureParameteri(shadow_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const f32 border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTextureParameterfv(shadow_tex_, GL_TEXTURE_BORDER_COLOR, border);
        glTextureParameteri(shadow_tex_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTextureParameteri(shadow_tex_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glCreateFramebuffers(1, &shadow_fbo_);
        glNamedFramebufferTexture(shadow_fbo_, GL_DEPTH_ATTACHMENT, shadow_tex_, 0);
        glNamedFramebufferDrawBuffer(shadow_fbo_, GL_NONE);
        glNamedFramebufferReadBuffer(shadow_fbo_, GL_NONE);
        if (glCheckNamedFramebufferStatus(shadow_fbo_, GL_FRAMEBUFFER)
            != GL_FRAMEBUFFER_COMPLETE) {
            log_error("render: shadow framebuffer incomplete");
            shadow_broken_ = true;
            return false;
        }
    }

    const Vec3 qdir = normalize(Vec3{std::floor(env_.sun_dir.x * 160.0f + 0.5f) / 160.0f,
                                     std::floor(env_.sun_dir.y * 160.0f + 0.5f) / 160.0f,
                                     std::floor(env_.sun_dir.z * 160.0f + 0.5f) / 160.0f});
    const Vec3 to_sun = -qdir;
    if (to_sun.y < 0.03f) {
        shadow_strength_ = 0.0f;
        return false;
    }
    const f32 light_lum = luminance(lighting_.sun_color);
    shadow_strength_ = 0.80f * f_clamp01((to_sun.y - 0.03f) / 0.10f)
                     * f_clamp01(light_lum / 6.0f);

    const f32 ext = 55.0f;
    const Vec3 fwd = qdir;
    const Vec3 up_hint = f_abs(fwd.y) > 0.93f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 side = normalize(cross(fwd, up_hint));
    const Vec3 up = cross(side, fwd);
    const f32 texel = 2.0f * ext / static_cast<f32>(kShadowSize);
    const f32 sx = dot(side, focus);
    const f32 sy = dot(up, focus);
    const Vec3 snapped = focus + side * (std::floor(sx / texel) * texel - sx)
                       + up * (std::floor(sy / texel) * texel - sy);
    const Vec3 eye = snapped - fwd * 130.0f;

    shadow_mat_ = mat4_ortho(-ext, ext, -ext, ext, 1.0f, 280.0f) * mat4_look_at(eye, snapped, up);
    frustum_ = frustum_from_view_proj(shadow_mat_);

    glBindTextureUnit(7, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_DEPTH_BUFFER_BIT);
    reset_state_cache();
    shadow_pass_ = true;
    return true;
}

void RenderDevice::shadow_end()
{
    flush_meshes();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shadow_pass_ = false;
    reset_state_cache();
}

u32 RenderDevice::copy_scene_depth()
{
    flush_meshes();
    return post_ ? post_->copySceneDepth() : 0u;
}

void RenderDevice::draw_island_haze(f32 near_d, f32 far_d, u32 depth_texture)
{
    const u32 count = static_cast<u32>(haze_params_.x);
    if (count == 0 || depth_texture == 0 || !post_) {
        return;
    }
    const HazeSpan span = haze_window_span(haze_, count, cam_pos_, near_d, far_d);
    if (span.empty()) {
        return;
    }
    bool touches = false;
    for (u32 i = 0; i < count; i++) {
        touches = touches || haze_sphere_touches(haze_[i], cam_pos_, span);
    }
    if (!touches) {
        return;
    }
    const ghost::engine::Shader* haze = shaders_.get("island_haze", "post/fullscreen");
    if (!haze) {
        return;
    }
    flush_meshes();
    reset_state_cache();
    const bool culled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    haze->use();
    glBindTextureUnit(0, depth_texture);
    haze->set("uDepth", 0);
    haze->set("uSplit", ghost::engine::PostProcess::depthSplit());
    haze->set("uSpan", glm::vec2(span.start, span.end));
    bind_vao(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    stats_.draw_calls++;
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    if (culled) {
        glEnable(GL_CULL_FACE);
    }
    reset_state_cache();
}

bool RenderDevice::render_clouds()
{
    if (video_active_ || !post_) {
        return false;
    }
    const ghost::engine::Shader* clouds = shaders_.get("clouds", "post/fullscreen");
    if (!clouds) {
        return false;
    }
    const glm::ivec2 size(std::max(static_cast<i32>(viewport_.x) / kCloudDivisor, 1),
                          std::max(static_cast<i32>(viewport_.y) / kCloudDivisor, 1));
    if (size != cloud_size_ || !cloud_fbo_) {
        GLuint tex = 0;
        glCreateTextures(GL_TEXTURE_2D, 1, &tex);
        glTextureStorage2D(tex, 1, GL_RGBA16F, size.x, size.y);
        glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        cloud_tex_ = ghost::engine::GlTexture(tex);
        GLuint fbo = 0;
        glCreateFramebuffers(1, &fbo);
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, tex, 0);
        cloud_fbo_ = ghost::engine::GlFramebuffer(fbo);
        cloud_size_ = size;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, cloud_fbo_.id());
    glViewport(0, 0, size.x, size.y);
    glDisable(GL_DEPTH_TEST);
    clouds->use();
    clouds->set("uTargetSize", glm::vec2(size));
    bind_vao(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    stats_.draw_calls++;
    reset_state_cache();
    post_->beginScene();
    glEnable(GL_DEPTH_TEST);
    return true;
}

void RenderDevice::draw_sky(f32 time)
{
    const bool clouds = render_clouds();
    const u32 program = shaders_.program("sky");
    if (!program) {
        return;
    }
    (void)time;
    glProgramUniform1i(program, glGetUniformLocation(program, "uCloudsOn"), clouds ? 1 : 0);
    if (clouds) {
        glBindTextureUnit(14, cloud_tex_.id());
    }
    use_program(program);
    glDepthMask(GL_FALSE);
    draw_quad();
    glDepthMask(GL_TRUE);
}

void RenderDevice::draw_mesh(const GpuMesh* mesh, const Mat4& model)
{
    if (!mesh || !mesh->loaded) {
        return;
    }
    stats_.meshes_submitted++;

    if (!frustum_test_aabb(frustum_, transform(model, mesh->bounds))) {
        stats_.meshes_culled++;
        return;
    }
    if (mesh_queue_count_ >= kMaxMeshDraws) {
        flush_meshes();
    }
    mesh_queue_[mesh_queue_count_++] = MeshDraw{mesh, model};
}

void RenderDevice::bind_procedural_ground()
{
    constexpr u32 kProceduralFirstUnit = 8;
    static constexpr const char* kNames[6] = {"island_grass", "island_soil", "island_soil_n",
                                              "island_rock", "island_rock_n", "island_rock_s"};
    for (u32 i = 0; i < 6; i++) {
        if (procedural_slots_[i] == kNoTexture) {
            procedural_slots_[i] = assets_.texture_slot(kNames[i]);
        }
        bind_texture(kProceduralFirstUnit + i, assets_.texture_gl(procedural_slots_[i]));
    }
}

void RenderDevice::flush_meshes()
{
    if (mesh_queue_count_ == 0) {
        return;
    }
    const u32 count = mesh_queue_count_;
    mesh_queue_count_ = 0;

    const u32 program = shaders_.program(shadow_pass_ ? "shadow_inst" : "lit");
    if (!program) {
        return;
    }
    const GLint maps_location = shadow_pass_ ? -1 : glGetUniformLocation(program, "uMaps");
    const GLint glow_location = shadow_pass_ ? -1 : glGetUniformLocation(program, "uGlow");

    std::sort(mesh_queue_, mesh_queue_ + count, [](const MeshDraw& a, const MeshDraw& b) {
        return a.mesh < b.mesh;
    });

    if (shadow_pass_) {
        glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, shadow_mat_.m);
    }
    use_program(program);
    set_cull(true);

    u32 first = 0;
    while (first < count) {
        const GpuMesh* mesh = mesh_queue_[first].mesh;
        u32 run = 1;
        while (first + run < count && mesh_queue_[first + run].mesh == mesh) {
            run++;
        }
        if (instance_cursor_ + run > kMaxMeshDraws) {
            instance_cursor_ = 0;
        }

        for (u32 i = 0; i < run; i++) {
            instance_staging_[i] = mesh_queue_[first + i].model;
        }

        const u32 base = instance_cursor_;
        glNamedBufferSubData(instance_vbo_, static_cast<GLintptr>(base * sizeof(Mat4)),
                             static_cast<GLsizeiptr>(run * sizeof(Mat4)), instance_staging_);
        instance_cursor_ += run;

        glVertexArrayVertexBuffer(mesh->vao(), 1, instance_vbo_,
                                  static_cast<GLintptr>(base * sizeof(Mat4)), sizeof(Mat4));
        bind_vao(mesh->vao());
        if (!shadow_pass_) {
            glProgramUniform1f(program, glow_location, mesh->glow);
        }
        for (u32 s = 0; s < mesh->submesh_count; s++) {
            const GpuSubmesh& sub = mesh->submeshes[s];
            if (!shadow_pass_) {
                bind_texture0(assets_.texture_gl(sub.texture_slot));
                const i32 maps = (sub.normal_slot != kNoTexture ? 1 : 0)
                               | (sub.surface_slot != kNoTexture ? 2 : 0)
                               | (sub.blend_slot != kNoTexture ? 4 : 0)
                               | (sub.ground ? 8 : 0)
                               | (sub.procedural ? 16 : 0);
                glProgramUniform1i(program, maps_location, maps);
                if (maps & 1) {
                    bind_texture(4, assets_.texture_gl(sub.normal_slot));
                }
                if (maps & 2) {
                    bind_texture(5, assets_.texture_gl(sub.surface_slot));
                }
                if (maps & 4) {
                    bind_texture(6, assets_.texture_gl(sub.blend_slot));
                }
                if (maps & 16) {
                    bind_procedural_ground();
                }
            }
            stats_.draw_calls++;
            stats_.instanced_submeshes += run;
            glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(sub.index_count),
                                    GL_UNSIGNED_INT,
                                    reinterpret_cast<const void*>(
                                        static_cast<u64>(sub.first_index) * sizeof(u32)),
                                    static_cast<GLsizei>(run));
        }
        first += run;
    }
}

void RenderDevice::draw_glass(const GpuMesh* mesh, const Mat4& model, f32 time)
{
    flush_meshes();
    if (shadow_pass_ || !mesh || !mesh->loaded) {
        return;
    }
    if (!frustum_test_aabb(frustum_, transform(model, mesh->bounds))) {
        return;
    }
    const u32 program = shaders_.program("glass");
    if (!program) {
        return;
    }
    glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, model.m);
    glProgramUniform4f(program, 4, shield_wet_, shield_wiper_, shield_rain_, time);
    glProgramUniform4f(program, 5, wipers_[0], wipers_[1], wipers_[2], wipers_[3]);
    glProgramUniform1f(program, 6, wiper_reach_);
    use_program(program);
    bind_vao(mesh->vao());
    set_cull(true);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (u32 i = 0; i < mesh->submesh_count; i++) {
        const GpuSubmesh& sub = mesh->submeshes[i];
        stats_.draw_calls++;
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.index_count), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<u64>(sub.first_index)
                                                     * sizeof(u32)));
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void RenderDevice::draw_rain(f32 intensity, f32 wind, Vec3 cam_vel, f32 time)
{
    flush_meshes();
    if (intensity <= 0.003f) {
        return;
    }
    const u32 program = shaders_.program("rain");
    if (!program) {
        return;
    }
    use_program(program);
    glProgramUniform4f(program, 1, time, intensity, wind, 0.0f);
    glProgramUniform4f(program, 2, cam_vel.x, cam_vel.y, cam_vel.z, 0.0f);
    glProgramUniform4f(program, 3, sky_fwd_.x, sky_fwd_.y, sky_fwd_.z, 0.0f);
    glProgramUniform4f(program, 7, fall_rot_.x, fall_rot_.y, fall_rot_.z, fall_rot_.w);

    const u32 drops = static_cast<u32>(3400.0f * f_clamp01(intensity));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    bind_vao(quad_vao_);
    stats_.draw_calls++;
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(drops * 6));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void RenderDevice::draw_snow(f32 intensity, f32 wind, f32 time)
{
    flush_meshes();
    if (intensity <= 0.003f) {
        return;
    }
    const u32 program = shaders_.program("snow");
    if (!program) {
        return;
    }
    const u32 near_count = static_cast<u32>(kSnowFlakes * f_clamp01(intensity));
    const u32 total = near_count + near_count / 3;
    const f32 wind_speed = wind * kSnowWindSpeed;
    use_program(program);
    glProgramUniform4f(program, 1, time, intensity, wind, static_cast<f32>(near_count));
    glProgramUniform4f(program, 7, fall_rot_.x, fall_rot_.y, fall_rot_.z, fall_rot_.w);
    glProgramUniform4f(program, 2, kSnowWindDir.x * wind_speed, kSnowWindDir.y * wind_speed,
                       0.0f, 0.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    set_cull(false);
    bind_vao(quad_vao_);
    stats_.draw_calls++;
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(total));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void RenderDevice::set_projection(const Mat4& proj)
{
    const Mat4 view_proj = proj * view_;
    glNamedBufferSubData(camera_ubo_, offsetof(CameraUbo, proj), sizeof(Mat4), proj.m);
    glNamedBufferSubData(camera_ubo_, offsetof(CameraUbo, view_proj), sizeof(Mat4), view_proj.m);
}

void RenderDevice::begin_viewmodel()
{
    flush_meshes();
    set_projection(viewmodel_proj_);
    ghost::engine::PostProcess::useNearDepthRange();
}

void RenderDevice::end_viewmodel()
{
    flush_meshes();
    set_projection(proj_);
    ghost::engine::PostProcess::useWorldDepthRange();
}

void RenderDevice::scene_grab()
{
    flush_meshes();
    if (post_) {
        glBindTextureUnit(2, post_->copySceneColor());
    }
}

void RenderDevice::post_process(f32 time)
{
    if (!post_) {
        return;
    }
    flush_meshes();
    const ghost::engine::Shader* resolve = shaders_.get("post/resolve", "post/fullscreen");
    if (resolve) {
        post_->resolveScene([&](GLuint color, GLuint depth) {
            resolve->use();
            resolve->set("uScene", 0);
            resolve->set("uDepth", 1);
            resolve->set("uSplit", ghost::engine::PostProcess::depthSplit());
            glBindTextureUnit(0, color);
            glBindTextureUnit(1, depth);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        });
    }
    ghost::engine::PostProcess::Settings& st = post_->settings();
    st.flash = f_clamp01(warp_flash_);
    st.swirl = warp_;
    st.screenChroma = glm::vec2(chroma_, chroma_seed_);
    st.hazeChroma = haze_params_.z * kHazeChromaPixels;
    st.hazeShimmer = haze_params_.w * kHazeShimmerScreen * viewport_.y;
    post_->finish();
    post_->setDistortion({}, time);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    reset_state_cache();
}

void RenderDevice::blit_texture(f32 x, f32 y, f32 w, f32 h, u32 gl_texture, f32 alpha, f32 time)
{
    const u32 program = shaders_.program("blit");
    if (!program) {
        return;
    }
    const f32 x0 = x / viewport_.x * 2.0f - 1.0f;
    const f32 x1 = (x + w) / viewport_.x * 2.0f - 1.0f;
    const f32 y1 = 1.0f - y / viewport_.y * 2.0f;
    const f32 y0 = 1.0f - (y + h) / viewport_.y * 2.0f;

    glProgramUniform4f(program, 0, x0, y0, x1, y1);
    glProgramUniform1f(program, 1, time);
    glProgramUniform1f(program, 2, alpha);
    glProgramUniform1f(program, 3, screen_fx_.power_seconds);
    glProgramUniform1f(program, 4, screen_fx_.burn);
    glProgramUniform1f(program, 5, screen_fx_.shake);
    glProgramUniform1f(program, 6, screen_fx_.pixelate);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    use_program(program);
    bind_texture0(gl_texture);
    draw_quad();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void RenderDevice::draw_lit_quad(const Mat4& model, u32 gl_texture, f32 time)
{
    if (shadow_pass_) {
        return;
    }
    const u32 program = shaders_.program("screen");
    if (!program) {
        return;
    }
    glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, model.m);
    glProgramUniform1f(program, 4, time);
    glProgramUniform1f(program, 5, screen_fx_.power_seconds);
    glProgramUniform1f(program, 6, screen_fx_.burn);
    glProgramUniform1f(program, 7, screen_fx_.shake);
    glProgramUniform1f(program, 8, screen_fx_.pixelate);
    use_program(program);
    bind_texture0(gl_texture);
    draw_quad();
}

bool RenderDevice::project_to_screen(Vec3 world, Vec2& out_screen) const
{
    const f32* m = view_proj_.m;
    const f32 cx = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
    const f32 cy = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
    const f32 cw = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
    if (cw < 1e-4f) {
        return false;
    }
    const f32 inv_w = 1.0f / cw;
    out_screen.x = (cx * inv_w * 0.5f + 0.5f) * viewport_.x;
    out_screen.y = (0.5f - cy * inv_w * 0.5f) * viewport_.y;
    return true;
}

} // namespace anom
