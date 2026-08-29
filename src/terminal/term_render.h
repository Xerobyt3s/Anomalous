#pragma once

#include "terminal/screen.h"
#include "terminal/term_font.h"
#include "terminal/term_view.h"

namespace anom {

class Arena;
class DiskStore;
class FontChain;
class RenderDevice;

class TermRenderer {
public:
    bool init(FontChain& fonts, Arena& scratch);
    void shutdown();

    void clear_persistence() { clear_pending_ = true; }
    void render(RenderDevice& device, const DiskStore& disks, const Screen& screen,
                const TermScene& scene, f32 time);

    u32 texture() const { return persist_tex_[persist_idx_]; }

private:
    struct GlyphInst {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 glyph = 0.0f;
        f32 color = 0.0f;
        f32 w = 0.0f;
    };

    u32 build_glyphs(const Screen& screen);
    void draw_points(RenderDevice& device, const TermScene& scene, f32 time);
    void draw_photo(RenderDevice& device, const DiskStore& disks, const TermScene& scene);
    void draw_video(RenderDevice& device, const TermScene& scene);
    void draw_wires(RenderDevice& device, const TermScene& scene);

    TermFont font_;
    GlyphInst glyphs_[kTermRows * kTermCols * 2];

    u32 fbo_ = 0;
    u32 color_tex_ = 0;
    u32 depth_tex_ = 0;
    u32 persist_fbo_[2] = {};
    u32 persist_tex_[2] = {};
    u32 persist_idx_ = 0;
    u32 text_vao_ = 0;
    u32 text_vbo_ = 0;
    u32 point_vao_ = 0;
    u32 point_vbo_ = 0;
    u32 empty_vao_ = 0;
    u32 pic_tex_ = 0;
    i32 pic_uploaded_ = -1;
    bool clear_pending_ = true;
    bool ready_ = false;
};

} // namespace anom
