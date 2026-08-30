#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <span>

namespace anom {

class Arena;
class RenderDevice;

inline constexpr u32 kBoltMaxVerts = 12288;
inline constexpr u32 kBoltMaxPoints = 128;

class BoltRenderer {
public:
    bool init(Arena& arena);
    void shutdown();

    void begin_frame() { count_ = 0; }

    static u32 subdivide(Vec3* points, u32 count, u32 capacity, u32 levels, f32 displacement,
                         u32& rng);

    void channel(std::span<const Vec3> points, f32 half_width, f32 intensity);
    void draw(RenderDevice& device, Vec3 colour, f32 core_radiance);

    u32 vertex_count() const { return count_; }

private:
    struct Vertex {
        Vec3 pos;
        Vec3 dir;
        Vec4 params;
    };

    Vertex* verts_ = nullptr;
    u32 count_ = 0;
    u32 vao_ = 0;
    u32 vbo_ = 0;
};

} // namespace anom
