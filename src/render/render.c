#include "render/render.h"
#include "render/debug_draw.h"
#include "render/text.h"
#include "assets/assets.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/platform.h"
#include "platform/gl_loader.h"

#include <stdio.h>
#include <string.h>

#define MAX_SHADERS 16
#define SHADER_PATH_MAX 128
#define HOT_RELOAD_INTERVAL 1.0

typedef struct ShaderEntry {
    char name[32];
    char vert_path[SHADER_PATH_MAX];
    char frag_path[SHADER_PATH_MAX];
    u32 program;
    i64 vert_mtime;
    i64 frag_mtime;
    b32 used;
} ShaderEntry;

typedef struct CameraUbo {
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
} CameraUbo;

static ShaderEntry s_shaders[MAX_SHADERS];
static u32 s_camera_ubo;
static Mat4 s_view_proj;
static Vec3 s_cam_pos;
static Vec2 s_viewport;
static Frustum s_frustum;
static Vec3 s_sun_dir = { -0.45f, -0.8f, -0.35f };
static Vec3 s_sun_color = { 1.0f, 0.95f, 0.85f };
static f32 s_ambient = 0.38f;
static Vec3 s_fog_color = { 0.62f, 0.68f, 0.76f };
static f32 s_fog_density = 0.0028f;
static f64 s_next_poll_time;
static Vec3 s_spot_pos[2];
static Vec3 s_spot_dir = { 0.0f, 0.0f, -1.0f };
static f32 s_spot_intensity;

static u32 shader_compile_stage(GLenum type, const char* path)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData src = platform_read_entire_file(&g_frame_arena, path);
    u32 shader = 0;
    if (src.data) {
        shader = glCreateShader(type);
        const GLchar* text = (const GLchar*)src.data;
        GLint length = (GLint)src.size;
        glShaderSource(shader, 1, &text, &length);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char info[2048];
            GLsizei info_len = 0;
            glGetShaderInfoLog(shader, sizeof(info), &info_len, info);
            log_error("shader compile failed %s: %.*s", path, (int)info_len, info);
            glDeleteShader(shader);
            shader = 0;
        }
    }
    arena_temp_end(temp);
    return shader;
}

static u32 shader_build(ShaderEntry* entry)
{
    u32 vs = shader_compile_stage(GL_VERTEX_SHADER, entry->vert_path);
    u32 fs = shader_compile_stage(GL_FRAGMENT_SHADER, entry->frag_path);
    if (!vs || !fs) {
        if (vs) { glDeleteShader(vs); }
        if (fs) { glDeleteShader(fs); }
        return 0;
    }
    u32 program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[2048];
        GLsizei info_len = 0;
        glGetProgramInfoLog(program, sizeof(info), &info_len, info);
        log_error("shader link failed %s: %.*s", entry->name, (int)info_len, info);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static u32 s_quad_vao;
static u32 s_quad_vbo;
static u32 s_quad_ebo;
static f32 s_screen_power;
static f32 s_screen_burn;
static f32 s_screen_shake;
static f32 s_screen_pixelate = 1.0f;

static void quad_init(void)
{
    f32 verts[4][8] = {
        { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
        { 0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
        { 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f },
        { -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
    };
    u32 indices[6] = { 0, 1, 2, 0, 2, 3 };
    glCreateBuffers(1, &s_quad_vbo);
    glNamedBufferStorage(s_quad_vbo, sizeof(verts), verts, 0);
    glCreateBuffers(1, &s_quad_ebo);
    glNamedBufferStorage(s_quad_ebo, sizeof(indices), indices, 0);
    glCreateVertexArrays(1, &s_quad_vao);
    glVertexArrayVertexBuffer(s_quad_vao, 0, s_quad_vbo, 0, 8 * sizeof(f32));
    glVertexArrayElementBuffer(s_quad_vao, s_quad_ebo);
    for (u32 i = 0; i < 3; i++) {
        glEnableVertexArrayAttrib(s_quad_vao, i);
        glVertexArrayAttribFormat(s_quad_vao, i, i == 2 ? 2 : 3, GL_FLOAT, GL_FALSE,
                                  (i == 0 ? 0 : (i == 1 ? 3 : 6)) * sizeof(f32));
        glVertexArrayAttribBinding(s_quad_vao, i, 0);
    }
}

b32 r_init(void)
{
    glCreateBuffers(1, &s_camera_ubo);
    glNamedBufferStorage(s_camera_ubo, sizeof(CameraUbo), 0, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_camera_ubo);
    quad_init();
    return 1;
}

void r_blit_texture(f32 x, f32 y, f32 w, f32 h, u32 gl_texture, f32 alpha)
{
    u32 program = r_shader("blit");
    if (!program) {
        return;
    }
    f32 x0 = x / s_viewport.x * 2.0f - 1.0f;
    f32 x1 = (x + w) / s_viewport.x * 2.0f - 1.0f;
    f32 y1 = 1.0f - y / s_viewport.y * 2.0f;
    f32 y0 = 1.0f - (y + h) / s_viewport.y * 2.0f;
    glProgramUniform4f(program, 0, x0, y0, x1, y1);
    glProgramUniform1f(program, 1, (f32)platform_time_now());
    glProgramUniform1f(program, 2, alpha);
    glProgramUniform1f(program, 3, s_screen_power);
    glProgramUniform1f(program, 4, s_screen_burn);
    glProgramUniform1f(program, 5, s_screen_shake);
    glProgramUniform1f(program, 6, s_screen_pixelate);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(program);
    glBindVertexArray(s_quad_vao);
    glBindTextureUnit(0, gl_texture);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void r_draw_lit_quad(Mat4 model, u32 gl_texture)
{
    u32 program = r_shader("screen");
    if (!program) {
        return;
    }
    glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, model.m);
    glProgramUniform1f(program, 4, (f32)platform_time_now());
    glProgramUniform1f(program, 5, s_screen_power);
    glProgramUniform1f(program, 6, s_screen_burn);
    glProgramUniform1f(program, 7, s_screen_shake);
    glProgramUniform1f(program, 8, s_screen_pixelate);
    glUseProgram(program);
    glBindVertexArray(s_quad_vao);
    glBindTextureUnit(0, gl_texture);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

void r_set_screen_fx(f32 power_seconds, f32 burn, f32 shake, f32 pixelate)
{
    s_screen_power = power_seconds;
    s_screen_burn = burn;
    s_screen_shake = shake;
    s_screen_pixelate = pixelate;
}

void r_shutdown(void)
{
    for (i32 i = 0; i < MAX_SHADERS; i++) {
        if (s_shaders[i].used && s_shaders[i].program) {
            glDeleteProgram(s_shaders[i].program);
        }
    }
    glDeleteBuffers(1, &s_camera_ubo);
}

u32 r_shader(const char* name)
{
    ShaderEntry* free_slot = 0;
    for (i32 i = 0; i < MAX_SHADERS; i++) {
        ShaderEntry* entry = &s_shaders[i];
        if (entry->used && strcmp(entry->name, name) == 0) {
            return entry->program;
        }
        if (!entry->used && !free_slot) {
            free_slot = entry;
        }
    }
    ASSERT(free_slot);
    if (!free_slot) {
        return 0;
    }
    ShaderEntry* entry = free_slot;
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->vert_path, sizeof(entry->vert_path), "shaders/%s.vert", name);
    snprintf(entry->frag_path, sizeof(entry->frag_path), "shaders/%s.frag", name);
    entry->vert_mtime = platform_file_mtime(entry->vert_path);
    entry->frag_mtime = platform_file_mtime(entry->frag_path);
    entry->program = shader_build(entry);
    entry->used = 1;
    if (entry->program) {
        log_info("shader loaded: %s", name);
    }
    return entry->program;
}

void r_hot_reload_poll(f64 now)
{
    if (now < s_next_poll_time) {
        return;
    }
    s_next_poll_time = now + HOT_RELOAD_INTERVAL;
    for (i32 i = 0; i < MAX_SHADERS; i++) {
        ShaderEntry* entry = &s_shaders[i];
        if (!entry->used) {
            continue;
        }
        i64 vert_mtime = platform_file_mtime(entry->vert_path);
        i64 frag_mtime = platform_file_mtime(entry->frag_path);
        if (vert_mtime == entry->vert_mtime && frag_mtime == entry->frag_mtime) {
            continue;
        }
        entry->vert_mtime = vert_mtime;
        entry->frag_mtime = frag_mtime;
        u32 program = shader_build(entry);
        if (program) {
            if (entry->program) {
                glDeleteProgram(entry->program);
            }
            entry->program = program;
            log_info("shader reloaded: %s", entry->name);
        } else {
            log_warn("shader reload failed, keeping previous: %s", entry->name);
        }
    }
}

void r_set_headlights(Vec3 pos_left, Vec3 pos_right, Vec3 dir, f32 intensity)
{
    s_spot_pos[0] = pos_left;
    s_spot_pos[1] = pos_right;
    s_spot_dir = vec3_normalize(dir);
    s_spot_intensity = intensity;
}

void r_set_environment(Vec3 sun_dir, Vec3 sun_color, f32 ambient, Vec3 fog_color, f32 fog_density)
{
    s_sun_dir = vec3_normalize(sun_dir);
    s_sun_color = sun_color;
    s_ambient = ambient;
    s_fog_color = fog_color;
    s_fog_density = fog_density;
}

static void view_setup(const Camera* cam, f32 width, f32 height)
{
    s_viewport = v2(width, height);
    f32 aspect = width / height;

    CameraUbo ubo;
    ubo.view = camera_view(cam);
    ubo.proj = camera_proj(cam, aspect);
    ubo.view_proj = mat4_mul(ubo.proj, ubo.view);
    ubo.cam_pos = vec4_from_vec3(cam->pos, 1.0f);
    ubo.viewport = v4(s_viewport.x, s_viewport.y, 1.0f / s_viewport.x, 1.0f / s_viewport.y);
    ubo.sun_dir = vec4_from_vec3(s_sun_dir, 0.0f);
    ubo.sun_color_ambient = vec4_from_vec3(s_sun_color, s_ambient);
    ubo.fog_color_density = vec4_from_vec3(s_fog_color, s_fog_density);
    f32 cone = cosf(21.0f * DEG_TO_RAD);
    for (u32 i = 0; i < 2; i++) {
        ubo.spot_pos_cone[i] = vec4_from_vec3(s_spot_pos[i], cone);
        ubo.spot_dir_intensity[i] = vec4_from_vec3(s_spot_dir, s_spot_intensity);
    }
    glNamedBufferSubData(s_camera_ubo, 0, sizeof(CameraUbo), &ubo);

    s_view_proj = ubo.view_proj;
    s_cam_pos = cam->pos;
    s_frustum = frustum_from_view_proj(s_view_proj);

    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glClearColor(s_fog_color.x, s_fog_color.y, s_fog_color.z, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void r_begin_frame(const Camera* cam)
{
    i32 width, height;
    platform_framebuffer_size(&width, &height);
    if (width <= 0 || height <= 0) {
        width = 1;
        height = 1;
    }
    view_setup(cam, (f32)width, (f32)height);
}

#define VIDEO_W 320
#define VIDEO_H 200

static u32 s_video_fbo;
static u32 s_video_tex;
static u32 s_video_depth;

b32 r_video_begin(const Camera* cam)
{
    if (!s_video_fbo) {
        glCreateTextures(GL_TEXTURE_2D, 1, &s_video_tex);
        glTextureStorage2D(s_video_tex, 1, GL_RGBA8, VIDEO_W, VIDEO_H);
        glTextureParameteri(s_video_tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(s_video_tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(s_video_tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(s_video_tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCreateTextures(GL_TEXTURE_2D, 1, &s_video_depth);
        glTextureStorage2D(s_video_depth, 1, GL_DEPTH_COMPONENT24, VIDEO_W, VIDEO_H);
        glCreateFramebuffers(1, &s_video_fbo);
        glNamedFramebufferTexture(s_video_fbo, GL_COLOR_ATTACHMENT0, s_video_tex, 0);
        glNamedFramebufferTexture(s_video_fbo, GL_DEPTH_ATTACHMENT, s_video_depth, 0);
        if (glCheckNamedFramebufferStatus(s_video_fbo, GL_FRAMEBUFFER)
            != GL_FRAMEBUFFER_COMPLETE) {
            log_error("render: video framebuffer incomplete");
            s_video_fbo = 0;
            return 0;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s_video_fbo);
    view_setup(cam, (f32)VIDEO_W, (f32)VIDEO_H);
    return 1;
}

u32 r_video_end(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return s_video_tex;
}

void r_end_frame(void)
{
    dd_flush();
    text_flush();
}

Mat4 r_view_proj(void)
{
    return s_view_proj;
}

Vec3 r_camera_pos(void)
{
    return s_cam_pos;
}

Vec2 r_viewport_size(void)
{
    return s_viewport;
}

b32 r_read_backbuffer_rgb(u8* out, i32 out_w, i32 out_h)
{
    i32 w = (i32)s_viewport.x;
    i32 h = (i32)s_viewport.y;
    if (w <= 0 || h <= 0 || out_w <= 0 || out_h <= 0) {
        return 0;
    }
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    u8* rgba = arena_push_array(&g_frame_arena, u8, (u64)w * (u64)h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (i32 oy = 0; oy < out_h; oy++) {
        i32 sy0 = oy * h / out_h;
        i32 sy1 = (oy + 1) * h / out_h;
        if (sy1 <= sy0) {
            sy1 = sy0 + 1;
        }
        for (i32 ox = 0; ox < out_w; ox++) {
            i32 sx0 = ox * w / out_w;
            i32 sx1 = (ox + 1) * w / out_w;
            if (sx1 <= sx0) {
                sx1 = sx0 + 1;
            }
            u32 sum[3] = { 0, 0, 0 };
            for (i32 sy = sy0; sy < sy1; sy++) {
                const u8* row = &rgba[(u64)(h - 1 - sy) * (u64)w * 4];
                for (i32 sx = sx0; sx < sx1; sx++) {
                    const u8* p = &row[(u64)sx * 4];
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                }
            }
            u32 n = (u32)((sx1 - sx0) * (sy1 - sy0));
            u8* dst = &out[((u64)oy * (u64)out_w + (u64)ox) * 3];
            dst[0] = (u8)(sum[0] / n);
            dst[1] = (u8)(sum[1] / n);
            dst[2] = (u8)(sum[2] / n);
        }
    }
    arena_temp_end(temp);
    return 1;
}

const Frustum* r_frustum(void)
{
    return &s_frustum;
}

void r_draw_mesh(const struct GpuMesh* mesh, Mat4 model)
{
    if (!mesh || !mesh->loaded) {
        return;
    }
    Aabb world_bounds = aabb_transform(model, mesh->bounds);
    if (!frustum_test_aabb(&s_frustum, world_bounds)) {
        return;
    }
    u32 program = r_shader("mesh");
    if (!program) {
        return;
    }
    glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, model.m);
    glUseProgram(program);
    glBindVertexArray(mesh->vao);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    for (u32 i = 0; i < mesh->submesh_count; i++) {
        const GpuSubmesh* sub = &mesh->submeshes[i];
        glBindTextureUnit(0, asset_texture_gl(sub->texture_slot));
        glDrawElements(GL_TRIANGLES, (GLsizei)sub->index_count, GL_UNSIGNED_INT,
                       (const void*)((u64)sub->first_index * sizeof(u32)));
    }
    glDisable(GL_CULL_FACE);
    glBindVertexArray(0);
    glUseProgram(0);
}

b32 r_project_to_screen(Vec3 world, Vec2* out_screen)
{
    const f32* m = s_view_proj.m;
    f32 cx = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
    f32 cy = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
    f32 cw = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
    if (cw < 1e-4f) {
        return 0;
    }
    f32 inv_w = 1.0f / cw;
    out_screen->x = (cx * inv_w * 0.5f + 0.5f) * s_viewport.x;
    out_screen->y = (0.5f - cy * inv_w * 0.5f) * s_viewport.y;
    return 1;
}
