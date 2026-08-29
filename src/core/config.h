#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <span>
#include <string_view>

namespace anom {

class Arena;

class Config {
public:
    static constexpr u32 kMaxEntries = 512;

    bool parse(Arena& arena, std::string_view text);

    bool has(std::string_view key) const;
    std::string_view get_str(std::string_view key, std::string_view fallback) const;
    f32 get_f32(std::string_view key, f32 fallback) const;
    i32 get_i32(std::string_view key, i32 fallback) const;
    Vec3 get_vec3(std::string_view key, Vec3 fallback) const;
    u32 get_f32_list(std::string_view key, std::span<f32> out) const;

    u32 count() const { return count_; }

private:
    struct Entry {
        std::string_view key;
        std::string_view value;
    };

    const Entry* find(std::string_view key) const;

    Entry entries_[kMaxEntries];
    u32 count_ = 0;
};

} // namespace anom
