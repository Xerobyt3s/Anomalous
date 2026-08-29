#pragma once

// Fundamental scalar types and the assertion macro.
//
// Ported from the C original's core/types.h. The typedefs are deliberately unchanged --
// they are used in every file and in on-disk formats (see assets/mesh_format.h), so
// their sizes are load-bearing.

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

static_assert(sizeof(f32) == 4, "f32 must be IEEE-754 binary32");
static_assert(sizeof(f64) == 8, "f64 must be IEEE-754 binary64");

// The C original had `typedef u32 b32` because C99 bools were awkward in structs it
// memset to zero. C++ `bool` is well behaved, so plain bool is used from here on and
// b32 survives only where a fixed 32-bit layout is required by a file format.
using b32 = u32;

// Replaces the original's ARRAY_COUNT macro. Taking the array by reference makes this
// reject pointers at compile time -- the macro silently accepted a decayed pointer and
// returned sizeof(ptr)/sizeof(*ptr), which is where "why is my count 2" bugs come from.
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
