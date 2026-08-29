#include "render/text.h"
#include "core/arena.h"
#include "core/log.h"
#include "core/utf8.h"
#include "platform/gl_loader.h"
#include "render/device.h"
#include "render/fontchain.h"

#include <cstddef>

namespace anom {

bool TextRenderer::init(FontChain& fonts, Arena& storage, Arena& scratch)
{
    fonts_ = &fonts;
    scratch_ = &scratch;

    StbttScratch binding(scratch);
    const stbtt_fontinfo* face = fonts.face(0);
    if (!face) {
        log_error("text: fontchain has no faces");
        return false;
    }

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(face, &ascent, &descent, &line_gap);
    const f32 scale = stbtt_ScaleForPixelHeight(face, kBakePixelSize);
    line_advance_ = static_cast<f32>(ascent - descent + line_gap) * scale;

    glCreateTextures(GL_TEXTURE_2D, 1, &atlas_texture_);
    glTextureStorage2D(atlas_texture_, 1, GL_R8, kAtlasSize, kAtlasSize);
    glTextureParameteri(atlas_texture_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(atlas_texture_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(atlas_texture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(atlas_texture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    verts_ = storage.push_array<Vertex>(static_cast<u64>(kMaxQuads) * 6);
    if (!verts_) {
        return false;
    }

    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, static_cast<GLsizeiptr>(kMaxQuads * 6 * sizeof(Vertex)), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    glCreateVertexArrays(1, &vao_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, x));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, u));
    glVertexArrayAttribBinding(vao_, 1, 0);
    glEnableVertexArrayAttrib(vao_, 2);
    glVertexArrayAttribFormat(vao_, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, color));
    glVertexArrayAttribBinding(vao_, 2, 0);

    log_info("text: dynamic atlas %upx ready", kAtlasSize);
    return true;
}

void TextRenderer::shutdown()
{
    if (vao_) { glDeleteVertexArrays(1, &vao_); }
    if (vbo_) { glDeleteBuffers(1, &vbo_); }
    if (atlas_texture_) { glDeleteTextures(1, &atlas_texture_); }
    vao_ = vbo_ = atlas_texture_ = 0;
}

void TextRenderer::begin_frame()
{
    vert_count_ = 0;
}

bool TextRenderer::atlas_alloc(u32 w, u32 h, u32& out_x, u32& out_y)
{
    if (shelf_x_ + w + kPad > kAtlasSize) {
        shelf_x_ = 0;
        shelf_y_ += shelf_row_h_ + kPad;
        shelf_row_h_ = 0;
    }
    if (shelf_y_ + h + kPad > kAtlasSize) {
        return false;
    }
    out_x = shelf_x_;
    out_y = shelf_y_;
    shelf_x_ += w + kPad;
    if (h > shelf_row_h_) {
        shelf_row_h_ = h;
    }
    return true;
}

TextRenderer::Glyph* TextRenderer::glyph_get(u32 cp)
{
    const u32 hash = (cp * 2654435761u) & (kHashSize - 1);
    Glyph* entry = nullptr;
    for (u32 probe = 0; probe < kHashSize; probe++) {
        Glyph& candidate = glyphs_[(hash + probe) & (kHashSize - 1)];
        if (candidate.used && candidate.cp == cp) {
            return &candidate;
        }
        if (!candidate.used) {
            entry = &candidate;
            break;
        }
    }
    if (!entry) {
        return nullptr;
    }
    entry->used = true;
    entry->cp = cp;

    i32 glyph = 0;
    i32 face_index = fonts_->find(cp, &glyph);
    if (face_index < 0) {
        face_index = fonts_->find(0xFFFDu, &glyph);
    }
    if (face_index < 0) {
        face_index = fonts_->find('?', &glyph);
    }

    const stbtt_fontinfo* face = fonts_->face(face_index);
    if (!face || !glyph) {
        entry->advance = kBakePixelSize * 0.5f;
        return entry;
    }

    const f32 scale = stbtt_ScaleForPixelHeight(face, kBakePixelSize);
    int adv = 0;
    int lsb = 0;
    stbtt_GetGlyphHMetrics(face, glyph, &adv, &lsb);
    entry->advance = static_cast<f32>(adv) * scale;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphBitmapBox(face, glyph, scale, scale, &x0, &y0, &x1, &y1);
    const i32 gw = x1 - x0;
    const i32 gh = y1 - y0;
    if (gw <= 0 || gh <= 0) {
        return entry;
    }

    u32 ax = 0;
    u32 ay = 0;
    if (!atlas_alloc(static_cast<u32>(gw), static_cast<u32>(gh), ax, ay)) {
        log_warn("text: glyph atlas full at cp %u", cp);
        return entry;
    }

    ArenaScope scope(*scratch_);
    StbttScratch binding(*scratch_);
    u8* raster = scratch_->push_array<u8>(static_cast<u64>(gw) * gh);
    if (!raster) {
        return entry;
    }
    stbtt_MakeGlyphBitmap(face, raster, gw, gh, gw, scale, scale, glyph);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(atlas_texture_, 0, static_cast<GLint>(ax), static_cast<GLint>(ay),
                        gw, gh, GL_RED, GL_UNSIGNED_BYTE, raster);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    entry->u0 = static_cast<f32>(ax) / kAtlasSize;
    entry->v0 = static_cast<f32>(ay) / kAtlasSize;
    entry->u1 = static_cast<f32>(ax + static_cast<u32>(gw)) / kAtlasSize;
    entry->v1 = static_cast<f32>(ay + static_cast<u32>(gh)) / kAtlasSize;
    entry->xoff = static_cast<f32>(x0);
    entry->yoff = static_cast<f32>(y0);
    entry->w = static_cast<f32>(gw);
    entry->h = static_cast<f32>(gh);
    return entry;
}

void TextRenderer::push_vertex(f32 x, f32 y, f32 u, f32 v, u32 color)
{
    Vertex& vertex = verts_[vert_count_++];
    vertex.x = x;
    vertex.y = y;
    vertex.u = u;
    vertex.v = v;
    vertex.color = color;
}

void TextRenderer::draw(f32 x, f32 y, f32 size, u32 color, std::string_view str)
{
    const f32 scale = size / kBakePixelSize;
    f32 pen_x = 0.0f;

    std::string_view cursor = str;
    while (u32 cp = utf8_next(cursor)) {
        if (cp < 32) {
            continue;
        }
        Glyph* g = glyph_get(cp);
        if (!g) {
            continue;
        }
        if (g->w > 0.0f && g->h > 0.0f) {
            if (vert_count_ + 6 > kMaxQuads * 6) {
                return;
            }
            const f32 x0 = x + (pen_x + g->xoff) * scale;
            const f32 y0 = y + g->yoff * scale;
            const f32 x1 = x0 + g->w * scale;
            const f32 y1 = y0 + g->h * scale;
            push_vertex(x0, y0, g->u0, g->v0, color);
            push_vertex(x1, y0, g->u1, g->v0, color);
            push_vertex(x1, y1, g->u1, g->v1, color);
            push_vertex(x0, y0, g->u0, g->v0, color);
            push_vertex(x1, y1, g->u1, g->v1, color);
            push_vertex(x0, y1, g->u0, g->v1, color);
        }
        pen_x += g->advance;
    }
}

f32 TextRenderer::measure(std::string_view str, f32 size)
{
    f32 pen_x = 0.0f;
    std::string_view cursor = str;
    while (u32 cp = utf8_next(cursor)) {
        if (cp < 32) {
            continue;
        }
        if (const Glyph* g = glyph_get(cp)) {
            pen_x += g->advance;
        }
    }
    return pen_x * (size / kBakePixelSize);
}

f32 TextRenderer::line_height(f32 size) const
{
    return line_advance_ * (size / kBakePixelSize);
}

void TextRenderer::flush(RenderDevice& device)
{
    if (!vert_count_) {
        return;
    }
    const u32 program = device.shaders().program("text");
    if (!program) {
        return;
    }

    glNamedBufferSubData(vbo_, 0, static_cast<GLsizeiptr>(vert_count_ * sizeof(Vertex)), verts_);
    device.use_program(program);
    device.bind_vao(vao_);
    device.bind_texture0(atlas_texture_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vert_count_));
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace anom
