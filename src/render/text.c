#include "render/text.h"
#include "render/render.h"
#include "render/fontchain.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/platform.h"
#include "platform/gl_loader.h"

#define TEXT_ATLAS_SIZE 2048
#define TEXT_BAKE_PIXEL_SIZE 64.0f
#define TEXT_MAX_QUADS 8192
#define TEXT_HASH_SIZE 4096
#define TEXT_PAD 2

typedef struct TextVert {
    f32 x, y;
    f32 u, v;
    u32 color;
} TextVert;

typedef struct TextGlyph {
    u32 cp;
    b32 used;
    f32 u0, v0, u1, v1;
    f32 xoff, yoff;
    f32 w, h;
    f32 advance;
} TextGlyph;

static TextGlyph s_glyphs[TEXT_HASH_SIZE];
static u32 s_shelf_x;
static u32 s_shelf_y;
static u32 s_shelf_row_h;
static f32 s_line_advance;
static u32 s_atlas_texture;
static u32 s_vbo;
static u32 s_vao;
static TextVert* s_verts;
static u32 s_vert_count;

b32 text_init(void)
{
    const stbtt_fontinfo* face = fontchain_face(0);
    if (!face) {
        log_error("text: fontchain has no faces");
        return 0;
    }
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(face, &ascent, &descent, &line_gap);
    f32 scale = stbtt_ScaleForPixelHeight(face, TEXT_BAKE_PIXEL_SIZE);
    s_line_advance = (f32)(ascent - descent + line_gap) * scale;

    glCreateTextures(GL_TEXTURE_2D, 1, &s_atlas_texture);
    glTextureStorage2D(s_atlas_texture, 1, GL_R8, TEXT_ATLAS_SIZE, TEXT_ATLAS_SIZE);
    glTextureParameteri(s_atlas_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(s_atlas_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(s_atlas_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(s_atlas_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    s_verts = arena_push_array(&g_perm_arena, TextVert, TEXT_MAX_QUADS * 6);

    glCreateBuffers(1, &s_vbo);
    glNamedBufferStorage(s_vbo, (GLsizeiptr)(TEXT_MAX_QUADS * 6 * sizeof(TextVert)), 0, GL_DYNAMIC_STORAGE_BIT);

    glCreateVertexArrays(1, &s_vao);
    glVertexArrayVertexBuffer(s_vao, 0, s_vbo, 0, sizeof(TextVert));
    glEnableVertexArrayAttrib(s_vao, 0);
    glVertexArrayAttribFormat(s_vao, 0, 2, GL_FLOAT, GL_FALSE, offsetof(TextVert, x));
    glVertexArrayAttribBinding(s_vao, 0, 0);
    glEnableVertexArrayAttrib(s_vao, 1);
    glVertexArrayAttribFormat(s_vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(TextVert, u));
    glVertexArrayAttribBinding(s_vao, 1, 0);
    glEnableVertexArrayAttrib(s_vao, 2);
    glVertexArrayAttribFormat(s_vao, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(TextVert, color));
    glVertexArrayAttribBinding(s_vao, 2, 0);

    log_info("text: dynamic atlas %dpx ready", TEXT_ATLAS_SIZE);
    return 1;
}

void text_shutdown(void)
{
    glDeleteVertexArrays(1, &s_vao);
    glDeleteBuffers(1, &s_vbo);
    glDeleteTextures(1, &s_atlas_texture);
}

void text_begin_frame(void)
{
    s_vert_count = 0;
}

static b32 atlas_alloc(u32 w, u32 h, u32* out_x, u32* out_y)
{
    if (s_shelf_x + w + TEXT_PAD > TEXT_ATLAS_SIZE) {
        s_shelf_x = 0;
        s_shelf_y += s_shelf_row_h + TEXT_PAD;
        s_shelf_row_h = 0;
    }
    if (s_shelf_y + h + TEXT_PAD > TEXT_ATLAS_SIZE) {
        return 0;
    }
    *out_x = s_shelf_x;
    *out_y = s_shelf_y;
    s_shelf_x += w + TEXT_PAD;
    if (h > s_shelf_row_h) {
        s_shelf_row_h = h;
    }
    return 1;
}

static TextGlyph* glyph_get(u32 cp)
{
    u32 h = (cp * 2654435761u) & (TEXT_HASH_SIZE - 1);
    TextGlyph* entry = 0;
    for (u32 probe = 0; probe < TEXT_HASH_SIZE; probe++) {
        TextGlyph* e = &s_glyphs[(h + probe) & (TEXT_HASH_SIZE - 1)];
        if (e->used && e->cp == cp) {
            return e;
        }
        if (!e->used) {
            entry = e;
            break;
        }
    }
    if (!entry) {
        return 0;
    }
    entry->used = 1;
    entry->cp = cp;

    i32 glyph = 0;
    i32 face_idx = fontchain_find(cp, &glyph);
    if (face_idx < 0) {
        face_idx = fontchain_find(0xFFFDu, &glyph);
    }
    if (face_idx < 0) {
        face_idx = fontchain_find('?', &glyph);
    }
    const stbtt_fontinfo* face = fontchain_face(face_idx);
    if (!face || !glyph) {
        entry->advance = TEXT_BAKE_PIXEL_SIZE * 0.5f;
        return entry;
    }

    f32 scale = stbtt_ScaleForPixelHeight(face, TEXT_BAKE_PIXEL_SIZE);
    int adv, lsb;
    stbtt_GetGlyphHMetrics(face, glyph, &adv, &lsb);
    entry->advance = (f32)adv * scale;

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(face, glyph, scale, scale, &x0, &y0, &x1, &y1);
    i32 gw = x1 - x0;
    i32 gh = y1 - y0;
    if (gw <= 0 || gh <= 0) {
        return entry;
    }

    u32 ax, ay;
    if (!atlas_alloc((u32)gw, (u32)gh, &ax, &ay)) {
        log_warn("text: glyph atlas full at cp %u", cp);
        return entry;
    }

    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    u8* raster = arena_push_array(&g_frame_arena, u8, (u64)gw * gh);
    stbtt_MakeGlyphBitmap(face, raster, gw, gh, gw, scale, scale, glyph);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(s_atlas_texture, 0, (GLint)ax, (GLint)ay, gw, gh,
                        GL_RED, GL_UNSIGNED_BYTE, raster);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    arena_temp_end(temp);

    entry->u0 = (f32)ax / TEXT_ATLAS_SIZE;
    entry->v0 = (f32)ay / TEXT_ATLAS_SIZE;
    entry->u1 = (f32)(ax + (u32)gw) / TEXT_ATLAS_SIZE;
    entry->v1 = (f32)(ay + (u32)gh) / TEXT_ATLAS_SIZE;
    entry->xoff = (f32)x0;
    entry->yoff = (f32)y0;
    entry->w = (f32)gw;
    entry->h = (f32)gh;
    return entry;
}

static void text_push_vert(f32 x, f32 y, f32 u, f32 v, u32 color)
{
    TextVert* vert = &s_verts[s_vert_count++];
    vert->x = x;
    vert->y = y;
    vert->u = u;
    vert->v = v;
    vert->color = color;
}

void text_draw(f32 x, f32 y, f32 size, u32 color, const char* str)
{
    f32 scale = size / TEXT_BAKE_PIXEL_SIZE;
    f32 pen_x = 0.0f;
    const char* p = str;
    u32 cp;
    while ((cp = utf8_next(&p)) != 0) {
        if (cp < 32) {
            continue;
        }
        TextGlyph* g = glyph_get(cp);
        if (!g) {
            continue;
        }
        if (g->w > 0.0f && g->h > 0.0f) {
            if (s_vert_count + 6 > TEXT_MAX_QUADS * 6) {
                return;
            }
            f32 x0 = x + (pen_x + g->xoff) * scale;
            f32 y0 = y + g->yoff * scale;
            f32 x1 = x0 + g->w * scale;
            f32 y1 = y0 + g->h * scale;
            text_push_vert(x0, y0, g->u0, g->v0, color);
            text_push_vert(x1, y0, g->u1, g->v0, color);
            text_push_vert(x1, y1, g->u1, g->v1, color);
            text_push_vert(x0, y0, g->u0, g->v0, color);
            text_push_vert(x1, y1, g->u1, g->v1, color);
            text_push_vert(x0, y1, g->u0, g->v1, color);
        }
        pen_x += g->advance;
    }
}

f32 text_measure(const char* str, f32 size)
{
    f32 scale = size / TEXT_BAKE_PIXEL_SIZE;
    f32 pen_x = 0.0f;
    const char* p = str;
    u32 cp;
    while ((cp = utf8_next(&p)) != 0) {
        if (cp < 32) {
            continue;
        }
        TextGlyph* g = glyph_get(cp);
        if (g) {
            pen_x += g->advance;
        }
    }
    return pen_x * scale;
}

f32 text_line_height(f32 size)
{
    return s_line_advance * (size / TEXT_BAKE_PIXEL_SIZE);
}

void text_flush(void)
{
    if (!s_vert_count) {
        return;
    }
    u32 program = r_shader("text");
    if (!program) {
        return;
    }
    glNamedBufferSubData(s_vbo, 0, (GLsizeiptr)(s_vert_count * sizeof(TextVert)), s_verts);
    glUseProgram(program);
    glBindVertexArray(s_vao);
    glBindTextureUnit(0, s_atlas_texture);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)s_vert_count);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
}
