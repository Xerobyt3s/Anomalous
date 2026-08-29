#pragma once

#include "core/types.h"

#include <span>
#include <string_view>

namespace anom {

class Arena;

struct Image8 {
    u8* pixels = nullptr;
    u32 width = 0;
    u32 height = 0;

    bool valid() const { return pixels != nullptr; }
};

struct Image16 {
    u16* pixels = nullptr;
    u32 width = 0;
    u32 height = 0;

    bool valid() const { return pixels != nullptr; }
};

Image8 load_image_gray(Arena& dst, Arena& scratch, std::string_view path);
Image16 load_image_gray16(Arena& dst, Arena& scratch, std::string_view path);
Image8 load_image_rgba(Arena& dst, Arena& scratch, std::string_view path);

Image8 decode_image_gray(std::span<const u8> bytes, Arena& dst, Arena& scratch);
Image8 decode_image_rgba(std::span<const u8> bytes, Arena& dst, Arena& scratch);

} // namespace anom
