#pragma once

#include "core/types.h"

namespace anom {

template<typename T>
struct Handle {
    static constexpr u32 kInvalidIndex = 0xFFFFFFFFu;

    u32 idx = kInvalidIndex;
    u32 gen = 0;

    constexpr bool valid() const { return idx != kInvalidIndex; }

    friend constexpr bool operator==(Handle a, Handle b)
    {
        return a.idx == b.idx && a.gen == b.gen;
    }

    friend constexpr bool operator!=(Handle a, Handle b) { return !(a == b); }
};

} // namespace anom
