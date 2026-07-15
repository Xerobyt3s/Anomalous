#include "render/text.h"
#include "render/render.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/platform.h"
#include "platform/gl_loader.h"

#define STBTT_malloc(x, u) ((void)(u), arena_push_size(&g_frame_arena, (x), 16))
#define STBTT_free(x, u) ((void)(u), (void)(x))
#pragma warning(push, 0)
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#pragma warning(pop)

#define TEXT_ATLAS_SIZE 1024
#define TEXT_BAKE_PIXEL_SIZE 64.0f
#define TEXT_FIRST_CHAR 32
#define TEXT_CHAR_COUNT 95
#define TEXT_MAX_QUADS 8192

typedef struct TextVert {
    f32 x, y;
    f32 u, v;
    u32 color;
} TextVert;

static stbtt_bakedchar s_baked[TEXT_CHAR_COUNT];
static stbtt_fontinfo s_font_info;
static f32 s_line_advance;
static u32 s_atlas_texture;
static u32 s_vbo;
static u32 s_vao;
static TextVert* s_verts;
static u32 s_vert_count;

b32 text_init(const char* ttf_path)
{
    FileData ttf = platform_read_entire_file(&g_perm_arena, ttf_path);
    if (!ttf.data) {
        log_error("text: failed to read font %s", ttf_path);
        return 0;
    }

    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    u8* bitmap = arena_push_array(&g_frame_arena, u8, TEXT_ATLAS_SIZE * TEXT_ATLAS_SIZE);
    int bake_result = stbtt_BakeFontBitmap(ttf.data, 0, TEXT_BAKE_PIXEL_SIZE, bitmap,
                                           TEXT_ATLAS_SIZE, TEXT_ATLAS_SIZE,
                                           TEXT_FIRST_CHAR, TEXT_CHAR_COUNT, s_baked);
    if (bake_result <= 0) {
        log_error("text: font bake failed (%d)", bake_result);
        arena_temp_end(temp);
        return 0;
    }

    if (!stbtt_InitFont(&s_font_info, ttf.data, stbtt_GetFontOffsetForIndex(ttf.data, 0))) {
        log_error("text: stbtt_InitFont failed");
        arena_temp_end(temp);
        return 0;
    }
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&s_font_info, &ascent, &descent, &line_gap);
    f32 scale = stbtt_ScaleForPixelHeight(&s_font_info, TEXT_BAKE_PIXEL_SIZE);
    s_line_advance = (f32)(ascent - descent + line_gap) * scale;

    glCreateTextures(GL_TEXTURE_2D, 1, &s_atlas_texture);
    glTextureStorage2D(s_atlas_texture, 1, GL_R8, TEXT_ATLAS_SIZE, TEXT_ATLAS_SIZE);
    glTextureParameteri(s_atlas_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(s_atlas_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(s_atlas_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(s_atlas_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(s_atlas_texture, 0, 0, 0, TEXT_ATLAS_SIZE, TEXT_ATLAS_SIZE, GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    arena_temp_end(temp);

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

    log_info("text: baked %d glyphs at %.0fpx from %s", TEXT_CHAR_COUNT, TEXT_BAKE_PIXEL_SIZE, ttf_path);
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
    f32 pen_y = 0.0f;
    for (const char* c = str; *c; c++) {
        if (*c < TEXT_FIRST_CHAR || *c >= TEXT_FIRST_CHAR + TEXT_CHAR_COUNT) {
            continue;
        }
        if (s_vert_count + 6 > TEXT_MAX_QUADS * 6) {
            return;
        }
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(s_baked, TEXT_ATLAS_SIZE, TEXT_ATLAS_SIZE, *c - TEXT_FIRST_CHAR, &pen_x, &pen_y, &q, 1);
        f32 x0 = x + q.x0 * scale;
        f32 y0 = y + q.y0 * scale;
        f32 x1 = x + q.x1 * scale;
        f32 y1 = y + q.y1 * scale;
        text_push_vert(x0, y0, q.s0, q.t0, color);
        text_push_vert(x1, y0, q.s1, q.t0, color);
        text_push_vert(x1, y1, q.s1, q.t1, color);
        text_push_vert(x0, y0, q.s0, q.t0, color);
        text_push_vert(x1, y1, q.s1, q.t1, color);
        text_push_vert(x0, y1, q.s0, q.t1, color);
    }
}

f32 text_measure(const char* str, f32 size)
{
    f32 scale = size / TEXT_BAKE_PIXEL_SIZE;
    f32 pen_x = 0.0f;
    f32 pen_y = 0.0f;
    for (const char* c = str; *c; c++) {
        if (*c < TEXT_FIRST_CHAR || *c >= TEXT_FIRST_CHAR + TEXT_CHAR_COUNT) {
            continue;
        }
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(s_baked, TEXT_ATLAS_SIZE, TEXT_ATLAS_SIZE, *c - TEXT_FIRST_CHAR, &pen_x, &pen_y, &q, 1);
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
