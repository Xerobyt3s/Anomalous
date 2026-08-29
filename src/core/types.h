#pragma once

#include <cstddef>
#include <cstdint>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using b32 = u32;

static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);

template<typename T, std::size_t N>
constexpr std::size_t array_count(const T (&)[N])
{
    return N;
}

constexpr u64 kilobytes(u64 n) { return n << 10; }
constexpr u64 megabytes(u64 n) { return n << 20; }
constexpr u64 gigabytes(u64 n) { return n << 30; }

#if defined(_MSC_VER)
    #define ANOMALOUS_DEBUGBREAK() __debugbreak()
#else
    #define ANOMALOUS_DEBUGBREAK() __builtin_trap()
#endif

#if ANOMALOUS_DEBUG
    #define ASSERT(x)                       \
        do {                                \
            if (!(x)) {                     \
                ANOMALOUS_DEBUGBREAK();     \
            }                               \
        } while (0)
#else
    #define ASSERT(x) ((void)sizeof(!(x)))
#endif
