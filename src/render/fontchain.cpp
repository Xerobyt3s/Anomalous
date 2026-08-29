#include "render/fontchain.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/filesystem.h"

namespace anom {
namespace {

Arena* g_stbtt_arena = nullptr;

void* stbtt_alloc(std::size_t size)
{
    return g_stbtt_arena ? g_stbtt_arena->push_bytes(size, 16) : nullptr;
}

std::string_view basename_of(std::string_view path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

} // namespace
} // namespace anom

#define STBTT_malloc(x, u) ((void)(u), ::anom::stbtt_alloc(x))
#define STBTT_free(x, u) ((void)(u), (void)(x))

#pragma warning(push, 0)
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#pragma warning(pop)

namespace anom {

StbttScratch::StbttScratch(Arena& arena) : previous_(g_stbtt_arena)
{
    g_stbtt_arena = &arena;
}

StbttScratch::~StbttScratch()
{
    g_stbtt_arena = previous_;
}

void FontChain::load(Arena& storage, std::string_view path)
{
    if (count_ >= kMaxFaces) {
        return;
    }
    const fs::FileData file = fs::read_entire_file(storage, path);
    if (!file.valid()) {
        log_info("fontchain: skipped %.*s (not found)", static_cast<int>(path.size()),
                 path.data());
        return;
    }

    Face& face = faces_[count_];
    const int offset = stbtt_GetFontOffsetForIndex(file.data, 0);
    if (offset < 0 || !stbtt_InitFont(&face.info, file.data, offset)) {
        log_warn("fontchain: failed to parse %.*s", static_cast<int>(path.size()), path.data());
        return;
    }
    face.name.assign(basename_of(path));
    face.loaded = true;
    count_++;
}

bool FontChain::init(Arena& storage, Arena& scratch)
{
    StbttScratch binding(scratch);
    count_ = 0;

    load(storage, "assets/fonts/mono.ttf");
    load(storage, "C:/Windows/Fonts/msgothic.ttc");
    load(storage, "C:/Windows/Fonts/malgun.ttf");
    load(storage, "C:/Windows/Fonts/msyh.ttc");
    load(storage, "C:/Windows/Fonts/seguisym.ttf");
    load(storage, "C:/Windows/Fonts/msyi.ttf");
    load(storage, "C:/Windows/Fonts/arial.ttf");

    if (count_ == 0) {
        log_error("fontchain: no fonts loaded");
        return false;
    }
    log_info("fontchain: %d faces loaded", count_);
    return true;
}

i32 FontChain::find(u32 codepoint, i32* out_glyph) const
{
    for (i32 i = 0; i < count_; i++) {
        const i32 glyph = stbtt_FindGlyphIndex(&faces_[i].info, static_cast<int>(codepoint));
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

const stbtt_fontinfo* FontChain::face(i32 index) const
{
    if (index < 0 || index >= count_) {
        return nullptr;
    }
    return &faces_[index].info;
}

} // namespace anom
