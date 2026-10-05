#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghost::engine::embedded {
struct File {
    std::uint64_t key;
    const unsigned char* data;
    std::size_t size;
};

extern const File* const kFiles;
extern const std::size_t kFileCount;

constexpr std::uint64_t nameKey(std::string_view name) {
    std::uint64_t hash = 0xcbf29ce484222325ull ^ 0x47686f7374ull;
    for (const char c : name) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

inline void scramble(unsigned char* bytes, std::size_t size, std::uint64_t key) {
    std::uint64_t state = key ^ 0x9E3779B97F4A7C15ull;
    for (std::size_t i = 0; i < size; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        bytes[i] ^= static_cast<unsigned char>(state >> 32);
    }
}

}
