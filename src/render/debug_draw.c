#include "render/debug_draw.h"
#include "render/render.h"
#include "render/text.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/gl_loader.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define DD_MAX_LINE_VERTS (1u << 17)
#define DD_MAX_2D_VERTS (1u << 15)
#define DD_MAX_TEXTS_3D 256
#define DD_TEXT_BUF_MAX 256
#define DD_CIRCLE_SEGMENTS 32

typedef struct DdVert {
    Vec3 pos;
    u32 color;
} DdVert;

typedef struct DdVert2 {
    f32 x, y;
    u32 color;
} DdVert2;

typedef struct DdText3d {
    Vec3 pos;
    f32 size;
    u32 color;
    const char* str;
} DdText3d;

static DdVert* s_line_verts;
static u32 s_line_vert_count;
static DdVert2* s_line2d_verts;
static u32 s_line2d_vert_count;
static DdVert2* s_tri2d_verts;
static u32 s_tri2d_vert_count;
static DdText3d s_texts_3d[DD_MAX_TEXTS_3D];
static u32 s_text_3d_count;
static u32 s_vbo;
static u32 s_vao;
static u32 s_vbo_2d[2];
static u32 s_vao_2d[2];
static b32 s_overflow_warned;

b32 dd_init(void)
{
    s_line_verts = arena_push_array(&g_perm_arena, DdVert, DD_MAX_LINE_VERTS);
    s_line2d_verts = arena_push_array(&g_perm_arena, DdVert2, DD_MAX_2D_VERTS);
    s_tri2d_verts = arena_push_array(&g_perm_arena, DdVert2, DD_MAX_2D_VERTS);

    glCreateBuffers(1, &s_vbo);
    glNamedBufferStorage(s_vbo, (GLsizeiptr)(DD_MAX_LINE_VERTS * sizeof(DdVert)), 0, GL_DYNAMIC_STORAGE_BIT);

    glCreateVertexArrays(1, &s_vao);
    glVertexArrayVertexBuffer(s_vao, 0, s_vbo, 0, sizeof(DdVert));
    glEnableVertexArrayAttrib(s_vao, 0);
    glVertexArrayAttribFormat(s_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(DdVert, pos));
    glVertexArrayAttribBinding(s_vao, 0, 0);
    glEnableVertexArrayAttrib(s_vao, 1);
    glVertexArrayAttribFormat(s_vao, 1, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(DdVert, color));
    glVertexArrayAttribBinding(s_vao, 1, 0);

    glCreateBuffers(2, s_vbo_2d);
    glCreateVertexArrays(2, s_vao_2d);
    for (i32 i = 0; i < 2; i++) {
        glNamedBufferStorage(s_vbo_2d[i], (GLsizeiptr)(DD_MAX_2D_VERTS * sizeof(DdVert2)), 0, GL_DYNAMIC_STORAGE_BIT);
        glVertexArrayVertexBuffer(s_vao_2d[i], 0, s_vbo_2d[i], 0, sizeof(DdVert2));
        glEnableVertexArrayAttrib(s_vao_2d[i], 0);
        glVertexArrayAttribFormat(s_vao_2d[i], 0, 2, GL_FLOAT, GL_FALSE, offsetof(DdVert2, x));
        glVertexArrayAttribBinding(s_vao_2d[i], 0, 0);
        glEnableVertexArrayAttrib(s_vao_2d[i], 1);
        glVertexArrayAttribFormat(s_vao_2d[i], 1, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(DdVert2, color));
        glVertexArrayAttribBinding(s_vao_2d[i], 1, 0);
    }
    return 1;
}

void dd_shutdown(void)
{
    glDeleteVertexArrays(1, &s_vao);
    glDeleteBuffers(1, &s_vbo);
    glDeleteVertexArrays(2, s_vao_2d);
    glDeleteBuffers(2, s_vbo_2d);
}

void dd_begin_frame(void)
{
    s_line_vert_count = 0;
    s_line2d_vert_count = 0;
    s_tri2d_vert_count = 0;
    s_text_3d_count = 0;
    s_overflow_warned = 0;
}

void dd_line(Vec3 a, Vec3 b, u32 color)
{
    if (s_line_vert_count + 2 > DD_MAX_LINE_VERTS) {
        if (!s_overflow_warned) {
            log_warn("dd: line vertex budget exceeded (%u)", DD_MAX_LINE_VERTS);
            s_overflow_warned = 1;
        }
        return;
    }
    DdVert* v = &s_line_verts[s_line_vert_count];
    v[0].pos = a;
    v[0].color = color;
    v[1].pos = b;
    v[1].color = color;
    s_line_vert_count += 2;
}

void dd_ray(Vec3 origin, Vec3 dir, f32 length, u32 color)
{
    dd_line(origin, vec3_add(origin, vec3_scale(vec3_normalize(dir), length)), color);
}

static void dd_basis_from_dir(Vec3 dir, Vec3* out_u, Vec3* out_v)
{
    Vec3 ref = f_abs(dir.y) < 0.99f ? v3(0.0f, 1.0f, 0.0f) : v3(1.0f, 0.0f, 0.0f);
    *out_u = vec3_normalize(vec3_cross(dir, ref));
    *out_v = vec3_cross(dir, *out_u);
}

void dd_arrow(Vec3 from, Vec3 to, f32 head_size, u32 color)
{
    dd_line(from, to, color);
    Vec3 dir = vec3_normalize(vec3_sub(to, from));
    if (vec3_length_sq(dir) < 0.5f) {
        return;
    }
    Vec3 u, v;
    dd_basis_from_dir(dir, &u, &v);
    Vec3 back = vec3_sub(to, vec3_scale(dir, head_size));
    f32 spread = head_size * 0.5f;
    dd_line(to, vec3_add(back, vec3_scale(u, spread)), color);
    dd_line(to, vec3_sub(back, vec3_scale(u, spread)), color);
    dd_line(to, vec3_add(back, vec3_scale(v, spread)), color);
    dd_line(to, vec3_sub(back, vec3_scale(v, spread)), color);
}

static void dd_box_edges(const Vec3 corners[8], u32 color)
{
    static const u8 edges[24] = { 0,1, 1,2, 2,3, 3,0, 4,5, 5,6, 6,7, 7,4, 0,4, 1,5, 2,6, 3,7 };
    for (i32 i = 0; i < 24; i += 2) {
        dd_line(corners[edges[i]], corners[edges[i + 1]], color);
    }
}

void dd_aabb(Aabb box, u32 color)
{
    Vec3 lo = box.min;
    Vec3 hi = box.max;
    Vec3 corners[8] = {
        { lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, lo.y, hi.z }, { lo.x, lo.y, hi.z },
        { lo.x, hi.y, lo.z }, { hi.x, hi.y, lo.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z },
    };
    dd_box_edges(corners, color);
}

void dd_obb(Vec3 center, Quat rot, Vec3 half_extents, u32 color)
{
    Mat3 r = quat_to_mat3(rot);
    Vec3 h = half_extents;
    Vec3 local[8] = {
        { -h.x, -h.y, -h.z }, { h.x, -h.y, -h.z }, { h.x, -h.y, h.z }, { -h.x, -h.y, h.z },
        { -h.x, h.y, -h.z }, { h.x, h.y, -h.z }, { h.x, h.y, h.z }, { -h.x, h.y, h.z },
    };
    Vec3 corners[8];
    for (i32 i = 0; i < 8; i++) {
        corners[i] = vec3_add(center, mat3_mul_vec3(r, local[i]));
    }
    dd_box_edges(corners, color);
}

void dd_circle(Vec3 center, Vec3 normal, f32 radius, u32 color)
{
    Vec3 n = vec3_normalize(normal);
    Vec3 u, v;
    dd_basis_from_dir(n, &u, &v);
    Vec3 prev = vec3_add(center, vec3_scale(u, radius));
    for (i32 i = 1; i <= DD_CIRCLE_SEGMENTS; i++) {
        f32 angle = (f32)i / (f32)DD_CIRCLE_SEGMENTS * 2.0f * PI32;
        Vec3 p = vec3_add(center,
                          vec3_add(vec3_scale(u, cosf(angle) * radius),
                                   vec3_scale(v, sinf(angle) * radius)));
        dd_line(prev, p, color);
        prev = p;
    }
}

void dd_sphere(Vec3 center, f32 radius, u32 color)
{
    dd_circle(center, v3(1.0f, 0.0f, 0.0f), radius, color);
    dd_circle(center, v3(0.0f, 1.0f, 0.0f), radius, color);
    dd_circle(center, v3(0.0f, 0.0f, 1.0f), radius, color);
}

void dd_cross(Vec3 p, f32 size, u32 color)
{
    f32 h = size * 0.5f;
    dd_line(v3(p.x - h, p.y, p.z), v3(p.x + h, p.y, p.z), color);
    dd_line(v3(p.x, p.y - h, p.z), v3(p.x, p.y + h, p.z), color);
    dd_line(v3(p.x, p.y, p.z - h), v3(p.x, p.y, p.z + h), color);
}

void dd_grid(Vec3 center, f32 extent, f32 step, u32 color)
{
    for (f32 offset = -extent; offset <= extent + step * 0.5f; offset += step) {
        dd_line(v3(center.x + offset, center.y, center.z - extent),
                v3(center.x + offset, center.y, center.z + extent), color);
        dd_line(v3(center.x - extent, center.y, center.z + offset),
                v3(center.x + extent, center.y, center.z + offset), color);
    }
}

static const char* dd_format(const char* fmt, va_list args)
{
    char* buf = arena_push_array(&g_frame_arena, char, DD_TEXT_BUF_MAX);
    vsnprintf(buf, DD_TEXT_BUF_MAX, fmt, args);
    return buf;
}

void dd_text_3d(Vec3 pos, f32 size, u32 color, const char* fmt, ...)
{
    if (s_text_3d_count >= DD_MAX_TEXTS_3D) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    const char* str = dd_format(fmt, args);
    va_end(args);
    DdText3d* entry = &s_texts_3d[s_text_3d_count++];
    entry->pos = pos;
    entry->size = size;
    entry->color = color;
    entry->str = str;
}

void dd_text_2d(f32 x, f32 y, f32 size, u32 color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const char* str = dd_format(fmt, args);
    va_end(args);
    text_draw(x, y, size, color, str);
}

void dd_line_2d(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    if (s_line2d_vert_count + 2 > DD_MAX_2D_VERTS) {
        return;
    }
    DdVert2* v = &s_line2d_verts[s_line2d_vert_count];
    v[0].x = x0; v[0].y = y0; v[0].color = color;
    v[1].x = x1; v[1].y = y1; v[1].color = color;
    s_line2d_vert_count += 2;
}

void dd_rect_2d(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    dd_line_2d(x0, y0, x1, y0, color);
    dd_line_2d(x1, y0, x1, y1, color);
    dd_line_2d(x1, y1, x0, y1, color);
    dd_line_2d(x0, y1, x0, y0, color);
}

static void dd_rect_2d_write(DdVert2* v, f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    v[0].x = x0; v[0].y = y0;
    v[1].x = x1; v[1].y = y0;
    v[2].x = x1; v[2].y = y1;
    v[3].x = x0; v[3].y = y0;
    v[4].x = x1; v[4].y = y1;
    v[5].x = x0; v[5].y = y1;
    for (i32 i = 0; i < 6; i++) {
        v[i].color = color;
    }
}

void dd_rect_2d_filled(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    if (s_tri2d_vert_count + 6 > DD_MAX_2D_VERTS) {
        return;
    }
    dd_rect_2d_write(&s_tri2d_verts[s_tri2d_vert_count], x0, y0, x1, y1, color);
    s_tri2d_vert_count += 6;
}

u32 dd_rect_2d_reserve(void)
{
    if (s_tri2d_vert_count + 6 > DD_MAX_2D_VERTS) {
        return 0xFFFFFFFFu;
    }
    u32 slot = s_tri2d_vert_count;
    dd_rect_2d_write(&s_tri2d_verts[slot], 0.0f, 0.0f, 0.0f, 0.0f, 0);
    s_tri2d_vert_count += 6;
    return slot;
}

void dd_rect_2d_fill_reserved(u32 slot, f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    if (slot >= s_tri2d_vert_count) {
        return;
    }
    dd_rect_2d_write(&s_tri2d_verts[slot], x0, y0, x1, y1, color);
}

void dd_flush(void)
{
    if (s_line_vert_count) {
        u32 program = r_shader("debug");
        if (program) {
            glNamedBufferSubData(s_vbo, 0, (GLsizeiptr)(s_line_vert_count * sizeof(DdVert)), s_line_verts);
            glUseProgram(program);
            glBindVertexArray(s_vao);
            glDrawArrays(GL_LINES, 0, (GLsizei)s_line_vert_count);
            glBindVertexArray(0);
            glUseProgram(0);
        }
    }

    if (s_tri2d_vert_count || s_line2d_vert_count) {
        u32 program = r_shader("debug2d");
        if (program) {
            glUseProgram(program);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (s_tri2d_vert_count) {
                glNamedBufferSubData(s_vbo_2d[0], 0, (GLsizeiptr)(s_tri2d_vert_count * sizeof(DdVert2)), s_tri2d_verts);
                glBindVertexArray(s_vao_2d[0]);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)s_tri2d_vert_count);
            }
            if (s_line2d_vert_count) {
                glNamedBufferSubData(s_vbo_2d[1], 0, (GLsizeiptr)(s_line2d_vert_count * sizeof(DdVert2)), s_line2d_verts);
                glBindVertexArray(s_vao_2d[1]);
                glDrawArrays(GL_LINES, 0, (GLsizei)s_line2d_vert_count);
            }
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glBindVertexArray(0);
            glUseProgram(0);
        }
    }

    for (u32 i = 0; i < s_text_3d_count; i++) {
        DdText3d* entry = &s_texts_3d[i];
        Vec2 screen;
        if (r_project_to_screen(entry->pos, &screen)) {
            f32 half_width = text_measure(entry->str, entry->size) * 0.5f;
            text_draw(screen.x - half_width, screen.y, entry->size, entry->color, entry->str);
        }
    }
}
