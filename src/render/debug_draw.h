#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class Arena;
class RenderDevice;
class TextRenderer;

inline constexpr u32 kDdWhite = 0xFFFFFFFFu;
inline constexpr u32 kDdGray = 0xFF808080u;
inline constexpr u32 kDdDark = 0xFF404040u;
inline constexpr u32 kDdRed = 0xFF3030F0u;
inline constexpr u32 kDdGreen = 0xFF30F030u;
inline constexpr u32 kDdBlue = 0xFFF06030u;
inline constexpr u32 kDdYellow = 0xFF30E0F0u;
inline constexpr u32 kDdCyan = 0xFFF0E030u;
inline constexpr u32 kDdMagenta = 0xFFF030F0u;
inline constexpr u32 kDdOrange = 0xFF3090F0u;

constexpr u32 dd_rgba(u8 r, u8 g, u8 b, u8 a)
{
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) | (static_cast<u32>(b) << 16)
         | (static_cast<u32>(a) << 24);
}

class DebugDraw {
public:
    static constexpr u32 kMaxLineVerts = 1u << 17;
    static constexpr u32 kMaxOverlayVerts = 1u << 12;
    static constexpr u32 kMax2dVerts = 1u << 15;
    static constexpr u32 kMaxTexts3d = 256;
    static constexpr u32 kTextBufMax = 128;
    static constexpr i32 kCircleSegments = 32;
    static constexpr u32 kInvalidSlot = 0xFFFFFFFFu;

    bool init(Arena& storage);
    void shutdown();

    void begin_frame();
    void overlay(bool enable) { overlay_mode_ = enable; }

    void line(Vec3 a, Vec3 b, u32 color);
    void ray(Vec3 origin, Vec3 dir, f32 length, u32 color);
    void arrow(Vec3 from, Vec3 to, f32 head_size, u32 color);
    void aabb(Aabb box, u32 color);
    void obb(Vec3 center, Quat rot, Vec3 half_extents, u32 color);
    void sphere(Vec3 center, f32 radius, u32 color);
    void circle(Vec3 center, Vec3 normal, f32 radius, u32 color);
    void cross(Vec3 p, f32 size, u32 color);
    void grid(Vec3 center, f32 extent, f32 step, u32 color);

    void text_3d(Vec3 pos, f32 size, u32 color, const char* fmt, ...);
    void text_2d(TextRenderer& text, f32 x, f32 y, f32 size, u32 color, const char* fmt, ...);

    void line_2d(f32 x0, f32 y0, f32 x1, f32 y1, u32 color);
    void rect_2d(f32 x0, f32 y0, f32 x1, f32 y1, u32 color);
    void rect_2d_filled(f32 x0, f32 y0, f32 x1, f32 y1, u32 color);
    u32 rect_2d_reserve();
    void rect_2d_fill_reserved(u32 slot, f32 x0, f32 y0, f32 x1, f32 y1, u32 color);

    void flush_world(RenderDevice& device);
    void flush_overlay(RenderDevice& device, TextRenderer& text);

    u32 line_vert_count() const { return line_vert_count_; }

private:
    struct Vert {
        Vec3 pos;
        u32 color;
    };

    struct Vert2 {
        f32 x, y;
        u32 color;
    };

    struct Text3d {
        Vec3 pos;
        f32 size;
        u32 color;
        FixedString<kTextBufMax> str;
    };

    void box_edges(const Vec3 corners[8], u32 color);

    Vert* line_verts_ = nullptr;
    Vert* overlay_verts_ = nullptr;
    Vert2* line2d_verts_ = nullptr;
    Vert2* tri2d_verts_ = nullptr;
    Text3d texts_3d_[kMaxTexts3d];

    u32 line_vert_count_ = 0;
    u32 overlay_vert_count_ = 0;
    u32 line2d_vert_count_ = 0;
    u32 tri2d_vert_count_ = 0;
    u32 text_3d_count_ = 0;

    bool overlay_mode_ = false;
    bool overflow_warned_ = false;

    u32 vbo_ = 0;
    u32 vao_ = 0;
    u32 vbo_2d_[2] = {};
    u32 vao_2d_[2] = {};
};

} // namespace anom
