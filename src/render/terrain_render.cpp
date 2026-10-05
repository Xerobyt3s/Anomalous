#include "render/terrain_render.h"
#include "core/arena.h"
#include "assets/image.h"
#include "core/log.h"
#include "physics/heightfield.h"
#include "platform/filesystem.h"
#include "platform/gl_loader.h"
#include "render/device.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace anom {
namespace {

struct TerrainVertex {
    Vec3 pos;
    Vec3 normal;
};

Vec3 vertex_normal(const Heightfield& hf, u32 ix, u32 iz)
{
    const u32 xl = ix > 0 ? ix - 1 : ix;
    const u32 xr = ix + 1 < hf.size_x() ? ix + 1 : ix;
    const u32 zd = iz > 0 ? iz - 1 : iz;
    const u32 zu = iz + 1 < hf.size_z() ? iz + 1 : iz;

    const f32 hl = hf.height_at(xl, iz);
    const f32 hr = hf.height_at(xr, iz);
    const f32 hd = hf.height_at(ix, zd);
    const f32 hu = hf.height_at(ix, zu);
    const f32 span_x = static_cast<f32>(xr - xl) * hf.cell_size();
    const f32 span_z = static_cast<f32>(zu - zd) * hf.cell_size();

    return normalize(Vec3{(hl - hr) / f_max(span_x, 1e-4f) * 2.0f,
                          1.0f,
                          (hd - hu) / f_max(span_z, 1e-4f) * 2.0f});
}

} // namespace

bool TerrainRenderer::init(RenderDevice& device, Arena& scratch, const Heightfield& hf,
                           const u8* roadmask, u32 mask_size)
{
    ArenaScope scope(scratch);

    const u32 sx = hf.size_x();
    const u32 sz = hf.size_z();
    const u64 vertex_count = static_cast<u64>(sx) * sz;

    auto* vertices = scratch.push_array<TerrainVertex>(vertex_count);
    if (!vertices) {
        return false;
    }
    for (u32 iz = 0; iz < sz; iz++) {
        for (u32 ix = 0; ix < sx; ix++) {
            TerrainVertex& v = vertices[static_cast<u64>(iz) * sx + ix];
            v.pos = Vec3{hf.origin().x + static_cast<f32>(ix) * hf.cell_size(),
                         hf.height_at(ix, iz),
                         hf.origin().z + static_cast<f32>(iz) * hf.cell_size()};
            v.normal = vertex_normal(hf, ix, iz);
        }
    }

    const u32 chunks_x = (sx - 1 + kChunkQuads - 1) / kChunkQuads;
    const u32 chunks_z = (sz - 1 + kChunkQuads - 1) / kChunkQuads;
    if (chunks_x * chunks_z > kMaxChunks) {
        log_error("terrain_render: too many chunks (%u)", chunks_x * chunks_z);
        return false;
    }

    const u64 max_indices = static_cast<u64>(sx - 1) * (sz - 1) * 6;
    auto* indices = scratch.push_array<u32>(max_indices);
    if (!indices) {
        return false;
    }

    u64 cursor = 0;
    chunk_count_ = 0;
    for (u32 cz = 0; cz < chunks_z; cz++) {
        for (u32 cx = 0; cx < chunks_x; cx++) {
            Chunk& chunk = chunks_[chunk_count_++];
            chunk.first_index = static_cast<u32>(cursor);
            chunk.bounds = aabb_empty();

            const u32 x0 = cx * kChunkQuads;
            const u32 z0 = cz * kChunkQuads;
            const u32 x1 = x0 + kChunkQuads < sx - 1 ? x0 + kChunkQuads : sx - 1;
            const u32 z1 = z0 + kChunkQuads < sz - 1 ? z0 + kChunkQuads : sz - 1;

            for (u32 iz = z0; iz < z1; iz++) {
                for (u32 ix = x0; ix < x1; ix++) {
                    const u32 i00 = iz * sx + ix;
                    const u32 i10 = i00 + 1;
                    const u32 i01 = i00 + sx;
                    const u32 i11 = i01 + 1;
                    indices[cursor++] = i00;
                    indices[cursor++] = i11;
                    indices[cursor++] = i10;
                    indices[cursor++] = i00;
                    indices[cursor++] = i01;
                    indices[cursor++] = i11;
                }
            }
            for (u32 iz = z0; iz <= z1; iz++) {
                for (u32 ix = x0; ix <= x1; ix++) {
                    chunk.bounds = expand(chunk.bounds, vertices[static_cast<u64>(iz) * sx + ix].pos);
                }
            }
            chunk.index_count = static_cast<u32>(cursor) - chunk.first_index;
        }
    }

    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, static_cast<GLsizeiptr>(vertex_count * sizeof(TerrainVertex)),
                         vertices, 0);
    glCreateBuffers(1, &ebo_);
    glNamedBufferStorage(ebo_, static_cast<GLsizeiptr>(cursor * sizeof(u32)), indices, 0);
    glCreateVertexArrays(1, &vao_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(TerrainVertex));
    glVertexArrayElementBuffer(vao_, ebo_);
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, pos));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 3, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, normal));
    glVertexArrayAttribBinding(vao_, 1, 0);

    if (roadmask && mask_size > 0) {
        glCreateTextures(GL_TEXTURE_2D, 1, &mask_texture_);
        glTextureStorage2D(mask_texture_, 1, GL_R8, static_cast<GLsizei>(mask_size),
                           static_cast<GLsizei>(mask_size));
        glTextureParameteri(mask_texture_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(mask_texture_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(mask_texture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(mask_texture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage2D(mask_texture_, 0, 0, 0, static_cast<GLsizei>(mask_size),
                            static_cast<GLsizei>(mask_size), GL_RED, GL_UNSIGNED_BYTE, roadmask);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    auto* heights = scratch.push_array<f32>(vertex_count);
    for (u32 iz = 0; iz < sz; iz++) {
        for (u32 ix = 0; ix < sx; ix++) {
            heights[static_cast<u64>(iz) * sx + ix] = hf.height_at(ix, iz);
        }
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &height_texture_);
    glTextureStorage2D(height_texture_, 1, GL_R32F, static_cast<GLsizei>(sx),
                       static_cast<GLsizei>(sz));
    glTextureParameteri(height_texture_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(height_texture_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(height_texture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(height_texture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureSubImage2D(height_texture_, 0, 0, 0, static_cast<GLsizei>(sx),
                        static_cast<GLsizei>(sz), GL_RED, GL_FLOAT, heights);

    glCreateVertexArrays(1, &scrub_vao_);

    const f32 black = 0.0f;
    glCreateTextures(GL_TEXTURE_2D, 1, &black_texture_);
    glTextureStorage2D(black_texture_, 1, GL_R32F, 1, 1);
    glTextureSubImage2D(black_texture_, 0, 0, 0, 1, 1, GL_RED, GL_FLOAT, &black);

    tex_grass_ = device.assets().texture_slot("grass");
    tex_rock_ = device.assets().texture_slot("rock");
    tex_road_ = device.assets().texture_slot("road");
    tex_meadow_ = device.assets().texture_slot("meadow_ground");

    params_ = Vec4{hf.origin().x, hf.origin().z, 1.0f / hf.span_x(), 1.0f / hf.span_z()};

    log_info("terrain_render: %u verts, %u chunks", static_cast<u32>(vertex_count), chunk_count_);
    ready_ = true;
    return true;
}

void TerrainRenderer::draw(RenderDevice& device)
{
    if (!ready_) {
        return;
    }
    const bool shadow_pass = device.shadow_pass_active();
    const u32 program = device.shaders().program(shadow_pass ? "shadow" : "terrain");
    if (!program) {
        return;
    }

    if (shadow_pass) {
        const Mat4 identity = mat4_identity();
        const Mat4 light_vp = device.shadow_matrix();
        glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, identity.m);
        glProgramUniformMatrix4fv(program, 4, 1, GL_FALSE, light_vp.m);
    } else {
        glProgramUniform4f(program, 0, params_.x, params_.y, params_.z, params_.w);
    }

    device.use_program(program);
    device.bind_vao(vao_);
    if (!shadow_pass) {
        device.bind_texture0(device.assets().texture_gl(tex_grass_));
        device.bind_texture(1, device.assets().texture_gl(tex_rock_));
        device.bind_texture(2, device.assets().texture_gl(tex_road_));
        device.bind_texture(3, mask_texture_);
        device.bind_texture(5, device.assets().texture_gl(tex_meadow_));
    }
    device.set_cull(true);

    const Frustum& frustum = device.frustum();
    chunks_drawn_ = 0;
    for (u32 i = 0; i < chunk_count_; i++) {
        const Chunk& chunk = chunks_[i];
        if (!frustum_test_aabb(frustum, chunk.bounds)) {
            continue;
        }
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(chunk.index_count), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<u64>(chunk.first_index)
                                                     * sizeof(u32)));
        chunks_drawn_++;
    }

}

void TerrainRenderer::draw_scrub(RenderDevice& device, Vec3 cam_pos, f32 time)
{
    if (!ready_ || device.shadow_pass_active()) {
        return;
    }
    const u32 program = device.shaders().program("scrub");
    if (!program) {
        return;
    }
    glProgramUniform4f(program, 1, cam_pos.x, cam_pos.z, time, kScrubFadeEnd);
    glProgramUniform4f(program, 2, params_.x, params_.y, params_.z, params_.w);
    glProgramUniform1i(program, 19, static_cast<i32>(press_count_));
    if (press_count_ > 0) {
        glProgramUniform4fv(program, 3, static_cast<GLsizei>(press_count_ * 2), &press_[0].x);
    }
    const Mat4 identity = mat4_identity();
    glProgramUniformMatrix4fv(program, 22, 1, GL_FALSE, identity.m);
    glProgramUniform3f(program, 26, cam_pos.x, cam_pos.y, cam_pos.z);
    glProgramUniform1i(program, 27, 0);
    device.use_program(program);
    device.bind_vao(scrub_vao_);
    device.bind_texture(3, mask_texture_);
    device.bind_texture(4, height_texture_);
    device.set_cull(false);
    scrub_rings(program, nullptr);
}

void TerrainRenderer::scrub_rings(u32 program, const Vec3* cam_local)
{
    const f32 spacings[2] = {kScrubNearSpacing, kScrubFarSpacing};
    const i32 grids[2] = {static_cast<i32>(kScrubNearGrid), static_cast<i32>(kScrubFarGrid)};
    for (i32 level = 0; level < 2; level++) {
        const f32 spacing = spacings[level];
        const i32 grid = grids[level];
        i32 x0 = -grid / 2;
        i32 z0 = -grid / 2;
        i32 x1 = grid - 1 - grid / 2;
        i32 z1 = grid - 1 - grid / 2;
        i32 rect_w = 0;
        if (cam_local) {
            const i32 cx = static_cast<i32>(std::floor(cam_local->x / spacing));
            const i32 cz = static_cast<i32>(std::floor(cam_local->z / spacing));
            const i32 lo = static_cast<i32>(std::floor(-kPatchExtent / spacing)) - 1;
            const i32 hi = static_cast<i32>(std::ceil(kPatchExtent / spacing)) + 1;
            x0 = std::max(x0, lo - cx);
            x1 = std::min(x1, hi - cx);
            z0 = std::max(z0, lo - cz);
            z1 = std::min(z1, hi - cz);
            if (x1 < x0 || z1 < z0) {
                continue;
            }
            rect_w = x1 - x0 + 1;
        }
        const i32 rect_h = z1 - z0 + 1;
        glProgramUniform4i(program, 28, x0, z0, rect_w, rect_h);
        glProgramUniform4f(program, 20, spacing, static_cast<f32>(grid), kScrubRingStart,
                           kScrubRingEnd);
        glProgramUniform1i(program, 21, level);
        const i32 clumps = cam_local ? rect_w * rect_h : grid * grid;
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, kScrubBladeVerts,
                              clumps * static_cast<i32>(kScrubSlots));
    }
}

u32 TerrainRenderer::patch_texture(Arena& scratch, std::string_view mesh_name)
{
    for (u32 i = 0; i < patch_texture_count_; i++) {
        if (patch_textures_[i].name == mesh_name) {
            return patch_textures_[i].texture;
        }
    }
    if (patch_texture_count_ >= kMaxPatchTextures) {
        if (!patch_cache_full_logged_) {
            patch_cache_full_logged_ = true;
            log_warn("terrain_render: grass patch cache full at %u mesh names", kMaxPatchTextures);
        }
        return 0;
    }
    PatchTexture& entry = patch_textures_[patch_texture_count_++];
    entry.name.assign(mesh_name);
    entry.texture = 0;

    ArenaScope scope(scratch);
    FixedString<128> path;
    path.format("assets/textures/%.*s_grass_h.png", static_cast<int>(mesh_name.size()),
                mesh_name.data());
    if (!fs::exists(path.view())) {
        return 0;
    }
    const Image16 raw = load_image_gray16(scratch, scratch, path.view());
    if (!raw.pixels) {
        return 0;
    }
    const u64 count = static_cast<u64>(raw.width) * raw.height;
    f32* heights = scratch.push_array<f32>(count);
    const f32 span = kPatchHeightMax - kPatchHeightMin;
    for (u64 i = 0; i < count; i++) {
        const u16 v = raw.pixels[i];
        heights[i] = v == 0 ? -1000.0f : kPatchHeightMin + span * (static_cast<f32>(v) / 65535.0f);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &entry.texture);
    glTextureStorage2D(entry.texture, 1, GL_R32F, static_cast<GLsizei>(raw.width),
                       static_cast<GLsizei>(raw.height));
    glTextureParameteri(entry.texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(entry.texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(entry.texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(entry.texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureSubImage2D(entry.texture, 0, 0, 0, static_cast<GLsizei>(raw.width),
                        static_cast<GLsizei>(raw.height), GL_RED, GL_FLOAT, heights);
    log_info("terrain_render: grass patch %s", path.c_str());
    return entry.texture;
}

void TerrainRenderer::draw_scrub_patches(RenderDevice& device, Vec3 cam_pos, f32 time,
                                         const ScrubPatch* patches, u32 count)
{
    if (!ready_ || device.shadow_pass_active() || count == 0) {
        return;
    }
    const u32 program = device.shaders().program("scrub");
    if (!program) {
        return;
    }
    const f32 inv = 1.0f / (2.0f * kPatchExtent);
    glProgramUniform4f(program, 2, -kPatchExtent, -kPatchExtent, inv, inv);
    glProgramUniform1i(program, 27, 1);
    device.use_program(program);
    device.bind_vao(scrub_vao_);
    device.bind_texture(3, black_texture_);
    device.set_cull(false);
    for (u32 i = 0; i < count; i++) {
        const ScrubPatch& p = patches[i];
        if (!p.texture || p.scale <= 1e-4f) {
            continue;
        }
        const Vec3 local = rotate(conjugate(p.rot), cam_pos - p.pos) * (1.0f / p.scale);
        const Vec3 over{f_max(f_abs(local.x) - kPatchExtent, 0.0f),
                        f_max(f_abs(local.y) - kPatchHeightMax, 0.0f),
                        f_max(f_abs(local.z) - kPatchExtent, 0.0f)};
        if (length(over) > kScrubFadeEnd) {
            continue;
        }
        const Mat4 model = mat4_trs(p.pos, p.rot, Vec3{p.scale, p.scale, p.scale});
        glProgramUniformMatrix4fv(program, 22, 1, GL_FALSE, model.m);
        glProgramUniform3f(program, 26, local.x, local.y, local.z);
        glProgramUniform4f(program, 1, local.x, local.z, time, kScrubFadeEnd);
        Vec4 press[kMaxPatchPress * 2];
        u32 press_count = 0;
        const Quat to_local = conjugate(p.rot);
        const f32 inv_scale = 1.0f / p.scale;
        for (u32 k = 0; k < patch_press_count_; k++) {
            const PressVolume& v = patch_press_[k];
            if (encode_press(&press[press_count * 2], rotate(to_local, v.centre - p.pos) * inv_scale,
                             v.half * inv_scale, to_local * v.rot)) {
                press_count++;
            }
        }
        glProgramUniform1i(program, 19, static_cast<i32>(press_count));
        if (press_count > 0) {
            glProgramUniform4fv(program, 3, static_cast<GLsizei>(press_count * 2), &press[0].x);
        }
        device.bind_texture(4, p.texture);
        scrub_rings(program, &local);
    }
}

bool TerrainRenderer::encode_press(Vec4* out, Vec3 centre, Vec3 half_extents, Quat rot)
{
    if (half_extents.x < 1e-3f || half_extents.z < 1e-3f) {
        return false;
    }
    const Vec3 ax = rotate(rot, Vec3{1.0f, 0.0f, 0.0f});
    const Vec3 az = rotate(rot, Vec3{0.0f, 0.0f, 1.0f});
    out[0] = Vec4{centre.x, centre.z, centre.y - half_extents.y, 0.0f};
    out[1] = Vec4{ax.x / half_extents.x, ax.z / half_extents.x,
                  az.x / half_extents.z, az.z / half_extents.z};
    return true;
}

void TerrainRenderer::add_press_volume(Vec3 centre, Vec3 half_extents, Quat rot)
{
    if (press_count_ < kMaxPressVolumes
        && encode_press(&press_[press_count_ * 2], centre, half_extents, rot)) {
        press_count_++;
    }
}

void TerrainRenderer::add_patch_press(Vec3 centre, Vec3 half_extents, Quat rot)
{
    if (patch_press_count_ < kMaxPatchPress) {
        patch_press_[patch_press_count_++] = PressVolume{centre, half_extents, rot};
    }
}

void TerrainRenderer::shutdown()
{
    if (!ready_) {
        return;
    }
    glDeleteVertexArrays(1, &vao_);
    glDeleteVertexArrays(1, &scrub_vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteBuffers(1, &ebo_);
    if (mask_texture_) {
        glDeleteTextures(1, &mask_texture_);
    }
    glDeleteTextures(1, &height_texture_);
    glDeleteTextures(1, &black_texture_);
    for (u32 i = 0; i < patch_texture_count_; i++) {
        if (patch_textures_[i].texture) {
            glDeleteTextures(1, &patch_textures_[i].texture);
        }
    }
    patch_texture_count_ = 0;
    ready_ = false;
}

} // namespace anom
