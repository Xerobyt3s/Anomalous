#include "render/terrain_render.h"
#include "render/render.h"
#include "assets/assets.h"
#include "physics/heightfield.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/gl_loader.h"

#define TERRAIN_CHUNK_QUADS 64
#define TERRAIN_MAX_CHUNKS 256

typedef struct TerrainVertex {
    Vec3 pos;
    Vec3 normal;
} TerrainVertex;

typedef struct TerrainChunk {
    u32 first_index;
    u32 index_count;
    Aabb bounds;
} TerrainChunk;

static u32 s_vao;
static u32 s_vbo;
static u32 s_ebo;
static u32 s_mask_texture;
static u32 s_tex_grass;
static u32 s_tex_rock;
static u32 s_tex_road;
static TerrainChunk s_chunks[TERRAIN_MAX_CHUNKS];
static u32 s_chunk_count;
static Vec4 s_terrain_params;
static u32 s_chunks_drawn;
static b32 s_ready;

static Vec3 terrain_vertex_normal(const Heightfield* hf, u32 ix, u32 iz)
{
    u32 xl = ix > 0 ? ix - 1 : ix;
    u32 xr = ix + 1 < hf->size_x ? ix + 1 : ix;
    u32 zd = iz > 0 ? iz - 1 : iz;
    u32 zu = iz + 1 < hf->size_z ? iz + 1 : iz;
    f32 hl = heightfield_height_at(hf, xl, iz);
    f32 hr = heightfield_height_at(hf, xr, iz);
    f32 hd = heightfield_height_at(hf, ix, zd);
    f32 hu = heightfield_height_at(hf, ix, zu);
    f32 span_x = (f32)(xr - xl) * hf->cell_size;
    f32 span_z = (f32)(zu - zd) * hf->cell_size;
    return vec3_normalize(v3((hl - hr) / f_max(span_x, 1e-4f) * 2.0f,
                             1.0f,
                             (hd - hu) / f_max(span_z, 1e-4f) * 2.0f));
}

b32 terrain_render_init(const struct Heightfield* hf, const u8* roadmask, u32 mask_size)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    u32 sx = hf->size_x;
    u32 sz = hf->size_z;
    u64 vertex_count = (u64)sx * sz;
    TerrainVertex* vertices = arena_push_array(&g_frame_arena, TerrainVertex, vertex_count);
    for (u32 iz = 0; iz < sz; iz++) {
        for (u32 ix = 0; ix < sx; ix++) {
            TerrainVertex* v = &vertices[(u64)iz * sx + ix];
            v->pos = v3(hf->origin.x + (f32)ix * hf->cell_size,
                        heightfield_height_at(hf, ix, iz),
                        hf->origin.z + (f32)iz * hf->cell_size);
            v->normal = terrain_vertex_normal(hf, ix, iz);
        }
    }

    u32 chunks_x = (sx - 1 + TERRAIN_CHUNK_QUADS - 1) / TERRAIN_CHUNK_QUADS;
    u32 chunks_z = (sz - 1 + TERRAIN_CHUNK_QUADS - 1) / TERRAIN_CHUNK_QUADS;
    if (chunks_x * chunks_z > TERRAIN_MAX_CHUNKS) {
        log_error("terrain_render: too many chunks (%u)", chunks_x * chunks_z);
        arena_temp_end(temp);
        return 0;
    }

    u64 max_indices = (u64)(sx - 1) * (sz - 1) * 6;
    u32* indices = arena_push_array(&g_frame_arena, u32, max_indices);
    u64 index_cursor = 0;
    s_chunk_count = 0;
    for (u32 cz = 0; cz < chunks_z; cz++) {
        for (u32 cx = 0; cx < chunks_x; cx++) {
            TerrainChunk* chunk = &s_chunks[s_chunk_count++];
            chunk->first_index = (u32)index_cursor;
            chunk->bounds = aabb_empty();
            u32 x0 = cx * TERRAIN_CHUNK_QUADS;
            u32 z0 = cz * TERRAIN_CHUNK_QUADS;
            u32 x1 = x0 + TERRAIN_CHUNK_QUADS < sx - 1 ? x0 + TERRAIN_CHUNK_QUADS : sx - 1;
            u32 z1 = z0 + TERRAIN_CHUNK_QUADS < sz - 1 ? z0 + TERRAIN_CHUNK_QUADS : sz - 1;
            for (u32 iz = z0; iz < z1; iz++) {
                for (u32 ix = x0; ix < x1; ix++) {
                    u32 i00 = iz * sx + ix;
                    u32 i10 = i00 + 1;
                    u32 i01 = i00 + sx;
                    u32 i11 = i01 + 1;
                    indices[index_cursor++] = i00;
                    indices[index_cursor++] = i11;
                    indices[index_cursor++] = i10;
                    indices[index_cursor++] = i00;
                    indices[index_cursor++] = i01;
                    indices[index_cursor++] = i11;
                }
            }
            for (u32 iz = z0; iz <= z1; iz++) {
                for (u32 ix = x0; ix <= x1; ix++) {
                    chunk->bounds = aabb_expand(chunk->bounds, vertices[(u64)iz * sx + ix].pos);
                }
            }
            chunk->index_count = (u32)index_cursor - chunk->first_index;
        }
    }

    glCreateBuffers(1, &s_vbo);
    glNamedBufferStorage(s_vbo, (GLsizeiptr)(vertex_count * sizeof(TerrainVertex)), vertices, 0);
    glCreateBuffers(1, &s_ebo);
    glNamedBufferStorage(s_ebo, (GLsizeiptr)(index_cursor * sizeof(u32)), indices, 0);
    glCreateVertexArrays(1, &s_vao);
    glVertexArrayVertexBuffer(s_vao, 0, s_vbo, 0, sizeof(TerrainVertex));
    glVertexArrayElementBuffer(s_vao, s_ebo);
    glEnableVertexArrayAttrib(s_vao, 0);
    glVertexArrayAttribFormat(s_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, pos));
    glVertexArrayAttribBinding(s_vao, 0, 0);
    glEnableVertexArrayAttrib(s_vao, 1);
    glVertexArrayAttribFormat(s_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, normal));
    glVertexArrayAttribBinding(s_vao, 1, 0);

    glCreateTextures(GL_TEXTURE_2D, 1, &s_mask_texture);
    glTextureStorage2D(s_mask_texture, 1, GL_R8, (GLsizei)mask_size, (GLsizei)mask_size);
    glTextureParameteri(s_mask_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(s_mask_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(s_mask_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(s_mask_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(s_mask_texture, 0, 0, 0, (GLsizei)mask_size, (GLsizei)mask_size,
                        GL_RED, GL_UNSIGNED_BYTE, roadmask);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    s_tex_grass = asset_texture_slot("grass");
    s_tex_rock = asset_texture_slot("rock");
    s_tex_road = asset_texture_slot("road");

    f32 span_x = (f32)(sx - 1) * hf->cell_size;
    f32 span_z = (f32)(sz - 1) * hf->cell_size;
    s_terrain_params = v4(hf->origin.x, hf->origin.z, 1.0f / span_x, 1.0f / span_z);

    arena_temp_end(temp);
    log_info("terrain_render: %u verts, %u chunks", (u32)vertex_count, s_chunk_count);
    s_ready = 1;
    return 1;
}

void terrain_render_draw(void)
{
    if (!s_ready) {
        return;
    }
    u32 program = r_shader("terrain");
    if (!program) {
        return;
    }
    glProgramUniform4f(program, 0, s_terrain_params.x, s_terrain_params.y,
                       s_terrain_params.z, s_terrain_params.w);
    glUseProgram(program);
    glBindVertexArray(s_vao);
    glBindTextureUnit(0, asset_texture_gl(s_tex_grass));
    glBindTextureUnit(1, asset_texture_gl(s_tex_rock));
    glBindTextureUnit(2, asset_texture_gl(s_tex_road));
    glBindTextureUnit(3, s_mask_texture);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    const Frustum* frustum = r_frustum();
    s_chunks_drawn = 0;
    for (u32 i = 0; i < s_chunk_count; i++) {
        const TerrainChunk* chunk = &s_chunks[i];
        if (!frustum_test_aabb(frustum, chunk->bounds)) {
            continue;
        }
        glDrawElements(GL_TRIANGLES, (GLsizei)chunk->index_count, GL_UNSIGNED_INT,
                       (const void*)((u64)chunk->first_index * sizeof(u32)));
        s_chunks_drawn++;
    }
    glDisable(GL_CULL_FACE);
    glBindVertexArray(0);
    glUseProgram(0);
}

void terrain_render_shutdown(void)
{
    if (!s_ready) {
        return;
    }
    glDeleteVertexArrays(1, &s_vao);
    glDeleteBuffers(1, &s_vbo);
    glDeleteBuffers(1, &s_ebo);
    glDeleteTextures(1, &s_mask_texture);
    s_ready = 0;
}

u32 terrain_render_chunks_drawn(void)
{
    return s_chunks_drawn;
}
