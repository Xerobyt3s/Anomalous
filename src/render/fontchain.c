#include "render/fontchain.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/platform.h"

#define STBTT_malloc(x, u) ((void)(u), arena_push_size(&g_frame_arena, (x), 16))
#define STBTT_free(x, u) ((void)(u), (void)(x))
#pragma warning(push, 0)
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#pragma warning(pop)

#define FONTCHAIN_MAX_FACES 8

typedef struct FaceEntry {
    stbtt_fontinfo info;
    char name[64];
    b32 loaded;
} FaceEntry;

static FaceEntry s_faces[FONTCHAIN_MAX_FACES];
static i32 s_face_count;

static void face_load(const char* path)
{
    if (s_face_count >= FONTCHAIN_MAX_FACES) {
        return;
    }
    FileData file = platform_read_entire_file(&g_perm_arena, path);
    if (!file.data) {
        log_info("fontchain: skipped %s (not found)", path);
        return;
    }
    FaceEntry* face = &s_faces[s_face_count];
    i32 offset = stbtt_GetFontOffsetForIndex(file.data, 0);
    if (offset < 0 || !stbtt_InitFont(&face->info, file.data, offset)) {
        log_warn("fontchain: failed to parse %s", path);
        return;
    }
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    for (u32 i = 0; base[i] && i < sizeof(face->name) - 1; i++) {
        face->name[i] = base[i];
        face->name[i + 1] = 0;
    }
    face->loaded = 1;
    s_face_count++;
}

b32 fontchain_init(void)
{
    s_face_count = 0;
    face_load("assets/fonts/mono.ttf");
    face_load("C:/Windows/Fonts/msgothic.ttc");
    face_load("C:/Windows/Fonts/malgun.ttf");
    face_load("C:/Windows/Fonts/msyh.ttc");
    face_load("C:/Windows/Fonts/seguisym.ttf");
    face_load("C:/Windows/Fonts/msyi.ttf");
    face_load("C:/Windows/Fonts/arial.ttf");
    if (!s_face_count) {
        log_error("fontchain: no fonts loaded");
        return 0;
    }
    log_info("fontchain: %d faces loaded", s_face_count);
    return 1;
}

i32 fontchain_find(u32 codepoint, i32* out_glyph)
{
    for (i32 i = 0; i < s_face_count; i++) {
        i32 glyph = stbtt_FindGlyphIndex(&s_faces[i].info, (int)codepoint);
        if (glyph) {
            if (out_glyph) {
                *out_glyph = glyph;
            }
            return i;
        }
    }
    if (out_glyph) {
        *out_glyph = 0;
    }
    return -1;
}

const stbtt_fontinfo* fontchain_face(i32 face)
{
    if (face < 0 || face >= s_face_count) {
        return 0;
    }
    return &s_faces[face].info;
}

u32 utf8_next(const char** p)
{
    const u8* s = (const u8*)*p;
    if (!s[0]) {
        return 0;
    }
    u32 cp;
    if (s[0] < 0x80) {
        cp = s[0];
        s += 1;
    } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((u32)(s[0] & 0x1F) << 6) | (u32)(s[1] & 0x3F);
        s += 2;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((u32)(s[0] & 0x0F) << 12) | ((u32)(s[1] & 0x3F) << 6) | (u32)(s[2] & 0x3F);
        s += 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80
               && (s[3] & 0xC0) == 0x80) {
        cp = ((u32)(s[0] & 0x07) << 18) | ((u32)(s[1] & 0x3F) << 12)
           | ((u32)(s[2] & 0x3F) << 6) | (u32)(s[3] & 0x3F);
        s += 4;
    } else {
        cp = 0xFFFD;
        s += 1;
    }
    *p = (const char*)s;
    return cp;
}
