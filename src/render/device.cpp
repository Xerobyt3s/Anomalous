#include "render/device.h"
#include "assets/watcher.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/gl_loader.h"

namespace anom {
namespace {

struct CameraUbo {
    Mat4 view;
    Mat4 proj;
    Mat4 view_proj;
    Vec4 cam_pos;
    Vec4 viewport;
    Vec4 sun_dir;
    Vec4 sun_color_ambient;
    Vec4 fog_color_density;
    Vec4 spot_pos_cone[2];
    Vec4 spot_dir_intensity[2];
    Mat4 shadow_mat;
    Vec4 shadow_params;
    Vec4 point_pos_radius[4];
    Vec4 point_color[4];
    Vec4 sky_ambient;
    Vec4 ground_ambient;
    Mat4 inv_view_proj;
    Vec4 ambient_horizon;
    Vec4 exposure_params;
    Vec4 retro_params;
    Vec4 cloud_sun_color;
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
    shaders_.init(watcher, scratch);
    assets_.init(watcher, scratch);

    glCreateBuffers(1, &camera_ubo_);
    glNamedBufferStorage(camera_ubo_, sizeof(CameraUbo), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, camera_ubo_);
    quad_init();
    return true;
}

void RenderDevice::shutdown()
{
    assets_.shutdown();
    shaders_.shutdown();

    if (bloom_tex_) { glDeleteTextures(1, &bloom_tex_); }
    if (bloom_fbo_) { glDeleteFramebuffers(1, &bloom_fbo_); }
    if (shadow_tex_) { glDeleteTextures(1, &shadow_tex_); }
    if (shadow_fbo_) { glDeleteFramebuffers(1, &shadow_fbo_); }
    if (video_fbo_) {
        glDeleteTextures(1, &video_tex_);
        glDeleteTextures(1, &video_depth_);
        glDeleteFramebuffers(1, &video_fbo_);
    }
    if (scene_fbo_) {
        glDeleteTextures(1, &scene_color_);
        glDeleteTextures(1, &scene_depth_);
        glDeleteTextures(1, &scene_copy_);
        glDeleteFramebuffers(1, &scene_fbo_);
    }
    if (quad_vao_) {
        glDeleteVertexArrays(1, &quad_vao_);
        glDeleteBuffers(1, &quad_vbo_);
        glDeleteBuffers(1, &quad_ebo_);
    }
    if (camera_ubo_) { glDeleteBuffers(1, &camera_ubo_); }
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

void RenderDevice::set_headlights(Vec3 left, Vec3 right, Vec3 dir, f32 intensity)
{
    spot_pos_[0] = left;
    spot_pos_[1] = right;
    spot_dir_ = normalize(dir);
    spot_intensity_ = intensity;
}

void RenderDevice::set_point_light(u32 index, Vec3 pos, Vec3 color, f32 radius)
{
    if (index >= kPointLights) {
        return;
    }
    point_pos_[index] = pos;
    point_color_[index] = color;
    point_radius_[index] = radius;
}

void RenderDevice::set_weather(f32 overcast, f32 wetness)
{
    weather_overcast_ = overcast;
    weather_wetness_ = wetness;
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
    ubo.inv_view_proj = inverse(ubo.view_proj);
    ubo.cam_pos = vec4_from_vec3(cam.pos, 1.0f);
    ubo.viewport = Vec4{viewport_.x, viewport_.y, 1.0f / viewport_.x, 1.0f / viewport_.y};
    ubo.sun_dir = vec4_from_vec3(normalize(env_.sun_dir), 0.0f);

    const f32 ambient_scalar = f_clamp01(0.25f + luminance(lighting_.ambient_zenith) * 4.0f);
    ubo.sun_color_ambient = vec4_from_vec3(lighting_.sun_color, ambient_scalar);

    const Vec3 fog_color = lighting_.ambient_horizon;
    ubo.fog_color_density = vec4_from_vec3(fog_color, env_.fog_density);
    ubo.exposure_params = Vec4{env_.exposure, env_.time_of_day,
                               retro_.enabled ? retro_.quantise_bits : 0.0f,
                               retro_.enabled ? retro_.pixel_scale : 1.0f};
    ubo.retro_params = Vec4{retro_.near_distance, retro_.far_distance, 1.0f, time_seconds_};
    ubo.cloud_sun_color = vec4_from_vec3(lighting_.cloud_sun_color, 0.0f);

    const f32 cone = std::cos(24.0f * kDegToRad);
    for (u32 i = 0; i < 2; i++) {
        ubo.spot_pos_cone[i] = vec4_from_vec3(spot_pos_[i], cone);
        ubo.spot_dir_intensity[i] = vec4_from_vec3(spot_dir_, spot_intensity_);
    }
    ubo.shadow_mat = shadow_mat_;
    ubo.shadow_params = Vec4{shadow_strength_, 1.0f / static_cast<f32>(kShadowSize),
                             weather_wetness_, weather_overcast_};
    for (u32 i = 0; i < kPointLights; i++) {
        ubo.point_pos_radius[i] = vec4_from_vec3(point_pos_[i], point_radius_[i]);
        ubo.point_color[i] = vec4_from_vec3(point_color_[i], 0.0f);
    }

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

void RenderDevice::scene_target_ensure(i32 width, i32 height)
{
    if (scene_broken_ || (scene_fbo_ && scene_w_ == width && scene_h_ == height)) {
        return;
    }
    if (scene_fbo_) {
        glDeleteTextures(1, &scene_color_);
        glDeleteTextures(1, &scene_depth_);
        glDeleteTextures(1, &scene_copy_);
        glDeleteFramebuffers(1, &scene_fbo_);
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &scene_color_);
    glTextureStorage2D(scene_color_, 1, GL_RGBA16F, width, height);
    glTextureParameteri(scene_color_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(scene_color_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(scene_color_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(scene_color_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_2D, 1, &scene_copy_);
    glTextureStorage2D(scene_copy_, 1, GL_RGBA16F, width, height);
    glTextureParameteri(scene_copy_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(scene_copy_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(scene_copy_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(scene_copy_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_2D, 1, &scene_depth_);
    glTextureStorage2D(scene_depth_, 1, GL_DEPTH_COMPONENT24, width, height);
    glTextureParameteri(scene_depth_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(scene_depth_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glCreateFramebuffers(1, &scene_fbo_);
    glNamedFramebufferTexture(scene_fbo_, GL_COLOR_ATTACHMENT0, scene_color_, 0);
    glNamedFramebufferTexture(scene_fbo_, GL_DEPTH_ATTACHMENT, scene_depth_, 0);
    if (glCheckNamedFramebufferStatus(scene_fbo_, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log_error("render: scene framebuffer incomplete, post-processing disabled");
        scene_broken_ = true;
        scene_fbo_ = 0;
        return;
    }
    scene_w_ = width;
    scene_h_ = height;

    if (bloom_tex_) {
        glDeleteTextures(1, &bloom_tex_);
        bloom_tex_ = 0;
    }
    if (!bloom_fbo_) {
        glCreateFramebuffers(1, &bloom_fbo_);
    }
    bloom_count_ = 0;
    i32 mw = width / 2 > 1 ? width / 2 : 1;
    i32 mh = height / 2 > 1 ? height / 2 : 1;
    for (i32 i = 0; i < kBloomMips; i++) {
        if (mw < 8 || mh < 8) {
            break;
        }
        bloom_w_[i] = mw;
        bloom_h_[i] = mh;
        bloom_count_++;
        mw /= 2;
        mh /= 2;
    }
    if (bloom_count_ > 0) {
        glCreateTextures(GL_TEXTURE_2D, 1, &bloom_tex_);
        glTextureStorage2D(bloom_tex_, bloom_count_, GL_RGBA16F, bloom_w_[0], bloom_h_[0]);
        glTextureParameteri(bloom_tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        glTextureParameteri(bloom_tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(bloom_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(bloom_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void RenderDevice::begin_frame(const Camera& cam, i32 fb_width, i32 fb_height)
{
    if (fb_width <= 0 || fb_height <= 0) {
        fb_width = 1;
        fb_height = 1;
    }
    stats_ = DrawStats{};
    reset_state_cache();
    scene_target_ensure(fb_width, fb_height);
    if (!scene_broken_ && scene_fbo_) {
        glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    }
    view_setup(cam, static_cast<f32>(fb_width), static_cast<f32>(fb_height));
}

void RenderDevice::end_frame()
{
    use_program(0);
    bind_vao(0);
}

bool RenderDevice::video_begin(const Camera& cam)
{
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
    glBindFramebuffer(GL_FRAMEBUFFER, video_fbo_);
    view_setup(cam, static_cast<f32>(kVideoWidth), static_cast<f32>(kVideoHeight));
    return true;
}

u32 RenderDevice::video_end()
{
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
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shadow_pass_ = false;
    reset_state_cache();
}

void RenderDevice::draw_sky(f32 time)
{
    const u32 program = shaders_.program("sky");
    if (!program) {
        return;
    }
    (void)time;
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

    const u32 program = shaders_.program(shadow_pass_ ? "shadow" : "mesh");
    if (!program) {
        return;
    }
    glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, model.m);
    if (shadow_pass_) {
        glProgramUniformMatrix4fv(program, 4, 1, GL_FALSE, shadow_mat_.m);
    }
    use_program(program);
    bind_vao(mesh->vao);
    set_cull(true);

    for (u32 i = 0; i < mesh->submesh_count; i++) {
        const GpuSubmesh& sub = mesh->submeshes[i];
        if (!shadow_pass_) {
            bind_texture0(assets_.texture_gl(sub.texture_slot));
        }
        stats_.draw_calls++;
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.index_count), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<u64>(sub.first_index)
                                                     * sizeof(u32)));
    }
}

void RenderDevice::draw_glass(const GpuMesh* mesh, const Mat4& model, f32 time)
{
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
    use_program(program);
    bind_vao(mesh->vao);
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

void RenderDevice::scene_grab()
{
    if (scene_broken_ || !scene_fbo_) {
        return;
    }
    glCopyImageSubData(scene_color_, GL_TEXTURE_2D, 0, 0, 0, 0,
                       scene_copy_, GL_TEXTURE_2D, 0, 0, 0, 0,
                       scene_w_, scene_h_, 1);
    glBindTextureUnit(2, scene_copy_);
}

bool RenderDevice::bloom_render()
{
    if (!bloom_count_ || !bloom_tex_) {
        return false;
    }
    const u32 down = shaders_.program("bloom_down");
    const u32 up = shaders_.program("bloom_up");
    if (!down || !up) {
        return false;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo_);
    bind_vao(quad_vao_);
    use_program(down);

    for (i32 i = 0; i < bloom_count_; i++) {
        const i32 sw = i == 0 ? scene_w_ : bloom_w_[i - 1];
        const i32 sh = i == 0 ? scene_h_ : bloom_h_[i - 1];
        glNamedFramebufferTexture(bloom_fbo_, GL_COLOR_ATTACHMENT0, bloom_tex_, i);
        glViewport(0, 0, bloom_w_[i], bloom_h_[i]);
        if (i == 0) {
            bind_texture0(scene_color_);
        } else {
            glTextureParameteri(bloom_tex_, GL_TEXTURE_BASE_LEVEL, i - 1);
            glTextureParameteri(bloom_tex_, GL_TEXTURE_MAX_LEVEL, i - 1);
            bind_texture0(bloom_tex_);
        }
        glProgramUniform4f(down, 1, 1.0f / static_cast<f32>(sw), 1.0f / static_cast<f32>(sh),
                           i == 0 ? 1.0f : 0.0f, 0.0f);
        stats_.draw_calls++;
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    use_program(up);
    for (i32 i = bloom_count_ - 2; i >= 0; i--) {
        glNamedFramebufferTexture(bloom_fbo_, GL_COLOR_ATTACHMENT0, bloom_tex_, i);
        glViewport(0, 0, bloom_w_[i], bloom_h_[i]);
        glTextureParameteri(bloom_tex_, GL_TEXTURE_BASE_LEVEL, i + 1);
        glTextureParameteri(bloom_tex_, GL_TEXTURE_MAX_LEVEL, i + 1);
        state_.texture0 = 0;
        bind_texture0(bloom_tex_);
        glProgramUniform4f(up, 1, 1.0f / static_cast<f32>(bloom_w_[i + 1]),
                           1.0f / static_cast<f32>(bloom_h_[i + 1]), 0.0f, 0.0f);
        stats_.draw_calls++;
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }

    glDisable(GL_BLEND);
    glTextureParameteri(bloom_tex_, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(bloom_tex_, GL_TEXTURE_MAX_LEVEL, bloom_count_ - 1);
    return true;
}

void RenderDevice::post_process(f32 time)
{
    if (scene_broken_ || !scene_fbo_) {
        return;
    }
    const bool bloom_ok = bloom_render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    reset_state_cache();

    const u32 program = shaders_.program("post");
    if (!program) {
        glViewport(0, 0, scene_w_, scene_h_);
        glDisable(GL_DEPTH_TEST);
        blit_texture(0.0f, viewport_.y, viewport_.x, -viewport_.y, scene_color_, 1.0f, time);
        glEnable(GL_DEPTH_TEST);
        return;
    }

    glProgramUniform1f(program, 1, time);
    glProgramUniform4f(program, 2, shield_wet_, shield_wiper_, shield_incar_, shield_rain_);
    glProgramUniform1f(program, 3, bloom_ok ? 1.0f : 0.0f);
    use_program(program);
    bind_texture0(scene_color_);
    glBindTextureUnit(1, scene_depth_);
    if (bloom_ok) {
        glBindTextureUnit(3, bloom_tex_);
    }
    glViewport(0, 0, scene_w_, scene_h_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    draw_quad();
    glDepthFunc(GL_LEQUAL);
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
