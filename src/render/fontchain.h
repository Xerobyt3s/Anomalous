#pragma once

#include "core/fixed_string.h"
#include "core/types.h"

#include <string_view>

#pragma warning(push, 0)
#include <stb_truetype.h>
#pragma warning(pop)

namespace anom {

class Arena;

class StbttScratch {
public:
    explicit StbttScratch(Arena& arena);
    ~StbttScratch();

    StbttScratch(const StbttScratch&) = delete;
    StbttScratch& operator=(const StbttScratch&) = delete;

private:
    Arena* previous_;
};

class FontChain {
public:
    static constexpr i32 kMaxFaces = 8;

    bool init(Arena& storage, Arena& scratch);

    i32 find(u32 codepoint, i32* out_glyph) const;
    const stbtt_fontinfo* face(i32 index) const;
    i32 count() const { return count_; }

private:
    struct Face {
        stbtt_fontinfo info{};
        FixedString<64> name;
        bool loaded = false;
    };

    void load(Arena& storage, std::string_view path);

    Face faces_[kMaxFaces];
    i32 count_ = 0;
};

} // namespace anom
