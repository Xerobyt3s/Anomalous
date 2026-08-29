#include "terminal/term_font.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/gl_loader.h"
#include "render/fontchain.h"

#include <cstring>

namespace anom {
namespace {

constexpr u32 kBuiltinCell = 8;
constexpr u32 kAtlasPixelW = kTermFontAtlasCols * kTermFontSlotW;
constexpr u32 kAtlasPixelH = kTermFontAtlasRows * kTermFontSlotH;

struct TermGlyph {
    char code;
    const char* rows[8];
};

#include "terminal/content/term_glyphs.inc"

static_assert(array_count(kTermGlyphs) == kTermFontBuiltinCount);

} // namespace

bool term_font_cp_wide(u32 cp)
{
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF)
        || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF)
        || (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6);
}

void TermFont::upload(u32 slot, const u8* pixels) const
{
    const i32 x = static_cast<i32>(slot % kTermFontAtlasCols) * kTermFontSlotW;
    const i32 y = static_cast<i32>(slot / kTermFontAtlasCols) * kTermFontSlotH;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(atlas_, 0, x, y, kTermFontSlotW, kTermFontSlotH, GL_RED,
                        GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void TermFont::bake_builtin() const
{
    u8 pixels[kTermFontSlotW * kTermFontSlotH];
    for (const TermGlyph& glyph : kTermGlyphs) {
        std::memset(pixels, 0, sizeof(pixels));
        for (u32 row = 0; row < kBuiltinCell; row++) {
            const char* bits = glyph.rows[row];
            for (u32 col = 0; col < kBuiltinCell; col++) {
                if (bits[col] == '#') {
                    pixels[(row + 2) * kTermFontSlotW + col] = 255;
                }
            }
        }
        upload(static_cast<u32>(static_cast<u8>(glyph.code)) - 32, pixels);
    }
}

void TermFont::bake_missing_box(u32 slot) const
{
    u8 pixels[kTermFontSlotW * kTermFontSlotH];
    std::memset(pixels, 0, sizeof(pixels));
    for (u32 x = 1; x < 7; x++) {
        pixels[3 * kTermFontSlotW + x] = 255;
        pixels[9 * kTermFontSlotW + x] = 255;
    }
    for (u32 y = 3; y <= 9; y++) {
        pixels[y * kTermFontSlotW + 1] = 255;
        pixels[y * kTermFontSlotW + 6] = 255;
    }
    upload(slot, pixels);
}

bool TermFont::init(FontChain& fonts, Arena& scratch)
{
    fonts_ = &fonts;
    scratch_ = &scratch;
    for (SlotEntry& entry : hash_) {
        entry = SlotEntry{};
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &atlas_);
    glTextureStorage2D(atlas_, 1, GL_R8, kAtlasPixelW, kAtlasPixelH);
    glTextureParameteri(atlas_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(atlas_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(atlas_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(atlas_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    u8 clear_px[kTermFontSlotW * kTermFontSlotH];
    std::memset(clear_px, 0, sizeof(clear_px));
    for (u32 i = 0; i < kTermFontMaxSlots; i++) {
        upload(i, clear_px);
    }

    bake_builtin();
    next_slot_ = kTermFontBuiltinCount;
    missing_slot_ = next_slot_++;
    bake_missing_box(missing_slot_);
    return true;
}

void TermFont::shutdown()
{
    if (atlas_) {
        glDeleteTextures(1, &atlas_);
        atlas_ = 0;
    }
}

u32 TermFont::bake_codepoint(u32 cp, bool wide)
{
    if (!fonts_ || !scratch_) {
        return missing_slot_;
    }
    i32 glyph = 0;
    const stbtt_fontinfo* face = fonts_->face(fonts_->find(cp, &glyph));
    if (!face || !glyph) {
        return missing_slot_;
    }
    if (next_slot_ >= kTermFontMaxSlots) {
        log_warn("term_font: atlas full at cp %u", cp);
        return missing_slot_;
    }

    const i32 box_w = wide ? 16 : 8;
    const f32 pixel_h = wide ? 11.0f : 10.0f;
    const f32 scale = stbtt_ScaleForPixelHeight(face, pixel_h);
    constexpr i32 baseline = 10;

    i32 x0 = 0;
    i32 y0 = 0;
    i32 x1 = 0;
    i32 y1 = 0;
    stbtt_GetGlyphBitmapBox(face, glyph, scale, scale, &x0, &y0, &x1, &y1);
    i32 gw = x1 - x0;
    i32 gh = y1 - y0;
    if (gw <= 0 || gh <= 0) {
        return missing_slot_;
    }
    gw = gw > 32 ? 32 : gw;
    gh = gh > 24 ? 24 : gh;

    ArenaScope scope(*scratch_);
    StbttScratch stbtt_scope(*scratch_);
    u8* raster = scratch_->push_array<u8>(static_cast<u64>(gw) * gh);
    if (!raster) {
        return missing_slot_;
    }
    stbtt_MakeGlyphBitmap(face, raster, gw, gh, gw, scale, scale, glyph);

    u8 pixels[kTermFontSlotW * kTermFontSlotH];
    std::memset(pixels, 0, sizeof(pixels));
    const i32 dst_x0 = (box_w - gw) / 2;
    const i32 dst_y0 = baseline + y0;
    for (i32 y = 0; y < gh; y++) {
        const i32 dy = dst_y0 + y;
        if (dy < 0 || dy >= static_cast<i32>(kTermFontSlotH)) {
            continue;
        }
        for (i32 x = 0; x < gw; x++) {
            const i32 dx = dst_x0 + x;
            if (dx < 0 || dx >= box_w) {
                continue;
            }
            if (raster[y * gw + x] >= 96) {
                pixels[dy * kTermFontSlotW + dx] = 255;
            }
        }
    }

    const u32 slot = next_slot_++;
    upload(slot, pixels);
    return slot;
}

u32 TermFont::slot(u32 cp, bool* out_wide)
{
    if (cp >= 32 && cp <= 127) {
        if (out_wide) {
            *out_wide = false;
        }
        return cp - 32;
    }

    const bool wide = term_font_cp_wide(cp);
    if (out_wide) {
        *out_wide = wide;
    }

    const u32 h = (cp * 2654435761u) & (kTermFontHashSize - 1);
    for (u32 probe = 0; probe < kTermFontHashSize; probe++) {
        SlotEntry& e = hash_[(h + probe) & (kTermFontHashSize - 1)];
        if (e.used && e.cp == cp) {
            return e.slot;
        }
        if (!e.used) {
            e.used = true;
            e.cp = cp;
            e.wide = wide;
            e.slot = bake_codepoint(cp, wide);
            return e.slot;
        }
    }
    return missing_slot_;
}

} // namespace anom
