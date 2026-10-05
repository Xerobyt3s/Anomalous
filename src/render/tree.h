#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "world/tree_gen.h"

namespace anom {
class Arena;
class RenderDevice;

inline constexpr u32 kTreeMaxInstances = 128;

class TreeRenderer {
public:
    bool init(Arena& scratch);
    void shutdown();

    void begin_frame();
    void submit(u32 variant, const Mat4& model);
    void draw(RenderDevice& device);

private:
    struct Variant {
        u32 vao = 0;
        u32 vbo = 0;
        u32 ebo = 0;
        u32 index_count = 0;
        u32 bark_index_count = 0;
        Aabb bounds = aabb_empty();
        u32 count = 0;
        Mat4 models[kTreeMaxInstances];
    };

    Variant variants_[kTreeVariants];
    Mat4 visible_[kTreeMaxInstances];
    u32 instance_vbo_ = 0;
    bool ready_ = false;
};

}
