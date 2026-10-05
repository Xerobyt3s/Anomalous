#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

class Arena;
class Heightfield;
class RenderDevice;

struct ScrubPatch {
    Vec3 pos{};
    Quat rot = quat_identity();
    f32 scale = 1.0f;
    u32 texture = 0;
};

class TerrainRenderer {
public:
    static constexpr u32 kChunkQuads = 64;
    static constexpr u32 kMaxChunks = 256;
    static constexpr u32 kScrubNearGrid = 165;
    static constexpr f32 kScrubNearSpacing = 0.17f;
    static constexpr u32 kScrubFarGrid = 212;
    static constexpr f32 kScrubFarSpacing = 0.36f;
    static constexpr f32 kScrubRingStart = 10.0f;
    static constexpr f32 kScrubRingEnd = 14.0f;
    static constexpr u32 kScrubSlots = 8;
    static constexpr u32 kScrubBladeVerts = 16;
    static constexpr f32 kScrubFadeEnd = 38.0f;
    static constexpr u32 kMaxPressVolumes = 8;
    static constexpr u32 kMaxPatchTextures = 64;
    static constexpr u32 kMaxPatchPress = 4;
    static constexpr f32 kPatchExtent = 24.0f;
    static constexpr f32 kPatchHeightMin = -8.0f;
    static constexpr f32 kPatchHeightMax = 8.0f;

    bool init(RenderDevice& device, Arena& scratch, const Heightfield& hf,
              const u8* roadmask, u32 mask_size);
    void shutdown();

    void draw(RenderDevice& device);
    void draw_scrub(RenderDevice& device, Vec3 cam_pos, f32 time);
    u32 patch_texture(Arena& scratch, std::string_view mesh_name);
    u32 register_patch_heights(std::string_view name, const f32* heights, u32 width, u32 height);
    void draw_scrub_patches(RenderDevice& device, Vec3 cam_pos, f32 time, const ScrubPatch* patches,
                            u32 count);

    void clear_press_volumes()
    {
        press_count_ = 0;
        patch_press_count_ = 0;
    }
    void add_press_volume(Vec3 centre, Vec3 half_extents, Quat rot);
    void add_patch_press(Vec3 centre, Vec3 half_extents, Quat rot);

    u32 chunks_drawn() const { return chunks_drawn_; }
    u32 chunk_count() const { return chunk_count_; }

private:
    struct Chunk {
        u32 first_index;
        u32 index_count;
        Aabb bounds;
    };

    struct PatchTexture {
        FixedString<32> name;
        u32 texture = 0;
    };

    struct PressVolume {
        Vec3 centre{};
        Vec3 half{};
        Quat rot = quat_identity();
    };

    void scrub_rings(u32 program, const Vec3* cam_local);
    static bool encode_press(Vec4* out, Vec3 centre, Vec3 half_extents, Quat rot);

    PressVolume patch_press_[kMaxPatchPress];
    u32 patch_press_count_ = 0;

    PatchTexture patch_textures_[kMaxPatchTextures];
    u32 patch_texture_count_ = 0;
    bool patch_cache_full_logged_ = false;
    u32 black_texture_ = 0;
    Vec4 press_[kMaxPressVolumes * 2] = {};
    u32 press_count_ = 0;

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
    u32 tex_meadow_ = 0;
    Vec4 params_{0.0f, 0.0f, 1.0f, 1.0f};
    bool ready_ = false;
};

} // namespace anom
