#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class Arena;
class Heightfield;
class RenderDevice;

class TerrainRenderer {
public:
    static constexpr u32 kChunkQuads = 64;
    static constexpr u32 kMaxChunks = 256;
    static constexpr u32 kScrubGrid = 512;
    static constexpr f32 kScrubFadeEnd = 38.0f;

    bool init(RenderDevice& device, Arena& scratch, const Heightfield& hf,
              const u8* roadmask, u32 mask_size);
    void shutdown();

    void draw(RenderDevice& device);
    void draw_scrub(RenderDevice& device, Vec3 cam_pos, f32 time);

    u32 chunks_drawn() const { return chunks_drawn_; }
    u32 chunk_count() const { return chunk_count_; }

private:
    struct Chunk {
        u32 first_index;
        u32 index_count;
        Aabb bounds;
    };

    Chunk chunks_[kMaxChunks];
    u32 chunk_count_ = 0;
    u32 chunks_drawn_ = 0;

    u32 vao_ = 0;
    u32 vbo_ = 0;
    u32 ebo_ = 0;
    u32 scrub_vao_ = 0;
    u32 mask_texture_ = 0;
    u32 height_texture_ = 0;
    u32 tex_grass_ = 0;
    u32 tex_rock_ = 0;
    u32 tex_road_ = 0;
    Vec4 params_{0.0f, 0.0f, 1.0f, 1.0f};
    bool ready_ = false;
};

} // namespace anom
