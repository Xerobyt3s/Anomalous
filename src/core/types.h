#pragma once

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;
typedef u32      b32;

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define KILOBYTES(n) ((u64)(n) << 10)
#define MEGABYTES(n) ((u64)(n) << 20)
#define GIGABYTES(n) ((u64)(n) << 30)

#if defined(_DEBUG)
    #define ASSERT(x) do { if (!(x)) { __debugbreak(); } } while (0)
#else
    #define ASSERT(x) ((void)sizeof(!(x)))
#endif
