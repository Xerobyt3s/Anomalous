#include "render/debug_draw.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/gl_loader.h"
#include "render/device.h"
#include "render/text.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace anom {
namespace {

void basis_from_dir(Vec3 dir, Vec3& out_u, Vec3& out_v)
{
    const Vec3 ref = f_abs(dir.y) < 0.99f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    out_u = normalize(cross(dir, ref));
    out_v = cross(dir, out_u);
}

} // namespace

bool DebugDraw::init(Arena& storage)
{
    line_verts_ = storage.push_array<Vert>(kMaxLineVerts);
    overlay_verts_ = storage.push_array<Vert>(kMaxOverlayVerts);
    line2d_verts_ = storage.push_array<Vert2>(kMax2dVerts);
    tri2d_verts_ = storage.push_array<Vert2>(kMax2dVerts);
    if (!line_verts_ || !overlay_verts_ || !line2d_verts_ || !tri2d_verts_) {
        return false;
    }

    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, static_cast<GLsizeiptr>(kMaxLineVerts * sizeof(Vert)), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    glCreateVertexArrays(1, &vao_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vert));
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vert, pos));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vert, color));
    glVertexArrayAttribBinding(vao_, 1, 0);

    glCreateBuffers(2, vbo_2d_);
    glCreateVertexArrays(2, vao_2d_);
    for (i32 i = 0; i < 2; i++) {
        glNamedBufferStorage(vbo_2d_[i], static_cast<GLsizeiptr>(kMax2dVerts * sizeof(Vert2)),
                             nullptr, GL_DYNAMIC_STORAGE_BIT);
        glVertexArrayVertexBuffer(vao_2d_[i], 0, vbo_2d_[i], 0, sizeof(Vert2));
        glEnableVertexArrayAttrib(vao_2d_[i], 0);
        glVertexArrayAttribFormat(vao_2d_[i], 0, 2, GL_FLOAT, GL_FALSE, offsetof(Vert2, x));
        glVertexArrayAttribBinding(vao_2d_[i], 0, 0);
        glEnableVertexArrayAttrib(vao_2d_[i], 1);
        glVertexArrayAttribFormat(vao_2d_[i], 1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                                  offsetof(Vert2, color));
        glVertexArrayAttribBinding(vao_2d_[i], 1, 0);
    }
    return true;
}

void DebugDraw::shutdown()
{
    if (vao_) { glDeleteVertexArrays(1, &vao_); }
    if (vbo_) { glDeleteBuffers(1, &vbo_); }
    if (vao_2d_[0]) { glDeleteVertexArrays(2, vao_2d_); }
    if (vbo_2d_[0]) { glDeleteBuffers(2, vbo_2d_); }
    vao_ = vbo_ = 0;
    vao_2d_[0] = vao_2d_[1] = 0;
    vbo_2d_[0] = vbo_2d_[1] = 0;
}

void DebugDraw::begin_frame()
{
    line_vert_count_ = 0;
    overlay_vert_count_ = 0;
    line2d_vert_count_ = 0;
    tri2d_vert_count_ = 0;
    text_3d_count_ = 0;
    overlay_mode_ = false;
    overflow_warned_ = false;
}

void DebugDraw::line(Vec3 a, Vec3 b, u32 color)
{
    Vert* verts = overlay_mode_ ? overlay_verts_ : line_verts_;
    u32& count = overlay_mode_ ? overlay_vert_count_ : line_vert_count_;
    const u32 max = overlay_mode_ ? kMaxOverlayVerts : kMaxLineVerts;
    if (count + 2 > max) {
        if (!overflow_warned_) {
            overflow_warned_ = true;
            log_warn("dd: line vertex budget exceeded (%u)", max);
        }
        return;
    }
    verts[count].pos = a;
    verts[count].color = color;
    verts[count + 1].pos = b;
    verts[count + 1].color = color;
    count += 2;
}

void DebugDraw::ray(Vec3 origin, Vec3 dir, f32 length, u32 color)
{
    line(origin, origin + normalize(dir) * length, color);
}

void DebugDraw::arrow(Vec3 from, Vec3 to, f32 head_size, u32 color)
{
    line(from, to, color);
    const Vec3 dir = normalize(to - from);
    if (length_sq(dir) < 0.5f) {
        return;
    }
    Vec3 u{0.0f, 0.0f, 0.0f};
    Vec3 v{0.0f, 0.0f, 0.0f};
    basis_from_dir(dir, u, v);
    const Vec3 back = to - dir * head_size;
    const f32 spread = head_size * 0.5f;
    line(to, back + u * spread, color);
    line(to, back - u * spread, color);
    line(to, back + v * spread, color);
    line(to, back - v * spread, color);
}

void DebugDraw::box_edges(const Vec3 corners[8], u32 color)
{
    static constexpr u8 edges[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6,
                                     6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
    for (i32 i = 0; i < 24; i += 2) {
        line(corners[edges[i]], corners[edges[i + 1]], color);
    }
}

void DebugDraw::aabb(Aabb box, u32 color)
{
    const Vec3 lo = box.min;
    const Vec3 hi = box.max;
    const Vec3 corners[8] = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
        {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };
    box_edges(corners, color);
}

void DebugDraw::obb(Vec3 center, Quat rot, Vec3 half_extents, u32 color)
{
    const Mat3 r = quat_to_mat3(rot);
    const Vec3 h = half_extents;
    const Vec3 local[8] = {
        {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, -h.y, h.z}, {-h.x, -h.y, h.z},
        {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z},
    };
    Vec3 corners[8];
    for (i32 i = 0; i < 8; i++) {
        corners[i] = center + r * local[i];
    }
    box_edges(corners, color);
}

void DebugDraw::circle(Vec3 center, Vec3 normal, f32 radius, u32 color)
{
    const Vec3 n = normalize(normal);
    Vec3 u{0.0f, 0.0f, 0.0f};
    Vec3 v{0.0f, 0.0f, 0.0f};
    basis_from_dir(n, u, v);

    Vec3 prev = center + u * radius;
    for (i32 i = 1; i <= kCircleSegments; i++) {
        const f32 angle = static_cast<f32>(i) / static_cast<f32>(kCircleSegments) * kTau;
        const Vec3 p = center + u * (std::cos(angle) * radius) + v * (std::sin(angle) * radius);
        line(prev, p, color);
        prev = p;
    }
}

void DebugDraw::sphere(Vec3 center, f32 radius, u32 color)
{
    circle(center, Vec3{1.0f, 0.0f, 0.0f}, radius, color);
    circle(center, Vec3{0.0f, 1.0f, 0.0f}, radius, color);
    circle(center, Vec3{0.0f, 0.0f, 1.0f}, radius, color);
}

void DebugDraw::cross(Vec3 p, f32 size, u32 color)
{
    const f32 h = size * 0.5f;
    line(Vec3{p.x - h, p.y, p.z}, Vec3{p.x + h, p.y, p.z}, color);
    line(Vec3{p.x, p.y - h, p.z}, Vec3{p.x, p.y + h, p.z}, color);
    line(Vec3{p.x, p.y, p.z - h}, Vec3{p.x, p.y, p.z + h}, color);
}

void DebugDraw::grid(Vec3 center, f32 extent, f32 step, u32 color)
{
    for (f32 offset = -extent; offset <= extent + step * 0.5f; offset += step) {
        line(Vec3{center.x + offset, center.y, center.z - extent},
             Vec3{center.x + offset, center.y, center.z + extent}, color);
        line(Vec3{center.x - extent, center.y, center.z + offset},
             Vec3{center.x + extent, center.y, center.z + offset}, color);
    }
}

void DebugDraw::text_3d(Vec3 pos, f32 size, u32 color, const char* fmt, ...)
{
    if (text_3d_count_ >= kMaxTexts3d) {
        return;
    }
    Text3d& entry = texts_3d_[text_3d_count_++];
    entry.pos = pos;
    entry.size = size;
    entry.color = color;

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(entry.str.data(), kTextBufMax, fmt, args);
    va_end(args);
}

void DebugDraw::text_2d(TextRenderer& text, f32 x, f32 y, f32 size, u32 color, const char* fmt,
                        ...)
{
    char buf[kTextBufMax];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    text.draw(x, y, size, color, buf);
}

void DebugDraw::line_2d(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    if (line2d_vert_count_ + 2 > kMax2dVerts) {
        return;
    }
    Vert2* v = &line2d_verts_[line2d_vert_count_];
    v[0].x = x0;
    v[0].y = y0;
    v[0].color = color;
    v[1].x = x1;
    v[1].y = y1;
    v[1].color = color;
    line2d_vert_count_ += 2;
}

void DebugDraw::rect_2d(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    line_2d(x0, y0, x1, y0, color);
    line_2d(x1, y0, x1, y1, color);
    line_2d(x1, y1, x0, y1, color);
    line_2d(x0, y1, x0, y0, color);
}

void DebugDraw::rect_2d_filled(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    if (tri2d_vert_count_ + 6 > kMax2dVerts) {
        return;
    }
    Vert2* v = &tri2d_verts_[tri2d_vert_count_];
    v[0].x = x0; v[0].y = y0;
    v[1].x = x1; v[1].y = y0;
    v[2].x = x1; v[2].y = y1;
    v[3].x = x0; v[3].y = y0;
    v[4].x = x1; v[4].y = y1;
    v[5].x = x0; v[5].y = y1;
    for (i32 i = 0; i < 6; i++) {
        v[i].color = color;
    }
    tri2d_vert_count_ += 6;
}

u32 DebugDraw::rect_2d_reserve()
{
    if (tri2d_vert_count_ + 6 > kMax2dVerts) {
        return kInvalidSlot;
    }
    const u32 slot = tri2d_vert_count_;
    tri2d_vert_count_ += 6;
    rect_2d_fill_reserved(slot, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    return slot;
}

void DebugDraw::rect_2d_fill_reserved(u32 slot, f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    if (slot >= tri2d_vert_count_) {
        return;
    }
    Vert2* v = &tri2d_verts_[slot];
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

void DebugDraw::flush_world(RenderDevice& device)
{
    if (line_vert_count_ || overlay_vert_count_) {
        const u32 program = device.shaders().program("debug");
        if (program) {
            device.use_program(program);
            device.bind_vao(vao_);
            if (line_vert_count_) {
                glNamedBufferSubData(vbo_, 0,
                                     static_cast<GLsizeiptr>(line_vert_count_ * sizeof(Vert)),
                                     line_verts_);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(line_vert_count_));
            }
            if (overlay_vert_count_) {
                glNamedBufferSubData(vbo_, 0,
                                     static_cast<GLsizeiptr>(overlay_vert_count_ * sizeof(Vert)),
                                     overlay_verts_);
                glDisable(GL_DEPTH_TEST);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(overlay_vert_count_));
                glEnable(GL_DEPTH_TEST);
            }

        }
    }

}

void DebugDraw::flush_overlay(RenderDevice& device, TextRenderer& text)
{
    if (tri2d_vert_count_ || line2d_vert_count_) {
        const u32 program = device.shaders().program("debug2d");
        if (program) {
            device.use_program(program);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (tri2d_vert_count_) {
                glNamedBufferSubData(vbo_2d_[0], 0,
                                     static_cast<GLsizeiptr>(tri2d_vert_count_ * sizeof(Vert2)),
                                     tri2d_verts_);
                device.bind_vao(vao_2d_[0]);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tri2d_vert_count_));
            }
            if (line2d_vert_count_) {
                glNamedBufferSubData(vbo_2d_[1], 0,
                                     static_cast<GLsizeiptr>(line2d_vert_count_ * sizeof(Vert2)),
                                     line2d_verts_);
                device.bind_vao(vao_2d_[1]);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(line2d_vert_count_));
            }
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);

        }
    }

    for (u32 i = 0; i < text_3d_count_; i++) {
        const Text3d& entry = texts_3d_[i];
        Vec2 screen{0.0f, 0.0f};
        if (device.project_to_screen(entry.pos, screen)) {
            const f32 half_width = text.measure(entry.str.view(), entry.size) * 0.5f;
            text.draw(screen.x - half_width, screen.y, entry.size, entry.color,
                      entry.str.view());
        }
    }
}

} // namespace anom
