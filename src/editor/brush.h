#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"

#include <span>
#include <string_view>

namespace anom {

class Arena;

inline constexpr u32 kStructMaxBrushes = 128;
inline constexpr u32 kBrushPolyMax = 8;
inline constexpr u32 kBrushMaxFaces = kBrushPolyMax + 2;
inline constexpr u32 kBPolyMax = 24;
inline constexpr u32 kBakeMaxPolys = 8192;
inline constexpr f32 kBrushEps = 1e-4f;
inline constexpr f32 kBrushMinSize = 0.25f;

enum class BrushKind : u32 {
    Box,
    Wedge,
    Poly,
};

struct Brush {
    BrushKind kind = BrushKind::Box;
    bool subtract = false;
    Vec3 pos;
    Vec3 size{2.0f, 2.0f, 2.0f};
    f32 yaw = 0.0f;
    f32 hollow = 0.0f;
    Vec2 points[kBrushPolyMax];
    u32 point_count = 0;
    FixedString<32> material{"concrete"};
};

struct Structure {
    FixedString<32> name{"untitled"};
    Brush brushes[kStructMaxBrushes];
    u32 count = 0;
};

struct BrushPoly {
    Vec3 v[kBPolyMax];
    u32 n = 0;
    Vec3 normal;
    FixedString<32> material;
};

struct BakeOutput {
    BrushPoly* polys = nullptr;
    u32 count = 0;
    u32 cap = 0;
};

f32 poly_area2(std::span<const Vec2> points);
bool poly_convex(std::span<const Vec2> points);
void poly_normalize(Brush& brush);
void poly_from_box(Brush& brush);

u32 brush_edit_points(const Brush& brush, Vec2* out);
u32 brush_faces(const Brush& brush, BrushPoly* out);
u32 brush_planes(const Brush& brush, Vec3* out_normal, f32* out_dist);
bool bpoly_clip(const BrushPoly& in, Vec3 plane_n, f32 plane_d, f32 keep_sign, BrushPoly& out);

u32 bake_expand_hollow(const Structure& st, Brush* out);
void bake_structure(BakeOutput& out, Arena& scratch, const Brush* brushes, u32 count);
bool bake_write_amsh(std::string_view path, Arena& scratch, const BakeOutput& out);

} // namespace anom
