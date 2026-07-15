#include "render/render.h"
#include "render/debug_draw.h"
#include "render/text.h"
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
} CameraUbo;

static ShaderEntry s_shaders[MAX_SHADERS];
static u32 s_camera_ubo;
static Mat4 s_view_proj;
static Vec3 s_cam_pos;
static Vec2 s_viewport;
static f64 s_next_poll_time;

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

b32 r_init(void)
{
    glCreateBuffers(1, &s_camera_ubo);
    glNamedBufferStorage(s_camera_ubo, sizeof(CameraUbo), 0, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_camera_ubo);
    return 1;
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

void r_begin_frame(const Camera* cam)
{
    i32 width, height;
    platform_framebuffer_size(&width, &height);
    if (width <= 0 || height <= 0) {
        width = 1;
        height = 1;
    }
    s_viewport = v2((f32)width, (f32)height);
    f32 aspect = (f32)width / (f32)height;

    CameraUbo ubo;
    ubo.view = camera_view(cam);
    ubo.proj = camera_proj(cam, aspect);
    ubo.view_proj = mat4_mul(ubo.proj, ubo.view);
    ubo.cam_pos = vec4_from_vec3(cam->pos, 1.0f);
    ubo.viewport = v4(s_viewport.x, s_viewport.y, 1.0f / s_viewport.x, 1.0f / s_viewport.y);
    glNamedBufferSubData(s_camera_ubo, 0, sizeof(CameraUbo), &ubo);

    s_view_proj = ubo.view_proj;
    s_cam_pos = cam->pos;

    glViewport(0, 0, width, height);
    glClearColor(0.03f, 0.05f, 0.07f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
