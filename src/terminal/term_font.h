#pragma once

#include "core/types.h"

namespace anom {

class Arena;
class FontChain;

inline constexpr u32 kTermFontSlotW = 16;
inline constexpr u32 kTermFontSlotH = 12;
inline constexpr u32 kTermFontAtlasCols = 32;
inline constexpr u32 kTermFontAtlasRows = 42;
inline constexpr u32 kTermFontBuiltinCount = 96;
inline constexpr u32 kTermFontMaxSlots = kTermFontAtlasCols * kTermFontAtlasRows;
inline constexpr u32 kTermFontHashSize = 4096;
inline constexpr u32 kTermFontBlock = 95;
inline constexpr u16 kTermFontWideCont = 0xFFFFu;

bool term_font_cp_wide(u32 codepoint);

class TermFont {
public:
    bool init(FontChain& fonts, Arena& scratch);
    void shutdown();

    u32 texture() const { return atlas_; }
    u32 slot(u32 codepoint, bool* out_wide);

private:
    struct SlotEntry {
        u32 cp = 0;
        u32 slot = 0;
        bool wide = false;
        bool used = false;
    };

    void upload(u32 slot, const u8* pixels) const;
    void bake_builtin() const;
    void bake_missing_box(u32 slot) const;
    u32 bake_codepoint(u32 codepoint, bool wide);

    SlotEntry hash_[kTermFontHashSize];
    FontChain* fonts_ = nullptr;
    Arena* scratch_ = nullptr;
    u32 atlas_ = 0;
    u32 next_slot_ = 0;
    u32 missing_slot_ = 0;
};

} // namespace anom
