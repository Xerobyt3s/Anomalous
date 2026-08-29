#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

inline u32 utf8_next(std::string_view& cursor)
{
    if (cursor.empty()) {
        return 0;
    }
    const auto* p = reinterpret_cast<const u8*>(cursor.data());
    const std::size_t len = cursor.size();

    u32 cp = 0;
    std::size_t advance = 1;
    if (p[0] < 0x80) {
        cp = p[0];
    } else if ((p[0] & 0xE0) == 0xC0 && len >= 2 && (p[1] & 0xC0) == 0x80) {
        cp = (static_cast<u32>(p[0] & 0x1F) << 6) | static_cast<u32>(p[1] & 0x3F);
        advance = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && len >= 3 && (p[1] & 0xC0) == 0x80
               && (p[2] & 0xC0) == 0x80) {
        cp = (static_cast<u32>(p[0] & 0x0F) << 12) | (static_cast<u32>(p[1] & 0x3F) << 6)
           | static_cast<u32>(p[2] & 0x3F);
        advance = 3;
    } else if ((p[0] & 0xF8) == 0xF0 && len >= 4 && (p[1] & 0xC0) == 0x80
               && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        cp = (static_cast<u32>(p[0] & 0x07) << 18) | (static_cast<u32>(p[1] & 0x3F) << 12)
           | (static_cast<u32>(p[2] & 0x3F) << 6) | static_cast<u32>(p[3] & 0x3F);
        advance = 4;
    } else {
        cp = 0xFFFD;
    }
    cursor.remove_prefix(advance);
    return cp;
}

} // namespace anom
