#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

class Arena;
class FontChain;
class RenderDevice;

class TextRenderer {
public:
    static constexpr u32 kAtlasSize = 2048;
    static constexpr f32 kBakePixelSize = 64.0f;
    static constexpr u32 kMaxQuads = 8192;
    static constexpr u32 kHashSize = 4096;
    static constexpr u32 kPad = 2;

    bool init(FontChain& fonts, Arena& storage, Arena& scratch);
    void shutdown();

    void begin_frame();
    void draw(f32 x, f32 y, f32 size, u32 color, std::string_view str);
    f32 measure(std::string_view str, f32 size);
    f32 line_height(f32 size) const;
    void flush(RenderDevice& device);

    u32 quad_count() const { return vert_count_ / 6; }

private:
    struct Glyph {
        u32 cp = 0;
        bool used = false;
        f32 u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        f32 xoff = 0.0f, yoff = 0.0f;
        f32 w = 0.0f, h = 0.0f;
        f32 advance = 0.0f;
    };

    struct Vertex {
        f32 x, y;
        f32 u, v;
        u32 color;
    };

    bool atlas_alloc(u32 w, u32 h, u32& out_x, u32& out_y);
    Glyph* glyph_get(u32 cp);
    void push_vertex(f32 x, f32 y, f32 u, f32 v, u32 color);

    Glyph glyphs_[kHashSize];
    Vertex* verts_ = nullptr;
    u32 vert_count_ = 0;

    FontChain* fonts_ = nullptr;
    Arena* scratch_ = nullptr;

    u32 shelf_x_ = 0;
    u32 shelf_y_ = 0;
    u32 shelf_row_h_ = 0;
    f32 line_advance_ = 0.0f;

    u32 atlas_texture_ = 0;
    u32 vbo_ = 0;
    u32 vao_ = 0;
};

} // namespace anom
